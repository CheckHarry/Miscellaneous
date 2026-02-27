#include <iostream>
#include <atomic>
#include <vector>
#include <thread>
#include <mutex>
#include <functional>
#include <list>
#include <memory>
#include <chrono>

// ----------------------------------------------------------------------
// RCU Core Implementation (Epoch Based)
// ----------------------------------------------------------------------

class RCU {
private:
    struct RetiredNode {
        std::function<void()> deleter; // Function to free the memory
        uint64_t epoch;                // The epoch when this was retired
    };

    // Global epoch counter
    std::atomic<uint64_t> global_epoch_{0};

    // Thread-local storage to track active readers
    // In a real production system, this would be a sophisticated registry.
    // Here, we use a simplified list protected by a mutex for registration.
    struct ThreadState {
        std::atomic<uint64_t> local_epoch{0}; // 0 means not reading
        std::atomic<bool> active{false};
    };

    // We need a way to track all threads. 
    // Note: In production, use a lock-free list or fixed array to avoid the mutex.
    std::list<std::shared_ptr<ThreadState>> registry_;
    std::mutex registry_mtx_;

    // List of objects waiting to be freed
    std::list<RetiredNode> retired_list_;
    std::mutex retired_mtx_;

    // Thread-local pointer to this thread's state
    static thread_local std::shared_ptr<ThreadState> my_state_;

public:
    RCU() = default;

    // ---------------------------------------------------------
    // Thread Registration (Must be called once per thread)
    // ---------------------------------------------------------
    void register_thread() {
        std::lock_guard<std::mutex> lock(registry_mtx_);
        my_state_ = std::make_shared<ThreadState>();
        registry_.push_back(my_state_);
    }

    void unregister_thread() {
        std::lock_guard<std::mutex> lock(registry_mtx_);
        if (my_state_) {
            registry_.remove(my_state_);
            my_state_ = nullptr;
        }
    }

    // ---------------------------------------------------------
    // Read-Side Primitives
    // ---------------------------------------------------------
    void read_lock() {
        if (!my_state_) register_thread();
        
        // Announce we are entering the current epoch
        uint64_t epoch = global_epoch_.load(std::memory_order_acquire);
        my_state_->local_epoch.store(epoch, std::memory_order_release);
        my_state_->active.store(true, std::memory_order_release);
        
        // Re-check global epoch to ensure we didn't miss a wrap-around 
        // or rapid update (optimization step, strictly optional for basic correctness)
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    void read_unlock() {
        if (my_state_) {
            // Mark as inactive (0 or specific flag)
            my_state_->active.store(false, std::memory_order_release);
        }
    }

    // ---------------------------------------------------------
    // Write-Side / Reclamation Primitives
    // ---------------------------------------------------------
    
    // Schedule an object for deletion (equivalent to call_rcu)
    template<typename T>
    void retire(T* ptr) {
        if (!ptr) return;

        std::lock_guard<std::mutex> lock(retired_mtx_);
        
        // We capture the CURRENT global epoch.
        // Readers in this epoch or earlier might still be looking at ptr.
        retired_list_.push_back({
            [ptr]() { delete ptr; },
            global_epoch_.load(std::memory_order_acquire)
        });
    }

    // Advance epoch and free old objects (synchronize_rcu logic)
    void synchronize() {
        // 1. Advance the global epoch
        uint64_t current_global = global_epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;

        // 2. Identify the "Minimum Active Epoch" among all readers
        // We need to find the oldest epoch any reader is currently stuck in.
        uint64_t min_epoch = current_global;

        {
            std::lock_guard<std::mutex> lock(registry_mtx_);
            for (auto& state : registry_) {
                if (state->active.load(std::memory_order_acquire)) {
                    uint64_t reader_epoch = state->local_epoch.load(std::memory_order_acquire);
                    if (reader_epoch < min_epoch) {
                        min_epoch = reader_epoch;
                    }
                }
            }
        }

        // 3. Reclaim memory
        // We can free any node whose retirement epoch is strictly LESS than the min_epoch.
        // If a node was retired at epoch 5, and the oldest reader is now at epoch 6,
        // it is safe to delete.
        std::lock_guard<std::mutex> lock(retired_mtx_);
        auto it = retired_list_.begin();
        while (it != retired_list_.end()) {
            if (it->epoch < min_epoch) {
                it->deleter(); // Actually delete the memory
                it = retired_list_.erase(it);
            } else {
                ++it;
            }
        }
    }
};

// Initialize the thread_local static member
thread_local std::shared_ptr<RCU::ThreadState> RCU::my_state_ = nullptr;

// Global RCU instance
RCU g_rcu;


// ----------------------------------------------------------------------
// Usage Example
// ----------------------------------------------------------------------

struct Config {
    int setting_a;
    int setting_b;
};

// The shared pointer that readers access
std::atomic<Config*> global_config{new Config{10, 20}};

void reader_thread(int id) {
    // Simulate periodic reading
    for (int i = 0; i < 5; ++i) {
        g_rcu.read_lock(); 
        
        // --- Critical Section ---
        Config* cfg = global_config.load(std::memory_order_consume);
        if (cfg) {
            // We can safely read 'cfg' here. It won't be deleted.
            printf("Reader %d read: %d, %d\n", id, cfg->setting_a, cfg->setting_b);
        }
        // Simulate work
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // --- End Critical Section ---
        g_rcu.read_unlock();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    g_rcu.unregister_thread();
}

void writer_thread() {
    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        
        // 1. Copy
        Config* old_cfg = global_config.load(std::memory_order_relaxed);
        Config* new_cfg = new Config(*old_cfg);
        
        // 2. Update
        new_cfg->setting_a += 100;
        new_cfg->setting_b += 100;
        printf("Writer updating to: %d, %d\n", new_cfg->setting_a, new_cfg->setting_b);

        // 3. Publish (Atomic Swap)
        global_config.store(new_cfg, std::memory_order_release);

        // 4. Retire the old data (Defer deletion)
        g_rcu.retire(old_cfg);

        // 5. Do maintenance (advance epoch and clean up)
        // In a real system, this might happen in a background thread
        g_rcu.synchronize();
    }
    g_rcu.unregister_thread();
}

int main() {
    std::vector<std::thread> threads;

    // Start readers
    for (int i = 0; i < 3; ++i) {
        threads.emplace_back(reader_thread, i);
    }

    // Start writer
    threads.emplace_back(writer_thread);

    for (auto& t : threads) {
        t.join();
    }
    
    // Cleanup final pointer
    delete global_config.load();

    return 0;
}