/*
 * MIT License
 *
 * Copyright (c) 2018 Alkenso (Vladimir Vashurkin)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "ExecutionQueueSource.h"
#include "ExecutionQueue.h"

namespace
{
    const std::chrono::milliseconds kLongTermJob { 100 };
    const std::chrono::milliseconds kTimeout = 5 * kLongTermJob;
    
    void WaitForLongTermJob()
    {
        std::this_thread::sleep_for(kLongTermJob);
    }
    
    MATCHER_P(CompareWithAtomic, value, "")
    {
        return value == arg;
    }
    
    class MockTaskProvider: public execq::details::ITaskProvider
    {
    public:
        MOCK_METHOD0(nextTask, execq::details::Task());
    };
    
    execq::details::Task MakeValidTask()
    {
        return execq::details::Task([] {});
    }
    
    execq::details::Task MakeInvalidTask()
    {
        return execq::details::Task();
    }
}

TEST(ExecutionQueueSource, SingleTask)
{
    execq::ExecutionQueueSource pool;
    
    ::testing::MockFunction<void(const std::atomic_bool&, std::string)> mockExecutor;
    auto queue = pool.createExecutionQueue<std::string>(mockExecutor.AsStdFunction());
    
    EXPECT_CALL(mockExecutor, Call(CompareWithAtomic(false), "qwe"))
    .WillOnce(::testing::Return());
    
    queue->push("qwe");
    
    
    // wait some time to be sure that object has been delivered to corresponding thread.
    WaitForLongTermJob();
}

TEST(ExecutionQueueSource, MultipleTasks)
{
    execq::ExecutionQueueSource pool;
    
    ::testing::MockFunction<void(const std::atomic_bool&, uint32_t)> mockExecutor;
    auto queue = pool.createExecutionQueue<uint32_t>(mockExecutor.AsStdFunction());
    
    const int count = 100;
    EXPECT_CALL(mockExecutor, Call(CompareWithAtomic(false), ::testing::_))
    .Times(count).WillRepeatedly(::testing::Return());
    
    for (int i = 0; i < count; i++)
    {
        queue->push(arc4random());
    }
    
    
    // wait some time to be sure that objects have been delivered to corresponding threads and processed.
    WaitForLongTermJob();
}

TEST(ExecutionQueueSource, TaskExecutionWhenQueueDestroyed)
{
    execq::ExecutionQueueSource pool;
    
    ::testing::MockFunction<void(const std::atomic_bool&, std::string)> mockExecutor;
    std::promise<std::pair<bool, std::string>> isExecutedPromise;
    auto isExecuted = isExecutedPromise.get_future();
    auto queue = pool.createExecutionQueue<std::string>([&isExecutedPromise] (const std::atomic_bool& shouldStop, std::string object) {
        // wait for double time comparing to time waiting before reset
        WaitForLongTermJob();
        WaitForLongTermJob();
        isExecutedPromise.set_value(std::make_pair(shouldStop.load(), object));
    });
    queue->push("qwe");
    
    
    // wait for enough time to push object into processing.
    WaitForLongTermJob();
    queue.reset();
    
    ASSERT_EQ(isExecuted.wait_for(kTimeout), std::future_status::ready);
    
    std::pair<bool, std::string> executeState = isExecuted.get();
    EXPECT_EQ(executeState.first, true);
    EXPECT_EQ(executeState.second, "qwe");
}

TEST(ExecutionQueueSource, ExecutionQueue_Delegate)
{
    class MockExecutionQueueDelegate: public execq::details::IExecutionQueueDelegate
    {
    public:
        MOCK_METHOD1(registerTaskProvider, void(execq::details::ITaskProvider& taskProvider));
        MOCK_METHOD1(unregisterTaskProvider, void(const execq::details::ITaskProvider& taskProvider));
        MOCK_METHOD0(taskProviderDidReceiveNewTask, void());
    };
    
    MockExecutionQueueDelegate delegate;
    
    
    //  Queue must call 'register' method when created and 'unregister' method when destroyed.
    EXPECT_CALL(delegate, registerTaskProvider(::testing::_))
    .WillOnce(::testing::Return());
    
    EXPECT_CALL(delegate, unregisterTaskProvider(::testing::_))
    .WillOnce(::testing::Return());
    
    
    ::testing::MockFunction<void(const std::atomic_bool&, std::string)> mockExecutor;
    execq::details::ExecutionQueue<std::string> queue(delegate, mockExecutor.AsStdFunction());
    
    EXPECT_CALL(delegate, taskProviderDidReceiveNewTask())
    .WillOnce(::testing::Return());
    
    EXPECT_CALL(mockExecutor, Call(CompareWithAtomic(false), "qwe"))
    .WillOnce(::testing::Return());
    
    queue.push("qwe");
    
    
    // wait some time to be sure that object has been delivered to corresponding thread.
    WaitForLongTermJob();
}

TEST(ExecutionQueueSource, TaskProvidersList_NoItems)
{
    execq::details::TaskProviderList providers;
    
    EXPECT_FALSE(providers.nextTask().valid());
}

TEST(ExecutionQueueSource, TaskProvidersList_SindleItem)
{
    execq::details::TaskProviderList providers;
    auto provider = std::make_shared<MockTaskProvider>();
    providers.add(*provider);
    
    
    // return valid tasks
    EXPECT_CALL(*provider, nextTask())
    .WillOnce(::testing::Return(::testing::ByMove(MakeValidTask())))
    .WillOnce(::testing::Return(::testing::ByMove(MakeValidTask())));
    
    EXPECT_TRUE(providers.nextTask().valid());
    EXPECT_TRUE(providers.nextTask().valid());
    
    
    // return invalid tasks
    EXPECT_CALL(*provider, nextTask())
    .WillOnce(::testing::Return(::testing::ByMove(MakeInvalidTask())))
    .WillOnce(::testing::Return(::testing::ByMove(MakeInvalidTask())));
    
    EXPECT_FALSE(providers.nextTask().valid());
    EXPECT_FALSE(providers.nextTask().valid());
}

TEST(ExecutionQueueSource, TaskProvidersList_MultipleProvidersWithValidTasks)
{
    execq::details::TaskProviderList providers;
    
    // Provider #1 has 2 valid tasks
    auto provider1 = std::make_shared<MockTaskProvider>();
    providers.add(*provider1);
    
    ::testing::MockFunction<void()> provider1Callback;
    EXPECT_CALL(*provider1, nextTask())
    .WillOnce(::testing::Return(::testing::ByMove(execq::details::Task(provider1Callback.AsStdFunction()))))
    .WillOnce(::testing::Return(::testing::ByMove(execq::details::Task(provider1Callback.AsStdFunction()))));
    
    EXPECT_CALL(provider1Callback, Call())
    .Times(2);
    
    
    // Provider #2 and #3 each has 1 valid task
    auto provider2 = std::make_shared<MockTaskProvider>();
    providers.add(*provider2);
    
    ::testing::MockFunction<void()> provider2Callback;
    EXPECT_CALL(*provider2, nextTask())
    .WillOnce(::testing::Return(::testing::ByMove(execq::details::Task(provider2Callback.AsStdFunction()))));
    
    EXPECT_CALL(provider2Callback, Call())
    .Times(1);
    
    auto provider3 = std::make_shared<MockTaskProvider>();
    providers.add(*provider3);
    
    ::testing::MockFunction<void()> provider3Callback;
    EXPECT_CALL(*provider3, nextTask())
    .WillOnce(::testing::Return(::testing::ByMove(execq::details::Task(provider3Callback.AsStdFunction()))));
    
    EXPECT_CALL(provider3Callback, Call())
    .Times(1);
    
    
    // Receiving task from the first provider
    auto provider1Task = providers.nextTask();
    ASSERT_TRUE(provider1Task.valid());
    provider1Task();
    
    
    // Receiving task from the second provider
    auto provider2Task = providers.nextTask();
    ASSERT_TRUE(provider2Task.valid());
    provider2Task();
    
    
    // Receiving task from the third provider
    auto provider3Task = providers.nextTask();
    ASSERT_TRUE(provider3Task.valid());
    provider3Task();
    
    
    // Receiving task from the first provider again
    auto provider1Task2 = providers.nextTask();
    ASSERT_TRUE(provider1Task2.valid());
    provider1Task2();
}

TEST(ExecutionQueueSource, TaskProvidersList_MultipleProvidersInvalidTasks)
{
    execq::details::TaskProviderList providers;
    
    // Providers have no valid tasks
    auto provider1 = std::make_shared<MockTaskProvider>();
    providers.add(*provider1);
    
    EXPECT_CALL(*provider1, nextTask())
    .WillOnce(::testing::Return(::testing::ByMove(execq::details::Task(MakeInvalidTask()))));
    
    auto provider2 = std::make_shared<MockTaskProvider>();
    providers.add(*provider2);
    
    EXPECT_CALL(*provider2, nextTask())
    .WillOnce(::testing::Return(::testing::ByMove(execq::details::Task(MakeInvalidTask()))));
    
    auto provider3 = std::make_shared<MockTaskProvider>();
    providers.add(*provider3);
    
    EXPECT_CALL(*provider3, nextTask())
    .WillOnce(::testing::Return(::testing::ByMove(execq::details::Task(MakeInvalidTask()))));
    
    
    // Providers have no valid tasks, returning invalid task
    EXPECT_FALSE(providers.nextTask().valid());
}

TEST(ExecutionQueueSource, TaskProvidersList_MultipleProviders_Valid_Invalid_Tasks)
{
    execq::details::TaskProviderList providers;
    
    // Provider #1 has 1 valid task
    auto provider1 = std::make_shared<MockTaskProvider>();
    providers.add(*provider1);
    
    ::testing::MockFunction<void()> provider1Callback;
    EXPECT_CALL(*provider1, nextTask())
    .WillOnce(::testing::Return(::testing::ByMove(execq::details::Task(provider1Callback.AsStdFunction()))));
    
    EXPECT_CALL(provider1Callback, Call())
    .Times(1);
    
    
    // Provider #2 has no valid tasks
    auto provider2 = std::make_shared<MockTaskProvider>();
    providers.add(*provider2);
    
    EXPECT_CALL(*provider2, nextTask())
    .WillOnce(::testing::Return(::testing::ByMove(MakeInvalidTask())));
    
    
    // Provider #3 has 1 valid task
    auto provider3 = std::make_shared<MockTaskProvider>();
    providers.add(*provider3);
    
    ::testing::MockFunction<void()> provider3Callback;
    EXPECT_CALL(*provider3, nextTask())
    .WillOnce(::testing::Return(::testing::ByMove(execq::details::Task(provider3Callback.AsStdFunction()))));
    
    EXPECT_CALL(provider3Callback, Call())
    .Times(1);
    
    
    // Receiving task from the first provider
    auto provider1Task = providers.nextTask();
    ASSERT_TRUE(provider1Task.valid());
    provider1Task();
    
    
    // Skipping task from the second provider (because it has invalid task)
    // and
    // Receiving task from the third provider
    auto provider3Task = providers.nextTask();
    ASSERT_TRUE(provider3Task.valid());
    provider3Task();
}

TEST(ExecutionQueueSource, TaskProvidersList_Add_Remove)
{
    execq::details::TaskProviderList providers;
    
    // No providers - no valid tasks
    EXPECT_FALSE(providers.nextTask().valid());
    
    // Add providers
    auto provider1 = std::make_shared<MockTaskProvider>();
    providers.add(*provider1);
    
    auto provider2 = std::make_shared<MockTaskProvider>();
    providers.add(*provider2);
    
    
    // Providers don't have valid tasks, so they both are checked
    EXPECT_CALL(*provider1, nextTask())
    .WillOnce(::testing::Return(::testing::ByMove(execq::details::Task(MakeInvalidTask()))));
    
    EXPECT_CALL(*provider2, nextTask())
    .WillOnce(::testing::Return(::testing::ByMove(execq::details::Task(MakeInvalidTask()))));
    
    EXPECT_FALSE(providers.nextTask().valid());
    
    
    // Provider 1 is removed, so check only provider #2
    providers.remove(*provider1);
    
    EXPECT_CALL(*provider2, nextTask())
    .WillOnce(::testing::Return(::testing::ByMove(execq::details::Task(MakeInvalidTask()))));
    
    EXPECT_FALSE(providers.nextTask().valid());
    
    
    // Provider 2 is removed, providers list is empty
    providers.remove(*provider2);
    
    EXPECT_FALSE(providers.nextTask().valid());
}
