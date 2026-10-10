// DrumSpread offline verification harness - test suite entry points.
#pragma once

#include <cstdint>

namespace tu
{
// module-level (TestModules.cpp)
void testOversampler();
void testFocusSplit();
void testModuleNulls();
void testDetectors();
void testCompressors();
void testSaturation();
void testShift();
void testRoom();

// engine-level (TestEngine.cpp)
void testLatency();
void testEngineNulls();
void testClickFree();
void testExtremes();
void testSampleRateIndependence();
void testBlockSizeIndependence();
void testRealtimeAllocations();
void testPerformance();
void testPresets (bool writeRenders);

// allocation counter (TestMain.cpp)
void setAllocCounting (bool on);
uint64_t allocCount();

} // namespace tu
