#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

/**
 * Harris-Michael Lock-Free Sorted Linked List with Tagged Pointers
 *
 * Key idea: the least-significant bit of every `next` pointer is
 * reserved as a "mark" (tag) bit that signals logical deletion.
 *
 * Deletion is two-phase:
 *   1. Logical  — CAS the victim's next pointer to set the mark bit.
 *                 This prevents any concurrent insert from linking
 *                 a new node after the victim.
 *   2. Physical — CAS the predecessor's next pointer to skip the victim.
 *                 If this CAS fails, a later traversal will retry it.
 *
 * `contains` is wait-free (just a traversal, no CAS).
 *
 * NOTE: This implementation does NOT include safe memory reclamation.
 *       In production code you would add hazard pointers, epoch-based
 *       reclamation (EBR), or similar to safely free removed nodes.
 */
template <typename K>
class TaggedLockFreeList {
    static_assert(std::is_integral<K>::value,
                  "Key type must be integral (for sentinel values).");

    // ------------------------------------------------------------------
    //  Node
    // ------------------------------------------------------------------
    struct Node {
        K key;
        std::atomic<uintptr_t> next;   // low bit = mark tag

        explicit Node(K k, Node* n = nullptr)
            : key(k),
              next(reinterpret_cast<uintptr_t>(n))
        {}
    };

    // ------------------------------------------------------------------
    //  Tagged-pointer helpers
    // ------------------------------------------------------------------
    static Node* ptr(uintptr_t tagged) {
        return reinterpret_cast<Node*>(tagged & ~uintptr_t(1));
    }

    static bool is_marked(uintptr_t tagged) {
        return (tagged & 1) != 0;
    }

    static uintptr_t make_tagged(Node* p, bool mark = false) {
        return reinterpret_cast<uintptr_t>(p) | uintptr_t(mark);
    }

    // ------------------------------------------------------------------
    //  Sentinels
    // ------------------------------------------------------------------
    Node* head_;   // key = MIN  (never deleted)
    Node* tail_;   // key = MAX  (never deleted)

    // ------------------------------------------------------------------
    //  Internal window search
    // ------------------------------------------------------------------
    //  Scans forward from head_ looking for the position where `key`
    //  belongs.  Returns (pred, curr) such that:
    //      pred->key  <  key  <=  curr->key
    //  Both pred and curr are unmarked, and pred->next == curr.
    //  Physically unlinks every marked node encountered along the way.
    //  Returns true if curr->key == key (i.e. key is present).
    // ------------------------------------------------------------------
    bool find(K key, Node*& out_pred, Node*& out_curr) {
    retry:
        Node* pred = head_;
        Node* curr = ptr(pred->next.load(std::memory_order_acquire));

        for (;;) {
            uintptr_t raw   = curr->next.load(std::memory_order_acquire);
            Node*     succ  = ptr(raw);
            bool      cmark = is_marked(raw);

            // ---- Skip & physically unlink any logically-deleted node ----
            if (cmark) {
                uintptr_t expected = make_tagged(curr, false);
                if (!pred->next.compare_exchange_strong(
                        expected,
                        make_tagged(succ, false),
                        std::memory_order_release,
                        std::memory_order_relaxed))
                {
                    goto retry;           // pred was changed; start over
                }
                // Unlink succeeded — advance curr, keep pred
                curr = succ;
                continue;
            }

            // ---- curr is live (unmarked) ----
            if (curr == tail_ || curr->key >= key) {
                out_pred = pred;
                out_curr = curr;
                return (curr != tail_ && curr->key == key);
            }

            pred = curr;
            curr = succ;
        }
    }

public:
    // ------------------------------------------------------------------
    //  Construction / Destruction
    // ------------------------------------------------------------------
    TaggedLockFreeList() {
        tail_ = new Node(std::numeric_limits<K>::max());
        head_ = new Node(std::numeric_limits<K>::min(), tail_);
    }

    ~TaggedLockFreeList() {
        Node* n = head_;
        while (n) {
            Node* nx = ptr(n->next.load(std::memory_order_relaxed));
            delete n;
            n = nx;
        }
    }

    TaggedLockFreeList(const TaggedLockFreeList&)            = delete;
    TaggedLockFreeList& operator=(const TaggedLockFreeList&) = delete;

    // ------------------------------------------------------------------
    //  insert — adds `key` if not already present
    //  Returns true on success, false if duplicate.
    // ------------------------------------------------------------------
    bool insert(K key) {
        Node* node = new Node(key);

        for (;;) {
            Node* pred;
            Node* curr;

            if (find(key, pred, curr)) {
                // Duplicate key
                delete node;
                return false;
            }

            // Point new node at curr
            node->next.store(make_tagged(curr), std::memory_order_relaxed);

            // Swing pred->next from curr to node
            uintptr_t expected = make_tagged(curr);
            if (pred->next.compare_exchange_strong(
                    expected,
                    make_tagged(node),
                    std::memory_order_release,
                    std::memory_order_relaxed))
            {
                return true;
            }
            // CAS failed — another thread changed pred->next; retry
        }
    }

    // ------------------------------------------------------------------
    //  remove — logically then physically deletes `key`
    //  Returns true if found and removed, false if not present.
    // ------------------------------------------------------------------
    bool remove(K key) {
        for (;;) {
            Node* pred;
            Node* curr;

            if (!find(key, pred, curr))
                return false;           // not in list

            // ---- Step 1: logical deletion (tag the mark bit) ----
            uintptr_t raw  = curr->next.load(std::memory_order_acquire);
            Node*     succ = ptr(raw);

            if (is_marked(raw))
                continue;               // another thread beat us

            uintptr_t expected = make_tagged(succ, false);
            if (!curr->next.compare_exchange_strong(
                    expected,
                    make_tagged(succ, true),    // <-- set mark bit
                    std::memory_order_release,
                    std::memory_order_relaxed))
            {
                continue;               // CAS failed; retry
            }

            // ---- Step 2: physical deletion (best effort) ----
            expected = make_tagged(curr, false);
            pred->next.compare_exchange_strong(
                expected,
                make_tagged(succ, false),
                std::memory_order_release,
                std::memory_order_relaxed);
            // If this fails, a future find() traversal will unlink it.

            return true;
        }
    }

    // ------------------------------------------------------------------
    //  contains — wait-free membership test
    // ------------------------------------------------------------------
    bool contains(K key) const {
        Node* curr = ptr(head_->next.load(std::memory_order_acquire));

        while (curr != tail_ && curr->key < key)
            curr = ptr(curr->next.load(std::memory_order_acquire));

        return curr != tail_
            && curr->key == key
            && !is_marked(curr->next.load(std::memory_order_acquire));
    }

    // ------------------------------------------------------------------
    //  dump — debugging print (NOT linearizable)
    // ------------------------------------------------------------------
    void dump() const {
        std::cout << "[HEAD] -> ";
        Node* curr = ptr(head_->next.load(std::memory_order_relaxed));
        while (curr != tail_) {
            uintptr_t raw = curr->next.load(std::memory_order_relaxed);
            if (is_marked(raw))
                std::cout << "(" << curr->key << ") -> ";   // marked
            else
                std::cout << curr->key << " -> ";
            curr = ptr(raw);
        }
        std::cout << "[TAIL]\n";
    }
};


// ==================================================================
//  Demo & smoke test
// ==================================================================
int main() {
    // ---- Single-threaded correctness ----
    TaggedLockFreeList<int> list;

    assert(list.insert(10));
    assert(list.insert(20));
    assert(list.insert(5));
    assert(list.insert(15));
    assert(!list.insert(10));           // duplicate → false

    assert(list.contains(5));
    assert(list.contains(10));
    assert(list.contains(15));
    assert(list.contains(20));
    assert(!list.contains(7));

    std::cout << "After inserts:  ";
    list.dump();                        // 5 -> 10 -> 15 -> 20

    assert(list.remove(10));
    assert(!list.contains(10));
    assert(!list.remove(10));           // already gone → false

    std::cout << "After remove(10): ";
    list.dump();                        // 5 -> 15 -> 20

    // ---- Multi-threaded stress test ----
    TaggedLockFreeList<int> mt;
    constexpr int THREADS = 8;
    constexpr int OPS     = 50000;

    std::vector<std::thread> pool;
    for (int t = 0; t < THREADS; ++t) {
        pool.emplace_back([&mt, t]() {
            for (int i = 0; i < OPS; ++i) {
                int key = (t * OPS + i) % 1000 + 1; // keep away from sentinels
                mt.insert(key);
                mt.contains(key);
                if (i % 3 == 0) mt.remove(key);
            }
        });
    }
    for (auto& th : pool) th.join();

    std::cout << "Stress test (" << THREADS << " threads x "
              << OPS << " ops) passed.\n";

    return 0;
}