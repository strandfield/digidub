#include "mediaobject.h"

#include "blackdetectthread.h"
#include "frameextractionthread.h"
#include "scdetthread.h"
#include "silencedetectthread.h"

#include "cache.h"
#include "exerun.h"

#include <QFileInfo>
#include <QTimer>
#include <QUuid>

class FFprobeOutputExtractor
{
private:
  QString m_output;

public:
  explicit FFprobeOutputExtractor(const QString& output)
      : m_output(output)
  {}

  QString tryExtract(const QString& key) const
  {
    const QString prefix = key + "=";
    int i = m_output.indexOf(prefix);
    if (i == -1)
    {
      return QString();
    }

    i += prefix.length();
    const int j = m_output.indexOf('\n', i);
    if (j == -1)
    {
      return QString();
    }

    const int len = j - i;
    return m_output.mid(i, len).simplified();
  }

  QString extract(const QString& key) const
  {
    QString val = tryExtract(key);

    if (val.isNull())
    {
      throw std::runtime_error("no such value: " + key.toStdString());
    }

    return val;
  }
};

MediaObject::MediaObject(const QString& filePath, QObject* parent)
    : QObject(parent)
    , m_filePath(filePath)
{
  QString output;
  auto args = QStringList() << "-v"
                            << "0"
                            << "-select_streams"
                            << "v:0"
                            << "-count_packets"
                            << "-show_entries"
                            << "stream=r_frame_rate,nb_read_packets"
                            << "-show_entries"
                            << "format_tags"
                            << "-show_entries"
                            << "format=duration" << filePath;
  ffprobe(args, &output);

  FFprobeOutputExtractor extractor{output};
  m_duration = extractor.extract("duration").toDouble();
  m_readPackets = extractor.extract("nb_read_packets").toInt();

  {
    QStringList parts = extractor.extract("r_frame_rate").split('/');
    if (parts.size() != 2)
    {
      throw std::runtime_error("bad r_frame_rate value");
    }

    m_frameRate.first = parts.at(0).toInt();
    m_frameRate.second = parts.at(1).toInt();
  }

  m_title = extractor.tryExtract("TAG:title");
}

MediaObject::~MediaObject()
{
  if (audioInfo())
  {
    qDebug() << "removing" << audioInfo()->filePath;
    QFile::remove(audioInfo()->filePath);
  }
}

const QString& MediaObject::filePath() const
{
  return m_filePath;
}

QString MediaObject::fileName() const
{
  return QFileInfo(filePath()).fileName();
}

TimeSegment MediaObject::convertFrameRangeToTimeSegment(int firstFrameIdx, int lastFrameIdx) const
{
  Q_ASSERT(framesInfo());
  const std::vector<VideoFrameInfo>& fs = framesInfo()->frames;

  Q_ASSERT(firstFrameIdx < lastFrameIdx);
  Q_ASSERT(firstFrameIdx >= 0 && lastFrameIdx < fs.size());

  const auto& first_frame = fs.at(firstFrameIdx);
  const auto& last_frame = fs.at(lastFrameIdx);
  const int64_t start = convertPtsToPosition(first_frame.pts);
  const int64_t end = convertPtsToPosition(last_frame.pts + 1);
  return TimeSegment::between(start, end);
}

void MediaObject::extractFrames()
{
  if (framesInfo() || frameExtractionThread())
  {
    qDebug() << "bad call";
    return;
  }

  m_frameExtractionThread = std::make_unique<FrameExtractionThread>(*this);
  connect(m_frameExtractionThread.get(),
          &QThread::finished,
          this,
          &MediaObject::onFrameExtractionFinished);
  m_frameExtractionThread->start();
}

void MediaObject::onFrameExtractionFinished()
{
  m_frames = std::make_unique<FramesInfo>();
  m_frames->frames = std::move(m_frameExtractionThread->frames());
  QTimer::singleShot(10, [this]() { m_frameExtractionThread.reset(); });

  Q_EMIT framesAvailable();
}

SilenceInfo* MediaObject::silenceInfo() const
{
  return m_silenceInfo.get();
}

void MediaObject::silencedetect()
{
  if (silenceInfo() || silencedetectThread())
  {
    qDebug() << "bad call";
    return;
  }

  m_silencedetectThread = std::make_unique<SilencedetectThread>(*this);
  connect(m_silencedetectThread.get(),
          &QThread::finished,
          this,
          &MediaObject::onSilencedetectFinished);
  m_silencedetectThread->start();
}

SilencedetectThread* MediaObject::silencedetectThread() const
{
  return m_silencedetectThread.get();
}

void MediaObject::onSilencedetectFinished()
{
  m_silenceInfo = std::make_unique<SilenceInfo>();
  m_silenceInfo->minimumDuration = m_silencedetectThread->duration();
  m_silenceInfo->silences = std::move(m_silencedetectThread->silences());
  QTimer::singleShot(10, [this]() { m_silencedetectThread.reset(); });
}

BlackFramesInfo* MediaObject::blackFramesInfo() const
{
  return m_blackFrames.get();
}

void MediaObject::blackdetect()
{
  if (blackFramesInfo() || blackdetectThread())
  {
    qDebug() << "bad call";
    return;
  }

  m_blackdetectThread = std::make_unique<BlackdetectThread>(*this);
  connect(m_blackdetectThread.get(), &QThread::finished, this, &MediaObject::onBlackdetectFinished);
  m_blackdetectThread->start();
}

BlackdetectThread* MediaObject::blackdetectThread() const
{
  return m_blackdetectThread.get();
}

void MediaObject::onBlackdetectFinished()
{
  m_blackFrames = std::make_unique<BlackFramesInfo>();
  m_blackFrames->minimumDuration = m_blackdetectThread->duration();
  m_blackFrames->blackframes = std::move(m_blackdetectThread->blackframes());
  QTimer::singleShot(10, [this]() { m_blackdetectThread.reset(); });
}

ScenesInfo* MediaObject::scenesInfo() const
{
  return m_scenes.get();
}

void MediaObject::scdet()
{
  if (scenesInfo() || scdetThread())
  {
    qDebug() << "bad call";
    return;
  }

  m_scdetThread = std::make_unique<ScdetThread>(*this);
  connect(m_scdetThread.get(), &QThread::finished, this, &MediaObject::onScdetFinished);
  m_scdetThread->start();
}

ScdetThread* MediaObject::scdetThread() const
{
  return m_scdetThread.get();
}

void MediaObject::onScdetFinished()
{
  m_scenes = std::make_unique<ScenesInfo>();
  m_scenes->scenechanges = std::move(m_scdetThread->scenechanges());
  QTimer::singleShot(10, [this]() { m_scdetThread.reset(); });
}

AudioWaveformInfo* MediaObject::audioInfo() const
{
  return m_audioInfo.get();
}

void MediaObject::onAudioExtractionFinished()
{
  if (auto* process = qobject_cast<QProcess*>(sender()))
  {
    const QString filepath = process->arguments().back();
    AudioWaveformInfo info;
    info.filePath = filepath;
    m_audioInfo = std::make_unique<AudioWaveformInfo>(info);
    Q_EMIT audioAvailable();
    process->deleteLater();
  }
}

void MediaObject::extractAudioInfo()
{
  const auto basename = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);

  const QString filepath = GetCacheDir() + "/" + basename + ".wav";

  QStringList args;
  args << "-y"
       << "-i" << filePath();
  args << "-map_metadata"
       << "-1";
  args << "-map"
       << "0:1";
  args << "-ac"
       << "1";
  args << filepath;

  QProcess* process = ::run("ffmpeg", args);
  connect(process, &QProcess::finished, this, &MediaObject::onAudioExtractionFinished);
}
