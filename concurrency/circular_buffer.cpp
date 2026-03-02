#include <atomic>
#include <vector>
#include <type_traits>
#include <assert.h>
#include <iostream>
#include <thread>

using namespace std;



template<typename T>
class CircularBuffer   
{
public:        vector<T> data;
        size_t total_size;
        alignas(64) atomic<size_t> start_;
        alignas(64) atomic<size_t> end_;
        
    
        explicit CircularBuffer(size_t n) : data(n + 1),total_size(n + 1), start_{}, end_{} {
            
        }

        bool is_full() {
            size_t cur_start = start_.load(std::memory_order_acquire);
            size_t cur_end = end_.load(std::memory_order_acquire);

            if ((cur_end + 1) % total_size == cur_start) {
                return true;
            }
            return false;
        }

        bool is_empty() {
            size_t cur_start = start_.load(std::memory_order_acquire);
            size_t cur_end = end_.load(std::memory_order_acquire);

            if (cur_start == cur_end) {
                return true;
            }
            return false;
        }

        bool push(const T& v) {
            if (is_full()) {
                return false;
            }

            size_t end = end_.load(std::memory_order_acquire);
            data[end] = v;
            end_.store((end + 1) % total_size, std::memory_order_release);

            return true;
        }

        // non defined if empty
        T pop() {
            size_t start = start_.load(std::memory_order_acquire);
            auto res = data[start];
            start_.store((start + 1) % total_size, std::memory_order_release);

            return res;
        }

   
};


void test_basic_operations() {
    CircularBuffer<int> c(3);
    
    assert(c.is_empty());
    assert(!c.is_full());

    assert(c.push(1));
    assert(c.push(2));
    assert(c.push(3));
    
    assert(!c.is_empty());
    assert(c.is_full());
    
    // Pushing to a full buffer should fail
    assert(!c.push(4));

    assert(c.pop() == 1);
    assert(!c.is_full());
    
    assert(c.pop() == 2);
    assert(c.pop() == 3);
    
    assert(c.is_empty());
    cout << "test_basic_operations passed." << endl;
}

void test_wrap_around() {
    CircularBuffer<int> c(3);
    
    // Push 3, pop 3 (internal pointers should move)
    c.push(10);
    c.push(20);
    c.push(30);
    c.pop(); // removes 10
    c.pop(); // removes 20
    
    // Buffer now has 1 item (30). Let's push more to force wrap-around
    assert(c.push(40));
    assert(c.push(50));
    assert(c.is_full()); // Contains 30, 40, 50
    
    assert(c.pop() == 30);
    assert(c.pop() == 40);
    assert(c.pop() == 50);
    assert(c.is_empty());

    cout << "test_wrap_around passed." << endl;
}

void test_spsc_concurrency() {
    // Single Producer Single Consumer Test
    const int NUM_ITEMS = 100000;
    CircularBuffer<int> c(1024); // Buffer size of 1024
    
    std::thread producer([&]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            // Spin until there is room to push
            while (!c.push(i)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        for (int i = 0; i < NUM_ITEMS; ++i) {
            // Spin until there is an item to pop
            while (c.is_empty()) {
                std::this_thread::yield();
            }
            int val = c.pop();
            assert(val == i); // Order must be strictly preserved
        }
    });

    producer.join();
    consumer.join();
    
    assert(c.is_empty());
    cout << "test_spsc_concurrency passed." << endl;
}

int main() {
    test_basic_operations();
    test_wrap_around();
    test_spsc_concurrency();
    
    cout << "All tests passed successfully!" << endl;
    return 0;
}