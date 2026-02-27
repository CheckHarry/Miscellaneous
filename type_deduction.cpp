#include <iostream>
#include <string>
#include <type_traits>
#include <utility>

// A helper function to convert the type T into a readable string.
// For simplicity in this demo, we assume the base type is 'int'.
template <typename T>
std::string get_type_name() {
    std::string name = "int"; 
    
    // Check if the underlying type is const
    if (std::is_const_v<std::remove_reference_t<T>>) {
        name = "const " + name;
    }
    
    // Check if it's an lvalue reference (&) or rvalue reference (&&)
    if (std::is_lvalue_reference_v<T>) {
        name += "&";
    } else if (std::is_rvalue_reference_v<T>) {
        name += "&&";
    }
    
    return name;
}

// The template function with a Forwarding Reference (Universal Reference)
template <typename T>
void f(T&& arg) {
    std::cout << "  T is deduced as:      " << get_type_name<T>() << "\n";
    std::cout << "  Parameter (T&&) is:   " << get_type_name<decltype(arg)>() << "\n\n";
}

int main() {
    int x = 10;
    const int cx = 20;

    std::cout << "1. Passing a standard Lvalue (int x):\n";
    f(x);

    std::cout << "2. Passing an Rvalue literal (10):\n";
    f(10);

    std::cout << "3. Passing a const Lvalue (const int cx):\n";
    f(cx);

    std::cout << "4. Passing an Rvalue via std::move(x):\n";
    f(std::move(x));

    return 0;
}