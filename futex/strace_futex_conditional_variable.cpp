#include <condition_variable>
#include <mutex>
#include <thread>
#include <cstdio>

std::mutex mtx;
std::condition_variable cv;
bool ready = false;

void worker() {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [] { return ready; });
    std::printf("Worker woke up!\n");
}

int main() {
    std::thread t(worker);

    // Give the worker time to start waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        std::lock_guard<std::mutex> lock(mtx);
        ready = true;
    }
    cv.notify_one();

    t.join();
    return 0;
}