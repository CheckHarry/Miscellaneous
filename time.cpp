#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

#ifdef _WIN32
#include <intrin.h>
#else
#include <x86intrin.h>
#endif

// --------------------------------------------------------------------------
// 1. Safe RDTSC Wrapper
// --------------------------------------------------------------------------
// We use _mm_lfence() to ensure the CPU doesn't reorder instructions
// around our timer.
uint64_t get_rdtsc() {
    _mm_lfence();  // Serializing instruction
    return __rdtsc();
}

// --------------------------------------------------------------------------
// 2. Frequency Calibration
// --------------------------------------------------------------------------
// Because RDTSC ticks at the Base Frequency, we need to calculate
// exactly what that frequency is to convert cycles -> seconds.
double estimate_tsc_frequency() {
    std::cout << "Calibrating TSC frequency... (waiting 100ms)" << std::endl;

    // 1. Get start time (Standard Clock and RDTSC)
    auto start_time = std::chrono::high_resolution_clock::now();
    uint64_t start_cycles = get_rdtsc();

    // 2. Sleep for a known duration (100ms is usually enough for accuracy)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 3. Get end time
    auto end_time = std::chrono::high_resolution_clock::now();
    uint64_t end_cycles = get_rdtsc();

    // 4. Calculate duration in seconds
    std::chrono::duration<double> elapsed = end_time - start_time;
    double seconds_elapsed = elapsed.count();

    // 5. Calculate Frequency (Cycles / Seconds)
    uint64_t cycles_elapsed = end_cycles - start_cycles;
    double frequency = cycles_elapsed / seconds_elapsed;

    return frequency;
}

// --------------------------------------------------------------------------
// Main Program
// --------------------------------------------------------------------------
int main() {
    // A. Estimate the specific TSC frequency for this machine
    double tsc_freq_hz = estimate_tsc_frequency();
    std::cout << "Detected TSC Frequency: " << (tsc_freq_hz / 1e9) << " GHz" << std::endl;
    std::cout << "------------------------------------------------" << std::endl;

    // B. Start the high-precision measurement
    uint64_t start = get_rdtsc();

    // --- WORKLOAD START ---
    volatile int k = 0;
    for (int i = 0; i < 50000000; ++i) {
        k++;
    }
    // --- WORKLOAD END ---

    // C. End the measurement
    uint64_t end = get_rdtsc();

    // D. Calculate Time
    uint64_t cycles_diff = end - start;
    double time_seconds = cycles_diff / tsc_freq_hz;

    // Output results
    std::cout << "Cycles Counted: " << cycles_diff << std::endl;
    std::cout << "Elapsed Time:   " << time_seconds << " seconds" << std::endl;
    std::cout << "Elapsed Time:   " << time_seconds * 1000.0 << " ms" << std::endl;

    return 0;
}