// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#include "exporter.h"
#include "matchalgo.h"
#include "mediaobject.h"
#include "project.h"

#include "blackdetectthread.h"
#include "frameextractionthread.h"
#include "scdetthread.h"
#include "silencedetectthread.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <QVersionNumber>

#include <iostream>

static bool helpRequested(const QStringList& args)
{
  return args.contains("-h") || args.contains("--help") || args.contains("-?");
}

namespace CreateCommand {

void loadAllData(QTextStream& cerr, MediaObject& primaryMedia, MediaObject& secondaryMedia)
{
  QEventLoop loop;

  for (MediaObject* media : {&primaryMedia, &secondaryMedia})
  {
    if (!media->framesInfo())
    {
      media->extractFrames();
      if (media->frameExtractionThread())
      {
        cerr << "Extracting frames for " << media->fileName() << "..." << Qt::endl;

        // TODO: display progress

        QObject::connect(media->frameExtractionThread(),
                         &QThread::finished,
                         &loop,
                         &QEventLoop::quit);

        loop.exec();
      }
    }
  }

  if (!primaryMedia.silenceInfo())
  {
    primaryMedia.silencedetect();

    if (primaryMedia.silencedetectThread())
    {
      cerr << "Detecting silences on  " << primaryMedia.fileName() << "..." << Qt::endl;

      QObject::connect(primaryMedia.silencedetectThread(),
                       &QThread::finished,
                       &loop,
                       &QEventLoop::quit);
      loop.exec();
    }
  }

  if (!primaryMedia.blackFramesInfo())
  {
    primaryMedia.blackdetect();

    if (primaryMedia.blackdetectThread())
    {
      cerr << "Detecting black frames on  " << primaryMedia.fileName() << "..." << Qt::endl;

      QObject::connect(primaryMedia.blackdetectThread(),
                       &QThread::finished,
                       &loop,
                       &QEventLoop::quit);
      loop.exec();
    }
  }

  if (!primaryMedia.scenesInfo())
  {
    primaryMedia.scdet();

    if (primaryMedia.scdetThread())
    {
      cerr << "Detecting scene changes on  " << primaryMedia.fileName() << "..." << Qt::endl;

      QObject::connect(primaryMedia.scdetThread(), &QThread::finished, &loop, &QEventLoop::quit);
      loop.exec();
    }
  }
}

} // namespace CreateCommand

static bool likelyVideo(const QFileInfo& info)
{
  const QString suffix = info.suffix().toLower();
  return suffix == "mkv" || suffix == "mp4";
}

static bool likelySubtitle(const QFileInfo& info)
{
  const QString suffix = info.suffix().toLower();
  return suffix == "srt" || suffix == "vtt";
}

int cmd_create(QStringList args)
{
  QTextStream cout{stdout};
  QTextStream cerr{stderr};

  if (helpRequested(args) || args.isEmpty())
  {
    cout << "digidub create [--detect-matches] [--title MyTitle] --output out.mkv -i video1.mkv -i "
            "video2.mkv"
         << Qt::endl;
    return 0;
  }

  DubbingProject project;

  QStringList inputs;
  QString savepath;
  bool detect_matches = false;

  for (int i(0); i < args.size();)
  {
    const QString& a = args.at(i++);
    if (a.startsWith("-"))
    {
      if (a == "--title" || a == "-t")
      {
        project.setProjectTitle(args.at(i++));
      }
      else if (a == "--output" || a == "-o")
      {
        project.setOutputFilePath(args.at(i++));
      }
      else if (a == "--input" || a == "-i")
      {
        inputs.push_back(args.at(i++));
      }
      else if (a == "--detect-matches" || a == "-dm")
      {
        detect_matches = true;
      }
      else
      {
        cerr << "Unknown option: " << a << "." << Qt::endl;
        return 1;
      }
    }
    else
    {
      if (!savepath.isEmpty())
      {
        cerr << "An output filename was already provided." << Qt::endl;
        return 1;
      }

      savepath = a;
    }
  }

  if (inputs.size() < 2)
  {
    cerr << "At least two inputs files must be specified." << Qt::endl;
    return 1;
  }

  for (const QString& input : inputs)
  {
    QFileInfo info{input};
    if (!info.exists() && (!detect_matches || likelySubtitle(info)))
    {
      cerr << "Warning: input file " << input << " does not exist." << Qt::endl;
    }

    if (likelyVideo(info))
    {
      if (project.videoFilePath().isEmpty())
      {
        project.setVideoFilePath(input);
      }
      else if (project.audioSourceFilePath().isEmpty())
      {
        project.setAudioSourceFilePath(input);
      }
      else
      {
        cerr << "Error: too many video files provided." << Qt::endl;
        return 1;
      }
    }
    else if (likelySubtitle(info))
    {
      project.setSubtitlesFilePath(input);
    }
    else
    {
      cerr << "Error: unknown input type '" << input << "'." << Qt::endl;
      return 1;
    }
  }

  if (detect_matches)
  {
    if (!QFile::exists(project.videoFilePath()))
    {
      cerr << "Input file does not exist " << project.videoFilePath() << "." << Qt::endl;
      return 1;
    }

    if (!QFile::exists(project.audioSourceFilePath()))
    {
      cerr << "Input file does not exist " << project.audioSourceFilePath() << "." << Qt::endl;
      return 1;
    }

    MediaObject video1{project.videoFilePath()};

    if (project.projectTitle().isEmpty())
    {
      if (!video1.title().isEmpty())
      {
        project.setProjectTitle(video1.title());
      }

      MediaObject video2{project.audioSourceFilePath()};

      CreateCommand::loadAllData(cerr, video1, video2);

      MatchDetector detector{video1, video2};

      std::vector<VideoMatch> matches = detector.run();
      project.addMatches(matches);
    }
  }

  if (project.projectTitle().isEmpty())
  {
    project.setProjectTitle(QFileInfo(project.videoFilePath()).fileName());
  }

  if (!savepath.isEmpty())
  {
    QFile outfile{savepath};
    outfile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    QTextStream stream{&outfile};
    project.dump(stream);
  }
  else
  {
    project.dump(cout);
  }

  return 0;
}

int cmd_export(QStringList args)
{
  QTextStream cout{stdout};
  QTextStream cerr{stderr};

  if (helpRequested(args) || args.isEmpty())
  {
    cout << "digidub export project.txt" << Qt::endl;
    return 0;
  }

  QString inputpath;

  if (args.size() != 1)
  {
    cerr << "invalid number of arguments" << Qt::endl;
    return 1;
  }

  inputpath = args[0];

  DubbingProject project{inputpath};

  MediaObject video{project.videoFilePath()};

  exportProject(project, video);

  return 0;
}

int main(int argc, char* argv[])
{
  QCoreApplication::setOrganizationName("Analogman Software");
  QCoreApplication::setApplicationName("DigiDub");
  QCoreApplication::setApplicationVersion(
      QVersionNumber(DIGIDUB_VERSION_MAJOR, DIGIDUB_VERSION_MINOR).toString());

  QCoreApplication app{argc, argv};

  const QStringList args = app.arguments();

  if (args.size() > 1)
  {
    if (args.at(1) == "create")
    {
      return cmd_create(args.mid(2));
    }
    else if (args.at(1) == "export")
    {
      return cmd_export(args.mid(2));
    }
    else if (!args.at(1).startsWith("-"))
    {
      std::cerr << "Unknown command " << args.at(1).toStdString() << std::endl;
      return 1;
    }
  }

  if (helpRequested(args) || args.size() <= 1)
  {
    QTextStream cout{stdout};
    cout << "digidub <command> [arguments..]" << Qt::endl;
    cout << Qt::endl;
    cout << "Available commands:" << Qt::endl;
    cout << "  create    create a project" << Qt::endl;
    cout << "  export    export a project" << Qt::endl;
    cout << Qt::endl;
    cout << "Get more information about a command using: digidub <command> --help" << Qt::endl;
  }
  else if (args.contains("-v") || args.contains("--version"))
  {
    std::cout << app.applicationVersion().toStdString() << std::endl;
  }

  return 0;
}
