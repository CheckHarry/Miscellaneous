#include <ranges>
#include <string>
#include <iostream>

using namespace std;
using namespace std::views;

struct FixField {
    int tag;
    string_view value;
};

auto parse_fix(string_view msg) {
    return msg
        | views::split('|')
        | views::filter([](auto chunk) {
            return !ranges::empty(chunk);
        })
        | views::transform([](auto chunk) {
            auto sv = string_view(chunk);
            auto eq = sv.find('=');
            int tag = 0;
            for (char c : sv.substr(0, eq))
                tag = tag * 10 + (c - '0');
            return FixField{tag, sv.substr(eq + 1)};
        });
}

int main() {
    string msg = "8=FIX.4.2|35=D|49=SENDER|56=TARGET|";
    for (auto [tag, value] : parse_fix(msg))
        cout << tag << " -> " << value << "\n";
    auto view = parse_fix(msg);
    auto it = view.begin();
    for (;it != view.end();it ++) {
        cout << (*it).tag <<  " ";
    }
}