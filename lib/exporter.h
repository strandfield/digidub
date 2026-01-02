// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#pragma once

#include "timesegment.h"

#include <QObject>

#include <vector>

class QProcess;

class DubbingProject;
class MediaObject;

struct OutputSegment
{
  TimeSegment output_segment;
  int source_id;
  TimeSegment source_segment;
};

std::vector<OutputSegment> dubCompute(const DubbingProject& project, const MediaObject& video);

void exportProject(const DubbingProject& project,
                   const MediaObject& video,
                   const QString& outputFilePath);

void exportProject(const DubbingProject& project, const MediaObject& video);

class DubExporter : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(float progress READ progress NOTIFY progressChanged)
public:
  DubExporter(const DubbingProject& project, const MediaObject& video, QObject* parent = nullptr);
  ~DubExporter();

  const DubbingProject& project() const;

  const QString& outputFilePath() const;
  void setOutputFilePath(const QString& path);

  void run();
  bool isRunning() const;

  QString status() const;
  float progress() const;

  void waitForFinished();

Q_SIGNALS:
  void statusChanged();
  void progressChanged();
  void finished();

private Q_SLOTS:
  void step();
  void advanceToNextStep();

private:
  template<typename Callback>
  QProcess* run(const QString& program, const QStringList& args, Callback&& onFinished);

private:
  const DubbingProject& m_project;
  const MediaObject& m_video;
  QString m_outputFilePath;
  struct Data;
  std::unique_ptr<Data> d;
};
