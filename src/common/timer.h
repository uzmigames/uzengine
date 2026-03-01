#pragma once

#include <windows.h>
#include <cstdint>

namespace uz {

class Timer {
public:
    Timer() {
        QueryPerformanceFrequency(&frequency_);
        reset();
    }

    void reset() {
        QueryPerformanceCounter(&startTime_);
        lastTime_ = startTime_;
    }

    // Returns elapsed time since last tick() call, in seconds
    double tick() {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double delta = static_cast<double>(now.QuadPart - lastTime_.QuadPart)
                     / static_cast<double>(frequency_.QuadPart);
        lastTime_ = now;
        return delta;
    }

    // Returns total elapsed time since reset(), in seconds
    double elapsed() const {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - startTime_.QuadPart)
             / static_cast<double>(frequency_.QuadPart);
    }

    // Returns total elapsed time since reset(), in microseconds
    uint64_t elapsedMicros() const {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return static_cast<uint64_t>(
            (now.QuadPart - startTime_.QuadPart) * 1'000'000 / frequency_.QuadPart
        );
    }

private:
    LARGE_INTEGER frequency_{};
    LARGE_INTEGER startTime_{};
    LARGE_INTEGER lastTime_{};
};

} // namespace uz
