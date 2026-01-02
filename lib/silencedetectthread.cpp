
#include "silencedetectthread.h"

#include "cache.h"
#include "exerun.h"
#include "mediaobject.h"
#include "vfparser.h"

#include <QFileInfo>

bool read_silencedetect_from_disk(std::vector<TimeSegment>& silences,
                                  double duration,
                                  const QString& cacheFilePath)
{
  QFile file{cacheFilePath};
  if (!file.open(QIODevice::ReadOnly))
  {
    qDebug() << "could not open " << cacheFilePath;
    return false;
  }

  QTextStream stream{&file};

  // check header
  {
    VideoFilters filters = vfparse(stream.readLine());
    if (filters.filters.empty() || filters.filters.front().name != "silencedetect")
    {
      return false;
    }

    const VideoFilter& f = filters.filters.front();
    const double d = f.args["d"].toDouble();
    if (!qFuzzyCompare(d, duration))
    {
      return false;
    }
  }

  while (!stream.atEnd())
  {
    QString line = stream.readLine();
    QStringList parts = line.split(',');
    if (parts.size() != 2)
    {
      break;
    }

    const double start = parts.front().toDouble();
    const double end = parts.back().toDouble();
    silences.push_back(TimeSegment::between(start * 1000, end * 1000));
  }

  return true;
}

void save_silencedetect_to_disk(const std::vector<TimeSegment>& silences,
                                double duration,
                                const QString& cacheFilePath)
{
  QFile file{cacheFilePath};
  if (!file.open(QIODevice::ReadWrite | QIODevice::Truncate))
  {
    qDebug() << "could not write " << cacheFilePath;
    return;
  }

  file.write(QString("silencedetect=n=-35dB:d=%1").arg(QString::number(duration)).toLatin1());
  file.write("\n");

  for (const TimeSegment& w : silences)
  {
    file.write(
        QString("%1,%2")
            .arg(QString::number(w.start() / double(1000)), QString::number(w.end() / double(1000)))
            .toUtf8());
    file.write("\n");
  }
}

SilencedetectThread::SilencedetectThread(const MediaObject& media)
    : m_filePath(media.filePath())
    , m_fileName(media.fileName())
    , m_nbFrames(media.numberOfPackets())
{
  CreateCacheDir();
}

SilencedetectThread::~SilencedetectThread() {}

double SilencedetectThread::duration() const
{
  return 0.4;
}

std::vector<TimeSegment>& SilencedetectThread::silences()
{
  assert(isFinished());
  return m_silences;
}

void SilencedetectThread::run()
{
  constexpr const char* duration_threshold = "0.4";

  m_silences.clear();

  const QString cache_filepath = GetCacheDir() + "/" + m_fileName + "."
                                 + QString::number(m_nbFrames) + ".silencedetect";

  if (QFileInfo::exists(cache_filepath))
  {
    if (read_silencedetect_from_disk(m_silences, duration(), cache_filepath))
    {
      return;
    }
    else
    {
      QFile::remove(cache_filepath);
    }
  }

  QString output;
  QStringList args;
  args << "-nostats"
       << "-hide_banner";
  args << "-i" << m_filePath;
  args << "-map"
       << "0:1";
  args << "-af" << QString("silencedetect=n=-35dB:d=%1").arg(duration_threshold);
  args << "-f"
       << "null"
       << "-";
  ffmpeg(args, &output);
  //qDebug().noquote() << output;

  qDebug() << "detecting silences...";

  QStringList lines = output.split('\n');
  lines.removeIf([](const QString& str) { return !str.contains("silencedetect"); });
  Q_ASSERT(lines.size() % 2 == 0);

  for (int i(0); i < lines.size(); i += 2)
  {
    QString line = lines.at(i);
    int index = line.indexOf("silence_start:");
    Q_ASSERT(index != -1);
    QString text = line.mid(index + 14).simplified();
    const double start = text.toDouble();

    line = lines.at(i + 1);
    index = line.indexOf("silence_duration:");
    Q_ASSERT(index != -1);
    text = line.mid(index + 17).simplified();
    const double duration = text.toDouble();

    auto w = TimeSegment(start * 1000, Duration(duration * 1000));

    // qDebug() << "found silencefrom " << formatSeconds(w.start()) << " to " << formatSeconds(w.end())
    //          << " (" << w.duration() << "s)";

    m_silences.push_back(w);
  }

  save_silencedetect_to_disk(m_silences, duration(), cache_filepath);
}
