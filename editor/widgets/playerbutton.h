// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#ifndef PLAYERBUTTON_H
#define PLAYERBUTTON_H

#include <QPixmap>
#include <QWidget>

class PlayerButton : public QWidget
{
  Q_OBJECT
public:
  explicit PlayerButton(const QString& resPattern, QWidget* parent = nullptr);
  ~PlayerButton();

Q_SIGNALS:
  void clicked();

protected:
  void paintEvent(QPaintEvent *event) override;

  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;

  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

protected:
  void setPixmaps(const QString& resPattern);

private:
  QPixmap m_imageDefault;
  QPixmap m_imageHovered;
  QPixmap m_imagePressed;
  bool m_hovered = false;
  bool m_pressed = false;
};

class PlayerPlayButton : public PlayerButton
{
  Q_OBJECT
public:
  explicit PlayerPlayButton(QWidget* parent = nullptr);
  ~PlayerPlayButton() = default;

  bool playing() const;
  void setPlaying(bool playing = true);
  void toggle();

Q_SIGNALS:
  void toggled();

private:
  bool m_playing = false;
};

#endif // PLAYERBUTTON_H
