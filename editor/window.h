// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#ifndef WINDOW_H
#define WINDOW_H

#include "timesegment.h"

#include <QMainWindow>

class QSettings;
class QUndoStack;

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

class DubbingProject;
class MatchObject;

class MediaObject;
class MatchEditorWidget;
class MatchListWindow;
class VideoPlayerWidget;

class MainWindow : public QMainWindow
{
  Q_OBJECT
public:
  MainWindow();
  ~MainWindow();

  QSettings& settings() const;
  QUndoStack& undoStack() const;

  QString getLastOpenDir() const;
  void updateLastOpenDir(const QString& path);

  void openFile(const QString& filePath);

  MatchEditorWidget* currentMatchEditorWidget() const;

  void findMatchAfter(MatchObject& matchObject);
  void findMatchBefore(MatchObject& matchObject);
  void findMatch(const TimeSegment& withinSegment, const TimeSegment& defaultResult);
  void insertMatchAt(int64_t pos);

public Q_SLOTS:
  void about();
  void setThumbnailSize(int s);

protected Q_SLOTS:
  void actOpen();
  void actSave();
  void doExport();
  void toggleMatchListPopup();
  void deleteCurrentMatch();

protected:
  void closeEvent(QCloseEvent* event) override;

private Q_SLOTS:
  void refreshUi();
  void updateWindowTitle();
  void launchMatchEditor();

private:
  void updateLastSaveDir(const QString& filePath);

private Q_SLOTS:
  void debugProc();

private:
  QSettings* m_settings = nullptr;
  QUndoStack* m_undoStack = nullptr;
  DubbingProject* m_project = nullptr;
  MediaObject* m_primaryMedia = nullptr;
  MediaObject* m_secondaryMedia = nullptr;
  struct
  {
    QAction* openProject = nullptr;
    QAction* saveProject = nullptr;
    QAction* exportProject = nullptr;
    QAction* toggleMatchListWindow = nullptr;
    QAction* deleteCurrentMatch = nullptr;
  } m_actions;
  MatchEditorWidget* m_matchEditorWidget = nullptr;
  MatchListWindow* m_matchListWindow = nullptr;
  QDialog* m_aboutDialog = nullptr;
};

inline MatchEditorWidget* MainWindow::currentMatchEditorWidget() const
{
  return m_matchEditorWidget;
}

#endif // WINDOW_H
