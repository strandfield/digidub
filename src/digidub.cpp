// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project
// For conditions of distribution and use, see copyright notice in LICENSE

#include "phash.h"

#include <QApplication>

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <QTimer>

#include <QDebug>

#include <iostream>

// FFMPEG helpers //

void run_executable(const QString& name, const QStringList& args, QString* stdOut = nullptr)
{
  QProcess instance;
  instance.setProgram(name);
  instance.setArguments(args);

  qDebug().noquote() << (QStringList() << name << args).join(" ");

  instance.start();

  instance.waitForFinished(20 * 60 * 1000);

  if (instance.exitCode() != 0)
  {
    qDebug().noquote() << instance.readAllStandardError();
  }

  if (stdOut)
  {
    *stdOut = QString::fromLocal8Bit(instance.readAllStandardOutput());
  }
}

void run_ffmpeg(const QStringList& args, QString* stdOut = nullptr)
{
  run_executable("ffmpeg", args, stdOut);
}

void run_ffprobe(const QStringList& args, QString* stdOut = nullptr)
{
  run_executable("ffprobe", args, stdOut);
}

//// VIDEO /////

struct VideoFrameInfo
{
  int pts;
  quint64 phash;
};

struct VideoInfo
{
  QString filePath;
  double duration;
  std::pair<int, int> exactFrameRate;
  int readPackets;
  std::vector<VideoFrameInfo> frames;

  using Frame = VideoFrameInfo;
};

double get_frame_delta(const VideoInfo& video)
{
  return video.exactFrameRate.second / double(video.exactFrameRate.first);
}

size_t get_number_of_frames(const VideoInfo& video)
{
  return video.frames.size();
}

const VideoFrameInfo& get_frame(const VideoInfo& video, size_t n)
{
  return video.frames.at(n);
}

int get_nth_frame_pts(const VideoInfo& video, size_t n)
{
  return n < video.frames.size() ? video.frames.at(n).pts : (video.frames.back().pts + 1);
}

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

VideoInfo get_video_info(const QString& filePath)
{
  VideoInfo result;
  result.filePath = filePath;

  QString output;
  auto args = QStringList() << "-v"
                            << "0"
                            << "-select_streams"
                            << "v:0"
                            << "-count_packets"
                            << "-show_entries"
                            << "stream=r_frame_rate,nb_read_packets"
                            << "-show_entries"
                            << "format=duration" << filePath;
  run_ffprobe(args, &output);

  FFprobeOutputExtractor extractor{output};
  result.duration = extractor.extract("duration").toDouble();
  result.readPackets = extractor.extract("nb_read_packets").toInt();

  {
    QStringList parts = extractor.extract("r_frame_rate").split('/');
    if (parts.size() != 2)
    {
      throw std::runtime_error("bad r_frame_rate value");
    }

    result.exactFrameRate.first = parts.at(0).toInt();
    result.exactFrameRate.second = parts.at(1).toInt();
  }

  return result;
}

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

void collect_frames(VideoInfo& video, const QDir& dir)
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
    video.frames.push_back(frame);
    QFile::remove(path);
  }
}

void compute_frames(VideoInfo& video)
{
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

  args << "-i" << video.filePath;

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

  QEventLoop ev;

  QObject::connect(&ffmpeg, &QProcess::finished, &ev, &QEventLoop::quit);

  ffmpeg.start();

  auto do_collect_frames = [&video, &temp_dir]() { collect_frames(video, QDir(temp_dir.path())); };

  QTimer timer;
  timer.setInterval(100);
  QObject::connect(&timer, &QTimer::timeout, do_collect_frames);
  timer.start();

  ev.exec();

  do_collect_frames();

  std::sort(video.frames.begin(),
            video.frames.end(),
            [](const VideoFrameInfo& a, const VideoFrameInfo& b) { return a.pts < b.pts; });
}

void fetch_frames(VideoInfo& video)
{
  QString search_path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

  const QString search_filepath = search_path + "/" + QFileInfo(video.filePath).fileName() + "."
                                  + QString::number(video.readPackets);

  if (QFileInfo::exists(search_filepath))
  {
    read_frames_from_disk(video.frames, search_filepath);
  }
  else
  {
    compute_frames(video);
    save_frames_to_disk(video.frames, search_filepath);
  }
}

int phashDist(const VideoFrameInfo& a, const VideoFrameInfo& b)
{
  return phashDist(a.phash, b.phash);
}

//// END - VIDEO /////

/// SEGMENT EXTRACTION ///

class SearchWindow
{
public:
  const VideoInfo* video;
  size_t firstFrame;
  size_t nbFrames;
  double duration;

public:
  SearchWindow(const VideoInfo& iVideo, size_t iFirstFrame = 0, double iDuration = 2)
      : video(&iVideo)
      , firstFrame(iFirstFrame)
      , nbFrames(1)
      , duration(iDuration)
  {
    reset(iFirstFrame);
  }

  void reset(size_t iFirstFrame)
  {
    const VideoInfo& v = *video;
    firstFrame = iFirstFrame;
    nbFrames = 1;

    const double d = duration;
    duration = 0;
    increase(d);
  }

  void reset(size_t iFirstFrame, double iDuration)
  {
    duration = iDuration;
    reset(iFirstFrame);
  }

  void reset(double iDuration)
  {
    nbFrames = 1;
    duration = 0;
    increase(iDuration);
  }

  bool increase(double d)
  {
    const VideoInfo& v = *video;
    const size_t cur_nb_frames = nbFrames;
    duration += d;

    int max_pts = get_nth_frame_pts(v, firstFrame) + duration / get_frame_delta(v);
    while (firstFrame + nbFrames < get_number_of_frames(v))
    {
      if (get_nth_frame_pts(v, nbFrames + firstFrame) < max_pts)
      {
        ++nbFrames;
      }
      else
      {
        break;
      }
    }

    return nbFrames > cur_nb_frames;
  }
};

class SearchWindows
{
public:
  SearchWindow a;
  SearchWindow b;

public:
  SearchWindows(const VideoInfo& firstVideo, size_t i, const VideoInfo& secondVideo, size_t j)
      : a(firstVideo, i)
      , b(secondVideo, j)
  {}

  SearchWindows(
      double d, const VideoInfo& firstVideo, size_t i, const VideoInfo& secondVideo, size_t j)
      : a(firstVideo, i, d)
      , b(secondVideo, j, d)
  {}

  bool increase(double d)
  {
    bool ok = false;

    if (a.increase(d))
    {
      ok = true;
    }
    if (b.increase(d))
    {
      ok = true;
    }

    return ok;
  }
};

std::optional<std::pair<size_t, size_t>> find_match(SearchWindows windows, int threshold = 4)
{
  int bestd = 64;
  size_t bestx = -1;
  size_t besty = -1;

  const size_t i = windows.a.firstFrame;
  const size_t j = windows.b.firstFrame;

  for (size_t x(0); x < windows.a.nbFrames; ++x)
  {
    for (size_t y(0); y < windows.b.nbFrames; ++y)
    {
      int d = phashDist(windows.a.video->frames.at(i + x), windows.b.video->frames.at(j + y));
      if (d == 0)
      {
        return std::pair(i + x, j + y);
      }
      else if (d < bestd)
      {
        bestd = d;
        bestx = x;
        besty = y;
      }
    }
  }

  if (bestd < threshold)
  {
    return std::pair(i + bestx, j + besty);
  }

  return std::nullopt;
}

std::pair<size_t, size_t> find_match_end(const VideoInfo& a, size_t i, const VideoInfo& b, size_t j)
{
  constexpr int diff_threshold = 24;

  while (i + 1 < get_number_of_frames(a) && j + 1 < get_number_of_frames(b))
  {
    const int diff = phashDist(a.frames.at(i + 1), b.frames.at(j + 1));

    if (diff <= diff_threshold)
    {
      i = i + 1;
      j = j + 1;
      continue;
    }

    // frames i+1 and j+1 do not really match.
    // lets see if we can find a closer match nearby

    SearchWindows windows{0.250, a, i + 1, b, j + 1};

    constexpr int threshold = 20;
    std::optional<std::pair<size_t, size_t>> search_match = find_match(windows, threshold);

    if (!search_match)
    {
      // frames i,j are the last one matching
      break;
    }

    std::tie(i, j) = *search_match;
  }

  return std::pair(i + 1, j + 1);
}

struct InputSegment
{
  int src;
  double start;
  double end;
};

struct OutputSegment
{
  int pts;
  int duration;
  InputSegment input;
};

std::vector<OutputSegment> extract_segments(const VideoInfo& a, const VideoInfo& b)
{
  size_t i = 0;
  size_t j = 0;

  std::vector<OutputSegment> result;

  int curpts = 0;

  while (i < get_number_of_frames(a))
  {
    SearchWindows search_windows{a, i, b, j};
    std::optional<std::pair<size_t, size_t>> match = find_match(search_windows);

    while (!match)
    {
      if (!search_windows.increase(2.0))
      {
        break;
      }
      match = find_match(search_windows);
    }

    if (!match)
    {
      // on a pas trouvé de match, donc on ajoute un segment pour terminer la video

      OutputSegment segment;
      segment.pts = curpts;
      segment.duration = a.frames.back().pts + 1 - curpts;
      segment.input.src = 0;
      //segment.input.speed = 1;
      segment.input.start = curpts * get_frame_delta(a);
      segment.input.end = (segment.pts + segment.duration) * get_frame_delta(a);
      result.push_back(segment);

      curpts += segment.duration;
      i = get_number_of_frames(a);
      break;
    }

    // on crée un segment pour rejoindre le match
    if (a.frames.at(match->first).pts > curpts)
    {
      OutputSegment segment;
      segment.pts = curpts;
      segment.duration = a.frames.at(match->first).pts - curpts;
      segment.input.src = 0;
      // segment.input.speed = 1;
      segment.input.start = curpts * get_frame_delta(a);
      segment.input.end = (segment.pts + segment.duration) * get_frame_delta(a);
      result.push_back(segment);

      curpts += segment.duration;
    }

    i = match->first;
    j = match->second;

    auto [iend, jend] = find_match_end(a, i, b, j);

    qDebug().nospace() << "New match [" << a.frames.at(i).pts << "," << b.frames.at(j).pts
                       << "] -> [" << get_nth_frame_pts(a, iend) << ", "
                       << get_nth_frame_pts(b, jend) << "]";

    assert(iend > i);
    if (iend > i)
    {
      OutputSegment segment;
      segment.pts = curpts;
      segment.duration = get_nth_frame_pts(a, iend) - curpts;
      segment.input.src = 1;
      segment.input.start = b.frames.at(j).pts * get_frame_delta(b);
      segment.input.end = segment.input.start + (segment.duration * get_frame_delta(a));
      // segment.input.speed = (segment.input.end - segment.input.start)
      //                       / (segment.duration * a.framedelta());
      result.push_back(segment);

      curpts += segment.duration;
    }

    i = iend;
    j = jend;
  }

  return result;
}

QString formatSeconds(double val)
{
  int minutes = int(val) / 60;
  double seconds = int(val) % 60;
  seconds += (val - int(val));

  const char* fmtstr = seconds < 10 ? "%1:0%2" : "%1:%2";
  return QString(fmtstr).arg(QString::number(minutes), QString::number(seconds));
}

QDebug operator<<(QDebug dbg, const OutputSegment& segment)
{
  dbg.noquote().nospace() << (segment.pts) << " --> " << (segment.pts + segment.duration);
  dbg << " : stream " << segment.input.src << " from " << formatSeconds(segment.input.start)
      << " to " << formatSeconds(segment.input.end);
  dbg << " (" << segment.duration << " frames)";
  return dbg;
}

/// END ///

struct ProgramData
{
  VideoInfo firstVideo;
  VideoInfo secondVideo;
  QString outputPath;
};

static ProgramData gProgramData = {};

void main_proc();

void invoke_main()
{
  QTimer::singleShot(1, &main_proc);
}

QString compute_output_path(const VideoInfo& video, const QString& userProvidedOutputPath)
{
  if (!userProvidedOutputPath.isEmpty())
  {
    return userProvidedOutputPath;
  }

  QDir videodir = QFileInfo(video.filePath).dir();

  QDir outputdir = videodir;
  if (!outputdir.cd("output"))
  {
    qDebug() << "creating output dir: " << outputdir.absolutePath();
    outputdir.mkdir("output");
    outputdir.cd("output");
  }

  QString result = outputdir.filePath(QFileInfo(video.filePath).fileName());
  qDebug() << "output path will be: " << result;
  return result;
}

void digidub(VideoInfo& video, VideoInfo& audioSource, const QString& userProvidedOutputPath)
{
  const QDir videodir = QFileInfo(video.filePath).dir();

  QDir tempdir = videodir;
  if (!tempdir.cd("temp"))
  {
    qDebug() << "creating temp dir: " << tempdir.absolutePath();
    tempdir.mkdir("temp");
    tempdir.cd("temp");
  }

  fetch_frames(video);
  fetch_frames(audioSource);

  std::vector<OutputSegment> segments = extract_segments(video, audioSource);
  qDebug() << segments.size() << "segments";
  for (const auto& s : segments)
  {
    qDebug() << s;
  }

  QFile mylist{tempdir.filePath("list.txt")};
  mylist.open(QIODevice::ReadWrite | QIODevice::Truncate);

  const QString source_audio1_path = tempdir.filePath("src1.wav");
  const QString source_audio2_path = tempdir.filePath("src2.wav");

  // extract audio 1
  {
    QStringList args;
    args << "-y"
         << "-i" << video.filePath;
    args << "-map_metadata"
         << "-1";
    args << "-map"
         << "0:1";
    args << "-ac"
         << "1";
    args << source_audio1_path;
    run_ffmpeg(args);
  }

  // extract audio 2
  {
    QStringList args;
    args << "-y"
         << "-i" << audioSource.filePath;
    args << "-map_metadata"
         << "-1";
    args << "-map"
         << "0:1";
    args << "-ac"
         << "1";
    args << source_audio2_path;
    run_ffmpeg(args);
  }

  int i = 0;
  for (const OutputSegment& seg : segments)
  {
    QStringList args;
    args << "-y";

    args << "-i" << (seg.input.src == 0 ? source_audio1_path : source_audio2_path);

    args << "-ss" << QString::number(seg.input.start);
    args << "-to" << QString::number(seg.input.end);

    QString outputpath = tempdir.filePath(QString::number(i++) + ".wav");
    args << outputpath;

    run_ffmpeg(args);

    mylist.write(QString("file '%1'\n").arg(outputpath).toUtf8());
  }

  mylist.flush();
  mylist.close();

  const QString output_audio_path = tempdir.filePath("concat.mka");

  // concat
  {
    QStringList args;
    args << "-y"
         << "-f"
         << "concat"
         << "-safe"
         << "0";
    args << "-i" << mylist.fileName();
    args << "-c:a"
         << "libopus";
    args << output_audio_path;
    run_ffmpeg(args);
  }

  const QString outputPath = compute_output_path(video, userProvidedOutputPath);

  constexpr bool merge_with_ffmpeg = false;
  if (merge_with_ffmpeg)
  {
    QStringList args;
    args << "-y";

    args << "-i" << video.filePath;

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

    args << outputPath;
    run_ffmpeg(args);
  }
  else
  {
    //mkvmerge -o output/Digimon.S1.E01.mkv --default-track-flag 1:0 Digimon.S1.E01.mkv --track-name "0:Mono - FR (Mixed)" --language 0:fre --default-track-flag 0:1 --original-flag 0 -a 0 output/concat.mka
    QStringList args;
    args << "-o" << outputPath;

    args << "--default-track-flag"
         << "1:0"; // remove default-track flag from EN audio
    args << video.filePath;

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
    {
      const QString video_filename = QFileInfo(video.filePath).completeBaseName();
      QStringList subtitles_paths;
      subtitles_paths << videodir.absoluteFilePath(video_filename + ".srt")
                      << videodir.absoluteFilePath("subs/" + video_filename + ".srt");

      for (const QString& subspath : subtitles_paths)
      {
        if (QFileInfo::exists(subspath))
        {
          args << "--track-name"
               << "0:Subtitles - EN";
          args << "--language"
               << "0:eng";
          args << "--default-track-flag"
               << "0:0"; // unset default-track flag
          args << "-s"
               << "0";
          args << subspath;

          break;
        }
      }
    }

    QString std_out;
    run_executable("mkvmerge", args, &std_out);
    if (!std_out.isEmpty())
    {
      qDebug() << std_out;
    }
  }

  qApp->exit();
}

void main_proc()
{
  if (gProgramData.firstVideo.filePath.isEmpty() || gProgramData.secondVideo.filePath.isEmpty())
  {
    qDebug() << "2 videos must be provided";
    return qApp->exit(1);
  }

  digidub(gProgramData.firstVideo, gProgramData.secondVideo, gProgramData.outputPath);
}

void set_video_arg(VideoInfo& video, const QString& arg)
{
  QFileInfo info{arg};
  if (info.exists() && info.suffix() == "mkv")
  {
    video = get_video_info(arg);
  }
  else
  {
    throw std::runtime_error("file does not exist");
  }
}

int main(int argc, char *argv[])
{
  QApplication::setApplicationName("digidub");

  QApplication app{argc, argv};

  ProgramData& pd = gProgramData;

  const QStringList& args = app.arguments();

  if (args.contains("-h") || args.contains("--help") || args.size() == 1)
  {
    std::cout << "digidub -a <main-video.mkv> -b <secondary-video.mkv> [-o <output.mkv>]"
              << std::endl;
    return 0;
  }

  for (int i(1); i < args.size(); ++i)
  {
    const QString& arg = args.at(i);

    if (arg == "-o")
    {
      pd.outputPath = args.at(++i);
    }
    else if (arg == "-a")
    {
      set_video_arg(pd.firstVideo, args.at(++i));
    }
    else if (arg == "-b")
    {
      set_video_arg(pd.secondVideo, args.at(++i));
    }
    else
    {
      qDebug() << "unknown arg: " << arg;
      return 1;
    }
  }

  invoke_main();

  return app.exec();
}
