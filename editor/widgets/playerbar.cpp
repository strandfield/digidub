#include "playerbar.h"

#include <QBrush>
#include <QPainter>
#include <QPen>

#include <QPaintEvent>

#include <utility>

qint64 Timestamp::to_msecs() const
{
  qint64 r = qint64(this->hours) * qint64(60 * 60 * 1000);
  r += this->minutes * (60 * 1000);
  r += this->seconds * 1000;
  r += this->milliseconds;
  return r;
}

Timestamp make_timestamp(qint64 msecs)
{
  Timestamp result;

  result.hours = msecs / (60 * 60 * 1000);
  msecs -= qint64(result.hours) * (60 * 60 * 1000);

  result.minutes = msecs / (60 * 1000);
  msecs -= result.minutes * (60 * 1000);

  result.seconds = msecs / 1000;
  msecs -= result.seconds * 1000;

  result.milliseconds = msecs;

  return result;
}

void append_to_string(QString &text, const Timestamp &ts)
{
  if (ts.hours < 10)
  {
    text += "0";
  }

  text += QString::number(ts.hours);

  text += ":";

  if (ts.minutes < 10)
  {
    text += "0";
  }

  text += QString::number(ts.minutes);

  text += ":";

  if (ts.seconds < 10)
  {
    text += "0";
  }

  text += QString::number(ts.seconds);

  text += ".";

  if (ts.milliseconds < 10)
  {
    text += "00";
  }
  else if (ts.milliseconds < 100)
  {
    text += "0";
  }

  text += QString::number(ts.milliseconds);
}

QString to_string(const Timestamp &ts)
{
  QString r;
  append_to_string(r, ts);
  return r;
}

PlayerBar::PlayerBar(QWidget *parent)
    : QWidget(parent)
{
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFixedHeight(5);
  setMinimumWidth(100);

  setMouseTracking(true);
}

PlayerBar::~PlayerBar() {}

void PlayerBar::setRange(double min, double max)
{
  bool u = false;

  if (std::exchange(m_min, min) != min)
  {
    u = true;
  }

  if (std::exchange(m_max, max) != max)
  {
    u = true;
  }

  if (u)
  {
    update();
  }
}

double PlayerBar::value() const
{
  return m_val;
}

void PlayerBar::setValue(double v)
{
  if (!qFuzzyCompare(v, m_val))
  {
    m_val = v;
    update();
  }
}

void PlayerBar::paintEvent(QPaintEvent *event)
{
  Q_UNUSED(event);

  const double ratio = (value() - m_min) / (m_max - m_min);

  QPainter painter{this};
  painter.setPen(Qt::NoPen);

  painter.setBrush(QBrush(QColor("#aeaeae")));
  painter.drawRect(this->rect());

  painter.setBrush(QBrush(QColor("#c0c0c0")));
  painter.drawRect(QRect(0, 0, this->width(), 1));

  painter.setBrush(QBrush(QColor("#e6e6e6")));
  painter.drawRect(QRect(0, 1, this->width(), this->height() - 2));

  painter.setBrush(QBrush(QColor("#0078d7")));
  painter.drawRect(QRect(0, 1, std::round(this->width() * ratio), this->height() - 2));
}

void PlayerBar::mousePressEvent(QMouseEvent *event)
{
  event->accept();
}

void PlayerBar::mouseMoveEvent(QMouseEvent *event)
{
  event->accept();

  const double ratio = event->position().x() / double(width());
  const double pos = m_min + ratio * (m_max - m_min);
  setToolTip(to_string(make_timestamp(pos * 1000)));
}

void PlayerBar::mouseReleaseEvent(QMouseEvent *event)
{
  event->accept();

  const double ratio = event->position().x() / double(width());
  const double pos = m_min + ratio * (m_max - m_min);
  Q_EMIT clicked(pos);
}

TimeDisplay::TimeDisplay(QWidget *parent)
    : QLabel(parent)
{
  QFont font = this->font();
  font.setWeight(QFont::Bold);
  setFont(font);

  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

TimeDisplay::~TimeDisplay() {}

void TimeDisplay::setMax(qint64 val)
{
  if (m_max != val)
  {
    m_max = val;
    update();
  }
}

void TimeDisplay::setCurrent(qint64 val)
{
  if (m_current != val)
  {
    m_current = val;
    update();
  }
}

void TimeDisplay::update()
{
  QString text;

  Timestamp ts = make_timestamp(m_current);
  append_to_string(text, ts);

  text += " / ";

  ts = make_timestamp(m_max);
  append_to_string(text, ts);

  setText(text);
}
