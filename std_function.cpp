#include <iostream>


template<typename ...Args>
void print(Args&&... args);



void print() {}

template<typename K, typename ...Args>
void print(K &&x, Args&& ... args) {
    std::cout << x;
    print(args...);
}



template<size_t I,typename ...Args>
struct TupleImpl;

template<size_t I>
struct TupleImpl<I> {};

template<size_t I,typename A, typename ...Args>
struct TupleImpl<I, A, Args...> : TupleImpl<I + 1,Args ...> {
    TupleImpl(A&& a,Args&& ...args) :TupleImpl<I + 1,Args ...>(std::forward<Args>(args)...), storage(std::move(a)) {
        
    }
    template <typename U, typename... Us>
    TupleImpl(U&& u, Us&&... us)
    : TupleImpl<I + 1, Args...>(std::forward<Us>(us)...)
    , storage(std::forward<U>(u)) {}
    A storage;

    template<size_t Index>
    auto& get() {
        if constexpr (Index == I) {
            return storage;
        } else {
            return TupleImpl<I + 1,Args ...>::template get <Index>();
        }
    }

    template<size_t Index>
    auto& get() const {
        if constexpr (Index == I) {
            return storage;
        } else {
            return TupleImpl<I + 1,Args ...>::template get <Index>();
        }
    }
};

template<typename ...Args>
struct Tuple : TupleImpl<0,Args...> {
    Tuple(Args&& ...args) : TupleImpl<0,Args...>(std::forward<Args>(args)...) {}

    template<typename ...Us>
    Tuple(Us&& ...args) : TupleImpl<0,Args...>(std::forward<Us>(args)...) {}
};


int main() {
    print(1,2,3);
    int x = 2;
    Tuple<int,int,int> t(1,x,3);
    std::cout << t.get<0>() << '\n';
    std::cout << t.get<1>() << '\n';
    std::cout << t.get<2>() << '\n';
}