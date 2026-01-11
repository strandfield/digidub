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

  bool isEditingMatchObject() const;
  void setCurrentMatchObject(MatchObject* mob);
  MatchObject* currentMatchObject() const;
  void reset();

  const VideoMatch& originalMatch() const;
  const VideoMatch& editedMatch() const;

  void clearCache();

public Q_SLOTS:
  void launchPreview();
  void accept();
  void cancel();

Q_SIGNALS:
  void editionFinished(bool accepted);

protected Q_SLOTS:
  void onMatchEdited();

private:
  QTemporaryDir& getTempDir();
  void refreshUi();

private:
  DubbingProject& m_project;
  MediaObject& m_video1;
  MediaObject& m_video2;
  VideoPlayerWidget* m_leftPlayer;
  VideoPlayerWidget* m_rightPlayer;
  std::array<MatchEditorItemWidget*, 2> m_items;
  QPushButton* m_preview_button;
  QPushButton* m_ok_button;
  QPushButton* m_cancel_button;
  QWidget* m_buttonsContainer;
  struct
  {
    QLabel* previousMatch;
    QLabel* currentMatch;
    QLabel* nextMatch;
  } m_navigation;
  MatchObject* m_editedMatchObject = nullptr;
  VideoMatch m_original_match;
  VideoMatch m_edited_match;
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
