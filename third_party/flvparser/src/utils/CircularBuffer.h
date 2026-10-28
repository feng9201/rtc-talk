#pragma once
#include <cstddef>
#include <memory>


namespace FlvParser {
    class CircularBuffer {
        char* buffer_;
        size_t capacity_;
        size_t max_capacity_;
        size_t head_ = 0;
        size_t tail_ = 0;
        size_t size_ = 0;
        std::unique_ptr<char> ptr_{ nullptr };
        size_t ptrSize_ = 0;

        void reallocate(size_t new_capacity);
    public:
        explicit CircularBuffer(size_t initial_capacity = 1024*256, size_t max_capacity = 1024*1024*2);
        ~CircularBuffer();

        bool append(const char* data, size_t length);
        void erase_front(size_t n);
        size_t size() const { return size_; }
        size_t available() const { return capacity_ - size_; }
        size_t capacity() const { return capacity_; }
        size_t max_capacity() const { return max_capacity_; }
        void set_max_capacity(size_t cap) { max_capacity_ = cap; }

        char* data_ptr(size_t size);
        size_t contiguous_size() const {
            return (head_ < tail_) ? (tail_ - head_) : (capacity_ - head_);
        }
    };
}