#include <functional>
#include <mutex>
#include <thread>
#include <vector>
//----------------------------------------------------------------------------------------------------------------------
// DQSim
// (c) 2026 Maximilian Rieger
//----------------------------------------------------------------------------------------------------------------------
namespace dqsim {
//----------------------------------------------------------------------------------------------------------------------
class ThreadPool {
  std::vector<std::thread> workers;
  std::vector<std::function<void()>> tasks;
  std::mutex queueMutex;
  bool stop = false;

 public:
  ThreadPool(size_t nThreads) {
    for (size_t i = 0; i < nThreads; ++i) {
      workers.emplace_back([this]() {
        while (true) {
          std::function<void()> task;
          {
            std::unique_lock lock(queueMutex);
            if (tasks.empty()) {
              if (stop) {
                return;
              }
              std::this_thread::yield();
              continue;
            } else {
              task = tasks.back();
              tasks.pop_back();
            }
          }
          task();
        }
      });
    }
  }

  template <typename F>
  void pushTask(F&& func) {
    std::unique_lock lock(queueMutex);
    tasks.emplace_back(std::forward<F>(func));
  }

  ~ThreadPool() {
    {
      std::unique_lock lock(queueMutex);
      stop = true;
    }
    for (auto& worker : workers) {
      worker.join();
    }
  }
};
//----------------------------------------------------------------------------------------------------------------------
}  // namespace dqsim
//----------------------------------------------------------------------------------------------------------------------
