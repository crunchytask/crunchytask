#include "taskqueue/thread_pool.h"

#include <atomic>
#include <chrono>
#include <thread>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("ThreadPool executes submitted tasks", "[thread_pool]") {
  tq::ThreadPool pool(2);
  std::atomic<int> counter{0};

  pool.Submit([&counter]() { counter.fetch_add(1); });
  pool.Submit([&counter]() { counter.fetch_add(1); });
  pool.WaitIdle();

  REQUIRE(counter.load() == 2);
  pool.Shutdown();
}

TEST_CASE("ThreadPool runs tasks concurrently", "[thread_pool]") {
  tq::ThreadPool pool(4);
  std::atomic<int> active{0};
  std::atomic<int> max_active{0};

  for (int i = 0; i < 8; ++i) {
    pool.Submit([&active, &max_active]() {
      const int current = active.fetch_add(1) + 1;
      int observed = max_active.load();
      while (current > observed &&
             !max_active.compare_exchange_weak(observed, current)) {
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      active.fetch_sub(1);
    });
  }

  pool.WaitIdle();
  REQUIRE(max_active.load() > 1);
  pool.Shutdown();
}

TEST_CASE("ThreadPool Shutdown waits for active tasks", "[thread_pool]") {
  tq::ThreadPool pool(2);
  std::atomic<bool> task_finished{false};

  pool.Submit([&task_finished]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    task_finished.store(true);
  });

  pool.Shutdown();
  REQUIRE(task_finished.load());
}

TEST_CASE("ThreadPool limits in-flight work to thread count", "[thread_pool]") {
  tq::ThreadPool pool(2);

  for (int i = 0; i < 4; ++i) {
    pool.Submit([]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    });
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  REQUIRE(pool.InFlightCount() >= 2);
  REQUIRE(pool.InFlightCount() <= 4);

  pool.WaitIdle();
  REQUIRE(pool.InFlightCount() == 0);
  pool.Shutdown();
}
