#include "matchalgo.h"

#include "mediaobject.h"
#include "phash.h"

#include <QDebug>

#include <algorithm>
#include <type_traits>

bool debugmatches = true;

inline int get_nth_frame_pts(const MatchAlgo::Video& video, size_t n)
{
  return n < video.frames.size() ? video.frames.at(n).pts : (video.frames.back().pts + 1);
}

QString formatSeconds(double val)
{
  int minutes = int(val) / 60;
  double seconds = int(val) % 60;
  seconds += (val - int(val));

  const char* fmtstr = seconds < 10 ? "%1:0%2" : "%1:%2";
  return QString(fmtstr).arg(QString::number(minutes), QString::number(seconds));
}

QDebug operator<<(QDebug dbg, const MatchAlgo::FrameSpan& object)
{
  constexpr bool showpts = false;
  const double df = object.video->frameDelta;
  dbg.noquote().nospace();
  dbg << object.video->media->fileName();
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

namespace MatchAlgo {

template<typename Fun>
void mark_frames(std::vector<Frame>& frames, double frameDelta, const TimeSegment& window, Fun&& fun)
{
  // TODO: use uint64_t instead?
  auto get_frame_timestamp = [&](const Frame& frame) -> double { return frame.pts * frameDelta; };

  auto within_window = [&get_frame_timestamp, window](const Frame& frame) {
    const double t = get_frame_timestamp(frame);
    return window.contains(t * 1000);
  };

  auto it = std::lower_bound(frames.begin(),
                             frames.end(),
                             window.start() / double(1000),
                             [&get_frame_timestamp](const Frame& e, double val) {
                               return get_frame_timestamp(e) < val;
                             });

  for (; it != frames.end(); ++it)
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
void mark_frames(std::vector<Frame>& frames,
                 double frameDelta,
                 const std::vector<TimeSegment>& windows,
                 Fun&& fun)
{
  for (const TimeSegment& w : windows)
  {
    mark_frames(frames, frameDelta, w, std::forward<Fun>(fun));
  }
}

void mark_silence_frames(Video& video)
{
  auto mark_silence = [](Frame& frame) { frame.silence = true; };
  mark_frames(video.frames, video.frameDelta, video.media->silenceInfo()->silences, mark_silence);
}

void silenceborders(std::vector<Frame>& frames, size_t n = 10)
{
  auto silence_frame = [](Frame& f) { f.silence = true; };

  // silence frames at the beginning if there is some silence nearby
  {
    auto it = std::find_if(frames.begin(), frames.begin() + n, [](const Frame& f) {
      return f.silence;
    });

    if (it != frames.begin() + n)
    {
      std::for_each(frames.begin(), it, silence_frame);
    }
  }

  // silence frames at the end if there is some silence nearby
  {
    auto it = std::find_if(frames.rbegin(), frames.rbegin() + n, [](const Frame& f) {
      return f.silence;
    });

    if (it != frames.rbegin() + n)
    {
      std::for_each(frames.rbegin(), it, silence_frame);
    }
  }
}

void mark_black_frames(Video& video)
{
  auto mark_black = [](Frame& frame) { frame.black = true; };
  mark_frames(video.frames,
              video.frameDelta,
              video.media->blackFramesInfo()->blackframes,
              mark_black);
}

void mark_sc_frames(Video& video, double threshold)
{
  Q_ASSERT(!video.frames.empty());

  auto get_frame_timestamp = [&video](const Frame& frame) { return frame.pts * video.frameDelta; };

  for (const SceneChange& e : video.media->scenesInfo()->scenechanges)
  {
    if (e.score < threshold)
    {
      continue;
    }

    auto it = std::lower_bound(video.frames.begin(),
                               video.frames.end(),
                               e.time,
                               [&get_frame_timestamp](const Frame& e, double val) {
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

void merge_small_scenes(Video& video, size_t minSize)
{
  auto find_next_scene =
      [&video](std::vector<Frame>::iterator from) -> std::vector<Frame>::iterator {
    if (from->scscore > 0)
    {
      ++from;
    }
    return std::find_if(from, video.frames.end(), [](const Frame& f) { return f.scscore > 0; });
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
      it->scscore = 0;
      it = next;
      break;
    }

    if (next->scscore < it->scscore)
    {
      next->scscore = 0;
    }
    else
    {
      it->scscore = 0;
      it = next;
    }
  }
}

inline int phashDist(const Frame& a, const Frame& b)
{
  return ::phashDist(a.phash, b.phash);
}

inline bool is_sc_frame(const Frame& frame)
{
  return frame.scscore > 0;
}

size_t find_silence_end(const FrameSpan& frames, size_t i)
{
  //get out of silence if we are in one.
  while (i < frames.size() && frames.at(i).silence)
  {
    ++i;
  }

  return i;
}

size_t find_next_silence(const FrameSpan& frames, size_t from = 0)
{
  // first, get out of silence if we are in one.
  size_t i = find_silence_end(frames, from);

  // then, go to next frame that is silence
  while (i < frames.size() && !frames.at(i).silence)
  {
    ++i;
  }

  return i;
}

size_t find_next_blackframe(const FrameSpan& frames, size_t from = 0)
{
  size_t i = from + 1;

  while (i < frames.size())
  {
    if (frames.at(i).black)
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

size_t find_next_scframe(const FrameSpan& frames, size_t from = 0)
{
  size_t i = from + 1;

  while (i < frames.size())
  {
    if (is_sc_frame(frames.at(i)))
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

size_t find_segment_end(const FrameSpan& frames, size_t start)
{
  size_t s = find_next_silence(frames, start);

  size_t segend = s;
  while (segend != frames.size())
  {
    const size_t scf = find_next_scframe(frames, segend);
    const size_t bf = find_next_blackframe(frames, segend);
    const size_t silence_end = find_silence_end(frames, segend);

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
      s = find_next_silence(frames, segend);

      segend = s;
    }
  }

  return segend;
}

std::vector<FrameSpan> extract_segments(const FrameSpan& frames)
{
  std::vector<FrameSpan> result;

  size_t i = 0;
  while (i < frames.size())
  {
    const size_t segment_start = i;
    const size_t segment_end = find_segment_end(frames, segment_start);
    result.push_back(frames.subspan(segment_start, segment_end - segment_start));
    i = segment_end;
  }

  return result;
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

  size_t i = 0;
  while (i < span.size())
  {
    size_t j = find_next_scframe(span, i);
    j = std::min(j, span.size());

    result.push_back(span.subspan(i, j - i));
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
    size_t searchAreaEnd,
    const Parameters& algoParams)
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

    if (m.score > algoParams.areaMatchThreshold) // no good match, stop here
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

  const Video& video = *span.video;

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

  const Video& video = *span.video;

  if (span.endOffset() < video.frames.size())
  {
    if (video.frames.at(span.endOffset()).black)
    {
      return true;
    }
  }

  return false;
}

bool likely_same_scene(FrameSpan a, FrameSpan b, double areaMatchThreshold)
{
  if (b.size() < a.size())
  {
    std::swap(a, b);
  }

  return find_best_matching_area_ex(a, b).score <= areaMatchThreshold;
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
                                                         int matchThreshold)
{
  assert(matchThreshold > 0);

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

std::pair<size_t, size_t> find_match_end(const Video& a,
                                         size_t i,
                                         const Video& b,
                                         size_t j,
                                         double speed,
                                         size_t i_end,
                                         size_t j_end,
                                         const Parameters& algoParams)
{
  assert(algoParams.frameUnmatchThreshold > 0);

  double jreal = j;

  while (i + 1 < i_end && std::round(jreal + speed) < j_end)
  {
    size_t next_frame_a = i + 1;
    size_t next_frame_b = std::round(jreal + speed);
    const int diff = phashDist(a.frames.at(next_frame_a), b.frames.at(next_frame_b));

    if (diff < algoParams.frameUnmatchThreshold)
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

    std::optional<std::pair<size_t, size_t>> search_match =
        find_best_match(span1, span2, algoParams.frameRematchThreshold);

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
    if (diff < algoParams.frameUnmatchThreshold)
    {
      ++i;
    }
  }

  return std::pair(i + 1, j + 1);
}

std::pair<size_t, size_t> find_match_end_backward(const Video& a,
                                                  size_t i,
                                                  const Video& b,
                                                  size_t j,
                                                  double speed,
                                                  size_t i_min,
                                                  size_t j_min,
                                                  const Parameters& algoParams)
{
  double jreal = j;

  while (i > i_min && std::round(jreal - speed) >= j_min)
  {
    size_t prev_frame_a = i - 1;
    size_t prev_frame_b = std::round(jreal - speed);

    const int diff = phashDist(a.frames.at(prev_frame_a), b.frames.at(prev_frame_b));

    if (diff < algoParams.frameUnmatchThreshold)
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

    std::optional<std::pair<size_t, size_t>> search_match =
        find_best_match(span1, span2, algoParams.frameRematchThreshold);

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
    if (diff < algoParams.frameUnmatchThreshold)
    {
      i = i_min;
    }
  }

  return std::pair(i, j);
}

std::pair<FrameSpan, FrameSpan> refine_match_2scenes(std::vector<FrameSpan>::const_iterator it,
                                                     const FrameSpan& basematch,
                                                     const FrameSpan& fullSearchArea,
                                                     const Parameters& algoParams)
{
  const Video& first_video = *(it->video);
  const Video& second_video = *basematch.video;
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
      if (likely_same_scene(*std::next(it), span, algoParams.areaMatchThreshold))
      {
        refined_match.moveEndOffset(span.endOffset());
      }
      else
      {
        break;
      }
    }

    const double speed_estimate = [&]() {
      double video1_realtime = (basepattern.endOffset() - vid1_sc) * first_video.frameDelta;
      double video2_realtime = (refined_match.endOffset() - vid2_sc) * second_video.frameDelta;
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
      if (likely_same_scene(*std::next(it), span, algoParams.areaMatchThreshold))
      {
        refined_match.moveStartOffsetTo(span.startOffset());
      }
      else
      {
        break;
      }
    }

    const double speed_estimate = [&]() {
      double video1_realtime = (vid1_sc - basepattern.startOffset()) * first_video.frameDelta;
      double video2_realtime = (vid2_sc - refined_match.startOffset()) * second_video.frameDelta;
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
                                                              fullSearchArea.startOffset(),
                                                              algoParams);
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
                                                 fullSearchArea.endOffset(),
                                                 algoParams);
      refined_pattern.moveEndOffset(vid1_end);
      refined_match.moveEndOffset(vid2_end);
    }
  }

  return std::pair(refined_pattern, refined_match);
}

std::pair<FrameSpan, FrameSpan> refine_match(std::vector<FrameSpan>::const_iterator begin,
                                             std::vector<FrameSpan>::const_iterator end,
                                             const FrameSpan& basematch,
                                             const FrameSpan& fullSearchArea,
                                             const Parameters& algoParams)
{
  // If we enter this function, we know that frames covered by [begin, end) roughly
  // match frames in "basematch".
  // The job of this function is to adjust the match near its start and end.

  assert(begin != end);

  const Video& first_video = *(begin->video);
  const Video& second_video = *basematch.video;
  const FrameSpan basepattern = merge(*begin, *std::prev(end));

  if (std::distance(begin, end) < 3)
  {
    // we have only one or two scenes in [begin, end).

    if (std::distance(begin, end) == 2)
    {
      return refine_match_2scenes(begin, basematch, fullSearchArea, algoParams);
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

    if (local_match.score > algoParams.areaMatchThreshold)
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
    if (local_match.score > algoParams.areaMatchThreshold)
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
    double video1_realtime = (vid1_last_sc - vid1_first_sc) * first_video.frameDelta;
    double video2_realtime = (vid2_last_sc - vid2_first_sc) * second_video.frameDelta;
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
                                               fullSearchArea.endOffset(),
                                               algoParams);
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
                                                            fullSearchArea.startOffset(),
                                                            algoParams);
    refined_pattern.moveStartOffsetTo(vid1_start);
    refined_match.moveStartOffsetTo(vid2_start);
  }

  return std::pair(refined_pattern, refined_match);
}

std::pair<FrameSpan, FrameSpan> find_best_subspan_match(const FrameSpan& pattern,
                                                        const FrameSpan& searchArea,
                                                        const Parameters& algoParams)
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

    if (m.score > algoParams.areaMatchThreshold)
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
                                                     searchArea.endOffset(),
                                                     algoParams);

    //   FrameSpan last_match_from_pattern = *std::prev(end_it);
    //   m.pattern.count = last_match_from_pattern.endOffset() - m.pattern.startOffset();
    m.match.count = last_match_from_sa.endOffset() - m.match.startOffset();

    if (debugmatches)
    {
      qDebug() << "  >>" << merge(*it, *std::prev(end_it)) << " ~ " << m.match;
    }

    std::tie(m.pattern, m.match) = refine_match(it, end_it, m.match, searchArea, algoParams);

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

TimeSegment to_timesegment(const FrameSpan& span)
{
  int pts = get_nth_frame_pts(*span.video, span.first);
  const int64_t start = std::round(pts * span.video->frameDelta * 1000);
  pts = get_nth_frame_pts(*span.video, span.first + span.count);
  const int64_t end = std::round(pts * span.video->frameDelta * 1000);
  return TimeSegment::between(start, end);
}

VideoMatch to_match(const std::pair<FrameSpan, FrameSpan>& m)
{
  VideoMatch result;
  result.a = to_timesegment(m.first);
  result.b = to_timesegment(m.second);
  return result;
}

std::vector<VideoMatch> find_matches(const FrameSpan& a,
                                     const FrameSpan& b,
                                     const Parameters& params)
{
  FrameSpan search_area = b;

  const std::vector<FrameSpan> segments = extract_segments(a);

  // for (const FrameSpan& segment : segments)
  // {
  //   qDebug() << segment;
  // }

  std::vector<VideoMatch> matches;

  for (const FrameSpan& segment : segments)
  {
    assert(segment.size() > 0);

    std::pair<FrameSpan, FrameSpan> matchingspans = find_best_subspan_match(segment,
                                                                            search_area,
                                                                            params);

    if (matchingspans.first.count > 0)
    {
      matches.push_back(to_match(matchingspans));
      search_area = FrameSpan(*b.video, matchingspans.second.endOffset(), -1);
    }
  }

  return matches;
}

FrameSpan to_framespan(const Video& v, const TimeSegment& tseg)
{
  auto get_frame_timestamp = [&v](const Frame& frame) -> int64_t {
    return std::round(frame.pts * v.frameDelta * 1000);
  };

  auto it = std::lower_bound(v.frames.begin(),
                             v.frames.end(),
                             tseg.start(),
                             [&get_frame_timestamp](const Frame& e, int64_t val) {
                               return get_frame_timestamp(e) < val;
                             });

  const size_t start_frame = std::distance(v.frames.begin(), it);

  it = std::lower_bound(v.frames.begin(),
                        v.frames.end(),
                        tseg.end(),
                        [&get_frame_timestamp](const Frame& e, int64_t val) {
                          return get_frame_timestamp(e) < val;
                        });

  const size_t end_frame = std::distance(v.frames.begin(), it);

  return FrameSpan(v, start_frame, end_frame - start_frame);
}

std::vector<VideoMatch> find_matches(const Video& a,
                                     const TimeSegment& segmentA,
                                     const Video& b,
                                     const TimeSegment& segmentB,
                                     const Parameters& params)
{
  // TODO: passer ça dans la classe MatchDetector.
  // il faut en effet se souvenir que l'on ne doit jamais sortir des deux segments.
  return find_matches(to_framespan(a, segmentA), to_framespan(b, segmentB), params);
}

} // namespace MatchAlgo

namespace MatchAlgo {

Video::Video(const MediaObject& media)
    : media(&media)
{
  this->frameDelta = this->media->frameDelta();

  const std::vector<VideoFrameInfo>& vframes = media.framesInfo()->frames;
  this->frames.reserve(vframes.size());

  for (const VideoFrameInfo& f : vframes)
  {
    Frame e;
    e.pts = f.pts;
    e.phash = f.phash;
    this->frames.push_back(e);
  }

  // ?TODO: add a "sentinel" frame?
}

} // namespace MatchAlgo

namespace {} // namespace

MatchDetector::MatchDetector(const MediaObject& a, const MediaObject& b)
    : m_a(&a)
    , m_b(&b)
    , segmentA(0, a.duration() * 1000)
    , segmentB(0, b.duration() * 1000)
{
  static_assert(std::is_same_v<decltype(a.duration()), double>,
                "duration is currently expected to be a double");

  assert(a.silenceInfo() && "silence info is missing");
  assert(a.blackFramesInfo() && "black frame info is missing");
  assert(a.scenesInfo() && "scenes info is missing");
  assert(a.framesInfo() && b.framesInfo() && "frame info is missing");

  if (!(a.silenceInfo() && a.blackFramesInfo() && a.scenesInfo() && a.framesInfo()
        && b.framesInfo()))
  {
    throw std::runtime_error("missing some data from MatchDetector inputs");
  }
}

std::vector<VideoMatch> MatchDetector::run()
{
  MatchAlgo::Video a{*m_a};
  MatchAlgo::Video b{*m_b};

  MatchAlgo::mark_silence_frames(a);

  MatchAlgo::silenceborders(a.frames);

  MatchAlgo::mark_black_frames(a);

  MatchAlgo::mark_sc_frames(a, this->parameters.scdetThreshold);

  MatchAlgo::merge_small_scenes(a, 7);

  return MatchAlgo::find_matches(a, this->segmentA, b, this->segmentB, this->parameters);
}
