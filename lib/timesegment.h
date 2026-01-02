// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#pragma once

#include <cstdint>

class QString;

class Duration
{
private:
  int64_t m_value; // msecs

public:
  explicit Duration(int64_t val)
      : m_value(val)
  {}

  int64_t toMSecs() const;

  enum Format { Seconds, HHMMSSzzz };

  QString toString(Format format) const;
  static Duration fromString(const QString& text);
  bool parse(const QString& text);
};

inline int64_t Duration::toMSecs() const
{
  return m_value;
}

class TimeSegment
{
private:
  int64_t m_start = 0; // msecs
  int64_t m_end = 0;   // msecs

public:
  TimeSegment() = default;
  TimeSegment(const TimeSegment&) = default;

  TimeSegment(int64_t start, int64_t end)
      : m_start(start)
      , m_end(end)
  {}

  TimeSegment(int64_t start, Duration d)
      : m_start(start)
      , m_end(start + d.toMSecs())
  {}

  static TimeSegment between(int64_t start, int64_t end);

  int64_t start() const;
  int64_t end() const;
  int64_t duration() const;
  double toSeconds() const;

  bool contains(int64_t t) const;

  QString toString() const;
  static TimeSegment fromString(const QString& text);
};

inline TimeSegment TimeSegment::between(int64_t start, int64_t end)
{
  return TimeSegment(start, end);
}

inline int64_t TimeSegment::start() const
{
  return m_start;
}

inline int64_t TimeSegment::end() const
{
  return m_end;
}

inline int64_t TimeSegment::duration() const
{
  return end() - start();
}

inline double TimeSegment::toSeconds() const
{
  return duration() / double(1000);
}

inline bool TimeSegment::contains(int64_t t) const
{
  return start() <= t && t < end();
}

inline bool operator==(const TimeSegment& lhs, const TimeSegment& rhs)
{
  return lhs.start() == rhs.start() && lhs.end() == rhs.end();
}

inline bool operator!=(const TimeSegment& lhs, const TimeSegment& rhs)
{
  return !(lhs == rhs);
}
