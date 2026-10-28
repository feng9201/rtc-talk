#include "CircularBuffer.h"
#include <cstring>
#include <algorithm>
#include <iostream>

namespace FlvParser {
    CircularBuffer::CircularBuffer(size_t initial_capacity, size_t max_capacity)
        : capacity_(initial_capacity), max_capacity_(max_capacity) {
        buffer_ = new char[initial_capacity];
    }

    CircularBuffer::~CircularBuffer() {
        delete[] buffer_;
        buffer_ = nullptr;
    }

    bool CircularBuffer::append(const char* data, size_t length) {
        if (available() < length) {
            size_t new_cap;
            if (capacity_ > 1024 * 512) {
                // >512KB: 以256KB为基数增长，避免翻倍过大
                size_t need = length - available();
                size_t extra = ((need + 256 * 1024 - 1) / (256 * 1024)) * (256 * 1024);
                new_cap = capacity_ + extra;
            } else {
                new_cap = std::max(capacity_ * 2, size_ + length);
            }

            if (new_cap > max_capacity_) {
                new_cap = max_capacity_;
            }

            if (new_cap > capacity_) {
                std::cout << "[CircularBuffer] append trigger realloc: cap " << capacity_
                          << " -> " << new_cap << ", size=" << size_ << ", need=" << length << std::endl;
                reallocate(new_cap);
            }

            // 达到上限仍不够，拒绝写入
            if (available() < length) {
                std::cout << "[CircularBuffer] append rejected: size=" << size_
                          << ", need=" << length << ", max=" << max_capacity_ << std::endl;
                return false;
            }
        }

        size_t first_chunk = std::min(length, capacity_ - tail_);
        memcpy(buffer_ + tail_, data, first_chunk);

        if (first_chunk < length) {
            memcpy(buffer_, data + first_chunk, length - first_chunk);
        }

        tail_ = (tail_ + length) % capacity_;
        size_ += length;
        return true;
    }

    void CircularBuffer::erase_front(size_t n) {
        n = std::min(n, size_);
        head_ = (head_ + n) % capacity_;
        size_ -= n;

        //// 自动收缩内存
        if (size_ < capacity_ / 4 && capacity_ > 1024*512) {
            reallocate(capacity_ / 2);
        }
    }

    char* CircularBuffer::data_ptr(size_t size)
    {
        if (size <= 0)
            return nullptr;
        if (ptrSize_ < size) {
            ptrSize_ = size;
            ptr_.reset(new char[ptrSize_] {0});
        }
        size_t first_chunk = std::min(size, capacity_ - head_);
        memcpy(ptr_.get(), buffer_ + head_, first_chunk);
        if (first_chunk < size) {
            memcpy(ptr_.get() + first_chunk, buffer_, size - first_chunk);
        }

        return ptr_.get();
    }

    void CircularBuffer::reallocate(size_t new_capacity) {
        std::cout << "[CircularBuffer] reallocate: " << capacity_ << " -> " << new_capacity
                  << ", size=" << size_ << std::endl;
        char* new_buffer = new char[new_capacity];

        if (size_ != 0) {
            size_t first_chunk = contiguous_size();
            memcpy(new_buffer, buffer_ + head_, first_chunk);

            if (size_ > first_chunk) {
                memcpy(new_buffer + first_chunk, buffer_, size_ - first_chunk);
            }
        }

        delete[] buffer_;
        buffer_ = nullptr;
        buffer_ = new_buffer;
        capacity_ = new_capacity;
        head_ = 0;
        tail_ = (size_ == 0 ? 0 : size_ % capacity_);
    }
}