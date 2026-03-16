#pragma once
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

template <typename T>
class ThreadQueue {
public:
    void push(T item) {
        {
            std::lock_guard lk(mtx_);
            queue_.push(std::move(item));
        }
        cv_.notify_one();
    }

    std::optional<T> pop(std::chrono::milliseconds timeout) {
        std::unique_lock lk(mtx_);
        if (!cv_.wait_for(lk, timeout, [&] { return !queue_.empty(); })) {
            return std::nullopt;
        }
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

private:
    std::queue<T> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
};