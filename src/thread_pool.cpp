#include "thread_pool.h"
#include <cassert>

ThreadPool::ThreadPool(size_t num_threads) : m_stop(false) {
    assert(num_threads > 0);

    m_threads.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        m_threads.emplace_back(&ThreadPool::worker, this);
    }
}

ThreadPool::~ThreadPool() {
    // Signal all workers to stop
    m_stop.store(true);
    m_cv.notify_all();

    // Wait for every thread to finish
    for (auto& t : m_threads) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void ThreadPool::worker() {
    while (true) {
        std::function<void()> job;

        {
            std::unique_lock<std::mutex> lock(m_mutex);

            // Wait until there's work or we're told to stop
            m_cv.wait(lock, [this]() {
                return !m_queue.empty() || m_stop.load();
                });

            // If stopped and no more work, exit
            if (m_stop.load() && m_queue.empty()) {
                return;
            }

            // Grab the job
            job = std::move(m_queue.front());
            m_queue.pop_front();
        }

        // Execute outside the lock
        job();
    }
}

void ThreadPool::queue(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(std::move(job));
    }
    m_cv.notify_one();
}