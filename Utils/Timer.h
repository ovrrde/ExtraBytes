#pragma once
#include <Windows.h>
#include <cstdint>
#include <stdio.h>

namespace ebp {

class Timer {
public:
    Timer() {
        QueryPerformanceFrequency(&m_freq);
    }

    void Start() {
        QueryPerformanceCounter(&m_start);
    }

    double ElapsedMs() const {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return (double)(now.QuadPart - m_start.QuadPart) / (double)m_freq.QuadPart * 1000.0;
    }

    double ElapsedUs() const {
        return ElapsedMs() * 1000.0;
    }

private:
    LARGE_INTEGER m_freq;
    LARGE_INTEGER m_start;
};

} // namespace ebp
