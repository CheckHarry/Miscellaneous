#include <immintrin.h>
#include <cstdint>
#include <iostream>
#include <iostream>
#include <vector>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <smmintrin.h>
#include <iostream>
#include <iomanip>
#include <immintrin.h>
#include <cstdint>

template <typename T>
void print_avx(const char* label, T reg) {
    std::cout << label << ": [ ";

    // --- 128-bit Registers (SSE/Legacy) ---
    if constexpr (std::is_same_v<T, __m128i>) {
        // Print as 8-bit integers (16 values)
        // You can change uint8_t to uint16_t or uint32_t if you prefer those views
        alignas(16) uint8_t vals[16];
        _mm_store_si128((__m128i*)vals, reg);
        for (int i = 0; i < 16; ++i) 
            std::cout << std::setw(3) << (int)vals[i] << " ";
    }
    else if constexpr (std::is_same_v<T, __m128>) {
        // Print as floats (4 values)
        alignas(16) float vals[4];
        _mm_store_ps(vals, reg);
        for (int i = 0; i < 4; ++i) 
            std::cout << std::fixed << std::setprecision(2) << vals[i] << " ";
    }
    else if constexpr (std::is_same_v<T, __m128d>) {
        // Print as doubles (2 values)
        alignas(16) double vals[2];
        _mm_store_pd(vals, reg);
        for (int i = 0; i < 2; ++i) 
            std::cout << std::fixed << std::setprecision(2) << vals[i] << " ";
    }

    // --- 256-bit Registers (AVX/AVX2) ---
    else if constexpr (std::is_same_v<T, __m256i>) {
        // Print as 8-bit integers (32 values)
        alignas(32) uint8_t vals[32];
        _mm256_store_si256((__m256i*)vals, reg);
        for (int i = 0; i < 32; ++i) 
            std::cout << std::setw(3) << (int)vals[i] << " ";
    }
    else if constexpr (std::is_same_v<T, __m256>) {
        // Print as floats (8 values)
        alignas(32) float vals[8];
        _mm256_store_ps(vals, reg);
        for (int i = 0; i < 8; ++i) 
            std::cout << std::fixed << std::setprecision(2) << vals[i] << " ";
    }
    else if constexpr (std::is_same_v<T, __m256d>) {
        // Print as doubles (4 values)
        alignas(32) double vals[4];
        _mm256_store_pd(vals, reg);
        for (int i = 0; i < 4; ++i) 
            std::cout << std::fixed << std::setprecision(2) << vals[i] << " ";
    }
    
    // --- 512-bit Registers (AVX-512) ---
    // Uncomment if you have an AVX-512 CPU
    /*
    else if constexpr (std::is_same_v<T, __m512i>) {
        alignas(64) uint8_t vals[64];
        _mm512_store_si512((void*)vals, reg);
        for (int i = 0; i < 64; ++i) std::cout << (int)vals[i] << " ";
    }
    */

    std::cout << "]" << std::endl;
}

template <typename T>
void print_avx(T reg) {
    print_avx("",reg);
}

inline int get_right_most_nonzero_index(const uint8_t* A) {
    __m128i vec = _mm_loadu_si128((const __m128i*)A);
    __m128i cmp = _mm_cmpeq_epi8(vec,  _mm_setzero_si128());
    print_avx(cmp);

    int mask = _mm_movemask_epi8(cmp);


    int non_zero_mask = (~mask) & 0xFFFF;

    if (non_zero_mask == 0) {
        return -1; 
    }

    int index = _bit_scan_reverse(non_zero_mask);
    
    
    return (int)index;
}


int get_right_most_nonzero_scalar(const uint8_t* A) {
    // Iterate backwards from the last element
    for (int i = 15; i >= 0; --i) {
        if (A[i] != 0) {
            return i; // Found it, return immediately
        }
    }
    return -1; // All are zero
}

int main() {
    // Example: Bytes at index 2 and 14 are non-zero.
    // The right-most index is 14.
    // Note: _mm_setr_epi8 sets in reverse order (index 0 first).
    uint8_t A[16]{
        0, 0, 5, 0, 
        0, 0, 0, 0,
        0, 0, 0, 0, 
        0, 0, 99, 1 
    };

    int index = get_right_most_nonzero_index(A);

    std::cout << "Right-most non-zero index: " << index << std::endl;

    return 0;
}