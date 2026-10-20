#pragma once
#include <vector>
#include <atomic>
#include <algorithm>
#include <cstring>

namespace Amp5650 {

// SPSC (Single Producer Single Consumer) Lock-Free Circular Ring Buffer
template <typename T>
class SPSCQueue {
public:
    explicit SPSCQueue(size_t capacity = 16384)
        : buffer(capacity), capacity(capacity), head(0), tail(0) {}

    void resize(size_t newCapacity) {
        capacity = newCapacity;
        buffer.resize(capacity);
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
    }

    void reset() {
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
    }

    size_t availableRead() const {
        size_t h = head.load(std::memory_order_acquire);
        size_t t = tail.load(std::memory_order_relaxed);
        return (h >= t) ? (h - t) : (capacity - t + h);
    }

    size_t availableWrite() const {
        return capacity - 1 - availableRead();
    }

    size_t write(const T* data, size_t count) {
        size_t avail = availableWrite();
        size_t toWrite = std::min(count, avail);
        if (toWrite == 0) return 0;

        size_t h = head.load(std::memory_order_relaxed);
        size_t firstPart = std::min(toWrite, capacity - h);
        std::memcpy(&buffer[h], data, firstPart * sizeof(T));

        size_t secondPart = toWrite - firstPart;
        if (secondPart > 0) {
            std::memcpy(&buffer[0], data + firstPart, secondPart * sizeof(T));
        }

        head.store((h + toWrite) % capacity, std::memory_order_release);
        return toWrite;
    }

    size_t read(T* data, size_t count) {
        size_t avail = availableRead();
        size_t toRead = std::min(count, avail);
        if (toRead == 0) return 0;

        size_t t = tail.load(std::memory_order_relaxed);
        size_t firstPart = std::min(toRead, capacity - t);
        std::memcpy(data, &buffer[t], firstPart * sizeof(T));

        size_t secondPart = toRead - firstPart;
        if (secondPart > 0) {
            std::memcpy(data + firstPart, &buffer[0], secondPart * sizeof(T));
        }

        tail.store((t + toRead) % capacity, std::memory_order_release);
        return toRead;
    }

private:
    std::vector<T> buffer;
    size_t capacity;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
};

} // namespace Amp5650
