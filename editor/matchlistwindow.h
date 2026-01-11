// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#pragma once

#include "timesegment.h"

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;

class DubbingProject;
class MatchObject;

class MediaObject;

// TODO: add a right-click menu ?
class MatchListWindow : public QWidget
{
  Q_OBJECT
public:
  explicit MatchListWindow(DubbingProject& project, QWidget* parent = nullptr);
  ~MatchListWindow();

  DubbingProject& project() const;
  void setVideoDuration(int64_t msecs);

Q_SIGNALS:
  void closed();
  void matchDoubleClicked(MatchObject* mob);
  void findMatchRequested(const TimeSegment& withinSegment, const TimeSegment& defaultResult);

protected Q_SLOTS:
  void onItemDoubleClicked(QTreeWidgetItem* item);

protected Q_SLOTS:
  void onMatchAdded(MatchObject* mob);
  void onMatchRemoved(MatchObject* mob);
  void onMatchChanged(MatchObject* mob);

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

  void closeEvent(QCloseEvent* ev) override;

private:
  void setupConnectionsTo(DubbingProject* project);
  void resetMatchList();
  void fill(QTreeWidgetItem* item, MatchObject* mob);
  MatchObject* getSelectedMatchObject() const;

  void findMatchAfter(MatchObject& matchObject);
  void findMatchBefore(MatchObject& matchObject);

private:
  DubbingProject& m_project;
  QTreeWidget* m_matchListWidget;
  int64_t m_videoDuration = 0;
};

