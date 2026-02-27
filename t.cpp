#include <atomic>
#include <thread>
#include <iostream>
#include <unordered_map>
#include <vector>





long long test_relaxed() {
    std::atomic<long long> x;
    long long b = 21;
    
    std::atomic<bool> start{false};
    int res = -1;
    auto th1 = std::thread([&](){
        while (!start.load(std::memory_order_relaxed));
        b = 169;
        x.store(1,std::memory_order_relaxed);
    });
    auto th2 = std::thread([&](){
        while (!start.load(std::memory_order_relaxed));
        
        while (!x.load(std::memory_order_relaxed));
        res = b;
    });

    start.store(true, std::memory_order_relaxed);
    th1.join();
    th2.join();
    return res;
}

int main() {
    
    while (true) {
        if (test_relaxed() != 169) {
            std::cout << "CATCH!\n";
            break;
        }
    }
        
}