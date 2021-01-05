// References:
// C++ Concurrency in Action ch. 7.2.4
#ifndef REFCOUNT_STACK_HPP
#define REFCOUNT_STACK_HPP

#include <atomic>
#include <cstdio>
#include <memory>
#include <ostream>
#include <thread>

template <typename T>
class refcount_stack {
private:
  struct node;

  // NOTE: This can be optimized on some platforms. We can limit the size of the
  // counter, and when we know, that our platform has spare bits for a pointer
  // (for example, because the address space is only 48 bits but a pointer is 64
  // bits), we can store the count inside the spare bits of the pointer.
  struct counted_node_ptr {
    int external_count;
    node *ptr;
  };

  struct node {
    std::shared_ptr<T> data;
    std::atomic_int internal_count;
    counted_node_ptr next;

    node(const T &data_)
        : data(new T(data_))
        , internal_count(0)
    {
    }
  };

  size_t count_;
  std::atomic<counted_node_ptr> head;

  void increase_head_count(counted_node_ptr &old_counter)
  {
    counted_node_ptr new_counter;
    do {
      new_counter = old_counter;
      ++new_counter.external_count;
      // head = new_counter
    } while (!head.compare_exchange_strong(old_counter, new_counter));
    old_counter.external_count = new_counter.external_count;
  }

public:
  refcount_stack()
      : count_(0)
  {
  }

  ~refcount_stack()
  {
    T data;
    while (pop(&data))
      ;
  }

  friend std::ostream &operator<<(std::ostream &, const refcount_stack &);

  size_t count() const { return count_; }

  void reinit()
  {
    T data;
    while (pop(&data))
      ;
  }

  void push(T data)
  {
    ++count_;
    counted_node_ptr new_node;
    new_node.ptr = new node(data);
    // internal_count is zero, and the external_count is one. Because this is a
    // new node, there's currently only one external reference to the node (the
    // head pointer itself).
    new_node.external_count = 1;
    new_node.ptr->next = head.load();
    while (!head.compare_exchange_weak(new_node.ptr->next, new_node))
      ;
  }

  bool pop(T *data)
  {
    counted_node_ptr old_head = head.load();
    for (;;) {
      increase_head_count(old_head);
      node *ptr = old_head.ptr;
      // We're at the end of list: no more entires.
      if (!ptr) {
        return false;
      }

      // If the pointer isn't a null pointer, try to remove the node
      if (head.compare_exchange_strong(old_head, ptr->next)) {
        // Take the ownership of the node and swap out the data in preparation
        // for returning it.
        std::shared_ptr<T> res;
        res.swap(ptr->data);

        // 1. We are removed the node from the list
        // 2. We are no longer accessing the node from this thread
        const int count_increase = old_head.external_count - 2;

        // If the reference count is now zero, the previous value
        // (which is what fetch_add returns) was the negative of what
        // you just added, in which case you can delete the node.
        if (ptr->internal_count.fetch_add(count_increase) == -count_increase) {
          delete ptr;
        }

        // Whether or not we deleted the node, we've finished.
        if (res) {
          *data = *res;
          --count_;
          return true;
        }
        return false;
      }
      // If the compare/exchange fails, another thread removed the node
      // before we did, or another thread added a new node to the stack.
      else if (ptr->internal_count.fetch_add(-1) == 1) {
        delete ptr;
      }
    }
  }
};

inline std::ostream &operator<<(std::ostream &os, const refcount_stack<int> &s)
{
  auto h = s.head.load();
  while (h.ptr) {
    // os << *h.ptr->data << " ";
    os << "[" << h.ptr << " next=" << h.ptr->next.ptr << " v=" << *h.ptr->data
       << "]\n";
    h = h.ptr->next;
  }
  os << "\n";
  return os;
}

#endif // REFCOUNTING_QUEUE_HP
