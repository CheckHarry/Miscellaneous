#include <iostream>
#include <memory>
#include <cassert>
#include <cstring>
#include <iostream>
#include <cassert>
#include <vector>
#include <set>


using namespace std;


void* extract_ptr(__uint128_t t) {
    __uint128_t a = 1;
    a = (a << 65);
    return (void*)(t & (a - 1));
}

__uint128_t pack_ptr(uint64_t tag,void *ptr) {
    return (__uint128_t(tag) << 64) | __uint128_t(ptr);
}


class PoolAllocator
{
    public:
    explicit PoolAllocator(size_t obj_size, size_t total_size) : obj_size_(max(obj_size, sizeof(Node))), total_size_(total_size) , mem_(new char[obj_size_ * total_size_]) {
        construct_free_list();
    }

    void construct_free_list() {
        for (size_t i = 0;i < total_size_;i ++) {
            Node *new_node = reinterpret_cast<Node*>(&mem_[i * obj_size_]);
            new_node->next = free_list;  
            free_list = pack_ptr(0,new_node);
        }
    };

    void* alloc() {
        if (extract_ptr(free_list)) {
            void* to_return = extract_ptr(free_list);
            free_list = static_cast<Node*>(extract_ptr(free_list))->next;
            return to_return;
        }
        return nullptr;
    }

    void dealloc(void *ptr) {
        Node *return_to_list = reinterpret_cast<Node*>(ptr);
        return_to_list->next = free_list;
        free_list = pack_ptr(0, return_to_list);
    }


    private:
    struct Node {
        __uint128_t next;
    };

    using lock_free_list_ptr_type = std::atomic<__uint128_t>;
    //static_assert(lock_free_list_ptr_type{}.is_lock_free());

    size_t obj_size_;
    size_t total_size_;
    std::unique_ptr<char[]> mem_;
    __uint128_t free_list{};
};



void test_basic_alloc() {
    PoolAllocator pool(sizeof(int), 4);
    void* p = pool.alloc();
    assert(p != nullptr);
    std::cout << "[PASS] test_basic_alloc\n";
}

void test_exhaust_pool() {
    PoolAllocator pool(sizeof(int), 4);
    for (int i = 0; i < 4; i++) {
        assert(pool.alloc() != nullptr);
    }
    assert(pool.alloc() == nullptr);
    std::cout << "[PASS] test_exhaust_pool\n";
}

void test_all_pointers_unique() {
    PoolAllocator pool(sizeof(int), 4);
    std::set<void*> ptrs;
    for (int i = 0; i < 4; i++) {
        void* p = pool.alloc();
        assert(p != nullptr);
        assert(ptrs.find(p) == ptrs.end());
        ptrs.insert(p);
    }
    std::cout << "[PASS] test_all_pointers_unique\n";
}

void test_dealloc_and_reuse() {
    PoolAllocator pool(sizeof(int), 1);
    void* p1 = pool.alloc();
    assert(p1 != nullptr);
    assert(pool.alloc() == nullptr);

    pool.dealloc(p1);
    void* p2 = pool.alloc();
    assert(p2 != nullptr);
    assert(p2 == p1); // should reuse the same slot
    std::cout << "[PASS] test_dealloc_and_reuse\n";
}

void test_dealloc_all_and_realloc() {
    const size_t count = 8;
    PoolAllocator pool(sizeof(int), count);
    std::vector<void*> ptrs;

    for (size_t i = 0; i < count; i++) {
        ptrs.push_back(pool.alloc());
    }
    assert(pool.alloc() == nullptr);

    for (void* p : ptrs) {
        pool.dealloc(p);
    }

    // should be able to allocate all slots again
    for (size_t i = 0; i < count; i++) {
        assert(pool.alloc() != nullptr);
    }
    assert(pool.alloc() == nullptr);
    std::cout << "[PASS] test_dealloc_all_and_realloc\n";
}

void test_large_object_size() {
    struct Big {
        char data[256];
    };
    PoolAllocator pool(sizeof(Big), 4);
    std::vector<void*> ptrs;

    for (int i = 0; i < 4; i++) {
        void* p = pool.alloc();
        assert(p != nullptr);
        // write to the full extent of the slot to check for overlap
        memset(p, static_cast<char>(i), sizeof(Big));
        ptrs.push_back(p);
    }

    // verify no slot was corrupted by a later write
    for (int i = 0; i < 4; i++) {
        Big* b = reinterpret_cast<Big*>(ptrs[i]);
        for (size_t j = 0; j < sizeof(Big); j++) {
            assert(b->data[j] == static_cast<char>(i));
        }
    }
    std::cout << "[PASS] test_large_object_size\n";
}

void test_object_smaller_than_pointer() {
    // obj_size < sizeof(void*), should still work because of max()
    PoolAllocator pool(1, 4);
    for (int i = 0; i < 4; i++) {
        assert(pool.alloc() != nullptr);
    }
    assert(pool.alloc() == nullptr);
    std::cout << "[PASS] test_object_smaller_than_pointer\n";
}

void test_single_slot_pool() {
    PoolAllocator pool(sizeof(int), 1);
    void* p = pool.alloc();
    assert(p != nullptr);
    assert(pool.alloc() == nullptr);

    pool.dealloc(p);
    assert(pool.alloc() != nullptr);
    assert(pool.alloc() == nullptr);
    std::cout << "[PASS] test_single_slot_pool\n";
}

void test_interleaved_alloc_dealloc() {
    PoolAllocator pool(sizeof(int), 2);
    void* p1 = pool.alloc();
    void* p2 = pool.alloc();
    assert(pool.alloc() == nullptr);

    pool.dealloc(p1);
    void* p3 = pool.alloc();
    assert(p3 == p1);
    assert(pool.alloc() == nullptr);

    pool.dealloc(p2);
    pool.dealloc(p3);

    // both slots available again
    assert(pool.alloc() != nullptr);
    assert(pool.alloc() != nullptr);
    assert(pool.alloc() == nullptr);
    std::cout << "[PASS] test_interleaved_alloc_dealloc\n";
}


void test_pack_and_extract_ptr() {
    std::cout << "[PASS] test_pack_and_extract_ptr\n";
    int a = 5;
    assert(extract_ptr(pack_ptr(0, &a)) == &a);
}


int main() {
    test_pack_and_extract_ptr();
    test_basic_alloc();
    test_exhaust_pool();
    test_all_pointers_unique();
    test_dealloc_and_reuse();
    test_dealloc_all_and_realloc();
    test_large_object_size();
    test_object_smaller_than_pointer();
    test_single_slot_pool();
    test_interleaved_alloc_dealloc();
    std::cout << "\nAll tests passed!\n";
}