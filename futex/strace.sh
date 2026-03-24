g++ -std=c++20 -pthread -o out strace_futex_conditional_variable.cpp
 strace -e futex ./out 