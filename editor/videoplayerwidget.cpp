#include "videoplayerwidget.h"

#include "widgets/playerbar.h"
#include "widgets/playerbutton.h"

#include "mediaobject.h"

#include <QAudioOutput>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QVideoWidget>

#include <QPushButton>

#include <QVBoxLayout>

#include <QDebug>

VideoPlayerWidget::VideoPlayerWidget()
{
  m_player = new QMediaPlayer(this);

  m_video_widget = new QVideoWidget;
  m_video_widget->setMinimumSize(400, 260);
  m_player->setVideoOutput(m_video_widget);

  m_audio_output = new QAudioOutput;
  m_player->setAudioOutput(m_audio_output);

  if (auto* l = new QVBoxLayout(this))
  {
    l->setSpacing(0);
    l->addWidget(m_video_widget, 2);

    l->addSpacing(6);

    m_playerBar = new PlayerBar;
    m_playerBar->setEnabled(false);
    l->addWidget(m_playerBar);

    l->addSpacing(2);

    if (auto* row = new QHBoxLayout)
    {
      if (auto* btnsrow = new QHBoxLayout)
      {
        btnsrow->setContentsMargins(QMargins());
        btnsrow->setSpacing(6);
        m_play_button = new PlayerPlayButton(this);
        m_stop_button = new PlayerButton(":/images/stop-%1.png", this);
        m_stepForwardButton = new PlayerButton(":/images/forward-%1.png", this);
        m_stepBackwardButton = new PlayerButton(":/images/backward-%1.png", this);

        btnsrow->addWidget(m_play_button, 0, Qt::AlignTop);
        btnsrow->addWidget(m_stop_button, 0, Qt::AlignVCenter);
        btnsrow->addWidget(m_stepBackwardButton, 0, Qt::AlignVCenter);
        btnsrow->addWidget(m_stepForwardButton, 0, Qt::AlignVCenter);

        row->addLayout(btnsrow);
      }

      row->addStretch();

      m_timeDisplay = new TimeDisplay(this);
      row->addWidget(m_timeDisplay, 0, Qt::AlignTop);

      l->addLayout(row);
    }

    l->addStretch();
  }

  // connect player bar
  {
    connect(m_player,
            &QMediaPlayer::positionChanged,
            this,
            &VideoPlayerWidget::onMediaPlayerPositionChanged);
    connect(m_playerBar, &PlayerBar::clicked, this, &VideoPlayerWidget::seekTime);
  }

  // connect buttons
  {
    connect(m_play_button,
            &PlayerPlayButton::toggled,
            this,
            &VideoPlayerWidget::onPlayButtonToggled);
    connect(m_stop_button, &PlayerButton::clicked, this, &VideoPlayerWidget::stop);
    connect(m_stepBackwardButton, &PlayerButton::clicked, this, &VideoPlayerWidget::stepBackward);
    connect(m_stepForwardButton, &PlayerButton::clicked, this, &VideoPlayerWidget::stepForward);
  }

  // other connections
  {
    connect(m_player,
            &QMediaPlayer::mediaStatusChanged,
            this,
            &VideoPlayerWidget::onMediaStatusChanged);
  }
}

VideoPlayerWidget::~VideoPlayerWidget() {}

MediaObject* VideoPlayerWidget::media() const
{
  return m_media;
}

void VideoPlayerWidget::setMedia(MediaObject* media)
{
  if (m_media == media)
  {
    return;
  }

  m_media = media;

  if (m_media)
  {
    m_player->setSource(QUrl::fromLocalFile(media->filePath()));
    m_playerBar->setEnabled(true);

    m_playerBar->setRange(0, m_media->duration());
    m_timeDisplay->setMax(m_media->duration() * 1000);
  }
  else
  {
    m_playerBar->setRange(0, 1);
    m_timeDisplay->setMax(0);
    m_playerBar->setEnabled(false);
  }
}

QMediaPlayer* VideoPlayerWidget::player() const
{
  return m_player;
}

int64_t VideoPlayerWidget::position() const
{
  return m_player->position();
}

void VideoPlayerWidget::play()
{
  m_player->play();

  // TODO: mieux vaudrait se connecter à un signal sur le player
  m_play_button->setPlaying(true);
}

void VideoPlayerWidget::pause()
{
  m_player->pause();

  // TODO: mieux vaudrait se connecter à un signal sur le player
  m_play_button->setPlaying(false);

  Q_EMIT currentPausedImageChanged();
}

void VideoPlayerWidget::stop()
{
  m_player->stop();
}

void VideoPlayerWidget::togglePlay()
{
  if (m_player->isPlaying())
  {
    pause();
  }
  else
  {
    play();
  }
}

void VideoPlayerWidget::stepForward()
{
  m_player->setPosition(m_player->position() + 3000);
}

void VideoPlayerWidget::stepBackward()
{
  m_player->setPosition(m_player->position() - 3000);
}

void VideoPlayerWidget::seekTime(double val)
{
  seek(std::round(val * 1000));
}

void VideoPlayerWidget::seek(int val)
{
  m_player->setPosition(val);

  if (m_player->playbackState() == QMediaPlayer::PausedState)
  {
    Q_EMIT currentPausedImageChanged();
  }
}

void VideoPlayerWidget::onMediaPlayerPositionChanged(qint64 pos)
{
  m_playerBar->setValue(pos / double(1000));
  m_timeDisplay->setCurrent(pos);
}

void VideoPlayerWidget::onMediaStatusChanged()
{
  if (m_player->mediaStatus() == QMediaPlayer::LoadedMedia)
  {
    qDebug() << m_player->audioTracks();
    m_player->setActiveAudioTrack(0);
  }
}

void VideoPlayerWidget::onPlayButtonToggled()
{
  if (m_play_button->playing())
  {
    play();
  }
  else
  {
    pause();
  }
}
