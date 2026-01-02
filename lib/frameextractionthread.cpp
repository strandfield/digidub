
#include "frameextractionthread.h"

#include "cache.h"
#include "mediaobject.h"
#include "phash.h"

#include <QCoreApplication>

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

void read_frames_from_disk(std::vector<VideoFrameInfo>& frames, const QString& filePath)
{
  QFile file{filePath};
  if (!file.open(QIODevice::ReadOnly))
  {
    qDebug() << "could not open " << filePath;
  }

  size_t n = 0;
  QDataStream stream{&file};
  stream >> n;
  for (size_t i(0); i < n; ++i)
  {
    VideoFrameInfo f;
    stream >> f.pts;
    stream >> f.phash;
    frames.push_back(f);
  }

  if (!file.atEnd())
  {
    qDebug() << "warning: not at end of file " << filePath;
  }
}

void save_frames_to_disk(const std::vector<VideoFrameInfo>& frames, const QString& filePath)
{
  QFile file{filePath};
  if (!file.open(QIODevice::ReadWrite | QIODevice::Truncate))
  {
    qDebug() << "could not write " << filePath;
  }

  QDataStream stream{&file};
  stream << size_t(frames.size());
  for (const VideoFrameInfo& e : frames)
  {
    stream << e.pts << e.phash;
  }
}

void collect_frames(std::vector<VideoFrameInfo>& frames, const QDir& dir)
{
  QStringList names = dir.entryList(QStringList() << "*.png");
  //qDebug() << names.size();

  PerceptualHash hash;

  for (const QString& name : names)
  {
    const QString path = dir.filePath(name);
    VideoFrameInfo frame;
    frame.phash = hash.hash(path);
    frame.pts = QFileInfo(path).baseName().toInt();
    frames.push_back(frame);
    QFile::remove(path);
  }
}

FrameExtractionThread::FrameExtractionThread(const MediaObject& media)
    : m_filePath(media.filePath())
    , m_nbFrames(media.numberOfPackets())
{
  CreateCacheDir();
}

FrameExtractionThread::~FrameExtractionThread() {}

std::vector<VideoFrameInfo>& FrameExtractionThread::frames()
{
  assert(isFinished());
  return m_frames;
}

void FrameExtractionThread::run()
{
  const QString search_filepath = GetCacheDir() + "/" + QFileInfo(m_filePath).fileName() + "."
                                  + QString::number(m_nbFrames);

  if (QFileInfo::exists(search_filepath))
  {
    read_frames_from_disk(m_frames, search_filepath);
    return;
  }

  QTemporaryDir temp_dir;

  if (!temp_dir.isValid())
  {
    qDebug() << "invalid temp dir";
    return;
  }

  // qDebug() << m_tempDir->path();

  QProcess ffmpeg;
  ffmpeg.setProgram("ffmpeg");

  auto args = QStringList();

  args << "-i" << m_filePath;

  args << "-vsync"
       << "0"
       << "-vf"
       << "format=gray,scale=32:32"
       << "-copyts"
       << "-f"
       << "image2"
       << "-frame_pts"
       << "true" << QString("%1/%d.png").arg(temp_dir.path());

  ffmpeg.setArguments(args);
  qDebug() << args.join(" ");

  ffmpeg.start();
  ffmpeg.waitForStarted();

  m_frames.reserve(m_nbFrames);

  for (;;)
  {
    msleep(100);

    QCoreApplication::processEvents();

    collect_frames(m_frames, QDir(temp_dir.path()));

    const float progress = m_frames.size() / float(m_nbFrames);
    emit progressChanged(progress);

    if (ffmpeg.state() == QProcess::NotRunning)
    {
      break;
    }
  }

  std::sort(m_frames.begin(), m_frames.end(), [](const VideoFrameInfo& a, const VideoFrameInfo& b) {
    return a.pts < b.pts;
  });

  save_frames_to_disk(m_frames, search_filepath);
}
