// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#ifndef PLAYERBAR_H
#define PLAYERBAR_H

#include <QLabel>
#include <QWidget>

class PlayerBar : public QWidget
{
  Q_OBJECT
public:
  explicit PlayerBar(QWidget *parent = nullptr);
  ~PlayerBar();

  void setRange(double min, double max);
  double value() const;
  void setValue(double v);

Q_SIGNALS:
  void clicked(double pos);

protected:
  void paintEvent(QPaintEvent *event) override;

  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  double m_min = 0;
  double m_max = 1;
  double m_val = 0;
};

struct Timestamp
{
  int hours;
  int minutes;
  int seconds;
  int milliseconds;

  qint64 to_msecs() const;
};

Timestamp make_timestamp(qint64 msecs);
QString to_string(const Timestamp &ts);

class TimeDisplay : public QLabel
{
  Q_OBJECT
public:
  explicit TimeDisplay(QWidget *parent = nullptr);
  ~TimeDisplay();

  void setMax(qint64 val);
  void setCurrent(qint64 val);

private:
  void update();

private:
  qint64 m_current = 0;
  qint64 m_max = 0;
};

#endif // PLAYERBAR_H
