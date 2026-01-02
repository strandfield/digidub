// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#pragma once

#include "mediainfo.h"

#include <QThread>

class MediaObject;

class ScdetThread : public QThread
{
  Q_OBJECT
public:
  explicit ScdetThread(const MediaObject& media);
  ~ScdetThread();

  std::vector<SceneChange>& scenechanges();

protected:
  void run() final;

private:
  QString m_filePath;
  QString m_fileName;
  int m_nbFrames;
  std::vector<SceneChange> m_scenechanges;
};
