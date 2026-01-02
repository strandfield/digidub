// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#pragma once

#include "timesegment.h"

struct VideoMatch
{
  TimeSegment a;
  TimeSegment b;
};

inline bool operator==(const VideoMatch& lhs, const VideoMatch& rhs)
{
  return lhs.a == rhs.a && lhs.b == rhs.b;
}

inline bool operator!=(const VideoMatch& lhs, const VideoMatch& rhs)
{
  return !(lhs == rhs);
}
