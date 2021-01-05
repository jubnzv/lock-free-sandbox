#include <atomic>
#include <cassert>
#include <iostream>
#include <memory>
#include <pthread.h>
#include <queue>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "hp_stack.hpp"
#include "refcount_stack.hpp"

#if defined(REFCOUNT)
static refcount_stack<int> s;
#elif defined(HP)
static hp_stack<int> s(128);
#else
#error "Unknown stack!"
#endif

void producer(int ntasks)
{
  for (int i = 0; i < ntasks; ++i) {
    s.push(i);
  }
}

void consumer(int num, int task_consuming_msec)
{
  int my_task = -1;
  for (;;) {
    bool succ = s.pop(&my_task);
    if (!succ) {
      continue;
    }
    if (my_task == 0) {
        s.push(0);
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(task_consuming_msec));
  }
}

// playground def-config
constexpr int NTASKS = 100;
constexpr int TASK_CONSUMING_MSEC = 100;
constexpr int NTHR_START = 4;
constexpr int NTHR_FIN = 10;

int main(int argc, char **argv)
{
  auto ntasks = NTASKS;
  auto task_consuming_msec = TASK_CONSUMING_MSEC;

  nice(0);

  if (argc > 1)
    ntasks = std::stoi(argv[1]);

  if (argc > 2)
    task_consuming_msec = std::stoi(argv[2]);

  // If our platform supplies an implementation for which
  // std::atomic_is_lock_free returns true, the whole memory reclamation issue
  // goes away. In this case we can use a simple queue implementation from
  // listing 7.9 of C++ concurrency in action:
  // https://github.com/anthonywilliams/ccia_code_samples/blob/4e3b2e2be2/listings/listing_7.9.cpp
  //
  // auto test = std::make_shared<int>(42);
  // std::cout << "Lock-free shared_ptr: " << std::boolalpha
  //           << std::atomic_is_lock_free(&test) << std::endl;

  for (int nthr = NTHR_START; nthr < NTHR_FIN; ++nthr) {
    // Add tasks in blocking mode.
    producer(ntasks);

    // Measure time for consuming
    auto tstart = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> vc;
    for (int i = 0; i < nthr; ++i) {
      vc.emplace_back(
          [i, task_consuming_msec] { consumer(i, task_consuming_msec); });

#ifdef SET_PRIORITY
      // Increase priority for consumer threads
      sched_param sch_params;
      sch_params.sched_priority = 60;
      pthread_setschedparam(vc[i].native_handle(), SCHED_RR, &sch_params);
#endif // SET_PRIORITY
    }

    for (int i = 0; i < nthr; ++i) {
      vc[i].join();
    }

    auto tfin = std::chrono::high_resolution_clock::now();

    std::cout << nthr << " "
              << std::chrono::duration_cast<std::chrono::milliseconds>(tfin -
                                                                       tstart)
                     .count()
              << std::endl;
  }
}
