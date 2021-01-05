#include <atomic>
#include <cassert>
#include <iostream>
#include <memory>
#include <pthread.h>
#include <queue>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "lock_queue.hpp"
#include "mpmc_bounded_queue.hpp"

#if defined(LOCK)
static lock_queue<int> q(128);
#elif defined(MPMC)
static mpmc_bounded_queue<int> q(128);
#else
#error "Unknown queue!"
#endif

void producer(int ntasks, int task_producing_msec)
{
  for (int i = 0; i < ntasks; ++i) {
    bool succ = false;
    while (!succ) {
      succ = q.push(i);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(task_producing_msec));
  }

  // nullptr task to notify consumers to shutdown
  q.push(-1);
}

void consumer(int num, int task_consuming_msec)
{
  int my_task = -1;
  for (;;) {
    bool succ = q.pop(&my_task);
    if (!succ) {
      continue;
    }
    if (my_task == -1) {
      succ = q.push(-1);
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(task_consuming_msec));
  }
}

// playground def-config
constexpr int NTASKS = 100;
constexpr int TASK_PRODUCING_MSEC = 10;
constexpr int TASK_CONSUMING_MSEC = 100;
constexpr int NTHR_START = 4;
constexpr int NTHR_FIN = 10;

int main(int argc, char **argv)
{
  auto ntasks = NTASKS;
  auto task_producing_msec = TASK_PRODUCING_MSEC;
  auto task_consuming_msec = TASK_CONSUMING_MSEC;

  nice(0);

  if (argc > 1)
    ntasks = std::stoi(argv[1]);

  if (argc > 2)
    task_producing_msec = std::stoi(argv[2]);

  if (argc > 3)
    task_consuming_msec = std::stoi(argv[3]);

  for (int nthr = NTHR_START; nthr < NTHR_FIN; ++nthr) {
    auto tstart = std::chrono::high_resolution_clock::now();

    std::thread p{[ntasks, task_producing_msec] {
      producer(ntasks, task_producing_msec);
    }};

#ifdef SET_PRIORITY
    // Set higher priority for producer thread
    sched_param sch_params;
    sch_params.sched_priority = 99;
    pthread_setschedparam(p.native_handle(), SCHED_RR, &sch_params);
#endif // SET_PRIORITY

    std::vector<std::thread> vc;
    for (int i = 0; i < nthr; ++i) {
      vc.emplace_back(
          [i, task_consuming_msec] { consumer(i, task_consuming_msec); });

#ifdef SET_PRIORITY
      // Decrease priority for consumer threads
      sched_param sch_params;
      sch_params.sched_priority = 75;
      pthread_setschedparam(vc[i].native_handle(), SCHED_RR, &sch_params);
#endif // SET_PRIORITY
    }

    p.join();
    for (int i = 0; i < nthr; ++i) {
      vc[i].join();
    }

    auto tfin = std::chrono::high_resolution_clock::now();

    q.reinit();

    std::cout << nthr << " "
              << std::chrono::duration_cast<std::chrono::milliseconds>(tfin -
                                                                       tstart)
                     .count()
              << std::endl;
  }
}
