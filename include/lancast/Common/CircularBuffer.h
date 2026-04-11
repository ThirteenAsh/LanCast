#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>

namespace lancast {

// Thread-safe circular buffer for producer-consumer pattern
template<typename T>
class CircularBuffer {
public:
    explicit CircularBuffer(size_t capacity)
        : capacity_(capacity), buffer_(capacity) {
        // assert(capacity > 0);
    }

    // Producer: push item, block if full
    void push(T item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_full_.wait(lock, [this]() { return !full(); });

        buffer_[write_pos_] = std::move(item);
        write_pos_ = (write_pos_ + 1) % capacity_;
        ++size_;

        cv_empty_.notify_one();
    }

    // Producer: push with timeout
    bool push(T item, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_full_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                               [this]() { return !full(); })) {
            return false;
        }

        buffer_[write_pos_] = std::move(item);
        write_pos_ = (write_pos_ + 1) % capacity_;
        ++size_;

        cv_empty_.notify_one();
        return true;
    }

    // Consumer: pop item, block if empty
    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_empty_.wait(lock, [this]() { return !empty(); });

        T item = std::move(buffer_[read_pos_]);
        read_pos_ = (read_pos_ + 1) % capacity_;
        --size_;

        cv_full_.notify_one();
        return item;
    }

    // Consumer: pop with timeout
    bool pop(T& item, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!cv_empty_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                [this]() { return !empty(); })) {
            return false;
        }

        item = std::move(buffer_[read_pos_]);
        read_pos_ = (read_pos_ + 1) % capacity_;
        --size_;

        cv_full_.notify_one();
        return true;
    }

    // Non-blocking peek
    bool peek(T& item) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (empty()) return false;
        item = buffer_[read_pos_];
        return true;
    }

    bool empty() const { return size_ == 0; }
    bool full() const { return size_ == capacity_; }
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        read_pos_ = 0;
        write_pos_ = 0;
        size_ = 0;
    }

private:
    const size_t capacity_;
    std::vector<T> buffer_;
    size_t read_pos_ = 0;
    size_t write_pos_ = 0;
    size_t size_ = 0;

    mutable std::mutex mutex_;
    std::condition_variable cv_empty_;
    std::condition_variable cv_full_;
};

}  // namespace lancast
