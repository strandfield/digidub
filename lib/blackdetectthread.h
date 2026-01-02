// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#pragma once

#include "mediainfo.h"

#include <QThread>

class MediaObject;

class BlackdetectThread : public QThread
{
  Q_OBJECT
public:
  explicit BlackdetectThread(const MediaObject& media);
  ~BlackdetectThread();

  double duration() const;
  std::vector<TimeSegment>& blackframes();

protected:
  void run() final;

private:
  QString m_filePath;
  QString m_fileName;
  int m_nbFrames;
  std::vector<TimeSegment> m_blackframes;
};
