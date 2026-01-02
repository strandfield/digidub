// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#pragma once

#include "mediainfo.h"

#include <QThread>

class MediaObject;

class SilencedetectThread : public QThread
{
  Q_OBJECT
public:
  explicit SilencedetectThread(const MediaObject& media);
  ~SilencedetectThread();

  double duration() const;
  std::vector<TimeSegment>& silences();

protected:
  void run() final;

private:
  QString m_filePath;
  QString m_fileName;
  int m_nbFrames;
  std::vector<TimeSegment> m_silences;
};
