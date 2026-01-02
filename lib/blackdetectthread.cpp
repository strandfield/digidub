
#include "blackdetectthread.h"

#include "cache.h"
#include "exerun.h"
#include "mediaobject.h"
#include "vfparser.h"

#include <QFile>

// TODO: very similar to read_silencedetect_from_disk(). maybe try merging the two?
bool read_blackdetect_from_disk(std::vector<TimeSegment>& blackframes,
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
    if (filters.filters.empty() || filters.filters.front().name != "blackdetect")
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
    blackframes.push_back(TimeSegment::between(start * 1000, end * 1000));
  }

  return true;
}

// TODO: very similar to save_silencedetect_to_disk(). maybe try merging the two?
void save_blackdetect_to_disk(const std::vector<TimeSegment>& silences,
                              double duration,
                              const QString& cacheFilePath)
{
  QFile file{cacheFilePath};
  if (!file.open(QIODevice::ReadWrite | QIODevice::Truncate))
  {
    qDebug() << "could not write " << cacheFilePath;
    return;
  }

  file.write(QString("blackdetect=d=%1:pix_th=0.05").arg(QString::number(duration)).toLatin1());
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

BlackdetectThread::BlackdetectThread(const MediaObject& media)
    : m_filePath(media.filePath())
    , m_fileName(media.fileName())
    , m_nbFrames(media.numberOfPackets())
{
  CreateCacheDir();
}

BlackdetectThread::~BlackdetectThread() {}

double BlackdetectThread::duration() const
{
  return 0.4;
}

std::vector<TimeSegment>& BlackdetectThread::blackframes()
{
  assert(isFinished());
  return m_blackframes;
}

void BlackdetectThread::run()
{
  constexpr const char* duration_threshold = "0.4";

  m_blackframes.clear();

  const QString cache_filepath = GetCacheDir() + "/" + m_fileName + "."
                                 + QString::number(m_nbFrames) + ".blackdetect";

  if (QFile::exists(cache_filepath))
  {
    if (read_blackdetect_from_disk(m_blackframes, duration(), cache_filepath))
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
       << "0:0";
  args << "-vf" << QString("blackdetect=d=%1:pix_th=0.05").arg(duration_threshold);
  args << "-f"
       << "null"
       << "-";
  ffmpeg(args, &output);

  qDebug() << "detecting back frames...";

  QStringList lines = output.split('\n');
  lines.removeIf([](const QString& str) { return !str.contains("[blackdetect @"); });

  for (QString line : lines)
  {
    const int black_start_index = line.indexOf("black_start:");

    Q_ASSERT(black_start_index != -1);
    if (black_start_index == -1)
    {
      continue;
    }

    const int black_end_index = line.indexOf("black_end:", black_start_index);

    Q_ASSERT(black_end_index != -1);
    if (black_end_index == -1)
    {
      continue;
    }

    const int black_duration_index = line.indexOf("black_duration:", black_end_index);

    Q_ASSERT(black_duration_index != -1);
    if (black_duration_index == -1)
    {
      continue;
    }

    QString text = line.mid(black_start_index + 12,
                            (black_end_index - 1) - (black_start_index + 12));
    const double start = text.toDouble();

    text = line.mid(black_end_index + 10, (black_duration_index - 1) - (black_end_index + 10));
    const double end = text.toDouble();

    auto w = TimeSegment::between(start * 1000, end * 1000);

    // qDebug() << "found black frames from " << formatSeconds(w.start()) << " to "
    //          << formatSeconds(w.end()) << " (" << w.duration() << "s)";

    m_blackframes.push_back(w);
  }

  save_blackdetect_to_disk(m_blackframes, duration(), cache_filepath);
}
