// Copyright (C) 2026 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#pragma once

#include <QLabel>
#include <QWidget>

class MediaObject;

class WaveformViewer : public QWidget
{
  Q_OBJECT
public:
  explicit WaveformViewer(QWidget *parent = nullptr);
  ~WaveformViewer();

  MediaObject *media() const;
  void setMedia(MediaObject *media);

  int64_t duration() const;

  int64_t position() const;
  void setPosition(int64_t pos);

  std::pair<int64_t, int64_t> visibleRange() const;
  int64_t convertCursorPosToVideoPosition(int cursorX) const;

Q_SIGNALS:
  void clicked(int64_t pos);

protected:
  void paintEvent(QPaintEvent *event) override;

  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  void ensurePositionIsVisible();

private:
  MediaObject *m_media = nullptr;
  int64_t m_duration = 1000;
  int64_t m_position = 0;
  int64_t m_scrollInPixel = 0;
  int64_t m_timePerPixel = 10;
};

