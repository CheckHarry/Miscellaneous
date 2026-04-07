#include <vector>
#include <iostream>
using namespace std;





template<typename T>
void push_down(vector<T> &v, int i) {
    int target = i;
    int left_child = 2 * i;
    int right_child = 2 * i + 1;

    if (left_child - 1 < v.size() && v[left_child] > v[target]) {
        target = left_child;
    }

    if (right_child - 1 < v.size() && v[right_child] > v[target]) {
        target = right_child;
    }

    if (target != i) {
        swap(v[target], v[i]);
        push_down(target);
    }
}



template<typename T>
void heapify(vector<T> &v, int i) {
    if (i - 1 >= v.size()) return;

    int left_child = 2 * i;
    int right_child = 2 * i + 1;

    heapify(v, left_child);
    heapify(v, right_child);

    push_down(v, i);
}


int main() {
    vector<int> a{1,4,1,3,1,5,6,7,1,23,5};
    heapify(a , 1);

    for (int i : a) {
        cout << i << " ";
    } cout << '\n';

}


