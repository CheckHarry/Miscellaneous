#include <array>
#include <string_view>
#include <iostream>

// Define our transition table structure
template <size_t MaxStates, size_t AlphabetSize = 256>
struct TransitionTable {
    std::array<std::array<int, AlphabetSize>, MaxStates> table{};
    std::array<bool, MaxStates> is_accepting{};
    int start_state = 0;

    // Initialize all transitions to an error state (-1)
    constexpr TransitionTable() {
        for (auto& row : table) {
            row.fill(-1);
        }
    }
};

// A constexpr function to compile a simple pattern into a state machine
// For this example, we'll just compile an exact string match.
template <size_t MaxStates>
constexpr TransitionTable<MaxStates> compile_simple_pattern(std::string_view pattern) {
    TransitionTable<MaxStates> dfa;
    int current_state = 0;

    for (char c : pattern) {
        // Transition from current_state to next state on character 'c'
        dfa.table[current_state][static_cast<unsigned char>(c)] = current_state + 1;
        current_state++;
    }
    
    // Mark the final state as accepting
    dfa.is_accepting[current_state] = true;
    
    return dfa;
}

// Helper macro/template to force compile-time evaluation
template<auto V>
struct ForceCompileTime {
    static constexpr auto value = V;
};

int main() {
    // 1. Generate the table at compile time
    constexpr std::string_view pattern = "hello";
    constexpr auto dfa = ForceCompileTime<compile_simple_pattern<10>(pattern)>::value;

    // 2. Use the table at runtime
    std::string_view test_str = "hello";
    int state = dfa.start_state;
    
    for (char c : test_str) {
        state = dfa.table[state][static_cast<unsigned char>(c)];
        if (state == -1) break; // Dead state
    }

    if (state != -1 && dfa.is_accepting[state]) {
        std::cout << "String matched!\n";
    } else {
        std::cout << "String did not match.\n";
    }

    return 0;
}