#include "matcheditorwidget.h"

#include "videoplayerwidget.h"

#include "mediaobject.h"
#include "project.h"

#include "frameextractionthread.h"

#include "appsettings.h"
#include "cache.h"
#include "exerun.h"

#include <QProgressDialog>

#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QSplitter>

#include <QVBoxLayout>

#include <QBrush>
#include <QIcon>
#include <QVariant>

#include <QShortcut>

#include <QDir>
#include <QTemporaryDir>

#include <QApplication>
#include <QDesktopServices>
#include <QUrl>

#include <QDebug>

#include <algorithm>
#include <iterator>
#include <utility>

QDebug operator<<(QDebug out, const TimeSegment& ts)
{
  out << ts.toString();
  return out;
}

class VideoFramesModel : public QAbstractListModel
{
  Q_OBJECT
public:
  explicit VideoFramesModel(MediaObject& media, QObject* parent = nullptr)
      : QAbstractListModel(parent)
      , m_media(media)
      , m_default_icon(":/images/missing.svg")
      , m_matchRange(0, -1)
  {
    Q_ASSERT(m_media.framesInfo());
    if (m_media.framesInfo())
    {
      m_icons.resize(m_media.framesInfo()->frames.size());
    }
    else
    {
      qDebug() << "frame info not loaded";
    }
  }

  MediaObject& media() const { return m_media; }

  int rowCount(const QModelIndex& parent) const override
  {
    return parent == QModelIndex() ? int(m_icons.size()) : 0;
  }

  QVariant data(const QModelIndex& index, int role) const override
  {
    const int i = index.row();

    switch (role)
    {
    case Qt::DisplayRole: {
      if (m_media.framesInfo())
      {
        return QString::number(m_media.framesInfo()->frames.at(i).pts);
      }
      return "#" + QString::number(i);
    }
    case Qt::DecorationRole: {
      if (m_icons[i].isNull())
      {
        auto* self = const_cast<VideoFramesModel*>(this);
        self->fetchMiniature(i);
        return m_default_icon;
      }
      return m_icons[i];
    }
    case Qt::ToolTipRole: {
      if (!m_media.framesInfo())
      {
        return QVariant();
      }
      const auto d = Duration(
          std::round(m_media.framesInfo()->frames.at(i).pts * m_media.frameDelta() * 1000));
      return d.toString(Duration::HHMMSSzzz);
    }
    case Qt::BackgroundRole: {
      if (i >= m_matchRange.first && i <= m_matchRange.second)
      {
        // TODO: put the background color in the settings
        return QBrush(QColor("limegreen"));
      }
    }
    default:
      break;
    }

    return QVariant();
  }

  Qt::ItemFlags flags(const QModelIndex& index) const
  {
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemNeverHasChildren;
  }

  void setSelection(TimeSegment seg)
  {
    m_match = seg;
    convertSelectionToFrameRange();
  }

  const std::pair<int, int>& selectionAsFrameRange() const { return m_matchRange; }
  const TimeSegment& selectionAsTimeSegment() const { return m_match; }

  void setMatchBegin(int n)
  {
    if (n >= m_matchRange.second)
    {
      return;
    }

    m_matchRange.first = n;

    convertFrameRangeToSelection();

    Q_EMIT dataChanged(index(0), index(m_icons.size() - 1), QList<int>{Qt::BackgroundRole});
  }

  void setMatchEnd(int n)
  {
    if (n <= m_matchRange.first)
    {
      return;
    }

    m_matchRange.second = n;

    convertFrameRangeToSelection();

    Q_EMIT dataChanged(index(0), index(m_icons.size() - 1), QList<int>{Qt::BackgroundRole});
  }

protected Q_SLOTS:
  void onProcessFinished()
  {
    auto* process = qobject_cast<QProcess*>(sender());

    if (!process)
    {
      return;
    }

    const QVariant prop = process->property("reqMin");
    if (prop.isNull())
    {
      qDebug() << "missing reqMin property";
      return;
    }

    int min = prop.toInt();

    const QDir outdir{process->property("reqOutdir").toString()};

    auto it = std::lower_bound(m_requests.begin(),
                               m_requests.end(),
                               min,
                               [](const MiniatureRequest& e, int min) { return e.min < min; });

    Q_ASSERT(it != m_requests.end());
    if (it == m_requests.end() || it->process != process)
    {
      qDebug() << "bad miniature request";
      return;
    }

    MiniatureRequest& req = *it;
    req.process = nullptr;

    const std::vector<VideoFrameInfo>& frames = m_media.framesInfo()->frames;

    for (int i(req.min); i <= req.max; ++i)
    {
      const QString filename = outdir.filePath(QString::number(frames[i].pts) + ".jpeg");
      m_icons[i] = QIcon(filename);
    }

    process->deleteLater();

    Q_EMIT dataChanged(index(req.min), index(req.max), QList<int>{Qt::DecorationRole});
  }

private:
  void convertSelectionToFrameRange()
  {
    if (!m_media.framesInfo())
    {
      return;
    }

    if (!m_match.duration())
    {
      m_matchRange.first = m_matchRange.second = -1;
      Q_EMIT dataChanged(index(0), index(m_icons.size() - 1), QList<int>{Qt::BackgroundRole});
      return;
    }

    const double framedelta = m_media.frameDelta();
    const std::vector<VideoFrameInfo>& frames = m_media.framesInfo()->frames;
    auto it = std::lower_bound(frames.begin(),
                               frames.end(),
                               m_match.start(),
                               [framedelta](const VideoFrameInfo& e, int64_t time) {
                                 return std::round(e.pts * framedelta * 1000) < time;
                               });

    m_matchRange.first = std::distance(frames.begin(), it);

    it = std::lower_bound(it,
                          frames.end(),
                          m_match.end(),
                          [framedelta](const VideoFrameInfo& e, int64_t time) {
                            return std::round(e.pts * framedelta * 1000) < time;
                          });

    m_matchRange.second = std::distance(frames.begin(), std::prev(it));

    Q_EMIT dataChanged(index(0), index(m_icons.size() - 1), QList<int>{Qt::BackgroundRole});
  }

  void convertFrameRangeToSelection()
  {
    Q_ASSERT(m_media.framesInfo());

    const std::vector<VideoFrameInfo>& frames = m_media.framesInfo()->frames;
    m_match = TimeSegment(m_media.convertPtsToPosition(frames[m_matchRange.first].pts),
                          m_media.convertPtsToPosition(frames[m_matchRange.second].pts + 1));

    qDebug() << "new match segment:" << m_match;
  }

  void fetchMiniature(int frameIndex)
  {
    Q_ASSERT(m_icons[frameIndex].isNull());

    if (!m_media.framesInfo())
    {
      return;
    }

    auto reqit = std::upper_bound(m_requests.begin(),
                                  m_requests.end(),
                                  frameIndex,
                                  [](int frameIndex, const MiniatureRequest& e) {
                                    return frameIndex < e.min;
                                  });

    if (!m_requests.empty() && reqit != m_requests.begin())
    {
      const MiniatureRequest& prevreq = *std::prev(reqit);
      if (prevreq.min <= frameIndex && frameIndex <= prevreq.max)
      {
        // a request has already been made for this frame
        return;
      }
    }

    const std::vector<VideoFrameInfo>& frames = m_media.framesInfo()->frames;

// TODO: adapter le nombre de frames à récupérer en fonction de la taille des miniatures ?
#ifndef NDEBUG
    constexpr double nsecs = 10;
#else
    constexpr double nsecs = 20;
#endif
    size_t min_index = std::max<int>(0, frameIndex - 0.5 * nsecs / m_media.frameDelta());
    size_t max_index = std::min<int>(frames.size() - 1,
                                     frameIndex + 0.5 * nsecs / m_media.frameDelta());

    // Adjust min_index and max_index so as not to request the same frames multiple times

    // while (!m_icons[min_index].isNull())
    // {
    //   ++min_index;
    //   ++max_index;
    // }

    // max_index = std::min(frames.size() - 1, max_index);
    // while (!m_icons[max_index].isNull())
    // {
    //   --max_index;
    // }

    if (!m_requests.empty() && reqit != m_requests.begin())
    {
      const MiniatureRequest& prevreq = *std::prev(reqit);
      min_index = std::max<size_t>(prevreq.max + 1, min_index);
    }

    if (reqit != m_requests.end())
    {
      Q_ASSERT(reqit->min > 1);
      max_index = std::min<size_t>(reqit->min - 1, max_index);
    }

    Q_ASSERT(max_index >= min_index);

    MiniatureRequest req;
    req.min = min_index;
    req.max = max_index;

    const auto seg = TimeSegment(frames.at(min_index).pts * m_media.frameDelta() * 1000,
                                 (frames.at(max_index).pts + 1) * m_media.frameDelta() * 1000);

    // ffmpeg -ss 20 -to 30 -i 3.mkv -vsync 0 -vf scale=64:64 -copyts -f image2 -frame_pts true frames/%d.jpeg

    QDir outdir{tempDir().path()};
    outdir.mkdir(QString::number(frameIndex));

    QStringList args;
    args << "-ss" << QString::number(seg.start() / double(1000));
    args << "-to" << QString::number(seg.end() / double(1000));
    args << "-i" << m_media.filePath();
    args << "-vsync"
         << "0";
    args << "-vf"
         << "scale=64:64";
    args << "-copyts";
    args << "-f"
         << "image2";
    args << "-frame_pts"
         << "true";
    args << QString("%1/%d.jpeg").arg(outdir.filePath(QString::number(frameIndex)));

    req.process = run("ffmpeg", args);
    req.process->setProperty("reqMin", int(req.min));
    req.process->setProperty("reqOutdir", outdir.filePath(QString::number(frameIndex)));

    connect(req.process, &QProcess::finished, this, &VideoFramesModel::onProcessFinished);

    m_requests.insert(reqit, req);
  }

  QTemporaryDir& tempDir()
  {
    if (!m_tempDir)
    {
      m_tempDir = std::make_unique<QTemporaryDir>();
    }

    return *m_tempDir;
  }

private:
  MediaObject& m_media;
  QIcon m_default_icon;
  std::vector<QIcon> m_icons;
  TimeSegment m_match;
  std::pair<int, int> m_matchRange;

  struct MiniatureRequest
  {
    int min;
    int max;
    QProcess* process;
  };

  std::vector<MiniatureRequest> m_requests;
  std::unique_ptr<QTemporaryDir> m_tempDir;
};

class VideoFramesView : public QListView
{
  Q_OBJECT
public:
  explicit VideoFramesView(VideoPlayerWidget& player, QWidget* parent = nullptr)
      : QListView(parent)
      , m_player(player)
  {
    setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setResizeMode(QListView::Adjust);

    auto* settings = AppSettings::getInstance(qApp);

    if (settings)
    {
      settings->watch(THUMBNAIL_SIZE_KEY, this, &VideoFramesView::onThumbnailSizeChanged);
      onThumbnailSizeChanged(settings->value(THUMBNAIL_SIZE_KEY, QVariant(48)));
    }

    setSelectionBehavior(QListView::SelectRows);
    setSelectionMode(QListView::ContiguousSelection);
    //  setSelectionRectVisible(true);

    // TODO: put the selection color in the settings
    // setStyleSheet("QListView::item:selected{background-color: rgb(255,255,0);}");

    Q_ASSERT(m_player.media());
    setModel(new VideoFramesModel(*m_player.media(), this));

    // setup context menu
    {
      auto* set_match_begin = new QAction("Set as match begin", this);
      connect(set_match_begin,
              &QAction::triggered,
              this,
              &VideoFramesView::setCurrentIndexAsMatchBegin);
      auto* shortcut_begin = new QShortcut(QKeySequence("B"), this);
      shortcut_begin->setContext(Qt::WidgetShortcut);
      connect(shortcut_begin, &QShortcut::activated, set_match_begin, &QAction::trigger);
      addAction(set_match_begin);

      auto* set_match_end = new QAction("Set as match end", this);
      connect(set_match_end, &QAction::triggered, this, &VideoFramesView::setCurrentIndexAsMatchEnd);
      auto* shortcut_end = new QShortcut(QKeySequence("E"), this);
      shortcut_end->setContext(Qt::WidgetShortcut);
      connect(shortcut_end, &QShortcut::activated, set_match_end, &QAction::trigger);
      addAction(set_match_end);

      setContextMenuPolicy(Qt::ActionsContextMenu);
    }
  }

  VideoPlayerWidget& videoPlayer() const { return m_player; }

  // be careful when accessing the model from outside not to modify
  // anything foolishly.
  VideoFramesModel* model() const { return static_cast<VideoFramesModel*>(QListView::model()); }

  // TODO: rename to setSelection?
  void setup(const TimeSegment& selection)
  {
    Q_ASSERT(m_player.media()->framesInfo());

    m_player.seek(selection.start());
    model()->setSelection(selection);

    scrollToFrameRange();

    connect(&m_player,
            &VideoPlayerWidget::currentPausedImageChanged,
            this,
            &VideoFramesView::scrollToCurrentImage);
  }

  void resetModel() { model()->setSelection(TimeSegment()); }

  TimeSegment matchRange() const
  {
    Q_ASSERT(model());
    return model()->selectionAsTimeSegment();
  }

Q_SIGNALS:
  void matchRangeEdited();

public Q_SLOTS:
  void scrollToFrameRange()
  {
    const std::pair<int, int> range = model()->selectionAsFrameRange();

    if (range.second == -1)
    {
      return;
    }

    scrollTo(model()->index(range.first));
  }

  void scrollToCurrentImage()
  {
    int64_t pos = m_player.position();

    if (!m_player.media()->framesInfo())
    {
      return;
    }

    MediaObject* media = m_player.media();

    const double framedelta = media->frameDelta();
    const std::vector<VideoFrameInfo>& frames = media->framesInfo()->frames;
    auto it = std::lower_bound(frames.begin(),
                               frames.end(),
                               pos,
                               [framedelta](const VideoFrameInfo& e, int64_t time) {
                                 return std::round(e.pts * framedelta * 1000) < time;
                               });

    if (it == frames.end())
    {
      return;
    }

    if (it != frames.begin())
    {
      const VideoFrameInfo& frame = *it;
      if (std::round(frame.pts * framedelta * 1000) > pos)
      {
        it = std::prev(it);
      }
    }

    int n = std::distance(frames.begin(), it);

    scrollTo(model()->index(n));
    setCurrentIndex(model()->index(n));
  }

protected Q_SLOTS:
  void setCurrentIndexAsMatchBegin()
  {
    const int i = currentIndex().row();

    if (i == -1)
    {
      return;
    }

    model()->setMatchBegin(i);

    Q_EMIT matchRangeEdited();
  }

  void setCurrentIndexAsMatchEnd()
  {
    const int i = currentIndex().row();

    if (i == -1)
    {
      return;
    }

    model()->setMatchEnd(i);

    Q_EMIT matchRangeEdited();
  }

  void onThumbnailSizeChanged(const QVariant& value)
  {
    bool ok;
    int icon_size = value.toInt(&ok);

    if (!ok)
    {
      icon_size = 48;
    }

    setIconSize(QSize(icon_size, icon_size));
  }

protected:
  void currentChanged(const QModelIndex& current, const QModelIndex& previous) override
  {
    QListView::currentChanged(current, previous);

    const int i = current.row();

    if (i == -1)
    {
      return;
    }

    Q_ASSERT(m_player.media());

    const MediaObject& m = *m_player.media();
    if (!m.framesInfo())
    {
      return;
    }

    const std::vector<VideoFrameInfo>& frames = m.framesInfo()->frames;

    if (i < 0 || i >= frames.size())
    {
      return;
    }

    m_player.seek(std::round((frames[i].pts + 0.5) * m.frameDelta() * 1000));
  }

private:
  VideoPlayerWidget& m_player;
};

class MatchEditorItemWidget : public QWidget
{
  Q_OBJECT
public:
  explicit MatchEditorItemWidget(VideoPlayerWidget& player, QWidget* parent = nullptr)
      : QWidget(parent)
  {
    auto* layout = new QVBoxLayout(this);

    if (auto* hlayout = new QHBoxLayout)
    {
      hlayout->addWidget(m_matchDisplay = new QLabel);
      hlayout->addWidget(m_selectionSizeDisplay = new QLabel);
      hlayout->addStretch();
      layout->addLayout(hlayout);
    }

    layout->addWidget(framesView = new VideoFramesView(player));

    connect(framesView,
            &VideoFramesView::matchRangeEdited,
            this,
            &MatchEditorItemWidget::onMatchEdited);
    connect(framesView->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this,
            &MatchEditorItemWidget::onSelectionChanged);

    connect(m_matchDisplay, &QLabel::linkActivated, this, &MatchEditorItemWidget::onLinkActivated);
  }

  void setSelection(const TimeSegment& selection)
  {
    setTimeDisplay(selection);
    framesView->setup(selection);
  }

  void reset() { framesView->resetModel(); }

public Q_SLOTS:

  void seekMatchStart()
  {
    TimeSegment ts = framesView->model()->selectionAsTimeSegment();
    framesView->videoPlayer().seek(ts.start());
  }

  void seekMatchEnd()
  {
    TimeSegment ts = framesView->model()->selectionAsTimeSegment();
    framesView->videoPlayer().seek(ts.end());
  }

  void onLinkActivated(const QString& link)
  {
    if (link == "#end")
    {
      seekMatchEnd();
    }
    else if (link == "#start")
    {
      seekMatchStart();
    }
  }

protected Q_SLOTS:
  void onMatchEdited()
  {
    TimeSegment ts = framesView->model()->selectionAsTimeSegment();
    setTimeDisplay(ts);
  }

  void onSelectionChanged()
  {
    const auto& selected_indexes = framesView->selectionModel()->selectedIndexes();

    if (selected_indexes.empty())
    {
      m_selectionSizeDisplay->clear();
    }
    else if (selected_indexes.size() == 1)
    {
      m_selectionSizeDisplay->setText(" | Selection: 1 frame");
    }
    else
    {
      m_selectionSizeDisplay->setText(
          QString(" | Selection: %1 frames").arg(QString::number(selected_indexes.size())));
    }
  }

protected:
  void setTimeDisplay(const TimeSegment& ts)
  {
    QString text = "";
    text += QString("<a href=\"#start\">%1</a>")
                .arg(Duration(ts.start()).toString(Duration::HHMMSSzzz));
    text += " - ";
    text += QString("<a href=\"#end\">%1</a>").arg(Duration(ts.end()).toString(Duration::HHMMSSzzz));

    text += QString(" (%1)").arg(QString::number(ts.duration() / double(1000)));

    m_matchDisplay->setText(text);
  }

public:
  VideoFramesView* framesView;

private:
  QLabel* m_matchDisplay;
  QLabel* m_selectionSizeDisplay;
};

MatchEditorWidget::MatchEditorWidget(DubbingProject& project,
                                     MediaObject& video1,
                                     MediaObject& video2,
                                     QWidget* parent)
    : QWidget(parent)
    , m_project(project)
    , m_video1(video1)
    , m_video2(video2)
{
  auto* layout = new QVBoxLayout(this);

  if (auto* splitter = new QSplitter)
  {
    layout->addWidget(splitter);

    splitter->setOrientation(Qt::Vertical);

    if (auto* playersrow = new QWidget)
    {
      auto* mylayout = new QHBoxLayout(playersrow);

      m_leftPlayer = new VideoPlayerWidget;
      m_rightPlayer = new VideoPlayerWidget;
      mylayout->addWidget(m_leftPlayer);
      mylayout->addWidget(m_rightPlayer);

      m_leftPlayer->setMedia(&video1);
      m_rightPlayer->setMedia(&video2);

      splitter->addWidget(playersrow);
    }

    auto* container = new QWidget;
    if (auto* layout = new QVBoxLayout(container))
    {
      if (m_buttonsContainer = new QWidget)
      {
        auto* sublayout = new QHBoxLayout(m_buttonsContainer);
        sublayout->addStretch();
        sublayout->addWidget(m_preview_button = new QPushButton("Preview"));
        sublayout->addWidget(m_ok_button = new QPushButton("Ok"));
        sublayout->addWidget(m_cancel_button = new QPushButton("Cancel"));
        layout->addWidget(m_buttonsContainer);
      }

      if (auto* sublayout = new QHBoxLayout)
      {
        sublayout->addWidget(m_items[0] = new MatchEditorItemWidget(*m_leftPlayer));
        sublayout->addWidget(m_items[1] = new MatchEditorItemWidget(*m_rightPlayer));

        layout->addLayout(sublayout);
      }
    }
    splitter->addWidget(container);
  }

  // setup connections
  {
    for (MatchEditorItemWidget* w : m_items)
    {
      connect(w->framesView,
              &VideoFramesView::matchRangeEdited,
              this,
              &MatchEditorWidget::onMatchEdited);
    }

    connect(m_preview_button, &QPushButton::clicked, this, &MatchEditorWidget::launchPreview);
    connect(m_ok_button, &QPushButton::clicked, this, &MatchEditorWidget::accept);
    connect(m_cancel_button, &QPushButton::clicked, this, &MatchEditorWidget::cancel);
  }

  refreshUi();

  m_buttonsContainer->setEnabled(false);
}

MatchEditorWidget::~MatchEditorWidget()
{
  clearCache();
}

VideoPlayerWidget* MatchEditorWidget::playerLeft() const
{
  return m_leftPlayer;
}

VideoPlayerWidget* MatchEditorWidget::playerRight() const
{
  return m_rightPlayer;
}

void MatchEditorWidget::setCurrentMatchObject(MatchObject* mob)
{
  if (currentMatchObject() != mob)
  {
    m_editedMatchObject = mob;

    if (mob)
    {
      m_original_match = mob->value();
      m_edited_match = m_original_match;

      m_items[0]->setSelection(m_original_match.a);
      m_items[1]->setSelection(m_original_match.b);

      m_buttonsContainer->setEnabled(true);
    }
  }
}

bool MatchEditorWidget::isEditingMatchObject() const
{
  return currentMatchObject() != nullptr;
}

void MatchEditorWidget::reset()
{
  m_items[0]->reset();
  m_items[1]->reset();
  m_original_match = m_edited_match = VideoMatch();
  m_tempDir.reset();
  refreshUi();
  m_buttonsContainer->setEnabled(false);
}

const VideoMatch& MatchEditorWidget::originalMatch() const
{
  return m_original_match;
}

const VideoMatch& MatchEditorWidget::editedMatch() const
{
  return m_edited_match;
}

void MatchEditorWidget::clearCache()
{
  QDir cachedir{GetCacheDir()};
  QStringList files = cachedir.entryList({"*.wav"});

  for (const QString& file : files)
  {
    qDebug() << "Removing" << cachedir.absoluteFilePath(file);
    QFile::remove(cachedir.absoluteFilePath(file));
  }
}

void MatchEditorWidget::launchPreview()
{
  QProgressDialog dialog{"Preparing preview", QString(), 0, 4, this};

  VideoMatch match;
  match.a = m_items[0]->framesView->matchRange();
  match.b = m_items[1]->framesView->matchRange();

  const QString source_audio2_path =
      GetCacheDir() + "/"
      + QFileInfo(m_items[1]->framesView->videoPlayer().media()->filePath()).completeBaseName()
      + ".wav";

  // extract audio 2
  if (!QFile::exists(source_audio2_path))
  {
    QStringList args;
    args << "-y"
         << "-i" << m_items[1]->framesView->videoPlayer().media()->filePath();
    args << "-map_metadata"
         << "-1";
    args << "-map"
         << "0:1";
    args << "-ac"
         << "1";
    args << source_audio2_path;

    looprun("ffmpeg", args);
  }

  dialog.setValue(1);

  const QString baseaudioname = QString::number(match.b.start()) + "-"
                                + QString::number(match.b.end());

  const QString baseaudiopath = getTempDir().filePath(baseaudioname + ".wav");

  // split audio 2
  if (!QFile::exists(baseaudiopath))
  {
    QStringList args;
    args << "-y";
    args << "-i" << source_audio2_path;
    args << "-ss" << Duration(match.b.start()).toString(Duration::HHMMSSzzz);
    args << "-to" << Duration(match.b.end()).toString(Duration::HHMMSSzzz);
    args << baseaudiopath;
    looprun("ffmpeg", args);
  }

  dialog.setValue(2);

  const double ratio = match.b.duration() / double(match.a.duration());

  const QString audiopath = getTempDir().filePath(baseaudioname + "x" + QString::number(ratio)
                                                  + ".wav");

  // speed-up audio 2
  if (!QFile::exists(audiopath))
  {
    QStringList args;

    args << "-y";
    args << "-i" << baseaudiopath;

    args << "-filter:a";
    args << QString("atempo=%1").arg(QString::number(ratio));

    args << audiopath;

    looprun("ffmpeg", args);
  }

  dialog.setValue(3);

  const QString previewpath = getTempDir().filePath("preview.mkv");

  // produce video
  {
    QStringList args;

    args << "-y";

    args << "-ss" << Duration(match.a.start()).toString(Duration::HHMMSSzzz);
    args << "-to" << Duration(match.a.end()).toString(Duration::HHMMSSzzz);
    args << "-i" << m_leftPlayer->media()->filePath();

    args << "-i" << audiopath;

    args << "-map"
         << "0:0";
    args << "-map"
         << "1:0";

    args << "-c:v"
         << "copy";

    args << "-c:a"
         << "copy";

    args << previewpath;
    looprun("ffmpeg", args);
  }

  dialog.setValue(4);

  QDesktopServices::openUrl(QUrl::fromLocalFile(previewpath));
}

void MatchEditorWidget::accept()
{
  const bool match_modified = m_original_match != m_edited_match;

  if (!match_modified)
  {
    return cancel();
  }

  Q_EMIT editionFinished(true);
}

void MatchEditorWidget::cancel()
{
  Q_EMIT editionFinished(false);
}

void MatchEditorWidget::onMatchEdited()
{
  m_edited_match.a = m_items[0]->framesView->matchRange();
  m_edited_match.b = m_items[1]->framesView->matchRange();
  refreshUi();
}

QTemporaryDir& MatchEditorWidget::getTempDir()
{
  if (!m_tempDir)
  {
    m_tempDir = std::make_unique<QTemporaryDir>();
  }

  return *m_tempDir;
}

void MatchEditorWidget::refreshUi()
{
  const bool match_modified = m_original_match != m_edited_match;
  m_ok_button->setText(match_modified ? "Save" : "Ok");
  m_cancel_button->setEnabled(match_modified);
}

#include "matcheditorwidget.moc"
