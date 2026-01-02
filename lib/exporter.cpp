#include "exporter.h"

#include "exerun.h"
#include "mediaobject.h"
#include "project.h"

#include <QEventLoop>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

std::vector<OutputSegment> dubCompute(const std::vector<VideoMatch>& matches, Duration mediaDuration)
{
  std::vector<OutputSegment> result;
  result.reserve(2 * matches.size());

  int64_t curtime = 0;

  for (const VideoMatch& m : matches)
  {
    if (m.a.start() > curtime)
    {
      OutputSegment seg;
      seg.output_segment = TimeSegment(curtime, m.a.start());
      seg.source_id = 0;
      seg.source_segment = seg.output_segment;

      result.push_back(seg);

      curtime = m.a.start();
    }

    if (m.a.duration() == 0)
    {
      continue;
    }

    OutputSegment seg;
    seg.output_segment = m.a;
    seg.source_id = 1;
    seg.source_segment = m.b;

    result.push_back(seg);

    curtime = m.a.end();
  }

  if (curtime < mediaDuration.toMSecs())
  {
    OutputSegment seg;
    seg.output_segment = TimeSegment(curtime, mediaDuration.toMSecs());
    seg.source_id = 0;
    seg.source_segment = seg.output_segment;

    result.push_back(seg);

    curtime = mediaDuration.toMSecs();
  }

  return result;
}

std::vector<OutputSegment> dubCompute(const DubbingProject& project, Duration mediaDuration)
{
  return dubCompute(convert2vm(project.matches()), mediaDuration);
}

std::vector<OutputSegment> dubCompute(const DubbingProject& project, const MediaObject& video)
{
  return dubCompute(project, Duration(video.duration() * 1000));
}

std::optional<double> extractPeakLevel(const QString& output)
{
  const QString searchstring = "Peak level dB: ";
  int offset = output.indexOf(searchstring);
  if (offset == -1)
  {
    return std::nullopt;
  }

  offset += searchstring.length();
  int eol = output.indexOf('\n', offset);
  bool ok;
  double value = output.mid(offset, eol - 1).simplified().toDouble(&ok);

  if (ok)
  {
    return value;
  }

  return std::nullopt;
}

void exportProject(const DubbingProject& project,
                   const MediaObject& video,
                   const QString& outputFilePath)
{
  qDebug() << "Exporting to" << outputFilePath;

  DubExporter exporter{project, video};
  exporter.setOutputFilePath(outputFilePath);
  exporter.run();
  exporter.waitForFinished();
}

void exportProject(const DubbingProject& project, const MediaObject& video)
{
  exportProject(project, video, project.resolvePath(project.outputFilePath()));
}

enum class ExportStep {
  ExtractAudioTracks = 1,
  MeasureGain = 2,
  ExtractAudioSegments = 3,
  PostProcessAudioSegments = 4,
  ConcatenateAudioSegments = 5,
  MergeFiles = 6,
  Done = 7,
};

struct DubExporter::Data
{
  QTemporaryDir tempDir;
  ExportStep currentStep = ExportStep::ExtractAudioTracks;
  bool waiting = false;

  int numberOfAudioTracksExtracted = 0;

  double audiogain = 0;

  std::vector<OutputSegment> osegs;

  std::vector<QProcess*> audioTrackProcs;
  int numberOfAudioSegmentsExtracted = 0;
  int numberOfAudioSegmentsPostProcessed = 0;

  QFile listtxt;
  QProcess* mkvmerge = nullptr;
};

DubExporter::DubExporter(const DubbingProject& project, const MediaObject& video, QObject* parent)
    : QObject(parent)
    , m_project(project)
    , m_video(video)
    , m_outputFilePath(project.resolvePath(project.outputFilePath()))
{}

DubExporter::~DubExporter() {}

const DubbingProject& DubExporter::project() const
{
  return m_project;
}

const QString& DubExporter::outputFilePath() const
{
  return m_outputFilePath;
}

void DubExporter::setOutputFilePath(const QString& path)
{
  m_outputFilePath = path;
}

void DubExporter::run()
{
  if (isRunning())
  {
    Q_ASSERT(false);
    return;
  }

  d = std::make_unique<Data>();
  if (!d->tempDir.isValid())
  {
    Q_ASSERT(false);
    d.reset();
    return;
  }

  d->osegs = dubCompute(project(), m_video);

  Q_EMIT statusChanged();

  step();
}

bool DubExporter::isRunning() const
{
  return d != nullptr;
}

QString DubExporter::status() const
{
  if (!d)
  {
    return QString();
  }

  switch (d->currentStep)
  {
  case ExportStep::ExtractAudioTracks:
    return "Extracting audio tracks";
  case ExportStep::MeasureGain:
    return "Measuring audio gain";
  case ExportStep::ExtractAudioSegments:
    return "Extracting audio segments";
  case ExportStep::PostProcessAudioSegments:
    return "Post-processing audio segments";
  case ExportStep::ConcatenateAudioSegments:
    return "Concatenating audio segments";
  case ExportStep::MergeFiles:
    return "Merging files";
  case ExportStep::Done:
    return "Done";
  default:
    break;
  }

  return QString();
}

float DubExporter::progress() const
{
  if (!d)
  {
    return 0;
  }

  if (d->currentStep == ExportStep::Done)
  {
    return 1;
  }

  const float stepvalue = 1 / float(static_cast<int>(ExportStep::Done));

  float p = stepvalue * static_cast<int>(d->currentStep);

  switch (d->currentStep)
  {
  case ExportStep::ExtractAudioTracks:
    p += 0.5 * stepvalue * d->numberOfAudioTracksExtracted;
    break;
  case ExportStep::ExtractAudioSegments:
    p += d->numberOfAudioSegmentsExtracted * (stepvalue / d->osegs.size());
    break;
  case ExportStep::PostProcessAudioSegments:
    p += d->numberOfAudioSegmentsPostProcessed * (stepvalue / d->osegs.size());
    break;
  default:
    break;
  }

  return p;
}

void DubExporter::waitForFinished()
{
  if (!isRunning())
  {
    return;
  }

  QEventLoop loop;
  connect(this, &DubExporter::finished, &loop, &QEventLoop::quit, Qt::QueuedConnection);
  loop.exec();
}

void DubExporter::step()
{
  Q_ASSERT(d);

  if (d->waiting)
  {
    return;
  }

  QTemporaryDir& tempDir = d->tempDir;

  const QString source_audio1_path = tempDir.filePath("src1.wav");
  const QString source_audio2_path = tempDir.filePath("src2.wav");
  const QString output_audio_path = tempDir.filePath("concat.mka");

  switch (d->currentStep)
  {
  case ExportStep::ExtractAudioTracks: {
    auto check_tracks_extracted = [this]() {
      d->numberOfAudioTracksExtracted += 1;
      if (d->numberOfAudioTracksExtracted == 2)
      {
        advanceToNextStep();
      }
      else
      {
        Q_EMIT progressChanged();
      }
    };

    // extract audio 1
    {
      QStringList args;
      args << "-y"
           << "-i" << project().resolvePath(project().videoFilePath());
      args << "-map_metadata"
           << "-1";
      args << "-map"
           << "0:1";
      args << "-ac"
           << "1";
      args << source_audio1_path;
      run("ffmpeg", args, check_tracks_extracted);
    }

    // extract audio 2
    {
      QStringList args;
      args << "-y"
           << "-hide_banner"
           << "-nostats"
           << "-i" << project().resolvePath(project().audioSourceFilePath());
      args << "-map_metadata"
           << "-1";
      args << "-map"
           << "0:1";
      args << "-ac"
           << "1";
      args << source_audio2_path;
      run("ffmpeg", args, check_tracks_extracted);
    }

    return;
  }

  case ExportStep::MeasureGain: {
    //ffmpeg -hide_banner -nostats -i Digi2x01.mkv -filter:a astats=measure_overall=Peak_level:measure_perchannel=0 -f null -

    auto parse_audio_gain = [this]() {
      auto* process = qobject_cast<QProcess*>(sender());
      std::optional<double> dblevel = extractPeakLevel(process->readAllStandardError());
      if (dblevel.has_value())
      {
        d->audiogain = std::abs(*dblevel) - 0.1;
        qDebug() << "found gain: " << d->audiogain;
      }

      advanceToNextStep();
    };

    QStringList args;
    args << "-hide_banner"
         << "-nostats";
    args << "-i" << source_audio2_path;
    args << "-filter:a"
         << "astats=measure_overall=Peak_level:measure_perchannel=0";
    args << "-f"
         << "null"
         << "-";

    run("ffmpeg", args, parse_audio_gain);

    return;
  }

  case ExportStep::ExtractAudioSegments: {
    auto check_segments_extracted = [this]() {
      d->numberOfAudioSegmentsExtracted += 1;

      if (d->numberOfAudioSegmentsExtracted == d->osegs.size())
      {
        for (QProcess* p : d->audioTrackProcs)
        {
          p->deleteLater();
        }

        d->audioTrackProcs.clear();
        advanceToNextStep();
      }
      else
      {
        Q_EMIT progressChanged();
      }
    };

    for (size_t i(0); i < d->osegs.size(); ++i)
    {
      const OutputSegment& seg = d->osegs[i];

      if (seg.source_id == 0)
      {
        QStringList args;
        args << "-y";
        args << "-i" << source_audio1_path;
        args << "-ss" << Duration(seg.source_segment.start()).toString(Duration::HHMMSSzzz);
        args << "-to" << Duration(seg.source_segment.end()).toString(Duration::HHMMSSzzz);
        args << tempDir.filePath(QString::number(i) + ".wav");
        d->audioTrackProcs.push_back(run("ffmpeg", args, check_segments_extracted));
      }
      else
      {
        QStringList args;
        args << "-y";
        args << "-i" << source_audio2_path;
        args << "-ss" << Duration(seg.source_segment.start()).toString(Duration::HHMMSSzzz);
        args << "-to" << Duration(seg.source_segment.end()).toString(Duration::HHMMSSzzz);
        args << tempDir.filePath(QString::number(i) + "-orig.wav");
        d->audioTrackProcs.push_back(run("ffmpeg", args, check_segments_extracted));
      }
    }

    return;
  }

  case ExportStep::PostProcessAudioSegments: {
    auto check_audio_postprocessing = [this]() {
      d->numberOfAudioSegmentsPostProcessed++;

      if (d->numberOfAudioSegmentsPostProcessed == d->osegs.size())
      {
        for (QProcess* p : d->audioTrackProcs)
        {
          p->deleteLater();
        }

        d->audioTrackProcs.clear();
        advanceToNextStep();
      }
      else
      {
        Q_EMIT progressChanged();
      }
    };

    for (size_t i(0); i < d->osegs.size(); ++i)
    {
      const OutputSegment& seg = d->osegs[i];

      if (seg.source_id == 0)
      {
        ++d->numberOfAudioSegmentsPostProcessed;
      }
      else if (seg.source_id == 1)
      {
        const QString inputpath = tempDir.filePath(QString::number(i) + "-orig.wav");

        QStringList args;
        args << "-y";
        args << "-i" << inputpath;

        const QString outputname = QString::number(i) + ".wav";
        const QString outputpath = tempDir.filePath(outputname);
        // apply audio filters
        {
          QStringList audiofilters;

          if (d->audiogain)
          {
            audiofilters << QString("volume=%1dB").arg(QString::number(d->audiogain));
          }

          const double ratio = seg.source_segment.duration()
                               / double(seg.output_segment.duration());
          audiofilters << QString("atempo=%1").arg(QString::number(ratio));

          args << "-filter:a";
          args << audiofilters.join(',');
        }
        args << outputpath;

        QProcess* proc = run("ffmpeg", args, check_audio_postprocessing);
        d->audioTrackProcs.push_back(proc);
      }
    }

    return;
  }

  case ExportStep::ConcatenateAudioSegments: {
    QFile& listtxt = d->listtxt;
    listtxt.setFileName(tempDir.filePath("list.txt"));
    listtxt.open(QIODevice::ReadWrite | QIODevice::Truncate);
    for (size_t i(0); i < d->osegs.size(); ++i)
    {
      const QString outputname = QString::number(i) + ".wav";
      listtxt.write(QString("file '%1'\n").arg(outputname).toUtf8());
    }
    listtxt.close();

    // concat
    {
      QStringList args;
      args << "-y"
           << "-f"
           << "concat"
           << "-safe"
           << "0";
      args << "-i" << listtxt.fileName();
      args << "-c:a"
           << "libopus";
      args << output_audio_path;
      run("ffmpeg", args, &DubExporter::advanceToNextStep);
    }

    return;
  }

  case ExportStep::MergeFiles: {
    constexpr bool merge_with_ffmpeg = false;
    if (merge_with_ffmpeg)
    {
      QStringList args;
      args << "-y";

      args << "-i" << m_video.filePath();

      args << "-i" << output_audio_path;

      args << "-map"
           << "0:0";
      args << "-map"
           << "0:1";
      args << "-map"
           << "1";

      args << "-c:v"
           << "copy";
      args << "-c:a"
           << "copy";

      args << outputFilePath();
      d->mkvmerge = run("mkvmerge", args, &DubExporter::advanceToNextStep);
    }
    else
    {
      //mkvmerge -o output/Digimon.S1.E01.mkv --default-track-flag 1:0 Digimon.S1.E01.mkv --track-name "0:Mono - FR (Mixed)" --language 0:fre --default-track-flag 0:1 --original-flag 0 -a 0 output/concat.mka
      QStringList args;
      args << "-o" << outputFilePath();

      if (!project().projectTitle().isEmpty())
      {
        args << "--title" << project().projectTitle();
      }

      args << "--no-subtitles";
      args << "--video-tracks"
           << "0";
      args << "--audio-tracks"
           << "1";
      args << "--default-track-flag"
           << "1:0"; // remove default-track flag from EN audio
      args << m_video.filePath();

      args << "--track-name"
           << "0:Mono - FR (Mixed)";
      args << "--language"
           << "0:fre";
      args << "--default-track-flag"
           << "0:1"; // set FR audio track as default
      args << "-a"
           << "0";
      args << output_audio_path;

      // subtitles
      if (!project().subtitlesFilePath().isEmpty())
      {
        args << "--track-name"
             << "0:Subtitles - EN";
        args << "--language"
             << "0:eng";
        args << "--default-track-flag"
             << "0:0"; // unset default-track flag
        args << "-s"
             << "0";
        args << project().resolvePath(project().subtitlesFilePath());
      }

      d->mkvmerge = run("mkvmerge", args, &DubExporter::advanceToNextStep);
    }

    return;
  }

  case ExportStep::Done: {
    const QString std_out = d->mkvmerge->readAllStandardOutput();
    if (!std_out.isEmpty())
    {
      qDebug() << std_out;
    }

    d->mkvmerge->deleteLater();
    d->mkvmerge = nullptr;
    d.reset();
    Q_EMIT finished();
    return;
  }

  } // end switch(d->currentStep)
}

void DubExporter::advanceToNextStep()
{
  d->currentStep = static_cast<ExportStep>(static_cast<int>(d->currentStep) + 1);
  d->waiting = false;

  Q_EMIT progressChanged();
  Q_EMIT statusChanged();

  step();
}

template<typename Callback>
QProcess* DubExporter::run(const QString& program, const QStringList& args, Callback&& onFinished)
{
  QProcess* process = ::run(program, args);
  process->setParent(this);
  connect(process, &QProcess::finished, this, std::forward<Callback>(onFinished));
  d->waiting = true;
  return process;
}
