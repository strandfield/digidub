#include "exerun.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

QProcess* run(const QString& name, const QStringList& args)
{
  qDebug().noquote() << (QStringList() << name << args).join(" ");

  QObject* parent = qApp->thread() == QThread::currentThread() ? qApp : nullptr;

  auto* process = new QProcess(parent);
  process->setProgram(name);
  process->setArguments(args);
  process->start();
  return process;
}

void loop(QProcess& process)
{
  if (process.state() == QProcess::NotRunning)
  {
    return;
  }

  QEventLoop ev;
  QObject::connect(&process, &QProcess::finished, &ev, &QEventLoop::exit);
  ev.exec();
}

QProcess* looprun(const QString& name, const QStringList& args)
{
  QProcess* process = run(name, args);
  loop(*process);
  process->deleteLater();
  return process;
}
