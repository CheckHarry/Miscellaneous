#include <thread>
#include <atomic>
#include <vector>
#include <iostream>
using namespace std;


struct Bad {
    atomic<int> c{};
};

struct alignas(64) Good {
    atomic<int> c{};
};

template<typename T>
void t() {
    vector<thread> ths;
    vector<T> v(8);

    for (int i = 0;i < 8;i ++) {
        ths.emplace_back([i,&v](){
            for (int j = 0;j < 10000000;j ++) {
                v[i].c.fetch_add(1);
            }
        });
    }   

    for (auto &th : ths) {
        th.join();
    }
}

void bad() {
    t<Bad>();
}

void good() {
    t<Good>();
}

int main(int argc,char *argv[]) {
    good();
}