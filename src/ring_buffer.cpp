#include "ring_buffer.h"

RingBuffer::RingBuffer(size_t capacity)
    : capacity_(capacity), buffer_(capacity)
{
}

void RingBuffer::push(const std::string &line)
{
    buffer_[head_] = line;
    head_ = (head_ + 1) % capacity_;
    if (count_ < capacity_)
        count_++;
}

std::vector<std::string> RingBuffer::snapshot() const
{
    std::vector<std::string> result;
    result.reserve(count_);

    size_t start = (count_ < capacity_) ? 0 : head_;
    for (size_t i = 0; i < count_; i++)
    {
        result.push_back(buffer_[(start + i) % capacity_]);
    }
    return result;
}

std::vector<std::string> RingBuffer::tail(size_t n) const
{
    auto snap = snapshot();
    if (n >= snap.size())
        return snap;
    return std::vector<std::string>(snap.end() - n, snap.end());
}