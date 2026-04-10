module;

#include <condition_variable>
#include <mutex>

export module semaphore;

export class Semaphore final {
  public:
    void signal();
    void wait();
    explicit Semaphore(int const initCount = 0);
    Semaphore(const Semaphore &) = delete;
    Semaphore(Semaphore &&) = delete;
    Semaphore &operator=(const Semaphore &) = delete;
    Semaphore &operator=(Semaphore &&) = delete;

  private:
    int count;
    std::mutex mutex;
    std::condition_variable cv;
};

Semaphore::Semaphore(int initCount) : count{initCount} {}

void Semaphore::signal() {
    std::lock_guard<std::mutex> lock{mutex};
    ++count;
    cv.notify_one();
}

void Semaphore::wait() {
    std::unique_lock<std::mutex> lock{mutex};
    cv.wait(lock, [&] { return count > 0; });
    --count;
}
