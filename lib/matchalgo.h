// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#ifndef MATCHALGO_H
#define MATCHALGO_H

#include "match.h"

// TODO: remove these
#include "mediainfo.h"

#include <utility>
#include <vector>

class MediaObject;

// struct InputSegment
// {
//   int src;
//   int start_msecs; // msecs
//   int end_msecs;   // msecs
// };

// struct OutputSegment
// {
//   int pts;
//   int pts_duration; // pts
//   InputSegment input;
// };

namespace MatchAlgo {

struct Parameters
{
  int frameUnmatchThreshold = 21;
  int frameRematchThreshold = 16;
  double areaMatchThreshold = 20;
  double scdetThreshold = 0;
};

struct Frame
{
  int pts;
  quint64 phash;

  // Les infos suivantes ne sont calculées que pour la vidéo principale
  bool silence = false;
  bool black = false;
  float scscore = 0; // > 0 pour un changmenet de scène
};

class Video
{
public:
  const MediaObject* media;
  double frameDelta;
  std::vector<Frame> frames;

public:
  explicit Video(const MediaObject& media);
};

class FrameSpan
{
public:
  const Video* video;
  size_t first;
  size_t count;

public:
  FrameSpan()
      : video(nullptr)
      , first(0)
      , count(0)
  {}

  FrameSpan(const Video& v, size_t offset, size_t n)
      : video(&v)
      , first(offset)
      , count(n)
  {
    first = std::min(first, video->frames.size());
    count = std::min(count, video->frames.size() - first);
  }

  size_t size() const { return count; }
  const Frame& at(size_t i) const
  {
    assert(i < count);
    return video->frames.at(first + i);
  }

  size_t startOffset() const { return first; }
  size_t endOffset() const { return first + count; }

  void moveStartOffsetTo(size_t dest)
  {
    assert(dest <= endOffset());
    count = endOffset() - dest;
    first = dest;
  }

  void moveEndOffset(size_t dest)
  {
    assert(dest > first);
    count = dest - first;
  }

  void widenLeft(size_t num)
  {
    num = std::min(this->first, num);
    this->first -= num;
    this->count += num;
    assert(endOffset() <= this->video->frames.size());
  }

  void trimLeft(size_t num)
  {
    num = std::min(num, this->count);
    this->first += num;
    this->count -= num;
  }

  FrameSpan left(size_t num) const
  {
    FrameSpan result{*this};

    if (num >= size())
    {
      return result;
    }

    result.count = num;
    return result;
  }

  FrameSpan right(size_t num) const
  {
    FrameSpan result{*this};

    if (num >= size())
    {
      return result;
    }

    result.first = endOffset() - num;
    result.count = num;
    return result;
  }

  FrameSpan subspan(size_t offset, size_t count) const
  {
    return FrameSpan(*video, first + offset, count);
  }

  bool contains(const FrameSpan& other) const
  {
    assert(other.video == this->video);
    return other.video == this->video && other.startOffset() >= startOffset()
           && other.endOffset() <= endOffset();
  }
};

inline bool operator==(const FrameSpan& lhs, const FrameSpan& rhs)
{
  return lhs.video == rhs.video && lhs.startOffset() == rhs.startOffset()
         && lhs.endOffset() == rhs.endOffset();
}

inline bool operator!=(const FrameSpan& lhs, const FrameSpan& rhs)
{
  return !(lhs == rhs);
}

} // namespace MatchAlgo

class MatchDetector
{
public:
  MatchAlgo::Parameters parameters;
  TimeSegment segmentA;
  TimeSegment segmentB;

public:
  MatchDetector(const MediaObject& a, const MediaObject& b);

  std::vector<VideoMatch> run();

private:
  const MediaObject* m_a;
  const MediaObject* m_b;
  // TODO: ajouter un système de log
};

#endif // MATCHALGO_H
