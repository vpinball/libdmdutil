#pragma once

#include <atomic>
#include <cstdint>

#include "FrameUtil.h"

namespace DMDUtil
{

/**
 * The upscaling algorithm libdmdutil uses when it has to enlarge a frame for a
 * display.
 *
 * This mirrors the algorithm the loaded Serum colorization selected, so that
 * libserum's in-frame scaling (which shade a dynamic-zone pixel samples) and
 * libdmdutil's display scaling (fitting the finished frame to the panel) agree.
 * libserum reports the authored choice through Serum_GetScalingAlgorithm();
 * DMD::SerumThread() pushes it here right after Serum_Load().
 *
 * Read from several output threads, written once per colorization load, hence
 * the atomic. Defaults to Scale2x: that is what libdmdutil has always used for
 * upscaling, and what an unset/failed Serum load and every pre-v8 cROMc resolve
 * to. Only a colorization that explicitly opts out selects line doubling.
 */
inline std::atomic<uint8_t> g_scalingAlgorithm{
    static_cast<uint8_t>(FrameUtil::ScalingAlgorithm::Scale2x)};

inline void SetScalingAlgorithm(uint8_t algorithm)
{
  g_scalingAlgorithm.store(algorithm, std::memory_order_relaxed);
}

inline FrameUtil::ScalingAlgorithm GetScalingAlgorithm()
{
  const uint8_t value = g_scalingAlgorithm.load(std::memory_order_relaxed);
  return (value == static_cast<uint8_t>(FrameUtil::ScalingAlgorithm::LineDoubling))
             ? FrameUtil::ScalingAlgorithm::LineDoubling
             : FrameUtil::ScalingAlgorithm::Scale2x;
}

}  // namespace DMDUtil
