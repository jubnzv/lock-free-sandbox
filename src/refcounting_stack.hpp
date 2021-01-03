// References:
// C++ Concurrency in Action ch. 7.2.4
#ifndef REFCOUNTING_STACK_HPP
#define REFCOUNTING_STACK_HPP

#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>

template <typename T>
class refcounting_stack {
private:
  struct node;

  // The external count is kept alongside the pointer to the node and is
  // increased every time the pointer is read.
  // with the node, it decrease the internal count. A simple operation that
  // reads the pointer will thus leave the external count increased by one
  // and the internal count decreased by one when it's finished.
  //
  // When the external count/pointer pairing is no longer required (that is,
  // the node is no longer accessible from a location accessible to multiple
  // threads), the internal count is increased by the value of the external
  // count minus one and external counter is discarded. Once the internal
  // count is equal to zero, there are no outstanding references to the node
  // and it can be safely deleted.
  //
  // NOTE: This can be optimized on some platforms. We can limit the size of the
  // counter, and when we know, that our platform has spare bits for a pointer
  // (for example, because the address space is only 48 bits but a pointer is 64
  // bits), we can store the count inside the spare bits of the pointer.
  struct counted_node_ptr {
    int external_count;
    node *ptr;
  };

  size_t bufsize_;

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

  std::atomic<counted_node_ptr> head;
  void increase_head_count(counted_node_ptr &old_counter)
  {
    counted_node_ptr new_counter;

    do {
      new_counter = old_counter;
      ++new_counter.external_count;
    } while (!head.compare_exchange_strong(old_counter, new_counter));

    old_counter.external_count = new_counter.external_count;
  }

public:
  refcounting_stack(size_t bufsize)
      : bufsize_(bufsize)
  {
  }

  ~refcounting_stack()
  {
    T data;
    while (pop(&data))
      ;
  }

  void reinit()
  {
    T data;
    while (pop(&data))
      ;
  }

  bool push(T data)
  {
    counted_node_ptr new_node;
    new_node.ptr = new node(data);
    // internal_count is zero, and the external_count is one. Because this is a
    // new node, there's currently only one external reference to the node (the
    // head pointer itself).
    new_node.external_count = 1;
    new_node.ptr->next = head.load();
    while (!head.compare_exchange_weak(new_node.ptr->next, new_node))
      ;
    return true;
  }

  bool pop(T *data)
  {
    counted_node_ptr old_head = head.load();
    for (;;) {
      increase_head_count(old_head);
      node *ptr = old_head.ptr;
      // If the pointer is a null pointer, you're at the end of list:
      // no more entires
      if (!ptr) {
        return false;
      }

      // If the pointer isn't a null pointer, try to remove the node
      if (head.compare_exchange_strong(old_head, ptr->next)) {
        // You've taken the ownership of the node and can swap out
        // the data in preparation for returning it.
        std::shared_ptr<T> res;
        res.swap(ptr->data);

        // You've removed the node from the list, so you drop one
        // off the count for that, and you're no longer accessing
        // the node from this thread, so you drop another off the
        // count for that.
        const int count_increase = old_head.external_count - 2;

        // If the reference count is now zero, the previous value
        // (which is what fetch_add returns) was the negative of what
        // you just added, in which case you can delete the node.
        if (ptr->internal_count.fetch_add(count_increase) == -count_increase) {
          delete ptr;
        }

        // Whether or not you deleted the node, you've finished.
        data = res.get();
        return res != nullptr;
      }
      // If the compare/exchange fails, another thread removed the node
      // before we did, or another thread added a new node to the stack.
      else if (ptr->internal_count.fetch_add(-1) == 1) {
        delete ptr;
      }
    }
  }
};

#endif // REFCOUNTING_QUEUE_HP
