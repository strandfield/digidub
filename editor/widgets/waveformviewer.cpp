#include "waveformviewer.h"

#include "playerbar.h" // for make_timestamp(), not ideal

#include "mediaobject.h"

#include <QBrush>
#include <QPainter>
#include <QPen>

#include <QPaintEvent>

#include <utility>

WaveformViewer::WaveformViewer(QWidget *parent)
    : QWidget(parent)
{
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFixedHeight(80);
  setMinimumWidth(100);

  setMouseTracking(true);
}

WaveformViewer::~WaveformViewer() {}

MediaObject *WaveformViewer::media() const
{
  return m_media;
}
void WaveformViewer::setMedia(MediaObject *media)
{
  m_media = media;
  if (media)
  {
    m_duration = media->duration() * 1000;
    update();

    if (!media->audioInfo())
    {
      connect(media,
              &MediaObject::audioAvailable,
              this,
              qOverload<>(&WaveformViewer::update),
              Qt::ConnectionType(Qt::AutoConnection | Qt::SingleShotConnection));
    }
  }
}

int64_t WaveformViewer::duration() const
{
  return m_duration;
}

int64_t WaveformViewer::position() const
{
  return m_position;
}

void WaveformViewer::setPosition(int64_t pos)
{
  pos = std::clamp<int64_t>(pos, 0, duration());

  if (m_position != pos)
  {
    m_position = pos;
    ensurePositionIsVisible();
    update();
  }
}

std::pair<int64_t, int64_t> WaveformViewer::visibleRange() const
{
  return std::pair(m_scrollInPixel * m_timePerPixel, (m_scrollInPixel + width()) * m_timePerPixel);
}

int64_t WaveformViewer::convertCursorPosToVideoPosition(int cursorX) const
{
  return (m_scrollInPixel + cursorX) * m_timePerPixel;
}

void WaveformViewer::paintEvent(QPaintEvent *event)
{
  Q_UNUSED(event);

  QPainter painter{this};
  painter.setPen(Qt::NoPen);

  painter.setBrush(QBrush(Qt::black));
  painter.drawRect(this->rect());

  // draw current pos
  {
    const std::pair<int64_t, int64_t> range = visibleRange();
    const auto [start, end] = range;
    if (position() >= start && position() < end)
    {
      int x = width() * (position() - start) / (end - start);

      painter.setBrush(Qt::NoBrush);
      painter.setPen(QPen(Qt::red));
      painter.drawLine(x, 0, x, height());
    }
  }

  if (m_media && m_media->audioInfo())
  {
    const AudioWaveformInfo &wav = *m_media->audioInfo();

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::yellow));

    for (int x(0); x < width(); ++x)
    {
      const WavSample sample = wav.getSampleForTime((x + m_scrollInPixel) * m_timePerPixel);
      int ymin = (height() / 2) - (height() / 2) * getWavSampleHigh(sample) / 255;
      int ymax = (height() / 2) - (height() / 2) * getWavSampleLow(sample) / 255;
      painter.drawLine(x, ymin, x, ymax);
    }
  }
}

void WaveformViewer::mousePressEvent(QMouseEvent *event)
{
  event->accept();
}

void WaveformViewer::mouseMoveEvent(QMouseEvent *event)
{
  event->accept();

  int64_t pos = convertCursorPosToVideoPosition(event->position().x());
  setToolTip(to_string(make_timestamp(pos)));
}

void WaveformViewer::mouseReleaseEvent(QMouseEvent *event)
{
  event->accept();

  int64_t pos = convertCursorPosToVideoPosition(event->position().x());
  Q_EMIT clicked(pos);
}

void WaveformViewer::ensurePositionIsVisible()
{
  const int64_t pos = position();
  const std::pair<int64_t, int64_t> range = visibleRange();
  const auto [start, end] = range;
  if (pos >= start && pos < end)
  {
    return;
  }

  if (pos < start)
  {
    m_scrollInPixel = pos / m_timePerPixel - (end - start) / m_timePerPixel;
    if (m_scrollInPixel < 0)
    {
      m_scrollInPixel = 0;
    }
  }
  else
  {
    m_scrollInPixel = pos / m_timePerPixel;
  }
}
