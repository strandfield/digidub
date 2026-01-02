// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#pragma once

#include "mediainfo.h"

#include <QThread>

class MediaObject;

class FrameExtractionThread : public QThread
{
  Q_OBJECT
public:
  explicit FrameExtractionThread(const MediaObject& media);
  ~FrameExtractionThread();

  std::vector<VideoFrameInfo>& frames();

Q_SIGNALS:
  void progressChanged(float value);

protected:
  void run() final;

private:
  QString m_filePath;
  int m_nbFrames;
  std::vector<VideoFrameInfo> m_frames;
};
