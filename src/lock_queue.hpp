#ifndef LOCK_QUEUE_HPP
#define LOCK_QUEUE_HPP

#include <atomic>
#include <cstddef>
#include <utility>

template <typename T>
class lock_queue {
private:
  struct Node {
    Node(T val)
        : value(std::move(val))
        , next(nullptr)
    {
    }
    T value;
    std::atomic<Node *> next;
  };

  Node *first, *last;
  size_t bufsize_;
  size_t count_;
  std::atomic<Node *> divider_;
  std::atomic<bool> producer_lock_;
  std::atomic<bool> consumer_lock_;

public:
  lock_queue(size_t bufsize)
      : bufsize_(bufsize)
      , count_(0)
  {
    first = divider_ = last = new Node(T{});
    producer_lock_ = consumer_lock_ = false;
  }

  ~lock_queue() { cleanup(); }

  void reinit()
  {
    cleanup();
    first = divider_ = last = new Node(T{});
    producer_lock_ = consumer_lock_ = false;
  }

  // returns false if queue is empty
  bool pop(T *result)
  {
    bool retval = false;

    while (consumer_lock_.exchange(true)) {
    } // lock

    // --- CRITICAL SECTION
    auto divl = divider_.load();
    if (divl->next != nullptr) {
      auto nxt = divl->next.load();
      *result = std::move(nxt->value);
      divider_.store(nxt);
      retval = true;
    }
    // ---

    consumer_lock_ = false; // unlock
    if (retval)
      --count_;
    return retval;
  }

  bool push(T t)
  {
    if (count_ == bufsize_)
      return false;
    Node *tmp = new Node(std::move(t));

    while (producer_lock_.exchange(true)) {
    } // lock

    // --- CRITICAL SECTION
    last->next = tmp;
    last = last->next;

    while (first != divider_) {
      Node *tmp = first;
      first = first->next;
      delete tmp;
    }
    // ---

    producer_lock_ = false; // unlock

    ++count_;
    return true;
  }

private:
  void cleanup()
  {
    while (first != nullptr) {
      Node *tmp = first;
      first = tmp->next;
      delete tmp;
    }
  }
};

#endif // LOCK_QUEUE_HPP
