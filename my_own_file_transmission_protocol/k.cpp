#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    std::string path = "./";
    
    for (const auto& entry : fs::directory_iterator(path)) {
        std::cout << entry.path()  << " : " << entry.file_size() << std::endl;
    }
}