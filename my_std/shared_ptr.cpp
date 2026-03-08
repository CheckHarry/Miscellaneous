#include <atomic>
#include <memory>
#include <iostream>


using namespace std;


template<typename T>
class my_shared_ptr
{   
    public:
    template<typename ...Args>
    explicit my_shared_ptr(Args&&... args) : rc(new std::atomic<int>(1)), raw_ptr(new T(std::forward<Args>(args)...)) {}
    
    my_shared_ptr(const my_shared_ptr<T>& rhs) noexcept : rc(rhs.rc), raw_ptr(rhs.raw_ptr) {
        rc->fetch_add(1);
    }

    my_shared_ptr(my_shared_ptr<T>&& rhs) noexcept : rc(rhs.rc), raw_ptr(rhs.raw_ptr) {
        rhs.rc = nullptr;
        rhs.raw_ptr = nullptr;
    }

    my_shared_ptr& operator=(my_shared_ptr<T> rhs) noexcept {
        swap(rc,rhs.rc);
        swap(raw_ptr,rhs.raw_ptr);
        return *this;
    }

    operator bool() {
        return static_cast<bool>(raw_ptr);
    };

    T* operator->() {
        return raw_ptr;
    };

    const T* operator->() const {
        return raw_ptr;
    };

    T& operator*() {
        return *raw_ptr;
    }

    const T& operator*() const {
        return *raw_ptr;
    }

    
    ~my_shared_ptr() {
        if (rc && rc->fetch_sub(1) == 1) {
            delete raw_ptr;
            delete rc;
        } 
    }
    private:
    std::atomic<int> *rc = nullptr;
    T *raw_ptr = nullptr;
};

struct A
{
    int res = 0;
    void func() {
        
        cout << "hello world " << res ++ << '\n';
    }
    ~A() {
        cout << "A deconstruct ! \n";
    }
};


int main() {
    my_shared_ptr<int> sp(1);
    my_shared_ptr<A> sp_b;
    my_shared_ptr<A> sp_b_2 = sp_b;
    my_shared_ptr<A> sp_b_other;
    sp_b->func();
    (*sp_b).func();
    sp_b_2->func();
    sp_b_other->func();
    sp_b_other = sp_b;
    cout << "END!\n";
}