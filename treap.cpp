#include <iostream>
#include <vector>
#include <deque>
#include <algorithm>
#include <random>
#include <chrono>
#include <cassert>
#include <cmath>
using namespace std;

// ======================== Treap ========================

mt19937 rng(chrono::steady_clock::now().time_since_epoch().count());

struct Node {
    int key, pri, sz;
    Node *L, *R;
    Node(int k) : key(k), pri(rng()), sz(1), L(nullptr), R(nullptr) {}
};

int sz(Node* n) { return n ? n->sz : 0; }

void pull(Node* n) {
    if (n) n->sz = 1 + sz(n->L) + sz(n->R);
}

// split into (<= key) and (> key)
pair<Node*, Node*> split(Node* n, int key) {
    if (!n) return {nullptr, nullptr};
    if (n->key <= key) {
        auto [l, r] = split(n->R, key);
        n->R = l;
        pull(n);
        return {n, r};
    } else {
        auto [l, r] = split(n->L, key);
        n->L = r;
        pull(n);
        return {l, n};
    }
}

Node* merge(Node* a, Node* b) {
    if (!a || !b) return a ? a : b;
    if (a->pri > b->pri) {
        a->R = merge(a->R, b);
        pull(a);
        return a;
    } else {
        b->L = merge(a, b->L);
        pull(b);
        return b;
    }
}

Node* insert(Node* root, int key) {
    auto [l, r] = split(root, key);
    return merge(merge(l, new Node(key)), r);
}

// erase exactly one occurrence of key
Node* erase_one(Node* root, int key) {
    auto [lr, r] = split(root, key);       // lr: <= key, r: > key
    auto [l, mid] = split(lr, key - 1);    // l: <= key-1, mid: == key (integers)
    if (mid) {
        // remove the root of mid, keep its children (other duplicates)
        Node* del = mid;
        mid = merge(mid->L, mid->R);
        del->L = del->R = nullptr;
        delete del;
    }
    return merge(merge(l, mid), r);
}

// 1-indexed k-th smallest
int kth(Node* n, int k) {
    assert(n && k >= 1 && k <= n->sz);
    int ls = sz(n->L);
    if (k <= ls)       return kth(n->L, k);
    if (k == ls + 1)   return n->key;
    return kth(n->R, k - ls - 1);
}

void destroy(Node* n) {
    if (!n) return;
    destroy(n->L);
    destroy(n->R);
    delete n;
}

// =================== Sliding Window Percentile ===================

vector<int> sliding_percentile(const vector<int>& arr, int w, double p) {
    Node* root = nullptr;
    deque<int> window;
    vector<int> results;

    for (int val : arr) {
        root = insert(root, val);
        window.push_back(val);

        if ((int)window.size() > w) {
            root = erase_one(root, window.front());
            window.pop_front();
        }

        int n = (int)window.size();
        int k = max(1, (int)ceil(p / 100.0 * n));
        results.push_back(kth(root, k));
    }

    destroy(root);
    return results;
}

// =================== Brute Force (for verification) ===================

vector<int> brute_force_percentile(const vector<int>& arr, int w, double p) {
    vector<int> results;
    for (int i = 0; i < (int)arr.size(); i++) {
        int lo = max(0, i - w + 1);
        vector<int> win(arr.begin() + lo, arr.begin() + i + 1);
        sort(win.begin(), win.end());
        int n = (int)win.size();
        int k = max(1, (int)ceil(p / 100.0 * n));
        results.push_back(win[k - 1]);
    }
    return results;
}

// ======================== Tests ========================

void test_basic_insert_and_kth() {
    cout << "Test 1: Basic insert & kth ... ";
    Node* root = nullptr;
    vector<int> vals = {5, 3, 8, 1, 4, 7, 9, 2, 6, 10};
    for (int v : vals) root = insert(root, v);

    for (int i = 1; i <= 10; i++) {
        assert(kth(root, i) == i);
    }
    destroy(root);
    cout << "PASSED\n";
}

void test_duplicates() {
    cout << "Test 2: Duplicates ... ";
    Node* root = nullptr;
    root = insert(root, 5);
    root = insert(root, 5);
    root = insert(root, 5);
    root = insert(root, 3);
    root = insert(root, 3);
    // sorted: 3 3 5 5 5
    assert(sz(root) == 5);
    assert(kth(root, 1) == 3);
    assert(kth(root, 2) == 3);
    assert(kth(root, 3) == 5);
    assert(kth(root, 5) == 5);
    destroy(root);
    cout << "PASSED\n";
}

void test_erase_one() {
    cout << "Test 3: Erase one occurrence ... ";
    Node* root = nullptr;
    root = insert(root, 5);
    root = insert(root, 5);
    root = insert(root, 3);
    // sorted: 3 5 5
    root = erase_one(root, 5);
    // sorted: 3 5
    assert(sz(root) == 2);
    assert(kth(root, 1) == 3);
    assert(kth(root, 2) == 5);

    root = erase_one(root, 3);
    // sorted: 5
    assert(sz(root) == 1);
    assert(kth(root, 1) == 5);
    destroy(root);
    cout << "PASSED\n";
}

void test_sliding_window_median() {
    cout << "Test 4: Sliding window median ... ";
    vector<int> arr = {3, 1, 4, 1, 5, 9, 2, 6, 5, 3};
    int w = 5;
    auto result = sliding_percentile(arr, w, 50);
    auto expected = brute_force_percentile(arr, w, 50);
    assert(result == expected);
    cout << "PASSED  ->  median values: ";
    for (int v : result) cout << v << " ";
    cout << "\n";
}

void test_sliding_window_p90() {
    cout << "Test 5: Sliding window 90th percentile ... ";
    vector<int> arr = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    int w = 5;
    auto result = sliding_percentile(arr, w, 90);
    auto expected = brute_force_percentile(arr, w, 90);
    assert(result == expected);
    cout << "PASSED  ->  p90 values: ";
    for (int v : result) cout << v << " ";
    cout << "\n";
}

void test_stress_random() {
    cout << "Test 6: Stress test (random) ... ";
    mt19937 gen(42);
    uniform_int_distribution<int> val_dist(1, 1000);
    uniform_int_distribution<int> p_dist(1, 100);

    int n = 5000, w = 50;
    vector<int> arr(n);
    for (int& v : arr) v = val_dist(gen);

    for (int trial = 0; trial < 5; trial++) {
        double p = p_dist(gen);
        auto result = sliding_percentile(arr, w, p);
        auto expected = brute_force_percentile(arr, w, p);
        assert(result == expected);
    }
    cout << "PASSED (5 random percentiles, n=5000, w=50)\n";
}

void test_window_size_1() {
    cout << "Test 7: Window size 1 ... ";
    vector<int> arr = {7, 2, 9, 4};
    auto result = sliding_percentile(arr, 1, 50);
    assert(result == arr);
    cout << "PASSED\n";
}

void test_p1_and_p100() {
    cout << "Test 8: p=1 (min) and p=100 (max) ... ";
    vector<int> arr = {5, 3, 8, 1, 4, 7, 9, 2, 6, 10};
    int w = 4;

    auto mins = sliding_percentile(arr, w, 1);
    auto maxs = sliding_percentile(arr, w, 100);
    auto expected_mins = brute_force_percentile(arr, w, 1);
    auto expected_maxs = brute_force_percentile(arr, w, 100);

    assert(mins == expected_mins);
    assert(maxs == expected_maxs);

    cout << "PASSED\n";
    cout << "         min: ";
    for (int v : mins) cout << v << " ";
    cout << "\n         max: ";
    for (int v : maxs) cout << v << " ";
    cout << "\n";
}

int main() {
    cout << "===== Treap Order-Statistic Tree Tests =====\n\n";

    test_basic_insert_and_kth();
    test_duplicates();
    test_erase_one();
    test_sliding_window_median();
    test_sliding_window_p90();
    test_stress_random();
    test_window_size_1();
    test_p1_and_p100();

    cout << "\nAll tests passed!\n";
    return 0;
}