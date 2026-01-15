// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#ifndef MEDIAOBJECT_H
#define MEDIAOBJECT_H

#include "mediainfo.h"

#include <QObject>

struct FramesInfo
{
  std::vector<VideoFrameInfo> frames;
};

struct SilenceInfo
{
  double minimumDuration;
  std::vector<TimeSegment> silences;
};

struct BlackFramesInfo
{
  double minimumDuration;
  std::vector<TimeSegment> blackframes;
};

struct ScenesInfo
{
  std::vector<SceneChange> scenechanges;
};

class BlackdetectThread;
class FrameExtractionThread;
class ScdetThread;
class SilencedetectThread;

class MediaObject : public QObject
{
  Q_OBJECT
public:
  explicit MediaObject(const QString& filePath, QObject* parent = nullptr);

  const QString& filePath() const;
  QString fileName() const;

  const QString& title() const;

  double duration() const;
  double frameRate() const;
  double frameDelta() const;
  int numberOfPackets() const;
  const std::pair<int, int>& frameRateAsRational() const;

  int64_t convertPtsToPosition(int pts) const;
  TimeSegment convertFrameRangeToTimeSegment(int firstFrameIdx, int lastFrameIdx) const;

  FramesInfo* framesInfo() const;
  void extractFrames();
  FrameExtractionThread* frameExtractionThread() const;

  SilenceInfo* silenceInfo() const;
  void silencedetect();
  SilencedetectThread* silencedetectThread() const;

  BlackFramesInfo* blackFramesInfo() const;
  void blackdetect();
  BlackdetectThread* blackdetectThread() const;

  ScenesInfo* scenesInfo() const;
  void scdet();
  ScdetThread* scdetThread() const;

Q_SIGNALS:
  void framesAvailable();

protected Q_SLOTS:
  void onFrameExtractionFinished();
  void onSilencedetectFinished();
  void onBlackdetectFinished();
  void onScdetFinished();

private:
  QString m_filePath;
  QString m_title;
  double m_duration;
  std::pair<int, int> m_frameRate;
  int m_readPackets;
  std::unique_ptr<FramesInfo> m_frames;
  std::unique_ptr<FrameExtractionThread> m_frameExtractionThread;
  std::unique_ptr<SilenceInfo> m_silenceInfo;
  std::unique_ptr<SilencedetectThread> m_silencedetectThread;
  std::unique_ptr<BlackFramesInfo> m_blackFrames;
  std::unique_ptr<BlackdetectThread> m_blackdetectThread;
  std::unique_ptr<ScenesInfo> m_scenes;
  std::unique_ptr<ScdetThread> m_scdetThread;
};

inline const QString& MediaObject::title() const
{
  return m_title;
}

inline double MediaObject::duration() const
{
  return m_duration;
}

inline double MediaObject::frameRate() const
{
  return m_frameRate.first / double(m_frameRate.second);
}

inline double MediaObject::frameDelta() const
{
  return m_frameRate.second / double(m_frameRate.first);
}

inline int MediaObject::numberOfPackets() const
{
  return m_readPackets;
}

inline const std::pair<int, int>& MediaObject::frameRateAsRational() const
{
  return m_frameRate;
}

inline int64_t MediaObject::convertPtsToPosition(int pts) const
{
  return (int64_t(1000) * int64_t(m_frameRate.second) * int64_t(pts)) / int64_t(m_frameRate.first);
}

inline FramesInfo* MediaObject::framesInfo() const
{
  return m_frames.get();
}

inline FrameExtractionThread* MediaObject::frameExtractionThread() const
{
  return m_frameExtractionThread.get();
}

#endif // MEDIAOBJECT_H
