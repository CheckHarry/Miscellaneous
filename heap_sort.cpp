#include <vector>
#include <iostream>
using namespace std;





template<typename T>
void push_down(vector<T> &v, int i, int n) {
    int target = i;
    int left_child = 2 * i;
    int right_child = 2 * i + 1;

    if (left_child - 1 < n && v[left_child - 1] > v[target - 1]) {
        target = left_child;
    }

    if (right_child - 1 < n && v[right_child - 1] > v[target - 1]) {
        target = right_child;
    }

    if (target != i) {
        swap(v[target - 1], v[i - 1]);
        push_down(v, target, n);
    }
}



template<typename T>
void heapify(vector<T> &v, int i, int n) {
    if (i - 1 >= n) return;

    int left_child = 2 * i;
    int right_child = 2 * i + 1;

    heapify(v, left_child, n);
    heapify(v, right_child, n);

    push_down(v, i, n);
}

template<typename T>
void heapify(vector<T> &v) {
    heapify(v,1,v.size());
}

template<typename T>
void heap_sort(vector<T> &v) {
    heapify(v);
    int n = v.size();
    for (int i = n - 1;i >= 1;i --) {
        swap(v[0], v[i]);
        push_down(v,1,i);
    } 

}


int main() {
    vector<int> a{1,4,1,3,1,5,6,7,1,23,5};
    heapify(a);

    for (int i : a) {
        cout << i << " ";
    } cout << '\n';

    heap_sort(a);
    for (int i : a) {
        cout << i << " ";
    } cout << '\n';
}


