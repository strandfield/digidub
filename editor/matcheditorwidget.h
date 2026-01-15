// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#ifndef MATCHEDITORWIDGET_H
#define MATCHEDITORWIDGET_H

#include "match.h"

#include <QWidget>

class QLabel;
class QPushButton;

class QTemporaryDir;

class DubbingProject;
class MatchObject;
class MediaObject;

class MatchEditorItemWidget;
class VideoPlayerWidget;

class MatchEditorWidget : public QWidget
{
  Q_OBJECT
public:
  MatchEditorWidget(DubbingProject& project,
                    MediaObject& video1,
                    MediaObject& video2,
                    QWidget* parent = nullptr);
  ~MatchEditorWidget();

  DubbingProject& project() const;
  MediaObject& video1() const;
  MediaObject& video2() const;

  VideoPlayerWidget* playerLeft() const;
  VideoPlayerWidget* playerRight() const;

  void setCurrentMatchObject(MatchObject* mob);
  MatchObject* currentMatchObject() const;
  void reset();

  void clearCache();

  struct SelectionRange
  {
    int first = 0;
    int last = -1;

    operator bool() const { return last >= first; }
  };

  std::pair<SelectionRange, SelectionRange> selectedRanges() const;

public Q_SLOTS:
  void launchPreview();

protected Q_SLOTS:
  void onLinkActivated(const QString& link);
  void onAnyFrameRangeEdited();
  void onMatchObjectChanged();

private:
  QTemporaryDir& getTempDir();
  void refreshUi();
  SelectionRange getSelectionRange(const MatchEditorItemWidget& item) const;

private:
  DubbingProject& m_project;
  MediaObject& m_video1;
  MediaObject& m_video2;
  VideoPlayerWidget* m_leftPlayer;
  VideoPlayerWidget* m_rightPlayer;
  std::array<MatchEditorItemWidget*, 2> m_items;
  struct
  {
    QLabel* previousMatch;
    QLabel* currentMatch;
    QLabel* nextMatch;
  } m_navigation;
  MatchObject* m_editedMatchObject = nullptr;
  std::unique_ptr<QTemporaryDir> m_tempDir;
};

inline DubbingProject& MatchEditorWidget::project() const
{
  return m_project;
}

inline MediaObject& MatchEditorWidget::video1() const
{
  return m_video1;
}

inline MediaObject& MatchEditorWidget::video2() const
{
  return m_video2;
}

inline MatchObject* MatchEditorWidget::currentMatchObject() const
{
  return m_editedMatchObject;
}

#endif // MATCHEDITORWIDGET_H
