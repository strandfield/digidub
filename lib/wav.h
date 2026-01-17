// Copyright (C) 2026 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#pragma once

#include <QString>

#include <algorithm>
#include <cstdint>
#include <vector>

using WavSample = uint16_t;

inline int getWavSampleHigh(WavSample sample)
{
  return sample >> 8;
}

inline int getWavSampleLow(WavSample sample)
{
  return -int(sample & 0xFF);
}

inline WavSample makeWavSample(int high, int low)
{
  high = std::clamp(high, 0, 255);
  low = std::clamp(std::abs(low), 0, 255);
  return (high << 8) + low;
}

std::vector<WavSample> readWav(const QString& filePath);
