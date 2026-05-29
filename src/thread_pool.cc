#include "taskqueue/thread_pool.h"

#include <condition_variable>
#include <deque>
#include <thread>
#include <utility>
#include <vector>

namespace tq {

struct ThreadPool::State {
  std::vector<std::thread> threads;
  std::deque<std::function<void()>> tasks;
  mutable std::mutex mutex;
  std::condition_variable task_available;
  std::condition_variable idle;
  std::size_t active_count = 0;
  bool stop = false;
};

ThreadPool::ThreadPool(std::size_t thread_count)
    : thread_count_(thread_count == 0 ? 1 : thread_count),
      state_(std::make_unique<State>()) {
  state_->threads.reserve(thread_count_);
  for (std::size_t i = 0; i < thread_count_; ++i) {
    state_->threads.emplace_back([this]() { WorkerLoop(); });
  }
}

ThreadPool::~ThreadPool() { Shutdown(); }

void ThreadPool::Submit(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->stop) {
      return;
    }
    state_->tasks.push_back(std::move(task));
  }
  state_->task_available.notify_one();
}

void ThreadPool::WaitIdle() {
  std::unique_lock<std::mutex> lock(state_->mutex);
  state_->idle.wait(lock, [this]() {
    return state_->tasks.empty() && state_->active_count == 0;
  });
}

void ThreadPool::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->stop) {
      return;
    }
    state_->stop = true;
  }
  state_->task_available.notify_all();

  for (std::thread& thread : state_->threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }
  state_->threads.clear();
}

std::size_t ThreadPool::ThreadCount() const { return thread_count_; }

std::size_t ThreadPool::ActiveCount() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->active_count;
}

std::size_t ThreadPool::PendingCount() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->tasks.size();
}

std::size_t ThreadPool::InFlightCount() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->tasks.size() + state_->active_count;
}

void ThreadPool::WorkerLoop() {
  while (true) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lock(state_->mutex);
      state_->task_available.wait(lock, [this]() {
        return state_->stop || !state_->tasks.empty();
      });

      if (state_->stop && state_->tasks.empty()) {
        return;
      }

      task = std::move(state_->tasks.front());
      state_->tasks.pop_front();
      ++state_->active_count;
    }

    task();

    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      --state_->active_count;
      if (state_->tasks.empty() && state_->active_count == 0) {
        state_->idle.notify_all();
      }
    }
  }
}

}  // namespace tq
