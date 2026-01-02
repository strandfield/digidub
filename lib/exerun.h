// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#pragma once

#include <QProcess>

#include <QStringList>

#include <QDebug>

QProcess* run(const QString& name, const QStringList& args);
void loop(QProcess& process);

QProcess* looprun(const QString& name, const QStringList& args);

inline int exec(const QString& name,
                const QStringList& args,
                QString* stdOut = nullptr,
                QString* stdErr = nullptr)
{
  QProcess* process = run(name, args);

  process->waitForFinished(20 * 60 * 1000);

  if (process->exitCode() != 0)
  {
    qDebug().noquote() << process->readAllStandardError();
  }

  if (stdOut)
  {
    *stdOut = QString::fromLocal8Bit(process->readAllStandardOutput());
  }

  if (stdErr)
  {
    *stdErr = QString::fromLocal8Bit(process->readAllStandardError());
  }

  process->deleteLater();
  return process->exitCode();
}

inline int ffmpeg(const QStringList& args, QString* stdOut = nullptr)
{
  return exec("ffmpeg", args, nullptr, stdOut);
}

inline int ffprobe(const QStringList& args, QString* stdOut = nullptr)
{
  return exec("ffprobe", args, stdOut);
}
