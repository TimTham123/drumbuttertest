// DrumSpread DSP core - lock-free telemetry from the audio thread to the UI.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace ds
{
/** Single-producer / single-consumer ring of the most recent output samples
    (mono). The producer never blocks; the consumer copies the latest window. */
class ScopeFifo
{
public:
    static constexpr int kSize = 8192; // power of two
    void push (float x) noexcept
    {
        const uint32_t w = writeIdx.load (std::memory_order_relaxed);
        buf[w & (kSize - 1)] = x;
        writeIdx.store (w + 1, std::memory_order_release);
    }
    /** Copies the newest n samples (n <= kSize) into dest, oldest first. */
    void readLatest (float* dest, int n) const noexcept
    {
        const uint32_t w = writeIdx.load (std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
            dest[i] = buf[(w - (uint32_t) n + (uint32_t) i) & (kSize - 1)];
    }
    uint32_t written() const noexcept { return writeIdx.load (std::memory_order_acquire); }

private:
    std::array<float, kSize> buf {};
    std::atomic<uint32_t> writeIdx { 0 };
};

struct Telemetry
{
    std::atomic<float> inLevel[2] { { 0.0f }, { 0.0f } };  // decaying peak, linear
    std::atomic<float> outLevel[2] { { 0.0f }, { 0.0f } };
    std::atomic<float> pressGrDb { 0.0f };
    std::atomic<uint32_t> subTriggers { 0 }, noiseTriggers { 0 };
    std::atomic<int> latency { 0 };
    std::atomic<int> chainOrder { 3 };
    ScopeFifo scope;
};

} // namespace ds
