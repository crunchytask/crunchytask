#ifndef TASKQUEUE_THREAD_POOL_H_
#define TASKQUEUE_THREAD_POOL_H_

#include <cstddef>
#include <functional>
#include <memory>

namespace tq {

class ThreadPool {
 public:
  explicit ThreadPool(std::size_t thread_count);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  void Submit(std::function<void()> task);
  void WaitIdle();
  void Shutdown();

  std::size_t ThreadCount() const;
  std::size_t ActiveCount() const;
  std::size_t PendingCount() const;
  std::size_t InFlightCount() const;

 private:
  void WorkerLoop();

  const std::size_t thread_count_;
  struct State;
  std::unique_ptr<State> state_;
};

}  // namespace tq

#endif  // TASKQUEUE_THREAD_POOL_H_
