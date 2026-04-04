#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;
using namespace std;
int main() {
    std::ifstream file("protocol.cpp", std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cout << "cannot open file .\n"  << std::endl;
       std::exit(-1);
    }

    std::streamsize fileSize = file.tellg();
    
    file.seekg(0, std::ios::beg);
    std::vector<char> a;
    a.resize(fileSize);
    if (!file.read(a.data(), fileSize)) {
      std::cout << "can't read\n" << std::endl;
      std::exit(-1);
    }
    cout << a.size() << std::endl;
    for (char c : a) {
        std::cout << c;
    }

    file.close();
}