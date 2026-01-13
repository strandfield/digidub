#include "window.h"

#include "appsettings.h"

#include "blackdetectthread.h"
#include "frameextractionthread.h"
#include "scdetthread.h"
#include "silencedetectthread.h"

#include "matchalgo.h"

#include "mediaobject.h"
#include "project.h"

#include "videoplayerwidget.h"

#include "matcheditorwidget.h"
#include "matchlistwindow.h"

#include "exporter.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>

#include <QAction>
#include <QMenu>
#include <QMenuBar>

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>

#include <QHBoxLayout>
#include <QVBoxLayout>

#include <QKeyEvent>

#include <QSettings>

#include <QStringList>
#include <QVariant>

#include <QApplication>
#include <QDebug>

#include <algorithm>

#include <format>
#include <fstream>

#include <iostream>

// settings key
constexpr const char* WINDOW_GEOM_KEY = "Window/geometry";
constexpr const char* LAST_OPEN_DIR_KEY = "lastOpenDir";
constexpr const char* LAST_SAVE_DIR_KEY = "lastSaveDir";

constexpr const char* DIGIDUB_PROJECT_FILTER = "DigiDub Project (*.txt)";

MainWindow::MainWindow()
{
  setWindowTitle("DigiDub");

  m_settings = new QSettings(QSettings::IniFormat,
                             QSettings::UserScope,
                             "Analogman Software",
                             "digidub");

  if (QMenu* menu = menuBar()->addMenu("File"))
  {
    m_actions.openProject = menu->addAction("Open...",
                                            QKeySequence("Ctrl+O"),
                                            this,
                                            &MainWindow::actOpen);
    m_actions.saveProject = menu->addAction("Save",
                                            QKeySequence("Ctrl+S"),
                                            this,
                                            &MainWindow::actSave);

    menu->addSeparator();

    m_actions.exportProject = menu->addAction("Export",
                                              QKeySequence("Ctrl+E"),
                                              this,
                                              &MainWindow::doExport);

    menu->addSeparator();
    menu->addAction("Quit", QKeySequence("Alt+F4"), this, &MainWindow::close);
  }

  if (QMenu* menu = menuBar()->addMenu("Edit"))
  {
    m_actions.deleteCurrentMatch = menu->addAction("Delete current match",
                                                   QKeySequence("Ctrl+D"),
                                                   this,
                                                   &MainWindow::deleteCurrentMatch);
  }

  if (QMenu* menu = menuBar()->addMenu("View"))
  {
    m_actions.toggleMatchListWindow = menu->addAction("Match list",
                                                      this,
                                                      &MainWindow::toggleMatchListPopup);
    m_actions.toggleMatchListWindow->setCheckable(true);

    if (QMenu* ts = menu->addMenu("Thumbnail size"))
    {
      ts->addAction("24", [this]() { setThumbnailSize(24); });
      ts->addAction("32", [this]() { setThumbnailSize(32); });
      ts->addAction("48", [this]() { setThumbnailSize(48); });
      ts->addAction("64", [this]() { setThumbnailSize(64); });
    }
  }

  menuBar()->addAction("About Qt", qApp, &QApplication::aboutQt);

#ifndef NDEBUG
  {
    menuBar()->addAction("Run debug proc", this, &MainWindow::debugProc);
  }
#endif

  setCentralWidget(new QStackedWidget(this));

  // restore window geometry
  {
    const auto geometry = settings().value(WINDOW_GEOM_KEY, QByteArray()).toByteArray();
    if (!geometry.isEmpty())
      restoreGeometry(geometry);
  }

  refreshUi();
}

MainWindow::~MainWindow()
{

}

QSettings& MainWindow::settings() const
{
  return *m_settings;
}

QString MainWindow::getLastOpenDir() const
{
  return settings().value(LAST_OPEN_DIR_KEY, QString()).toString();
}

void MainWindow::updateLastOpenDir(const QString& path)
{
  QFileInfo info{path};
  if (info.isFile())
  {
    settings().setValue(LAST_OPEN_DIR_KEY, info.absolutePath());
  }
  else if (info.isDir())
  {
    settings().setValue(LAST_OPEN_DIR_KEY, path);
  }
}

void MainWindow::openFile(const QString& filePath)
{
  if (QFileInfo(filePath).suffix().toUpper() == "TXT")
  {
    try
    {
      m_project = new DubbingProject(filePath, this);

    } catch (std::exception& ex)
    {
      qDebug() << "Exception:" << ex.what();
      return;
    }

    if (!m_project || m_project->projectFilePath().isEmpty())
    {
      QMessageBox::information(this,
                               "Error",
                               "Could not open project.\nAre you sure this is a DigiDub project?");
      return;
    }

    if (!m_project->videoFilePath().isEmpty())
    {
      m_primaryMedia = new MediaObject(m_project->resolvePath(m_project->videoFilePath()), this);
    }

    if (!m_project->audioSourceFilePath().isEmpty())
    {
      m_secondaryMedia = new MediaObject(m_project->resolvePath(m_project->audioSourceFilePath()),
                                         this);
    }

    if (m_primaryMedia && m_secondaryMedia)
    {
      launchMatchEditor();
    }

    setupConnectionsTo(m_project);
  }
  else
  {
    qDebug() << "Unknown file type";
  }

  m_matchListWindow = new MatchListWindow(*m_project, *this, this);
  //m_matchListWindow->show();

  connect(m_matchListWindow,
          &MatchListWindow::closed,
          this,
          &MainWindow::refreshUi,
          Qt::QueuedConnection);

  connect(m_matchListWindow, &MatchListWindow::matchDoubleClicked, this, [this](MatchObject* mob) {
    if (m_matchEditorWidget)
    {
      m_matchEditorWidget->setCurrentMatchObject(mob);
      refreshUi();
    }
  });

  refreshUi();
}

void MainWindow::setThumbnailSize(int n)
{
  auto* settings = AppSettings::getInstance(qApp);
  settings->setValue(THUMBNAIL_SIZE_KEY, std::clamp(n, 24, 64));
}

void MainWindow::actOpen()
{
  if (m_project)
  {
    QMessageBox::information(this, "Nope", "Project already loaded.");
    return;
  }

  QString path = QFileDialog::getOpenFileName(this,
                                              "Open",
                                              getLastOpenDir(),
                                              QString(DIGIDUB_PROJECT_FILTER));

  if (path.isEmpty())
  {
    return;
  }

  updateLastOpenDir(path);

  openFile(path);
}

void MainWindow::actSave()
{
  if (!m_project)
  {
    return;
  }

  QString savepath = m_project->projectFilePath();

  if (savepath.isEmpty())
  {
    const QString dir = settings().value(LAST_SAVE_DIR_KEY, QString()).toString();
    savepath = QFileDialog::getSaveFileName(this, "Save", dir, DIGIDUB_PROJECT_FILTER);

    if (savepath.isEmpty())
    {
      return;
    }

    updateLastSaveDir(savepath);

    m_project->setProjectFilePath(savepath);
    m_project->convertFilePathsToRelative();
  }

  m_project->save(savepath);
  m_project->setModified(false);
}

void MainWindow::doExport()
{
  if (!m_project)
  {
    return;
  }

  if (m_project->outputFilePath().isEmpty())
  {
    const QString dir = settings().value(LAST_SAVE_DIR_KEY, QString()).toString();

    const QString output_path = QFileDialog::getSaveFileName(this,
                                                             "Export",
                                                             dir,
                                                             QString("Dubbing output (*.mkv)"));

    if (output_path.isEmpty())
    {
      return;
    }

    updateLastSaveDir(output_path);

    m_project->setOutputFilePath(output_path);
    m_project->convertFilePathsToRelative();
    m_project->setModified();
  }

  QProgressDialog progress{this};
  progress.setModal(true);
  progress.setRange(0, 1000);
  progress.setLabelText("Exporting...");
  progress.setCancelButton(nullptr);
  progress.show();

  m_actions.exportProject->setEnabled(false);

  DubExporter exporter{*m_project, *m_primaryMedia};

  connect(&exporter, &DubExporter::progressChanged, this, [&exporter, &progress]() {
    progress.setValue(exporter.progress() * 1000);
  });
  connect(&exporter, &DubExporter::statusChanged, this, [&exporter, &progress]() {
    progress.setLabelText(exporter.status());
  });

  exporter.run();
  exporter.waitForFinished();

  m_actions.exportProject->setEnabled(true);
}

void MainWindow::toggleMatchListPopup()
{
  if (m_matchListWindow)
  {
    m_matchListWindow->setVisible(!m_matchListWindow->isVisible());
  }
}

void MainWindow::deleteCurrentMatch()
{
  MatchObject* mob = m_matchEditorWidget->currentMatchObject();

  MatchObject* prev = mob->previous();
  const int64_t dprev = prev ? mob->distanceTo(*prev) : std::numeric_limits<int64_t>::max();
  MatchObject* next = mob->next();
  const int64_t dnext = next ? mob->distanceTo(*next) : std::numeric_limits<int64_t>::max();

  m_project->removeMatch(mob);
  mob->deleteLater();

  if (prev || next)
  {
    m_matchEditorWidget->setCurrentMatchObject(dprev < dnext ? prev : next);
  }

  m_project->setModified(true);

  refreshUi();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
  settings().setValue(WINDOW_GEOM_KEY, saveGeometry());
  QMainWindow::closeEvent(event);
}

void MainWindow::onMatchEditingFinished(bool accepted)
{
  qDebug() << __FUNCSIG__ << "accepted=" << accepted;

  if (accepted)
  {
    const VideoMatch match = m_matchEditorWidget->editedMatch();
    auto* mobj = m_matchEditorWidget->currentMatchObject();
    qDebug() << mobj;
    if (mobj && mobj->value() != match)
    {
      mobj->setValue(match);
      m_project->setModified();
    }
  }

  m_matchEditorWidget->reset();
}

void MainWindow::refreshUi()
{
  m_actions.openProject->setEnabled(!m_project);
  m_actions.saveProject->setEnabled(m_project && m_project->modified());
  m_actions.exportProject->setEnabled(m_project != nullptr && m_primaryMedia);
  m_actions.toggleMatchListWindow->setEnabled(m_project);
  m_actions.toggleMatchListWindow->setChecked(m_matchListWindow && m_matchListWindow->isVisible());
  m_actions.deleteCurrentMatch->setEnabled(m_matchEditorWidget
                                           && m_matchEditorWidget->currentMatchObject());

  updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
  if (m_project)
  {
    QString title = m_project->projectTitle();
    if (m_project->modified())
    {
      title += "*";
    }

    title += " | DigiDub";
    setWindowTitle(title);
  }
  else
  {
    setWindowTitle("DigiDub");
  }
}

void MainWindow::launchMatchEditor()
{
  // TODO: faire ça proprement avec un QProgressDialog
  for (MediaObject* media : {m_primaryMedia, m_secondaryMedia})
  {
    if (!media->framesInfo())
    {
      media->extractFrames();
      if (media->frameExtractionThread())
      {
        connect(media, &MediaObject::framesAvailable, this, &MainWindow::launchMatchEditor);
        return;
      }
    }
  }
  m_matchEditorWidget = new MatchEditorWidget(*m_project, *m_primaryMedia, *m_secondaryMedia);
  auto* sw = qobject_cast<QStackedWidget*>(centralWidget());
  // TODO: clear QStackedWidget

  sw->addWidget(m_matchEditorWidget);
  sw->setCurrentIndex(sw->count() - 1);

  connect(m_matchEditorWidget,
          &MatchEditorWidget::editionFinished,
          this,
          &MainWindow::onMatchEditingFinished);

  m_project->sortMatches();
  if (!m_project->matches().empty())
  {
    m_matchEditorWidget->setCurrentMatchObject(m_project->matches().front());
    refreshUi();
  }
}

void MainWindow::setupConnectionsTo(DubbingProject* project)
{
  connect(project, &DubbingProject::modifiedChanged, this, &MainWindow::refreshUi);
}

void MainWindow::updateLastSaveDir(const QString& filePath)
{
  if (!filePath.isEmpty())
  {
    settings().setValue(LAST_SAVE_DIR_KEY, QFileInfo(filePath).absolutePath());
  }
}

void MainWindow::findMatchAfter(MatchObject& matchObject)
{
  MatchObject* next_match = nullptr;
  {
    std::vector<MatchObject*> matches = m_project->matches();
    ::sort(matches);

    auto it = std::find(matches.begin(), matches.end(), &matchObject);
    Q_ASSERT(it != matches.end());
    it = std::next(it);
    if (it != matches.end())
    {
      next_match = *it;
    }
  }

  const int64_t start = matchObject.value().a.end();
  int64_t end = 0;
  if (next_match)
  {
    end = next_match->value().a.start();
  }
  else
  {
    end = m_primaryMedia->duration() * 1000;
  }

  const auto srcsegment = TimeSegment(start, end);
  findMatch(srcsegment,
            TimeSegment(matchObject.value().b.end(),
                        matchObject.value().b.end() + srcsegment.duration()));
}

void MainWindow::findMatchBefore(MatchObject& matchObject)
{
  MatchObject* prev_match = nullptr;
  {
    std::vector<MatchObject*> matches = m_project->matches();
    ::sort(matches);

    auto it = std::find(matches.begin(), matches.end(), &matchObject);
    Q_ASSERT(it != matches.end());
    if (it != matches.begin())
    {
      prev_match = *std::prev(it);
    }
  }

  const int64_t end = matchObject.value().a.start();

  int64_t start = 0;
  if (prev_match)
  {
    start = prev_match->value().a.end();
  }

  const auto srcsegment = TimeSegment(start, end);
  findMatch(srcsegment,
            TimeSegment(matchObject.value().b.start() - srcsegment.duration(),
                        srcsegment.duration()));
}

void MainWindow::findMatch(const TimeSegment& withinSegment, const TimeSegment& defaultResult)
{
  QProgressDialog progress{"", "Cancel", 0, 4};
  QEventLoop loop;

  // TODO: do better
  // check that we have all the data
  {
    for (MediaObject* media : {m_primaryMedia, m_secondaryMedia})
    {
      Q_ASSERT(media->framesInfo());
    }

    if (!m_primaryMedia->silenceInfo())
    {
      m_primaryMedia->silencedetect();

      if (m_primaryMedia->silencedetectThread())
      {
        progress.setLabelText("Detecting silences on " + m_primaryMedia->fileName() + "...");
        connect(m_primaryMedia->silencedetectThread(),
                &QThread::finished,
                &loop,
                &QEventLoop::quit,
                Qt::QueuedConnection);
        loop.exec();
      }
    }

    progress.setValue(1);

    if (!m_primaryMedia->blackFramesInfo())
    {
      m_primaryMedia->blackdetect();

      if (m_primaryMedia->blackdetectThread())
      {
        progress.setLabelText("Detecting black frames on " + m_primaryMedia->fileName() + "...");

        connect(m_primaryMedia->blackdetectThread(),
                &QThread::finished,
                &loop,
                &QEventLoop::quit,
                Qt::QueuedConnection);
        loop.exec();
      }
    }

    progress.setValue(2);

    if (!m_primaryMedia->scenesInfo())
    {
      m_primaryMedia->scdet();

      if (m_primaryMedia->scdetThread())
      {
        progress.setLabelText("Detecting scene changes " + m_primaryMedia->fileName() + "...");

        connect(m_primaryMedia->scdetThread(),
                &QThread::finished,
                &loop,
                &QEventLoop::quit,
                Qt::QueuedConnection);
        loop.exec();
      }
    }

    progress.setValue(3);
  }

  progress.setLabelText("Running match algorithm. (may freeze)");

  MatchDetector detector{*m_primaryMedia, *m_secondaryMedia};

  detector.segmentA() = withinSegment;

  std::vector<VideoMatch> matches = detector.run();

  VideoMatch match;
  match.a = withinSegment;
  match.b = defaultResult;

  if (!matches.empty())
  {
    auto it = std::max_element(matches.begin(),
                               matches.end(),
                               [](const VideoMatch& a, const VideoMatch& b) {
                                 return a.a.duration() < b.a.duration();
                               });

    match = *it;
  }

  MatchObject* obj = m_project->createMatch(match);
  m_project->addMatch(obj);
  m_project->setModified();

  m_matchEditorWidget->setCurrentMatchObject(obj);
}

void MainWindow::insertMatchAt(int64_t pos)
{
  std::vector<MatchObject*> allmatches = m_project->matches();
  ::sort(allmatches);

  auto it = std::upper_bound(allmatches.begin(),
                             allmatches.end(),
                             pos,
                             [](int64_t v, MatchObject* e) { return v < e->value().a.start(); });

  if (it != allmatches.begin())
  {
    MatchObject* m = *std::prev(it);
    if (pos <= m->value().a.end())
    {
      QMessageBox::information(this, "Error", "Frame is already part of a match.");
      return;
    }
  }

  if (it != allmatches.end())
  {
    findMatchBefore(**it);
  }
  else
  {
    findMatchAfter(**std::prev(it));
  }
}

void MainWindow::debugProc()
{
  openFile("C:\\work\\digidub\\ignore\\ds1e03.txt");
}
