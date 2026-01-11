// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#ifndef PROJECT_H
#define PROJECT_H

#include "match.h"

#include <QObject>

#include <vector>

// TXT format:
// DIGIDUB PROJECT
// VERSION 1
// TITLE Digimon S1E1 - Toto
// VIDEO C:/video-en.mkv
// AUDIO C:/video-fr.mkv
// OUTPUT C:/output.mkv
// SUBTITLES C:/video.srt
// BEGIN MATCHLIST
// aaa-bbb ~ aaa-bbb
// aaa-bbb ~ aaa-bbb
// aaa-bbb ~ aaa-bbb
// END MATCHLIST

class QDir;

class DubbingProject;

class MatchObject : public QObject
{
  Q_OBJECT
public:
  explicit MatchObject(const QString& text, QObject* parent = nullptr);
  explicit MatchObject(const VideoMatch& val, QObject* parent = nullptr);

  int id() const;

  DubbingProject* project() const;
  MatchObject* previous() const;
  MatchObject* next() const;
  int64_t distanceTo(const MatchObject& other) const;

  const VideoMatch& value() const;
  void setValue(const VideoMatch& val);

  QString toString() const;

Q_SIGNALS:
  void changed();

private:
  VideoMatch m_value;
};

void sort(std::vector<MatchObject*>& matches);
std::vector<VideoMatch> convert2vm(const std::vector<MatchObject*>& matches);

class DubbingProject : public QObject
{
  Q_OBJECT

  Q_PROPERTY(QString projectTitle READ projectTitle WRITE setProjectTitle NOTIFY projectTitleChanged)
  Q_PROPERTY(
      QString outputFilePath READ outputFilePath WRITE setOutputFilePath NOTIFY projectTitleChanged)
  Q_PROPERTY(bool modified READ modified WRITE setModified NOTIFY modifiedChanged)
public:
  explicit DubbingProject(QObject* parent = nullptr);
  explicit DubbingProject(const QString& filePathOrTitle, QObject* parent = nullptr);
  DubbingProject(const QString& videoPath, const QString& audioPath, QObject* parent = nullptr);

  const QString& projectTitle() const;
  void setProjectTitle(const QString& title);

  const QString& projectFilePath() const;
  void setProjectFilePath(const QString& path);
  int convertFilePathsToAbsolute();
  int convertFilePathsToRelative();
  QDir projectDirectory() const;

  bool load(const QString& projectFilePath);
  void save(const QString& path);
  void dump(QTextStream& out);

  const QString& videoFilePath() const;
  void setVideoFilePath(const QString& path);
  const QString& audioSourceFilePath() const;
  void setAudioSourceFilePath(const QString& path);
  const QString& subtitlesFilePath() const;
  void setSubtitlesFilePath(const QString& subtitlesFile);

  const QString& outputFilePath() const;
  void setOutputFilePath(const QString& filePath);

  QString resolvePath(const QString& filePath) const;

  MatchObject* createMatch(const VideoMatch& val);
  void addMatch(MatchObject* match);
  void removeMatch(MatchObject* match);
  const std::vector<MatchObject*>& matches() const;
  void addMatches(const std::vector<VideoMatch>& values);

  void sortMatches();

  bool modified() const;
  void setModified(bool modified = true);

Q_SIGNALS:
  void projectFilePathChanged();
  void projectTitleChanged();
  void subtitlesFilePathChanged(const QString& newValue);
  void outputFilePathChanged(const QString& newValue);
  void matchAdded(MatchObject* match);
  void matchRemoved(MatchObject* match);
  void matchChanged(MatchObject* match);
  void modifiedChanged();

protected Q_SLOTS:
  void onMatchChanged();

private:
  void setupConnectionsTo(MatchObject* mobj);

private:
  QString m_projectTitle;
  QString m_projectFilePath;
  QString m_videoFilePath;
  QString m_audioSourceFilePath;
  QString m_subtitlesFilePath;
  QString m_outputFilePath;
  std::vector<MatchObject*> m_matches;
  bool m_modified = false;
};

#endif // PROJECT_H
