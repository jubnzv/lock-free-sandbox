// http://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue
#ifndef MPMC_BOUNDED_QUEUE_HPP
#define MPMC_BOUNDED_QUEUE_HPP

#include <atomic>
#include <cassert>
#include <memory>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

template <typename T>
class mpmc_bounded_queue {
  struct cell_t {
    std::atomic<size_t> sequence_;
    T data_;
  };

  alignas(64) cell_t *const buffer_;
  alignas(64) size_t const buffer_mask_;
  alignas(64) std::atomic<size_t> enqueue_pos_;
  alignas(64) std::atomic<size_t> dequeue_pos_;

public:
  mpmc_bounded_queue(size_t bufsize)
      : buffer_(new cell_t[bufsize])
      , buffer_mask_(bufsize - 1)
  {
    // Assume buffer is 2-aligned
    assert((bufsize >= 2) && ((bufsize & (bufsize - 1)) == 0));
    reinit();
  }

  ~mpmc_bounded_queue() { delete[] buffer_; }

  void reinit()
  {
    size_t bufsize = buffer_mask_ + 1;
    // Fill sequence
    for (size_t i = 0; i != bufsize; ++i)
      buffer_[i].sequence_.store(i, std::memory_order_relaxed);

    // Start enqueue and dequeue with 0
    enqueue_pos_.store(0, std::memory_order_relaxed);
    dequeue_pos_.store(0, std::memory_order_relaxed);
  }

  // We assume that push might not be successful
  bool push(T data)
  {
    cell_t *cell;
    size_t pos;
    bool res = false;

    // CAS loop
    while (!res) {
      pos = enqueue_pos_.load(std::memory_order_relaxed);

      // Using (p & bufmask) instead of (p % bufsize)
      cell = &buffer_[pos & buffer_mask_];
      auto seq = cell->sequence_.load(std::memory_order_acquire);
      intptr_t diff = intptr_t(seq) - intptr_t(pos);

      // Oops, queue moved indeterminately forward
      if (diff < 0)
        return false;

      // If we are guessed enqueue_pos_ => we are done
      if (diff == 0)
        res = enqueue_pos_.compare_exchange_weak(pos, pos + 1,
                                                 std::memory_order_relaxed);
    }

    cell->data_ = std::move(data);
    cell->sequence_.store(pos + 1, std::memory_order_release);
    return true;
  }

  // We assume pop to may fail too
  bool pop(T *data)
  {
    cell_t *cell;
    size_t pos;
    bool res = false;
    assert(data);

    // CAS loop
    while (!res) {
      pos = dequeue_pos_.load(std::memory_order_relaxed);
      cell = &buffer_[pos & buffer_mask_];
      auto seq = cell->sequence_.load(std::memory_order_acquire);
      intptr_t diff = intptr_t(seq) - intptr_t(pos + 1);

      // The same unlucky case
      if (diff < 0)
        return false;

      // If we are guessed dequeue_pos_ => we are done
      if (diff == 0)
        res = dequeue_pos_.compare_exchange_weak(pos, pos + 1,
                                                 std::memory_order_relaxed);
    }

    *data = std::move(cell->data_);
    cell->sequence_.store(pos + buffer_mask_ + 1, std::memory_order_release);
    return true;
  }
};

#endif // MPMC_BOUNDED_QUEUE_HPP
