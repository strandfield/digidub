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

#include <algorithm>
#include <iostream>
#include <iterator>
#include <optional>

constexpr const char* VERSION_STRING = "1.0";

bool debugmatches = false;
int frame_unmatch_threshold = 21;
int frame_rematch_threshold = 16;
double area_match_threshold = 20;

// DEBUG/PRINT HELPERS //

QString formatSeconds(double val)
{
  int minutes = int(val) / 60;
  double seconds = int(val) % 60;
  seconds += (val - int(val));

  const char* fmtstr = seconds < 10 ? "%1:0%2" : "%1:%2";
  return QString(fmtstr).arg(QString::number(minutes), QString::number(seconds));
}

// FFMPEG helpers //

void run_executable(const QString& name,
                    const QStringList& args,
                    QString* stdOut = nullptr,
                    QString* stdErr = nullptr)
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

  if (stdErr)
  {
    *stdErr = QString::fromLocal8Bit(instance.readAllStandardError());
  }
}

void run_ffmpeg(const QStringList& args, QString* stdOut = nullptr)
{
  run_executable("ffmpeg", args, nullptr, stdOut);
}

void run_ffprobe(const QStringList& args, QString* stdOut = nullptr)
{
  run_executable("ffprobe", args, stdOut);
}

//// VIDEO /////

class TimeWindow
{
private:
  double m_start = 0;
  double m_end = 0;

public:
  TimeWindow() = default;

  double start() const { return m_start; }
  double end() const { return m_end; }
  double duration() const { return m_end - m_start; }

  bool contains(double t) const { return start() <= t && t < end(); }

  static TimeWindow fromStartAndEnd(double s, double e)
  {
    TimeWindow w;
    w.m_start = s;
    w.m_end = e;
    return w;
  }

  static TimeWindow fromStartAndDuration(double s, double d)
  {
    TimeWindow w;
    w.m_start = s;
    w.m_end = s + d;
    return w;
  }
};

std::ostream& operator<<(std::ostream& out, const TimeWindow& w)
{
  out << formatSeconds(w.start()).toStdString() << " - " << formatSeconds(w.end()).toStdString();
  return out;
}

struct VideoFrameInfo
{
  int pts;
  quint64 phash;
  bool silence = false;
  bool black = false;
  std::optional<double> scscore;
  bool excluded = false;
  bool reusable = false;
};

struct SceneChange
{
  double score;
  double time;
};

struct VideoInfo
{
  QString filePath;
  double duration;
  std::pair<int, int> exactFrameRate;
  int readPackets;
  std::vector<VideoFrameInfo> frames;
  std::vector<TimeWindow> silences;
  std::vector<TimeWindow> blackframes;
  std::vector<SceneChange> scenechanges;

  using Frame = VideoFrameInfo;
};

inline double get_frame_delta(const VideoInfo& video)
{
  return video.exactFrameRate.second / double(video.exactFrameRate.first);
}

inline size_t get_number_of_frames(const VideoInfo& video)
{
  return video.frames.size();
}

inline const VideoFrameInfo& get_frame(const VideoInfo& video, size_t n)
{
  return video.frames.at(n);
}

inline double get_frame_timestamp(const VideoInfo& video, size_t n)
{
  return get_frame(video, n).pts * get_frame_delta(video);
}

inline int get_nth_frame_pts(const VideoInfo& video, size_t n)
{
  return n < video.frames.size() ? video.frames.at(n).pts : (video.frames.back().pts + 1);
}

inline int phashDist(const VideoFrameInfo& a, const VideoFrameInfo& b)
{
  return phashDist(a.phash, b.phash);
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

// FRAME UTILS //

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

class FrameSpan
{
public:
  const VideoInfo* video;
  size_t first;
  size_t count;

public:
  FrameSpan()
      : video(nullptr)
      , first(0)
      , count(0)
  {}

  FrameSpan(const VideoInfo& v, size_t offset, size_t n)
      : video(&v)
      , first(offset)
      , count(n)
  {
    first = std::min(first, get_number_of_frames(v));
    count = std::min(count, get_number_of_frames(v) - first);
  }

  size_t size() const { return count; }
  const VideoFrameInfo& at(size_t i) const
  {
    assert(i < count);
    return video->frames.at(first + i);
  }

  size_t startOffset() const { return first; }
  size_t endOffset() const { return first + count; }

  void moveStartOffsetTo(size_t dest)
  {
    assert(dest <= endOffset());
    count = endOffset() - dest;
    first = dest;
  }

  void moveEndOffset(size_t dest)
  {
    assert(dest > first);
    count = dest - first;
  }

  void widenLeft(size_t num)
  {
    num = std::min(this->first, num);
    this->first -= num;
    this->count += num;
    assert(endOffset() <= this->video->frames.size());
  }

  void trimLeft(size_t num)
  {
    num = std::min(num, this->count);
    this->first += num;
    this->count -= num;
  }

  FrameSpan left(size_t num) const
  {
    FrameSpan result{*this};

    if (num >= size())
    {
      return result;
    }

    result.count = num;
    return result;
  }

  FrameSpan right(size_t num) const
  {
    FrameSpan result{*this};

    if (num >= size())
    {
      return result;
    }

    result.first = endOffset() - num;
    result.count = num;
    return result;
  }

  FrameSpan subspan(size_t offset, size_t count) const
  {
    return FrameSpan(*video, first + offset, count);
  }

  bool contains(const FrameSpan& other) const
  {
    assert(other.video == this->video);
    return other.video == this->video && other.startOffset() >= startOffset()
           && other.endOffset() <= endOffset();
  }
};

inline bool operator==(const FrameSpan& lhs, const FrameSpan& rhs)
{
  return lhs.video == rhs.video && lhs.startOffset() == rhs.startOffset()
         && lhs.endOffset() == rhs.endOffset();
}

inline bool operator!=(const FrameSpan& lhs, const FrameSpan& rhs)
{
  return !(lhs == rhs);
}

QDebug operator<<(QDebug dbg, const FrameSpan& object)
{
  constexpr bool showpts = false;
  const double df = get_frame_delta(*object.video);
  dbg.noquote().nospace();
  dbg << QFileInfo(object.video->filePath).completeBaseName();
  dbg << "[" << formatSeconds(get_nth_frame_pts(*object.video, object.first) * df);
  dbg << "-" << formatSeconds(get_nth_frame_pts(*object.video, object.first + object.count) * df);
  if (showpts)
  {
    dbg << "|" << get_nth_frame_pts(*object.video, object.startOffset()) << "-"
        << get_nth_frame_pts(*object.video, object.endOffset());
  }
  dbg << "]";
  return dbg;
}

inline size_t get_begin_offset(const FrameSpan& span)
{
  return span.first;
}

inline size_t get_end_offset(const FrameSpan& span)
{
  return span.first + span.count;
}

inline double get_duration(const FrameSpan& span)
{
  return span.count * get_frame_delta(*span.video);
}

inline bool is_sc_frame(const VideoFrameInfo& frame)
{
  return frame.scscore.has_value();
}

inline bool is_sc_frame_safe(const VideoInfo& video, size_t frameIndex)
{
  if (frameIndex > video.frames.size())
  {
    return false;
  }

  if (frameIndex == 0 || frameIndex == video.frames.size())
  {
    return true;
  }

  return is_sc_frame(video.frames.at(frameIndex));
}

size_t find_next_blackframe(const VideoInfo& video, size_t frameIndex)
{
  size_t i = frameIndex + 1;

  while (i < get_number_of_frames(video))
  {
    if (video.frames.at(i).black)
    {
      break;
    }
    else
    {
      ++i;
    }
  }

  return i;
}

size_t find_next_scframe(const VideoInfo& video, size_t frameIndex)
{
  size_t i = frameIndex + 1;

  while (i < get_number_of_frames(video))
  {
    if (is_sc_frame(video.frames.at(i)))
    {
      break;
    }
    else
    {
      ++i;
    }
  }

  return i;
}

// SILENCE DETECTION //

std::vector<TimeWindow> silencedetect(VideoInfo& video)
{
  constexpr const char* duration_threshold = "0.4";

  std::vector<TimeWindow> result;

  QString output;
  QStringList args;
  args << "-nostats"
       << "-hide_banner";
  args << "-i" << video.filePath;
  args << "-map"
       << "0:1";
  args << "-af" << QString("silencedetect=n=-35dB:d=%1").arg(duration_threshold);
  args << "-f"
       << "null"
       << "-";
  run_ffmpeg(args, &output);
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

    auto w = TimeWindow::fromStartAndDuration(start, duration);

    // qDebug() << "found silencefrom " << formatSeconds(w.start()) << " to " << formatSeconds(w.end())
    //          << " (" << w.duration() << "s)";

    result.push_back(w);
  }

  return result;
}

void silenceborders(std::vector<VideoFrameInfo>& frames, size_t n = 10)
{
  auto silence_frame = [](VideoFrameInfo& f) { f.silence = true; };

  // silence frames at the beginning if there is some silence nearby
  {
    auto it = std::find_if(frames.begin(), frames.begin() + n, [](const VideoFrameInfo& f) {
      return f.silence;
    });

    if (it != frames.begin() + n)
    {
      std::for_each(frames.begin(), it, silence_frame);
    }
  }

  // silence frames at the end if there is some silence nearby
  {
    auto it = std::find_if(frames.rbegin(), frames.rbegin() + n, [](const VideoFrameInfo& f) {
      return f.silence;
    });

    if (it != frames.rbegin() + n)
    {
      std::for_each(frames.rbegin(), it, silence_frame);
    }
  }
}

FrameSpan get_span(const VideoInfo& video, const TimeWindow& window)
{
  auto get_frame_timestamp = [&video](const VideoFrameInfo& frame) {
    return frame.pts * get_frame_delta(video);
  };

  auto within_window = [&get_frame_timestamp, window](const VideoFrameInfo& frame) {
    const double t = get_frame_timestamp(frame);
    return window.contains(t);
  };

  auto begin = std::lower_bound(video.frames.begin(),
                                video.frames.end(),
                                window.start(),
                                [&get_frame_timestamp](const VideoFrameInfo& e, double val) {
                                  return get_frame_timestamp(e) < val;
                                });

  auto it = begin;
  while (it != video.frames.end() && within_window(*it))
    ++it;

  size_t start_offset = std::distance(video.frames.begin(), begin);
  size_t end_offset = std::distance(video.frames.begin(), it);
  return FrameSpan(video, start_offset, end_offset - start_offset);
}

template<typename Fun>
void mark_frames(VideoInfo& video, const TimeWindow& window, Fun&& fun)
{
  auto get_frame_timestamp = [&video](const VideoFrameInfo& frame) {
    return frame.pts * get_frame_delta(video);
  };

  auto within_window = [&get_frame_timestamp, window](const VideoFrameInfo& frame) {
    const double t = get_frame_timestamp(frame);
    return window.contains(t);
  };

  auto it = std::lower_bound(video.frames.begin(),
                             video.frames.end(),
                             window.start(),
                             [&get_frame_timestamp](const VideoFrameInfo& e, double val) {
                               return get_frame_timestamp(e) < val;
                             });

  for (; it != video.frames.end(); ++it)
  {
    if (within_window(*it))
    {
      fun(*it);
    }
    else
    {
      break;
    }
  }
}

template<typename Fun>
void mark_frames(VideoInfo& video, const std::vector<TimeWindow>& windows, Fun&& fun)
{
  for (const TimeWindow& w : windows)
  {
    mark_frames(video, w, std::forward<Fun>(fun));
  }
}

void mark_silence_frames(VideoInfo& video)
{
  auto mark_silence = [](VideoFrameInfo& frame) { frame.silence = true; };

  for (const TimeWindow& w : video.silences)
  {
    mark_frames(video, w, mark_silence);
  }
}

void fetch_silences(VideoInfo& video)
{
  // read from disk or compute on the fly
  {
    QString search_path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    const QString cache_filepath = search_path + "/" + QFileInfo(video.filePath).fileName() + "."
                                   + QString::number(video.readPackets) + ".silencedetect";

    if (QFileInfo::exists(cache_filepath))
    {
      QFile file{cache_filepath};
      if (!file.open(QIODevice::ReadOnly))
      {
        qDebug() << "could not open " << cache_filepath;
      }

      QTextStream stream{&file};
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
        video.silences.push_back(TimeWindow::fromStartAndEnd(start, end));
      }
    }
    else
    {
      video.silences = silencedetect(video);

      QFile file{cache_filepath};
      if (file.open(QIODevice::ReadWrite | QIODevice::Truncate))
      {
        for (const TimeWindow& w : video.silences)
        {
          file.write(
              QString("%1,%2").arg(QString::number(w.start()), QString::number(w.end())).toUtf8());
          file.write("\n");
        }
      }
      else
      {
        qDebug() << "could not write " << cache_filepath;
      }
    }
  }

  mark_silence_frames(video);
}

// BLACK FRAME DETECTION //

void mark_black_frames(VideoInfo& video)
{
  auto mark_black = [](VideoFrameInfo& frame) { frame.black = true; };

  for (const TimeWindow& w : video.blackframes)
  {
    mark_frames(video, w, mark_black);
  }
}

std::vector<TimeWindow> blackdetect(const VideoInfo& video)
{
  constexpr const char* duration_threshold = "0.4";

  std::vector<TimeWindow> result;

  QString output;
  QStringList args;
  args << "-nostats"
       << "-hide_banner";
  args << "-i" << video.filePath;
  args << "-map"
       << "0:0";
  args << "-vf" << QString("blackdetect=d=%1:pix_th=0.05").arg(duration_threshold);
  args << "-f"
       << "null"
       << "-";
  run_ffmpeg(args, &output);

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

    auto w = TimeWindow::fromStartAndEnd(start, end);

    // qDebug() << "found black frames from " << formatSeconds(w.start()) << " to "
    //          << formatSeconds(w.end()) << " (" << w.duration() << "s)";

    result.push_back(w);
  }

  return result;
}

void fetch_black_frames(VideoInfo& video)
{
  // read from disk or compute on the fly
  {
    QString search_path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    const QString cache_filepath = search_path + "/" + QFileInfo(video.filePath).fileName() + "."
                                   + QString::number(video.readPackets) + ".blackdetect";

    if (QFileInfo::exists(cache_filepath))
    {
      QFile file{cache_filepath};
      if (!file.open(QIODevice::ReadOnly))
      {
        qDebug() << "could not open " << cache_filepath;
      }

      QTextStream stream{&file};
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
        video.blackframes.push_back(TimeWindow::fromStartAndEnd(start, end));
      }
    }
    else
    {
      video.blackframes = blackdetect(video);

      QFile file{cache_filepath};
      if (file.open(QIODevice::ReadWrite | QIODevice::Truncate))
      {
        for (const TimeWindow& w : video.blackframes)
        {
          file.write(
              QString("%1,%2").arg(QString::number(w.start()), QString::number(w.end())).toUtf8());
          file.write("\n");
        }
      }
      else
      {
        qDebug() << "could not write " << cache_filepath;
      }
    }
  }

  mark_black_frames(video);
}

// SCENE CHANGE DETECTION //

void mark_sc_frames(VideoInfo& video)
{
  Q_ASSERT(!video.frames.empty());

  auto get_frame_timestamp = [&video](const VideoFrameInfo& frame) {
    return frame.pts * get_frame_delta(video);
  };

  for (const SceneChange& e : video.scenechanges)
  {
    auto it = std::lower_bound(video.frames.begin(),
                               video.frames.end(),
                               e.time,
                               [&get_frame_timestamp](const VideoFrameInfo& e, double val) {
                                 return get_frame_timestamp(e) < val;
                               });

    if (it != video.frames.end())
    {
      const double t = get_frame_timestamp(*it);

      if (!qFuzzyCompare(t, e.time) && it != video.frames.begin())
      {
        --it;
      }

      it->scscore = e.score;

      //qDebug() << "sc at frame pts = " << it->pts << "(score=" << e.score << ")";
    }
  }

  //qDebug() << video.scenechanges.size() << "scenes";
}

std::vector<SceneChange> scdet(const VideoInfo& video)
{
  std::vector<SceneChange> result;

  QString output;
  QStringList args;
  args << "-nostats"
       << "-hide_banner";
  args << "-i" << video.filePath;
  args << "-map"
       << "0:0";
  args << "-vf" << QString("scdet");
  args << "-f"
       << "null"
       << "-";
  run_ffmpeg(args, &output);

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
    result.push_back(sc);
  }

  return result;
}

void fetch_sc(VideoInfo& video)
{
  // read from disk or compute on the fly
  {
    QString search_path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    const QString cache_filepath = search_path + "/" + QFileInfo(video.filePath).fileName() + "."
                                   + QString::number(video.readPackets) + ".scdet";

    if (QFileInfo::exists(cache_filepath))
    {
      QFile file{cache_filepath};
      if (!file.open(QIODevice::ReadOnly))
      {
        qDebug() << "could not open " << cache_filepath;
      }

      QTextStream stream{&file};
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
        video.scenechanges.push_back(SceneChange{.score = score, .time = time});
      }
    }
    else
    {
      video.scenechanges = scdet(video);

      QFile file{cache_filepath};
      if (file.open(QIODevice::ReadWrite | QIODevice::Truncate))
      {
        for (const SceneChange& e : video.scenechanges)
        {
          file.write(
              QString("%1,%2").arg(QString::number(e.score), QString::number(e.time)).toUtf8());
          file.write("\n");
        }
      }
      else
      {
        qDebug() << "could not write " << cache_filepath;
      }
    }
  }
}

void fetch_sc_frames(VideoInfo& video)
{
  fetch_sc(video);
  mark_sc_frames(video);
}

void filter_sc(VideoInfo& video, double minScore)
{
  auto it = std::remove_if(video.scenechanges.begin(),
                           video.scenechanges.end(),
                           [minScore](const SceneChange& e) { return e.score < minScore; });

  video.scenechanges.erase(it, video.scenechanges.end());
}

void merge_small_scenes(VideoInfo& video, size_t minSize)
{
  auto find_next_scene =
      [&video](std::vector<VideoFrameInfo>::iterator from) -> std::vector<VideoFrameInfo>::iterator {
    if (from->scscore.has_value())
    {
      ++from;
    }
    return std::find_if(from, video.frames.end(), [](const VideoFrameInfo& f) {
      return f.scscore.has_value();
    });
  };

  auto it = video.frames.begin();

  while (it != video.frames.end())
  {
    auto next = find_next_scene(it);

    size_t n = std::distance(it, next);

    if (n >= minSize)
    {
      it = next;
      continue;
    }

    if (next == video.frames.end())
    {
      it->scscore.reset();
      it = next;
      break;
    }

    if (next->scscore.value() < it->scscore.value())
    {
      next->scscore.reset();
    }
    else
    {
      it->scscore.reset();
      it = next;
    }
  }
}

// EXCLUDED SEGMENTS //

void mark_excluded_frames(VideoInfo& video, const std::vector<TimeWindow>& segments)
{
  assert(video.frames.size() > 0);

  auto mark_excluded = [](VideoFrameInfo& frame) { frame.excluded = true; };

  for (const TimeWindow& w : segments)
  {
    mark_frames(video, w, mark_excluded);
  }
}

void prevent_breaks_at_frames(VideoInfo& video, const std::vector<TimeWindow>& segments)
{
  assert(video.frames.size() > 0);

  auto f = [](VideoFrameInfo& frame) { frame.silence = false; };

  for (const TimeWindow& w : segments)
  {
    mark_frames(video, w, f);
  }
}

//// END - VIDEO /////

/// SEGMENT EXTRACTION ///

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

size_t find_silence_end(const VideoInfo& video, size_t i)
{
  //get out of silence if we are in one.
  while (i < get_number_of_frames(video) && get_frame(video, i).silence)
  {
    ++i;
  }

  return i;
}

size_t find_next_silence(const VideoInfo& video, size_t from = 0)
{
  // first, get out of silence if we are in one.
  size_t i = find_silence_end(video, from);

  // then, go to next frame that is silence
  while (i < get_number_of_frames(video) && !get_frame(video, i).silence)
  {
    ++i;
  }

  return i;
}

size_t find_next_excluded(const VideoInfo& video, size_t from = 0)
{
  const size_t vend = get_number_of_frames(video);
  assert(from == vend || !video.frames.at(from).excluded);

  size_t i = from;
  while (i < vend && !get_frame(video, i).excluded)
  {
    ++i;
  }

  return i;
}

struct MatchingArea
{
  FrameSpan pattern;
  FrameSpan match;
  double score;
};

MatchingArea find_best_matching_area_ex(const FrameSpan& pattern, const FrameSpan& searchArea)
{
  MatchingArea result;
  result.score = 64;
  result.pattern = pattern;
  result.match = FrameSpan(*searchArea.video, searchArea.first + searchArea.count, 0);

  if (searchArea.size() < pattern.size())
  {
    return result;
  }

  // TODO: we could define "best" not only in terms of average,
  // but also in terms of number of phash-dist less than a given value.
  double bestavg = 64;

  size_t imax = searchArea.size() - pattern.size();
  for (size_t i(0); i <= imax; ++i)
  {
    int dacc = 0;
    int dmin = 64;
    for (size_t j(0); j < pattern.size(); ++j)
    {
      int d = phashDist(pattern.at(j), searchArea.at(i + j));
      dacc += d;
      dmin = std::min(d, dmin);
    }

    double avg = dacc / double(pattern.size());

    // TODO: should this be kept?
    // maybe... but 4 is too harsh
    // if (dmin >= 4)
    // {
    //   continue;
    // }

    if (avg < bestavg)
    {
      bestavg = avg;
      result.match = searchArea.subspan(i, pattern.size());
      result.score = avg;
    }
  }

  return result;
}

FrameSpan find_best_matching_area(const FrameSpan& pattern, const FrameSpan& searchArea)
{
  auto result = find_best_matching_area_ex(pattern, searchArea);
  qDebug() << result.score << result.pattern << result.match;
  return result.match;
}

std::vector<FrameSpan> split_at_scframes(const FrameSpan& span)
{
  std::vector<FrameSpan> result;

  size_t i = span.first;
  while (i < span.first + span.count)
  {
    size_t j = find_next_scframe(*span.video, i);
    if (j > span.first + span.count)
    {
      j = span.first + span.count;
    }

    result.push_back(FrameSpan(*span.video, i, j - i));
    i = j;
  }

  return result;
}

inline FrameSpan merge(FrameSpan a, FrameSpan b)
{
  if (b.startOffset() < a.startOffset())
  {
    std::swap(a, b);
  }

  return FrameSpan{*a.video, a.startOffset(), b.endOffset() - a.startOffset()};
}

std::pair<std::vector<FrameSpan>::const_iterator, FrameSpan> extend_match(
    MatchingArea matchStart,
    std::vector<FrameSpan>::const_iterator ikframes_begin,
    std::vector<FrameSpan>::const_iterator ikframes_end,
    size_t searchAreaEnd)
{
  std::pair<FrameSpan, FrameSpan> prev_match{matchStart.pattern, matchStart.match};

  auto it = ikframes_begin;
  while (it != ikframes_end)
  {
    FrameSpan current_pattern = *it;

    // we start with the ideal search window
    FrameSpan search_area{*prev_match.second.video,
                          prev_match.second.endOffset(),
                          current_pattern.size()};
    // we then extend the search window to cover skipped or extra frames
    const size_t prev_pattern_extra = std::max<size_t>(prev_match.first.count / 20, 3);
    search_area.first -= std::min(prev_pattern_extra, search_area.first);
    search_area.count += 2 * prev_pattern_extra;
    const size_t cur_pattern_extra = std::max<size_t>(current_pattern.size() / 20, 3);
    search_area.count += cur_pattern_extra;
    if (search_area.endOffset() > searchAreaEnd)
    {
      search_area.count = searchAreaEnd - search_area.first;
    }

    if (search_area.size() < current_pattern.size())
    {
      break;
    }

    // then we try to find a match:
    MatchingArea m = find_best_matching_area_ex(current_pattern, search_area);

    if (m.score > area_match_threshold) // no good match, stop here
    {
      break;
    }

    // we try to refine the match by searching for a match that includes the previous segment.
    // this helps avoiding getting too far ahead.
    if (m.match.startOffset() != prev_match.second.endOffset())
    {
      // TODO: use compute_symetric_span_around_keyframe()

      FrameSpan match_concat = merge(prev_match.second, m.match);
      if (m.match.startOffset() < prev_match.second.endOffset())
      {
        size_t diff = prev_match.second.endOffset() - m.match.startOffset();
        match_concat.widenLeft(diff);
        match_concat.count += diff;
      }

      FrameSpan pattern_concat = *it;
      size_t number_of_frames_from_prev_pattern = 0;
      size_t number_of_frames_removed_from_cur_pattern = 0;
      if (it->size() >= prev_match.first.size())
      {
        number_of_frames_removed_from_cur_pattern = (it->size() - prev_match.first.size());
        pattern_concat.count -= number_of_frames_removed_from_cur_pattern;
        pattern_concat = merge(prev_match.first, pattern_concat);
        number_of_frames_from_prev_pattern = prev_match.first.size();
      }
      else
      {
        const size_t sizediff = (prev_match.first.size() - it->size());
        pattern_concat = prev_match.first;
        pattern_concat.first += sizediff;
        pattern_concat.count -= sizediff;
        number_of_frames_from_prev_pattern = pattern_concat.size();
        pattern_concat = merge(pattern_concat, *it);
      }

      MatchingArea refined_match = find_best_matching_area_ex(pattern_concat, match_concat);
      refined_match.match.trimLeft(number_of_frames_from_prev_pattern);
      refined_match.match.count += number_of_frames_removed_from_cur_pattern;
      assert(refined_match.match.count == m.match.count);
      if (refined_match.match.startOffset() != m.match.startOffset())
      {
        // qDebug() << "    refined match " << m.match << " to " << refined_match.match;
        m.match = refined_match.match;
      }
    }

    // save the match:
    prev_match = std::pair(current_pattern, m.match);

    // go on to the next set of inter-frames
    ++it;
  }

  return std::pair(it, prev_match.second);
}

// rename to "around sc frame"
FrameSpan compute_symetric_span_around_keyframe(const FrameSpan& a,
                                                const FrameSpan& b,
                                                size_t n = std::numeric_limits<size_t>::max())
{
  assert(a.endOffset() == b.startOffset());

  n = std::min({n, a.size(), b.size()});

  FrameSpan result = b;
  result.count = n;
  result.widenLeft(n);

  return result;
}

bool starts_with_black_frames(const FrameSpan& span)
{
  if (span.size() == 0)
  {
    return false;
  }

  if (span.at(0).black)
  {
    return true;
  }

  const VideoInfo& video = *span.video;

  if (span.startOffset() > 0)
  {
    if (video.frames.at(span.startOffset() - 1).black)
    {
      return true;
    }
  }

  return false;
}

bool ends_with_black_frames(const FrameSpan& span)
{
  if (span.size() == 0)
  {
    return false;
  }

  if (span.at(span.size() - 1).black)
  {
    return true;
  }

  const VideoInfo& video = *span.video;

  if (span.endOffset() < video.frames.size())
  {
    if (video.frames.at(span.endOffset()).black)
    {
      return true;
    }
  }

  return false;
}

bool likely_same_scene(FrameSpan a, FrameSpan b)
{
  if (b.size() < a.size())
  {
    std::swap(a, b);
  }

  return find_best_matching_area_ex(a, b).score <= area_match_threshold;
}

size_t number_of_frames_in_range(std::vector<FrameSpan>::const_iterator begin,
                                 std::vector<FrameSpan>::const_iterator end)
{
  return std::accumulate(begin, end, size_t(0), [](size_t n, const FrameSpan& e) {
    return n + e.size();
  });
}

std::optional<std::pair<size_t, size_t>> find_best_match(const FrameSpan& a,
                                                         const FrameSpan& b,
                                                         int matchThreshold = -1)
{
  if (matchThreshold < 0)
  {
    matchThreshold = frame_rematch_threshold;
  }

  int bestd = 64;
  size_t bestx = -1;
  size_t besty = -1;

  for (size_t x(0); x < a.size(); ++x)
  {
    for (size_t y(0); y < b.size(); ++y)
    {
      int d = phashDist(a.at(x), b.at(y));
      if (d < bestd)
      {
        bestd = d;
        bestx = x;
        besty = y;
      }
      else if (d == bestd)
      {
        if (std::abs(int(x) - int(y)) < std::abs(int(bestx) - int(besty)))
        {
          bestx = x;
          besty = y;
        }
      }
    }
  }

  if (bestd <= matchThreshold)
  {
    return std::pair(a.startOffset() + bestx, b.startOffset() + besty);
  }

  return std::nullopt;
}

std::pair<size_t, size_t> find_match_end(const VideoInfo& a,
                                         size_t i,
                                         const VideoInfo& b,
                                         size_t j,
                                         double speed,
                                         size_t i_end,
                                         size_t j_end)
{
  double jreal = j;

  while (i + 1 < i_end && std::round(jreal + speed) < j_end)
  {
    size_t next_frame_a = i + 1;
    size_t next_frame_b = std::round(jreal + speed);
    const int diff = phashDist(a.frames.at(next_frame_a), b.frames.at(next_frame_b));

    if (diff < frame_unmatch_threshold)
    {
      i = next_frame_a;
      j = next_frame_b;
      jreal = jreal + speed;
      continue;
    }

    // frames next_frame_a and next_frame_b do not really match.
    // lets see if we can find a closer match nearby
    auto span1 = FrameSpan(a, next_frame_a, i_end - next_frame_a).left(4);
    auto span2 = FrameSpan(b, next_frame_b, j_end - next_frame_b).left(4);

    std::optional<std::pair<size_t, size_t>> search_match = find_best_match(span1, span2);

    if (!search_match)
    {
      // frames i,j are the last one matching
      break;
    }

    std::tie(i, j) = *search_match;
    jreal = j;
  }

  if ((i + 1) + 1 == i_end)
  {
    // we are just missing one frame from video A.
    // let's see if we can include it in the match
    const int diff = phashDist(a.frames.at(i + 1), b.frames.at(j));
    if (diff < frame_unmatch_threshold)
    {
      ++i;
    }
  }

  return std::pair(i + 1, j + 1);
}

std::pair<size_t, size_t> find_match_end_backward(const VideoInfo& a,
                                                  size_t i,
                                                  const VideoInfo& b,
                                                  size_t j,
                                                  double speed,
                                                  size_t i_min,
                                                  size_t j_min)
{
  double jreal = j;

  while (i > i_min && std::round(jreal - speed) >= j_min)
  {
    size_t prev_frame_a = i - 1;
    size_t prev_frame_b = std::round(jreal - speed);

    const int diff = phashDist(a.frames.at(prev_frame_a), b.frames.at(prev_frame_b));

    if (diff < frame_unmatch_threshold)
    {
      i = prev_frame_a;
      j = prev_frame_b;
      jreal = jreal - speed;
      continue;
    }

    // frames prev_frame_a and prev_frame_b do not really match.
    // lets see if we can find a closer match nearby
    auto span1 = FrameSpan(a, i_min, i - i_min).right(4);
    auto span2 = FrameSpan(b, j_min, j - j_min).right(4);

    std::optional<std::pair<size_t, size_t>> search_match = find_best_match(span1, span2);

    if (!search_match)
    {
      // frames i,j are the last one matching
      break;
    }

    std::tie(i, j) = *search_match;
    jreal = j;
  }

  if (i == i_min + 1)
  {
    // we are just missing one frame from video A.
    // let's see if we can include it in the match
    const int diff = phashDist(a.frames.at(i_min), b.frames.at(j));
    if (diff < frame_unmatch_threshold)
    {
      i = i_min;
    }
  }

  return std::pair(i, j);
}

std::pair<FrameSpan, FrameSpan> refine_match_2scenes(std::vector<FrameSpan>::const_iterator it,
                                                     const FrameSpan& basematch,
                                                     const FrameSpan& fullSearchArea)
{
  const VideoInfo& first_video = *(it->video);
  const VideoInfo& second_video = *basematch.video;
  const FrameSpan basepattern = merge(*it, *std::next(it));

  // since we only have 2 scenes, we can't reliably compute a speed factor between the
  // two videos...

  const FrameSpan first_to_second_scene_transition =
      compute_symetric_span_around_keyframe(*it, *std::next(it), 5);

  assert(basematch.size() >= first_to_second_scene_transition.size());

  MatchingArea local_match = find_best_matching_area_ex(first_to_second_scene_transition, basematch);

  size_t vid1_sc = local_match.pattern.startOffset() + local_match.pattern.size() / 2;
  size_t vid2_sc = local_match.match.startOffset() + local_match.match.size() / 2;

  // we then extend the match from both ends.

  FrameSpan refined_pattern = basepattern;
  FrameSpan refined_match = basematch;

  // TODO: if pattern ends with a scene change (and no black frames),
  // we should try to include (or exclude) frames from the match to match
  // the scene change.
  // that way we can compute a speed prior.
  // if pattern both ends and stars with a fade to black we are toast and
  // can't do anything.

  std::optional<double> speed;
  const std::pair plausible_speedrange{0.95, 1.05};

  if (!ends_with_black_frames(refined_pattern))
  {
    const size_t nb_frames_sc2 = std::next(it)->size();
    const auto plausible_frame_range =
        std::pair<size_t, size_t>(std::ceil(nb_frames_sc2 * plausible_speedrange.first),
                                  std::floor(nb_frames_sc2 * plausible_speedrange.second));

    FrameSpan search_span{*basematch.video,
                          vid2_sc + plausible_frame_range.first,
                          plausible_frame_range.second - plausible_frame_range.first};
    std::vector<FrameSpan> scenes_vid2 = split_at_scframes(search_span);
    for (const FrameSpan& span : scenes_vid2)
    {
      if (likely_same_scene(*std::next(it), span))
      {
        refined_match.moveEndOffset(span.endOffset());
      }
      else
      {
        break;
      }
    }

    const double speed_estimate = [&]() {
      double video1_realtime = (basepattern.endOffset() - vid1_sc) * get_frame_delta(first_video);
      double video2_realtime = (refined_match.endOffset() - vid2_sc)
                               * get_frame_delta(second_video);
      assert(video1_realtime > 0);
      assert(video2_realtime > 0);
      return video2_realtime / video1_realtime;
    }();

    // TODO: throw refinement away if speed_estimate is too bad

    speed = speed_estimate;
  }

  if (!starts_with_black_frames(refined_pattern))
  {
    const size_t nb_frames_vid1 = it->size();
    const auto plausible_nbframes_range_vid2 =
        std::pair<size_t, size_t>(std::ceil(nb_frames_vid1 * plausible_speedrange.first),
                                  std::floor(nb_frames_vid1 * plausible_speedrange.second));

    FrameSpan search_span{*basematch.video,
                          vid2_sc - 1 - plausible_nbframes_range_vid2.second,
                          plausible_nbframes_range_vid2.second
                              - plausible_nbframes_range_vid2.first};
    std::vector<FrameSpan> scenes_vid2 = split_at_scframes(search_span);
    std::reverse(scenes_vid2.begin(), scenes_vid2.end());
    for (const FrameSpan& span : scenes_vid2)
    {
      if (likely_same_scene(*std::next(it), span))
      {
        refined_match.moveStartOffsetTo(span.startOffset());
      }
      else
      {
        break;
      }
    }

    const double speed_estimate = [&]() {
      double video1_realtime = (vid1_sc - basepattern.startOffset()) * get_frame_delta(first_video);
      double video2_realtime = (vid2_sc - refined_match.startOffset())
                               * get_frame_delta(second_video);
      assert(video1_realtime > 0);
      assert(video2_realtime > 0);
      return video2_realtime / video1_realtime;
    }();

    // TODO: throw refinement away if speed_estimate is too bad

    speed = speed_estimate;
  }

  if (speed.has_value())
  {
    if (starts_with_black_frames(refined_pattern))
    {
      auto [vid1_start, vid2_start] = find_match_end_backward(first_video,
                                                              vid1_sc - 1,
                                                              second_video,
                                                              vid2_sc - 1,
                                                              *speed,
                                                              refined_pattern.startOffset(),
                                                              fullSearchArea.startOffset());
      refined_pattern.moveStartOffsetTo(vid1_start);
      refined_match.moveStartOffsetTo(vid2_start);
    }

    if (ends_with_black_frames(refined_pattern))
    {
      auto [vid1_end, vid2_end] = find_match_end(first_video,
                                                 vid1_sc,
                                                 second_video,
                                                 vid2_sc,
                                                 *speed,
                                                 refined_pattern.endOffset(),
                                                 fullSearchArea.endOffset());
      refined_pattern.moveEndOffset(vid1_end);
      refined_match.moveEndOffset(vid2_end);
    }
  }

  return std::pair(refined_pattern, refined_match);
}

std::pair<FrameSpan, FrameSpan> refine_match(std::vector<FrameSpan>::const_iterator begin,
                                             std::vector<FrameSpan>::const_iterator end,
                                             const FrameSpan& basematch,
                                             const FrameSpan& fullSearchArea)
{
  // If we enter this function, we know that frames covered by [begin, end) roughly
  // match frames in "basematch".
  // The job of this function is to adjust the match near its start and end.

  assert(begin != end);

  const VideoInfo& first_video = *(begin->video);
  const VideoInfo& second_video = *basematch.video;
  const FrameSpan basepattern = merge(*begin, *std::prev(end));

  if (std::distance(begin, end) < 3)
  {
    // we have only one or two scenes in [begin, end).

    if (std::distance(begin, end) == 2)
    {
      return refine_match_2scenes(begin, basematch, fullSearchArea);
    }

    // TODO: handle the 1 scene case
    // happens in ep34
    return std::pair(basepattern, basematch);
  }

  // we are going to focus around the scene changes at the beginning and the end.

  // first, we compute matching frames between the two videos around the first scene change
  size_t vid1_first_sc;
  size_t vid2_first_sc;
  {
    const FrameSpan first_to_second_scene_transition =
        compute_symetric_span_around_keyframe(*begin, *std::next(begin), 5);

    const size_t search_area_size = number_of_frames_in_range(begin, std::next(begin, 3));

    MatchingArea local_match = find_best_matching_area_ex(first_to_second_scene_transition,
                                                          basematch.left(search_area_size));

    if (local_match.score > area_match_threshold)
    {
      // happens in S1E46
      qDebug() << "please verify the match near" << local_match.pattern << "~" << local_match.match
               << QString(" (score=%1)").arg(local_match.score);
    }

    vid1_first_sc = local_match.pattern.startOffset() + local_match.pattern.size() / 2;
    vid2_first_sc = local_match.match.startOffset() + local_match.match.size() / 2;
  }

  // then, we do the same around the last scene change
  size_t vid1_last_sc;
  size_t vid2_last_sc;
  {
    const FrameSpan penultimate_to_last_scene_transition =
        compute_symetric_span_around_keyframe(*std::prev(end, 2), *std::prev(end), 5);
    const size_t search_area_size = number_of_frames_in_range(std::prev(end, 3), end);
    MatchingArea local_match = find_best_matching_area_ex(penultimate_to_last_scene_transition,
                                                          basematch.right(search_area_size));
    if (local_match.score > area_match_threshold)
    {
      // happens in S1E46
      qDebug() << "please verify the match near" << local_match.pattern << "~" << local_match.match
               << QString(" (score=%1)").arg(local_match.score);
    }

    vid1_last_sc = local_match.pattern.startOffset() + local_match.pattern.size() / 2;
    vid2_last_sc = local_match.match.startOffset() + local_match.match.size() / 2;
  }

  // based on that, we can estimate a speed adjustment ratio between the two videos
  const double speed = [&]() {
    double video1_realtime = (vid1_last_sc - vid1_first_sc) * get_frame_delta(first_video);
    double video2_realtime = (vid2_last_sc - vid2_first_sc) * get_frame_delta(second_video);
    assert(video1_realtime > 0);
    assert(video2_realtime > 0);
    return video2_realtime / video1_realtime;
  }();

  // we then extend the match from both ends.

  FrameSpan refined_pattern = basepattern;
  FrameSpan refined_match = basematch;

  // starting with the end
  {
    auto [vid1_end, vid2_end] = find_match_end(first_video,
                                               vid1_last_sc,
                                               second_video,
                                               vid2_last_sc,
                                               speed,
                                               refined_pattern.endOffset(),
                                               fullSearchArea.endOffset());
    refined_pattern.moveEndOffset(vid1_end);
    refined_match.moveEndOffset(vid2_end);
  }

  // then with the beginning
  {
    auto [vid1_start, vid2_start] = find_match_end_backward(first_video,
                                                            vid1_first_sc - 1,
                                                            second_video,
                                                            vid2_first_sc - 1,
                                                            speed,
                                                            refined_pattern.startOffset(),
                                                            fullSearchArea.startOffset());
    refined_pattern.moveStartOffsetTo(vid1_start);
    refined_match.moveStartOffsetTo(vid2_start);
  }

  return std::pair(refined_pattern, refined_match);
}

std::pair<FrameSpan, FrameSpan> find_best_subspan_match(const FrameSpan& pattern,
                                                        const FrameSpan& searchArea)
{
  if (debugmatches)
  {
    qDebug() << "S:" << pattern << " A:" << searchArea;
  }

  std::pair<FrameSpan, FrameSpan> result;

  const std::vector<FrameSpan> patspans = split_at_scframes(pattern);
  // for (const FrameSpan& s : patspans)
  // {
  //   qDebug() << get_nth_frame_pts(*pattern.video, s.startOffset());
  // }

  auto it = patspans.begin();
  while (it != patspans.end())
  {
    {
      const size_t nb_frames_remaining = std::accumulate(it,
                                                         patspans.end(),
                                                         size_t(0),
                                                         [](size_t n, const FrameSpan& e) {
                                                           return n + e.size();
                                                         });

      if (nb_frames_remaining < result.first.size())
      {
        // there is no way we can do better with what remains, return now!
        break;
      }
    }

    MatchingArea m;

    if (std::next(it) != patspans.end())
    {
      // This "extended_pattern" is a fix for episode 31.
      // The segment starting at (7:02,eng,frame#10560) and ending at (7:04,eng) gets matched
      // with the one starting at (8:04,fre) by find_best_matching_area_ex() which
      // in turns causes extend_match() to fail on subsequent segments.
      // Correct: (7:02,eng) matches with (7:19,fre) ; (8:04,fre) matches with (7:45,eng).
      // By extending the pattern to the next segment, we reduce the probability of an erroneous match.
      // The idea is more or less always the same: the bigger the pattern, the less likely
      // to match the wrong frames.
      const FrameSpan extended_pattern{*it->video,
                                       it->startOffset(),
                                       std::next(it)->endOffset() - it->startOffset()};
      m = find_best_matching_area_ex(extended_pattern, searchArea);
      const size_t pattern_extra_size = std::next(it)->size();
      m.pattern.count -= pattern_extra_size;
      m.match.count -= pattern_extra_size;
    }
    else
    {
      m = find_best_matching_area_ex(*it, searchArea);
    }

    if (m.score > area_match_threshold)
    {
      if (debugmatches)
      {
        qDebug() << "  X" << *it;
      }

      ++it;
      continue;
    }

    if (debugmatches)
    {
      qDebug() << " >" << m.pattern << " ~ " << m.match;
    }

    auto [end_it, last_match_from_sa] = extend_match(m,
                                                     std::next(it),
                                                     patspans.end(),
                                                     searchArea.endOffset());

    //   FrameSpan last_match_from_pattern = *std::prev(end_it);
    //   m.pattern.count = last_match_from_pattern.endOffset() - m.pattern.startOffset();
    m.match.count = last_match_from_sa.endOffset() - m.match.startOffset();

    if (debugmatches)
    {
      qDebug() << "  >>" << merge(*it, *std::prev(end_it)) << " ~ " << m.match;
    }

    std::tie(m.pattern, m.match) = refine_match(it, end_it, m.match, searchArea);

    if (debugmatches)
    {
      qDebug() << "  >>>" << m.pattern << " ~ " << m.match;
    }

    if (m.pattern.count > result.first.count)
    {
      // if (result.first.count > 0)
      // {
      //   qDebug() << "    discarding " << result.first << "~" << result.second << " in favour of "
      //            << m.pattern << "~" << m.match;
      // }

      result = std::pair(m.pattern, m.match);
    }

    it = end_it;
  }

  return result;
}

size_t find_segment_end(const VideoInfo& video, size_t start)
{
  const size_t vend = get_number_of_frames(video);

  if (video.frames.at(start).excluded)
  {
    while (start < vend && video.frames.at(start).excluded)
      ++start;
    return start;
  }

  size_t s = find_next_silence(video, start);
  const size_t e = find_next_excluded(video, start);
  if (e <= s)
    return e;

  size_t segend = s;
  while (segend != vend)
  {
    const size_t scf = find_next_scframe(video, segend);
    const size_t bf = find_next_blackframe(video, segend);
    const size_t silence_end = find_silence_end(video, segend);

    if (std::min(scf, bf) <= silence_end)
    {
      if (scf <= silence_end)
      {
        segend = scf;
      }
      else
      {
        segend = bf;
      }

      break;
    }
    else
    {
      s = find_next_silence(video, segend);
      if (e <= s)
        return e;

      segend = s;
    }
  }

  return segend;
}

std::vector<FrameSpan> extract_segments(const VideoInfo& video)
{
  std::vector<FrameSpan> result;

  size_t i = 0;
  while (i < get_number_of_frames(video))
  {
    const size_t segment_start = i;
    const size_t segment_end = find_segment_end(video, segment_start);
    result.push_back(FrameSpan(video, segment_start, segment_end - segment_start));
    i = segment_end;
  }

  return result;
}

std::vector<std::pair<FrameSpan, FrameSpan>> find_forced_matches(
    const FrameSpan& segment,
    const std::vector<std::pair<TimeWindow, TimeWindow>>& forcedMatches,
    const VideoInfo& audio)
{
  std::vector<std::pair<FrameSpan, FrameSpan>> result;

  for (const auto& p : forcedMatches)
  {
    auto spans = std::pair(get_span(*segment.video, p.first), get_span(audio, p.second));

    if (segment.contains(spans.first))
    {
      result.push_back(spans);
    }
  }

  return result;
}

class Dubber
{
private:
  const VideoInfo& m_video;
  int m_curpts = 0;

public:
  std::vector<OutputSegment> result;

public:
  explicit Dubber(const VideoInfo& inputVideo)
      : m_video(inputVideo)
  {}

  void dub(const FrameSpan& videoSegment, const FrameSpan& audio)
  {
    assert(videoSegment.video == &m_video);

    if (videoSegment.size() == 0)
    {
      return;
    }

    qDebug() << "M:" << videoSegment << "~" << audio;

    // on crée un segment pour rejoindre le match
    if (m_video.frames.at(videoSegment.startOffset()).pts > m_curpts)
    {
      OutputSegment oseg;
      oseg.pts = m_curpts;
      oseg.duration = m_video.frames.at(videoSegment.startOffset()).pts - m_curpts;
      oseg.input.src = 0;
      // segment.input.speed = 1;
      oseg.input.start = m_curpts * get_frame_delta(m_video);
      oseg.input.end = (oseg.pts + oseg.duration) * get_frame_delta(m_video);
      result.push_back(oseg);

      m_curpts += oseg.duration;
    }

    if (videoSegment.size() > 0)
    {
      OutputSegment oseg;
      oseg.pts = m_curpts;
      oseg.duration = get_nth_frame_pts(m_video, videoSegment.endOffset()) - m_curpts;
      oseg.input.src = 1;
      oseg.input.start = audio.video->frames.at(audio.startOffset()).pts
                         * get_frame_delta(*audio.video);
      //  segment.input.end = segment.input.start + (segment.duration * get_frame_delta(a));
      oseg.input.end = get_nth_frame_pts(*audio.video, audio.endOffset())
                       * get_frame_delta(*audio.video);
      // segment.input.speed = (segment.input.end - segment.input.start)
      //                       / (segment.duration * a.framedelta());
      result.push_back(oseg);

      m_curpts += oseg.duration;
    }
  }

  void writeFinalSegment()
  {
    if (m_curpts < m_video.frames.back().pts)
    {
      const int duration = m_video.frames.back().pts + 1 - m_curpts;
      const double secs = duration * get_frame_delta(m_video);
      if (secs >= 0.250)
      {
        // on ajoute un segment pour terminer la video
        OutputSegment segment;
        segment.pts = m_curpts;
        segment.duration = duration;
        segment.input.src = 0;
        //segment.input.speed = 1;
        segment.input.start = m_curpts * get_frame_delta(m_video);
        segment.input.end = (segment.pts + segment.duration) * get_frame_delta(m_video);
        result.push_back(segment);

        m_curpts += segment.duration;
      }
    }
  }
};

std::vector<OutputSegment> compute_dub(
    const VideoInfo& a,
    const VideoInfo& b,
    const std::vector<std::pair<TimeWindow, TimeWindow>>& forcedMatches)
{
  Dubber dubber{a};

  FrameSpan search_area{b, 0, b.frames.size()};

  const std::vector<FrameSpan> segments = extract_segments(a);

  // for (const FrameSpan& segment : segments)
  // {
  //   qDebug() << segment;
  // }

  for (const FrameSpan& segment : segments)
  {
    assert(segment.size() > 0);

    if (!segment.at(0).excluded)
    {
      std::pair<FrameSpan, FrameSpan> matchingspans = find_best_subspan_match(segment, search_area);

      if (matchingspans.first.count > 0)
      {
        dubber.dub(matchingspans.first, matchingspans.second);
        search_area = FrameSpan(b, matchingspans.second.endOffset(), -1);
      }
    }
    else
    {
      std::vector<std::pair<FrameSpan, FrameSpan>> fmatches = find_forced_matches(segment,
                                                                                  forcedMatches,
                                                                                  b);

      for (const auto& p : fmatches)
      {
        dubber.dub(p.first, p.second);
      }

      if (!fmatches.empty())
      {
        search_area = FrameSpan(b, fmatches.back().second.endOffset(), -1);
      }
    }

    while (search_area.startOffset() > 0
           && search_area.video->frames.at(search_area.startOffset() - 1).reusable)
    {
      search_area.widenLeft(1);
    }
  }

  dubber.writeFinalSegment();

  return dubber.result;
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
  QString command;
  VideoInfo firstVideo;
  VideoInfo secondVideo;
  QString outputPath;
  std::vector<TimeWindow> excludedSegments;
  std::vector<TimeWindow> noBreakSegments;
  std::vector<std::pair<TimeWindow, TimeWindow>> forcedMatches;
  std::vector<TimeWindow> reusableSegments;
  std::optional<double> scThreshold;
  std::optional<double> areaMatchThreshold;
  std::optional<int> frameUnmatchThreshold;
  bool dryRun = false;
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

void digidub(VideoInfo& video,
             VideoInfo& audioSource,
             const QString& userProvidedOutputPath,
             const std::vector<TimeWindow>& excludedSegments,
             const std::vector<TimeWindow>& noBreakSegments,
             const std::vector<std::pair<TimeWindow, TimeWindow>>& forcedMatches,
             const std::vector<TimeWindow>& reusableSegments,
             std::optional<double> scThreshold,
             bool dryRun = false)
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

  fetch_silences(video);
  silenceborders(video.frames);

  fetch_black_frames(video);
  //fetch_black_frames(audioSource);

  fetch_sc(video);
  filter_sc(video, scThreshold.value_or(0.));
  mark_sc_frames(video);
  //fetch_sc_frames(audioSource);

  merge_small_scenes(video, 7);
  //merge_small_scenes(audioSource, 7);

  mark_excluded_frames(video, excludedSegments);
  for (const auto& p : forcedMatches)
  {
    mark_excluded_frames(video, {p.first});
  }
  prevent_breaks_at_frames(video, noBreakSegments);

  mark_frames(audioSource, reusableSegments, [](VideoFrameInfo& frame) { frame.reusable = true; });

  std::vector<OutputSegment> segments = compute_dub(video, audioSource, forcedMatches);
  qDebug() << segments.size() << "segments";
  for (const auto& s : segments)
  {
    qDebug() << s;
  }

  if (dryRun)
  {
    qApp->exit();
    return;
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
    if (seg.input.src == 0)
    {
      QStringList args;
      args << "-y";

      args << "-i" << source_audio1_path;

      args << "-ss" << QString::number(seg.input.start);
      args << "-to" << QString::number(seg.input.end);

      const QString outputname = QString::number(i++) + ".wav";
      QString outputpath = tempdir.filePath(outputname);
      args << outputpath;

      run_ffmpeg(args);

      mylist.write(QString("file '%1'\n").arg(outputname).toUtf8());
    }
    else
    {
      const int num = i++;
      QStringList args;
      args << "-y";
      args << "-i" << source_audio2_path;
      args << "-ss" << QString::number(seg.input.start);
      args << "-to" << QString::number(seg.input.end);
      QString outputpath = tempdir.filePath(QString::number(num) + "-orig.wav");
      args << outputpath;
      run_ffmpeg(args);

      args.clear();
      args << "-y";
      args << "-i" << outputpath;

      const QString outputname = QString::number(num) + ".wav";
      outputpath = tempdir.filePath(outputname);
      // apply speed ratio
      {
        double ratio = (seg.input.end - seg.input.start) / (seg.duration * get_frame_delta(video));
        args << "-filter:a";
        args << QString("atempo=%1").arg(QString::number(ratio));
      }
      args << outputpath;
      run_ffmpeg(args);

      mylist.write(QString("file '%1'\n").arg(outputname).toUtf8());
    }
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
          std::cout << "Found subtitles file: " << subspath.toStdString() << std::endl;

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
  if (gProgramData.command == "dub")
  {
    if (gProgramData.firstVideo.filePath.isEmpty() || gProgramData.secondVideo.filePath.isEmpty())
    {
      qDebug() << "2 videos must be provided";
      return qApp->exit(1);
    }

    if (gProgramData.areaMatchThreshold.has_value())
    {
      area_match_threshold = gProgramData.areaMatchThreshold.value();
    }

    if (gProgramData.frameUnmatchThreshold.has_value())
    {
      frame_unmatch_threshold = gProgramData.frameUnmatchThreshold.value();
    }

    digidub(gProgramData.firstVideo,
            gProgramData.secondVideo,
            gProgramData.outputPath,
            gProgramData.excludedSegments,
            gProgramData.noBreakSegments,
            gProgramData.forcedMatches,
            gProgramData.reusableSegments,
            gProgramData.scThreshold,
            gProgramData.dryRun);
  }
  else if (gProgramData.command == "silencedetect")
  {
    fetch_silences(gProgramData.firstVideo);

    if (gProgramData.firstVideo.silences.empty())
    {
      std::cout << "no silences detected.";
      return qApp->exit(0);
    }

    std::cout << "detected " << gProgramData.firstVideo.silences.size()
              << " silences:" << std::endl;

    for (const TimeWindow& w : gProgramData.firstVideo.silences)
    {
      std::cout << w << std::endl;
    }

    return qApp->exit(0);
  }
  else if (gProgramData.command == "blackdetect")
  {
    fetch_black_frames(gProgramData.firstVideo);

    if (gProgramData.firstVideo.blackframes.empty())
    {
      std::cout << "no black frames detected.";
      return qApp->exit(0);
    }

    std::cout << "detected " << gProgramData.firstVideo.blackframes.size()
              << " black frames:" << std::endl;

    for (const TimeWindow& w : gProgramData.firstVideo.blackframes)
    {
      std::cout << w << std::endl;
    }

    return qApp->exit(0);
  }
  else if (gProgramData.command == "scdet")
  {
    fetch_sc(gProgramData.firstVideo);
    filter_sc(gProgramData.firstVideo, gProgramData.scThreshold.value_or(0.));

    std::cout << "detected " << gProgramData.firstVideo.scenechanges.size()
              << " scene changes:" << std::endl;

    for (const SceneChange& sc : gProgramData.firstVideo.scenechanges)
    {
      std::cout << formatSeconds(sc.time).toStdString() << " (score=" << sc.score << ")"
                << std::endl;
    }

    return qApp->exit(0);
  }
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

double parse_timestamp(const QString& text)
{
  QStringList parts = text.split(':');

  if (parts.size() > 3)
  {
    throw std::runtime_error("bad timestamp format");
  }

  const double seconds = parts.back().toDouble();
  parts.pop_back();

  int minutes = 0;

  if (!parts.empty())
  {
    QString p = parts.back();
    parts.pop_back();

    if (p.length() > 2)
    {
      throw std::runtime_error("bad timestamp format");
    }

    if (p.startsWith('0'))
    {
      p = p.mid(1);
    }

    minutes = p.toInt();
  }

  int hours = 0;

  if (!parts.empty())
  {
    QString p = parts.back();
    parts.pop_back();

    while (p.startsWith('0'))
    {
      p = p.mid(1);
    }

    hours = p.toInt();
  }

  return hours * 3600 + minutes * 60 + seconds;
}

TimeWindow parse_timespan(const QString& arg)
{
  const std::pair<QString, QString> start_end = [&arg]() {
    QStringList parts = arg.split('-');
    if (parts.size() != 2)
    {
      throw std::runtime_error("bad format for timewindow");
    }

    return std::pair(parts.front().simplified(), parts.back().simplified());
  }();

  const double start = parse_timestamp(start_end.first);
  const double end = parse_timestamp(start_end.second);
  return TimeWindow::fromStartAndEnd(start, end);
}

void parse_timespan_arg(std::vector<TimeWindow>& output, const QString& arg)
{
  output.push_back(parse_timespan(arg));
}

void parse_forcematch_arg(std::vector<std::pair<TimeWindow, TimeWindow>>& output, const QString& arg)
{
  const std::pair<QString, QString> p = [&arg]() {
    QStringList parts = arg.split('~');
    if (parts.size() != 2)
    {
      throw std::runtime_error("bad format for --force-match arg");
    }

    return std::pair(parts.front().simplified(), parts.back().simplified());
  }();

  TimeWindow first = parse_timespan(p.first);
  TimeWindow second = parse_timespan(p.second);
  output.push_back(std::pair(first, second));
}

void parse_dub_args(ProgramData& pd, const QStringList& args)
{
  for (int i(0); i < args.size(); ++i)
  {
    const QString& arg = args.at(i);

    if (arg == "-o")
    {
      pd.outputPath = args.at(++i);
    }
    else if (arg == "--with")
    {
      set_video_arg(pd.secondVideo, args.at(++i));
    }
    else if (arg == "--exclude")
    {
      parse_timespan_arg(pd.excludedSegments, args.at(++i));
    }
    else if (arg == "--no-break")
    {
      parse_timespan_arg(pd.noBreakSegments, args.at(++i));
    }
    else if (arg == "--force-match")
    {
      parse_forcematch_arg(pd.forcedMatches, args.at(++i));
    }
    else if (arg == "--reusable")
    {
      parse_timespan_arg(pd.reusableSegments, args.at(++i));
    }
    else if (arg == "--sc-threshold")
    {
      bool ok;
      pd.scThreshold = args.at(++i).toDouble(&ok);
      if (!ok)
      {
        qDebug() << "could not parse double argument: " << args.at(i);
        std::exit(1);
      }
    }
    else if (arg == "--area-match-threshold")
    {
      bool ok;
      pd.areaMatchThreshold = args.at(++i).toDouble(&ok);
      if (!ok)
      {
        qDebug() << "could not parse double argument: " << args.at(i);
        std::exit(1);
      }
    }
    else if (arg == "--frame-unmatch-threshold")
    {
      bool ok;
      pd.frameUnmatchThreshold = args.at(++i).toInt(&ok);
      if (!ok)
      {
        qDebug() << "could not parse int argument: " << args.at(i);
        std::exit(1);
      }
    }
    else if (arg == "--dry-run")
    {
      pd.dryRun = true;
    }
    else if (arg == "--debug-matches")
    {
      debugmatches = true;
    }
    else if (!args.startsWith("-"))
    {
      set_video_arg(pd.firstVideo, args.at(i));
    }
    else
    {
      qDebug() << "unknown arg: " << arg;
      std::exit(1);
    }
  }
}

void parse_single_video_arg(ProgramData& pd, const QStringList& args)
{
  for (int i(0); i < args.size(); ++i)
  {
    const QString& arg = args.at(i);

    if (QFileInfo(arg).exists())
    {
      set_video_arg(pd.firstVideo, arg);
    }
    else
    {
      qDebug() << "unknown arg: " << arg;
      std::exit(1);
    }
  }
}

void parse_scdet_args(ProgramData& pd, const QStringList& args)
{
  for (int i(0); i < args.size(); ++i)
  {
    const QString& arg = args.at(i);

    if (arg == "--sc-threshold")
    {
      bool ok;
      pd.scThreshold = args.at(++i).toDouble(&ok);
      if (!ok)
      {
        qDebug() << "could not parse double argument: " << args.at(i);
        std::exit(1);
      }
    }
    else if (!args.startsWith("-"))
    {
      set_video_arg(pd.firstVideo, args.at(i));
    }
    else
    {
      qDebug() << "unknown arg: " << arg;
      std::exit(1);
    }
  }
}

void showDubHelp()
{
  std::cout << "dub a video using audio from another video." << std::endl;
  std::cout << "a perceptual hash (phash) algorithm is used to match frames between the first and "
               "second video so that audio can be extracted and synced."
            << std::endl;

  std::cout << std::endl;

  std::cout << "Syntax:" << std::endl;
  std::cout << "digidub dub <main-video.mkv> --with <secondary-video.mkv> [options]" << std::endl;

  std::cout << std::endl;

  constexpr const char* opts = R"(
  -o <output.mkv>                      Specify an output file name.
  --exclude <timewindow-spec>          Exclude part of the video from the dubbing process.
  --sc-threshold <value>               Change the scene-change threshold.
  --no-break <timewindow-spec>         Prevent a break within the specified time window.
  --area-match-threshold <value>       Change the area match threshold.
  --frame-unmatch-threshold <value>    Change the frame unmatch threshold.
  --reusable <timewindow-spec>         Specify that some audio can be reused.
  --force-match <match-spec>           Force a match.
  --debug-matches                      Print debugging information about the matches.
  --dry-run                            Do not produce the output file.
)";

  std::cout << "Options:" << opts << std::endl;

  std::cout << "Parameter syntax:" << std::endl;
  std::cout << "  <time-spec>       --> mm:ss.zzz" << std::endl;
  std::cout << "  <timewindow-spec> --> <time-spec>-<time-spec>" << std::endl;
  std::cout << "  <match-spec>      --> <timewindow-spec>~<timewindow-spec>" << std::endl;

  std::cout << std::endl;
}

void showSilenceDetectHelp()
{
  constexpr const char* des = R"(
Silences are used to split the video to dub into segments that are then
individually dubbed.
)";
  std::cout << "Performs silence detection on a video and prints the result." << des << std::endl;

  std::cout << "Syntax:" << std::endl;
  std::cout << "digidub silencedetect <video.mkv>" << std::endl;
}

void showBlackDetectHelp()
{
  constexpr const char* des = R"(
Blacks frames are used to split the video to dub into segments that are then
individually dubbed.
)";
  std::cout << "Performs black frame detection on a video and prints the result." << des
            << std::endl;

  std::cout << "Syntax:" << std::endl;
  std::cout << "digidub blackdetect <video.mkv>" << std::endl;
}

void showScdetHelp()
{
  constexpr const char* des = R"(
While silences and black frames are used to split the video into segments,
scene changes are used to split segments into scenes.
Scenes are then matched between the two input videos.
)";
  std::cout << "Performs scene-change detection on a video and prints the result." << des
            << std::endl;

  std::cout << "Syntax:" << std::endl;
  std::cout << "digidub scdet <video.mkv>" << std::endl;
}

void showHelp(const QStringList& args)
{
  if (args.size() > 1)
  {
    if (args.at(1) == "dub")
    {
      return showDubHelp();
    }
    else if (args.at(1) == "silencedetect")
    {
      return showSilenceDetectHelp();
    }
    else if (args.at(1) == "blackdetect")
    {
      return showBlackDetectHelp();
    }
    else if (args.at(1) == "scdet")
    {
      return showScdetHelp();
    }
  }

  std::cout << "digidub is a Qt-based commandline program for dubbing a video with audio from "
               "another video."
            << std::endl;
  std::cout
      << "digidub uses a perceptual hash (phash) algorithm to find similar frames between the "
         "video to dub and the video used as audio source in order to extract and sync the audio."
      << std::endl;
  std::cout << "The program assumes that FFmpeg and mkvmerge are installed on your system."
            << std::endl;

  std::cout << std::endl;

  std::cout << "Main syntax:" << std::endl;
  std::cout << "digidub dub <main-video.mkv> --with <secondary-video.mkv> [-o <output.mkv>]"
            << std::endl;

  std::cout << std::endl;

  std::cout << "Commands:" << std::endl;
  std::cout << "  dub              dub a video with audio from another video" << std::endl;
  std::cout << "  silencedetect    performs silence detection on a video" << std::endl;
  std::cout << "  blackdetect      performs black frames detection on a video" << std::endl;
  std::cout << "  scdet            performs scene-change detection on a video" << std::endl;
}

int main(int argc, char *argv[])
{
  QApplication::setApplicationName("digidub");

  QApplication app{argc, argv};

  ProgramData& pd = gProgramData;

  const QStringList& args = app.arguments();

  if (args.contains("-h") || args.contains("--help") || args.size() == 1)
  {
    showHelp(args);
    return 0;
  }

  if (args.contains("-v") || args.contains("--version"))
  {
    std::cout << VERSION_STRING << std::endl;
    return 0;
  }

  if (args.at(1) == "dub")
  {
    pd.command = "dub";
    parse_dub_args(pd, args.mid(2));
  }
  else if (args.at(1) == "silencedetect" || args.at(1) == "blackdetect")
  {
    pd.command = args.at(1);
    parse_single_video_arg(pd, args.mid(2));
  }
  else if (args.at(1) == "scdet")
  {
    pd.command = args.at(1);
    parse_scdet_args(pd, args.mid(2));
  }
  else
  {
    std::cerr << "Unknown command: " << args.at(1).toStdString();
    return 1;
  }

  invoke_main();

  return app.exec();
}
