
#include "scdetthread.h"

#include "cache.h"
#include "exerun.h"
#include "mediaobject.h"
#include "vfparser.h"

#include <QFile>

bool read_scdet_results_from_disk(std::vector<SceneChange>& scenechanges,
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
    if (filters.filters.empty() || filters.filters.front().name != "scdet")
    {
      return false;
    }

    const VideoFilter& f = filters.filters.front();
    Q_UNUSED(f);
  }

  while (!stream.atEnd())
  {
    QString line = stream.readLine();
    QStringList parts = line.split(',');
    if (parts.size() != 2)
    {
      break;
    }

    const double score = parts.front().toDouble();
    const double time = parts.back().toDouble();
    scenechanges.push_back(SceneChange{.score = score, .time = time});
  }

  return true;
}

void save_scdet_results_to_disk(const std::vector<SceneChange>& scenechanges,
                                const QString& cacheFilePath)
{
  QFile file{cacheFilePath};
  if (!file.open(QIODevice::ReadWrite | QIODevice::Truncate))
  {
    qDebug() << "could not write " << cacheFilePath;
    return;
  }

  file.write("scdet");
  file.write("\n");

  for (const SceneChange& e : scenechanges)
  {
    file.write(QString("%1,%2").arg(QString::number(e.score), QString::number(e.time)).toUtf8());
    file.write("\n");
  }
}

ScdetThread::ScdetThread(const MediaObject& media)
    : m_filePath(media.filePath())
    , m_fileName(media.fileName())
    , m_nbFrames(media.numberOfPackets())
{
}

ScdetThread::~ScdetThread() {}

std::vector<SceneChange>& ScdetThread::scenechanges()
{
  assert(isFinished());
  return m_scenechanges;
}

void ScdetThread::run()
{
  m_scenechanges.clear();

  const QString cache_filepath = GetCacheDir() + "/" + m_fileName + "."
                                 + QString::number(m_nbFrames) + ".scdet";

  if (QFile::exists(cache_filepath))
  {
    if (read_scdet_results_from_disk(m_scenechanges, cache_filepath))
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
  args << "-vf" << QString("scdet");
  args << "-f"
       << "null"
       << "-";
  ffmpeg(args, &output);

  qDebug() << "detecting scene changes...";

  // example line:
  // [scdet @ 000001a1ba65ef00] lavfi.scd.score: 10.525, lavfi.scd.time: 45.167

  QStringList lines = output.split('\n');
  lines.removeIf([](const QString& str) { return !str.contains("[scdet @"); });

  for (QString line : lines)
  {
    const int score_index = line.indexOf("lavfi.scd.score:");

    Q_ASSERT(score_index != -1);
    if (score_index == -1)
    {
      continue;
    }

    const int time_index = line.indexOf(", lavfi.scd.time:", score_index);

    Q_ASSERT(time_index != -1);
    if (time_index == -1)
    {
      continue;
    }

    QString text = line.mid(score_index + 17, time_index - (score_index + 17));
    const double score = text.toDouble();

    text = line.mid(time_index + 17).simplified();
    const double time = text.toDouble();

    // qDebug() << "found scene change at " << formatSeconds(time) << " (score = " << score << ")";

    SceneChange sc;
    sc.score = score;
    sc.time = time;
    m_scenechanges.push_back(sc);
  }

  save_scdet_results_to_disk(m_scenechanges, cache_filepath);
}
