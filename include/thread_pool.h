#pragma once

#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <cstddef>

class ThreadPool {
public:
    ThreadPool(size_t num_threads);
    ~ThreadPool();

    void queue(std::function<void()> job);

private:
    void worker();

    std::vector<std::thread>        m_threads;
    std::deque<std::function<void()>> m_queue;
    std::mutex                      m_mutex;
    std::condition_variable         m_cv;
    std::atomic<bool>               m_stop;
};