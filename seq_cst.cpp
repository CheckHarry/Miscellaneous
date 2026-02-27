#include <atomic>
#include <thread>
#include <iostream>
#include <unordered_map>


std::pair<int,int> test_relax() {
    std::atomic<long long> x, y;
    x = 0;
    y = 0;

    std::atomic<bool> start{false};
    int xx = 0,yy = 0;
    auto th1 = std::thread([&](){
        while (!start.load(std::memory_order_relaxed));
        y.store(1, std::memory_order_relaxed);
        xx = x.load(std::memory_order_relaxed);
    });
    auto th2 = std::thread([&](){
        while (!start.load(std::memory_order_relaxed));
        x.store(1, std::memory_order_relaxed);
        yy = y.load(std::memory_order_relaxed);
    });

    start.store(true, std::memory_order_relaxed);
    th1.join();
    th2.join();
    return {xx,yy};
}

std::pair<int,int> test_acquire_release() {
    std::atomic<long long> x, y;
    x = 0;
    y = 0;

    std::atomic<bool> start{false};
    int xx = 0,yy = 0;
    auto th1 = std::thread([&](){
        while (!start.load(std::memory_order_relaxed));
        y.store(1, std::memory_order_release);
        xx = x.load(std::memory_order_acquire);
    });
    auto th2 = std::thread([&](){
        while (!start.load(std::memory_order_relaxed));
        x.store(1, std::memory_order_release);
        yy = y.load(std::memory_order_acquire);
    });

    start.store(true, std::memory_order_relaxed);
    th1.join();
    th2.join();
    return {xx,yy};
}

std::pair<int,int> test_seq_cst() {
    std::atomic<long long> x, y;
    x = 0;
    y = 0;

    std::atomic<bool> start{false};
    int xx = 0,yy = 0;
    auto th1 = std::thread([&](){
        while (!start.load(std::memory_order_relaxed));
        y.store(1, std::memory_order_seq_cst);
        xx = x.load(std::memory_order_seq_cst);
    });
    auto th2 = std::thread([&](){
        while (!start.load(std::memory_order_relaxed));
        x.store(1, std::memory_order_seq_cst);
        yy = y.load(std::memory_order_seq_cst);
    });

    start.store(true, std::memory_order_relaxed);
    th1.join();
    th2.join();
    return {xx,yy};
}

int main() {
    while (true) {
        auto [x,y] = test_seq_cst();
        //std::cout << x << " " << y << '\n';
        if (x == 0 && y == 0) {
            std::cout << "CATCH!\n";
            break;
        }
    }
        
}