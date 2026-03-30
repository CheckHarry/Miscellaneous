#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <atomic>
#include <iostream>
#include <thread>
#include <vector>
#include <cerrno>   // Required for errno
#include <cstdlib>  // Required for std::exit

using namespace std;

static int futex(uint32_t *uaddr, int op, uint32_t val,
                 const struct timespec *timeout, uint32_t *uaddr2, uint32_t val3)
{
    return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

// Global counter to track how many times EAGAIN saved us from a deadlock!
std::atomic<int> eagain_occurrences{0};

class MyMutex
{
public:
    MyMutex() : m_(0) {}

    void lock() {
        while (true) {
            std::atomic_ref<std::uint32_t> ar(m_);
            std::uint32_t expected = 0;
            
            // Try to grab the lock
            if (ar.compare_exchange_strong(expected, 1)) {
                break;
            }

            // Lock is held, go to sleep expecting the value to be 1
            int res = futex(&m_, FUTEX_WAIT, 1, nullptr, nullptr, 0);
            
            if (res < 0) {
                if (errno == EAGAIN) {
                    // I ADDED THIS LINE: Track when EAGAIN happens
                    eagain_occurrences++; 
                } 
                else if (errno != EAGAIN && errno != EINTR) { // Added EINTR for safety
                    std::cerr << "ERROR " << res << " errno " << errno << '\n';
                    std::exit(-1);
                }
            }
        }
    }

    void unlock() {
        std::atomic_ref<std::uint32_t> ar(m_);
        ar.store(0);
        // Unconditional wake (from your original code)
        futex(&m_, FUTEX_WAKE, 1, nullptr, nullptr, 0);
    }
    
private:
    uint32_t m_;    
};

// --- THE TEST CASE ---

void test() {
    MyMutex lock;
    int shared_counter = 0; // NON-atomic counter to test the mutex
    
    const int num_threads = 100;
    const int increments_per_thread = 10000;

    cout << "Starting " << num_threads << " threads...\n";
    cout << "Each thread will increment the counter " << increments_per_thread << " times.\n";
    cout << "Expected final count: " << (num_threads * increments_per_thread) << "\n\n";

    vector<thread> vec;
    for (int i = 0; i < num_threads; i++) {
        // Fixed the lambda capture: we don't need 'i', but we pass 'lock' and 'shared_counter' by reference
        vec.push_back(thread([&lock, &shared_counter, increments_per_thread]() {
            for (int j = 0; j < increments_per_thread; j++) {
                lock.lock();
                
                // CRITICAL SECTION
                // If the mutex is broken, threads will overwrite each other's work here.
                shared_counter++; 
                
                lock.unlock();
            }
        }));
    }

    // Wait for all threads to finish
    for (auto &th : vec) {
        th.join();
    }

    // --- RESULTS ---
    cout << "========================================\n";
    cout << "Actual final count:   " << shared_counter << '\n';
    
    if (shared_counter == (num_threads * increments_per_thread)) {
        cout << "Result: SUCCESS! The mutex works perfectly.\n";
    } else {
        cout << "Result: FAILED! Race condition detected.\n";
    }
    
    cout << "EAGAIN occurrences:   " << eagain_occurrences.load() << '\n';
    cout << "========================================\n";
}

int main() {
    test();
    return 0;
}