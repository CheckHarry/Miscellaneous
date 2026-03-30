#include <ranges>
#include <string>
#include <iostream>

using namespace std;
using namespace std::views;


int main() {
    string csv = "hello,world,foo,bar";
    for (auto word : csv | views::split(','))
        cout << string_view(word) << "\n";
}