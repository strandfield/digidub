#include "playerbutton.h"

#include <QPainter>

#include <QPaintEvent>

PlayerButton::PlayerButton(const QString& resPattern, QWidget* parent)
    : QWidget(parent)
{
  setPixmaps(resPattern);
}

PlayerButton::~PlayerButton() {}

void PlayerButton::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);

  QPixmap* pix = &m_imageDefault;

  if (m_pressed)
  {
    pix = &m_imagePressed;
  }
  else if (m_hovered)
  {
    pix = &m_imageHovered;
  }

  QPainter painter{this};
  painter.drawPixmap(0, 0, *pix);
}

void PlayerButton::enterEvent(QEnterEvent* event)
{
  QWidget::enterEvent(event);
  m_hovered = true;
  update();
}

void PlayerButton::leaveEvent(QEvent* event)
{
  QWidget::leaveEvent(event);
  m_hovered = false;
  update();
}

void PlayerButton::mousePressEvent(QMouseEvent* event)
{
  QWidget::mousePressEvent(event);
  m_pressed = true;
  update();
}

void PlayerButton::mouseMoveEvent(QMouseEvent* event)
{
  QWidget::mouseMoveEvent(event);
}

void PlayerButton::mouseReleaseEvent(QMouseEvent* event)
{
  QWidget::mousePressEvent(event);
  if (rect().contains(event->pos()))
  {
    Q_EMIT clicked();
  }

  m_pressed = false;
  update();
}

void PlayerButton::setPixmaps(const QString& resPattern)
{
  m_imageDefault = QPixmap(resPattern.arg("default"));
  m_imageHovered = QPixmap(resPattern.arg("hovered"));
  m_imagePressed = QPixmap(resPattern.arg("pressed"));

  setFixedSize(m_imageDefault.size());
  update();
}

PlayerPlayButton::PlayerPlayButton(QWidget* parent)
    : PlayerButton(":/images/play-%1.png", parent)
{
  connect(this, &PlayerButton::clicked, this, &PlayerPlayButton::toggle);
}

bool PlayerPlayButton::playing() const
{
  return m_playing;
}

void PlayerPlayButton::setPlaying(bool playing)
{
  if (m_playing != playing)
  {
    m_playing = playing;

    setPixmaps(m_playing ? ":/images/pause-%1.png" : ":/images/play-%1.png");
  }
}

void PlayerPlayButton::toggle()
{
  setPlaying(!playing());
  Q_EMIT toggled();
}
