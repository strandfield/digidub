// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#ifndef VIDEOPLAYERWIDGET_H
#define VIDEOPLAYERWIDGET_H

#include <QWidget>

class QAudioOutput;
class QMediaPlayer;
class QVideoWidget;

class MediaObject;
class PlayerBar;
class PlayerButton;
class PlayerPlayButton;
class TimeDisplay;

class VideoPlayerWidget : public QWidget
{
  Q_OBJECT
public:
  VideoPlayerWidget();
  ~VideoPlayerWidget();

  MediaObject* media() const;
  void setMedia(MediaObject* media);

  QMediaPlayer* player() const;

  int64_t position() const;

public Q_SLOTS:
  void play();
  void pause();
  void stop();
  void togglePlay();
  void stepForward();
  void stepBackward();
  void seekTime(double val);
  void seek(int val);

Q_SIGNALS:
  void currentPausedImageChanged();

protected Q_SLOTS:
  void onMediaPlayerPositionChanged(qint64 pos);
  void onMediaStatusChanged();
  void onPlayButtonToggled();

private:
  MediaObject* m_media = nullptr;
  QMediaPlayer* m_player = nullptr;
  QVideoWidget* m_video_widget = nullptr;
  QAudioOutput* m_audio_output = nullptr;
  PlayerBar* m_playerBar = nullptr;
  TimeDisplay* m_timeDisplay = nullptr;
  PlayerPlayButton* m_play_button = nullptr;
  PlayerButton* m_stop_button = nullptr;
  PlayerButton* m_stepForwardButton = nullptr;
  PlayerButton* m_stepBackwardButton = nullptr;
};

#endif // VIDEOPLAYERWIDGET_H
