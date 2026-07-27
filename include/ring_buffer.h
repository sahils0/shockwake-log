#pragma once

#include <cstddef>
#include <string>
#include <vector>

// RingBuffer: Fixed-size circular queue for log lines
// Overwrites oldest lines when full, keeping memory constant
// Provides snapshot of recent N lines for incident context
class RingBuffer {
public:
  explicit RingBuffer(size_t capacity);

  void push(const std::string &line);

  std::vector<std::string> snapshot() const;

  std::vector<std::string> tail(size_t n) const;

private:
  std::vector<std::string> buffer_;
  size_t capacity_;
  size_t head_ = 0;
  size_t count_ = 0;
};
