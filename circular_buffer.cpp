#include <atomic>
#include <vector>

using namespace std;



class CircularBuffer
{
    private:
        vector<int> data;
        atomic<size_t> start_;
        atomic<size_t> end_;
    public:
        explicit CircularBuffer(size_t n) : data(n + 1), start_{}, end_{} {
            
        }
};


int main() {

}