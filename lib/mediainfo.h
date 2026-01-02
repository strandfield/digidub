// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#ifndef MEDIAINFO_H
#define MEDIAINFO_H

#include "timesegment.h"

#include <QString>

#include <optional>

struct VideoFrameInfo
{
  int pts;
  quint64 phash;
};

struct SceneChange
{
  double score;
  double time;
};

#endif // MEDIAINFO_H
