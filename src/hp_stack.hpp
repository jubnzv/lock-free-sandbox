// Naive implementation of lock-free stack based on hazard pointers.
// References: C++ Concurrency in Action ch. 7.2.
#ifndef HP_STACK_HPP
#define HP_STACK_HPP

#include <atomic>
#include <cstddef>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

using hp_t = std::atomic<void *> &;

static constexpr int s_max_hazard_pointers = 100;

struct hazard_pointer {
  std::atomic<std::thread::id> id;
  std::atomic<void *> pointer;
};

static hazard_pointer s_hazard_pointers[s_max_hazard_pointers];

class hp_owner {
  hazard_pointer *hp;

public:
  hp_owner()
      : hp(nullptr)
  {
    for (int i = 0; i < s_max_hazard_pointers; ++i) {
      std::thread::id old_id;
      if (s_hazard_pointers[i].id.compare_exchange_strong(
              old_id, std::this_thread::get_id())) {
        hp = &s_hazard_pointers[i];
        break;
      }
    }
    if (!hp)
      throw std::runtime_error("No hazard pointers available");
  }

  ~hp_owner()
  {
    hp->pointer.store(nullptr);
    hp->id.store(std::thread::id());
  }

  hp_owner(hp_owner const &) = delete;
  hp_owner operator=(hp_owner const &) = delete;

  hp_t get_pointer() { return hp->pointer; }
};

static inline hp_t get_hazard_pointer_for_current_thread()
{
  thread_local static hp_owner hazard; // Each thread has its own hp
  return hazard.get_pointer();
}

static inline bool outstanding_hazard_pointers_for(void *p)
{
  for (int i = 0; i < s_max_hazard_pointers; ++i) {
    if (s_hazard_pointers[i].pointer.load() == p)
      return true;
  }
  return false;
}

template <typename T>
static inline void do_delete(void *p)
{
  delete static_cast<T *>(p);
}

struct data_to_reclaim {
  void *data;
  std::function<void(void *)> deleter;
  data_to_reclaim *next;

  template <typename T>
  data_to_reclaim(T *p)
      : data(p)
      , deleter(&do_delete<T>)
      , next(0)
  {
  }
  ~data_to_reclaim() { deleter(data); }
};

static std::atomic<data_to_reclaim *> nodes_to_reclaim;

static inline void add_to_reclaim_list(data_to_reclaim *node)
{
  node->next = nodes_to_reclaim.load();
  while (!nodes_to_reclaim.compare_exchange_weak(node->next, node))
    ;
}

template <typename T>
static inline void reclaim_later(T *data)
{
  add_to_reclaim_list(new data_to_reclaim(data));
}

static inline void delete_nodes_with_no_hazards()
{
  // First claims the entire list of nodes to be reclaimed: ensure that this is
  // the only thread trying to reclaim this particular set of nodes; other
  // threads are now free to add further nodes to the list or event try to
  // reclaim them without impacting the operation of this thread.
  data_to_reclaim *current = nodes_to_reclaim.exchange(nullptr);

  while (current) {
    data_to_reclaim *const next = current->next;

    if (!outstanding_hazard_pointers_for(current->data)) {
      delete current;
    } else {
      add_to_reclaim_list(current);
    }
    current = next;
  }
}

template <typename T>
class hp_stack {
  struct node {
    T data;
    node *next;
    node(T data)
        : data(std::move(data))
    {
    }
  };

  std::atomic<node *> head;
  size_t bufsize_;

public:
  hp_stack(size_t bufsize)
      : bufsize_(bufsize)
  {
  }

  ~hp_stack()
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
    node *const new_node = new node(data);
    new_node->next = head.load();
    while (!head.compare_exchange_weak(new_node->next, new_node))
      ;
    return true;
  }

  bool pop(T *data)
  {
    hp_t hp = get_hazard_pointer_for_current_thread();
    node *old_head = head.load(); // read pointer to the old head
    do {
      // Loop until we've set the hazard pointer to head
      node *temp;
      do {
        temp = old_head;
        hp.store(old_head); // set hazard pointer
        old_head = head.load();
        // If the old head node is going to be deleted, head itself must have
        // changed, so we can check this and keep looping until we know that the
        // head pointer still has the same value we set our hazard pointer to.
      } while (old_head != temp);
      // We're using compare_exchange_strong here because we're doing work
      // inside the while loop: a spurious failure on compare_exchange_weak
      // would result in resetting the hazard pointer unnecessarily.
    } while (old_head &&
             !head.compare_exchange_strong(old_head, old_head->next));

    // Clear hazard pointer once we're finished
    hp.store(nullptr);

    // Now we can proceed, safe in the knowledge that no other thread will
    // delete the nodes from under us.

    if (old_head) {
      *data = old_head->data;
      // Check for hazard pointers referencing a node before we delete it.
      if (outstanding_hazard_pointers_for(old_head)) {
        reclaim_later(old_head);
      } else {
        delete old_head;
      }
      delete_nodes_with_no_hazards();
    }
    return old_head != nullptr;
  }
};

#endif // HP_QUEUE_HP
