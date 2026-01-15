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

class MainWindow;

class MatchListWindow : public QWidget
{
  Q_OBJECT
public:
  MatchListWindow(DubbingProject& project, MainWindow& window, QWidget* parent = nullptr);
  ~MatchListWindow();

  DubbingProject& project() const;

Q_SIGNALS:
  void closed();
  void matchDoubleClicked(MatchObject* mob);

protected Q_SLOTS:
  void onItemDoubleClicked(QTreeWidgetItem* item);

protected Q_SLOTS:
  void onMatchAdded(MatchObject* mob);
  void onMatchRemoved(MatchObject* mob);
  void onMatchChanged(MatchObject* mob);
  void findMatchBeforeSelected();
  void findMatchAfterSelected();

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

  void closeEvent(QCloseEvent* ev) override;

private:
  void setupConnectionsTo(DubbingProject* project);
  void resetMatchList();
  void fill(QTreeWidgetItem* item, MatchObject* mob);
  MatchObject* getSelectedMatchObject() const;

private:
  DubbingProject& m_project;
  MainWindow& m_window;
  QTreeWidget* m_matchListWidget;
};

