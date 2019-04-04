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

#pragma once

#include "execq/IExecutionStream.h"
#include "execq/internal/ThreadWorkerPool.h"

#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace execq
{
    namespace details
    {
        class ExecutionStream: public IExecutionStream, private IThreadWorkerPoolTaskProvider
        {
        public:
            ExecutionStream(std::shared_ptr<ThreadWorkerPool> workerPool, std::function<void(const std::atomic_bool& isCanceled)> executee);
            ~ExecutionStream();
            
        public: // IExecutionStream
            virtual void start() final;
            virtual void stop() final;
            
        private: // IThreadWorkerPoolTaskProvider
            virtual bool execute() final;
            virtual bool hasTask() const final;
            
        private:
            void waitPendingTasks();
            
        private:
            std::atomic_bool m_shouldQuit { false };
            std::atomic_bool m_started { false };
            
            size_t m_tasksRunningCount { 0 };
            std::mutex m_taskCompleteMutex;
            std::condition_variable m_taskCompleteCondition;
            
            std::mutex m_taskStartMutex;
            std::condition_variable m_taskStartCondition;
            
            std::shared_ptr<ThreadWorkerPool> m_workerPool;
            std::function<void(const std::atomic_bool& shouldQuit)> m_executee;
            
            ThreadWorker m_additionalWorker;
        };
    }
}
