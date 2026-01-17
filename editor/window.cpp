#include "window.h"

#include "appsettings.h"
#include "commands.h"

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
#include <QUndoStack>

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
                             "digidub",
                             this);

  m_undoStack = new QUndoStack(this);
  m_undoStack->setActive(false);
  connect(m_undoStack, &QUndoStack::cleanChanged, this, &MainWindow::refreshUi);

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
    QAction* undo = m_undoStack->createUndoAction(this);
    undo->setShortcut(QKeySequence("Ctrl+Z"));
    QAction* redo = m_undoStack->createRedoAction(this);
    redo->setShortcut(QKeySequence("Ctrl+Y"));

    menu->addAction(undo);
    menu->addAction(redo);

    menu->addSeparator();

    // TODO: disable these actions when triggering them makes no sense
    m_actions.findMatchBefore = menu->addAction("Find match before current match",
                                                QKeySequence("Ctrl+Alt+Left"),
                                                this,
                                                &MainWindow::findMatchBeforeCurrentMatch);
    m_actions.findMatchAfter = menu->addAction("Find match after current match",
                                               QKeySequence("Ctrl+Alt+Right"),
                                               this,
                                               &MainWindow::findMatchAfterCurrentMatch);
    m_actions.insertMatch = menu->addAction("Insert match from selection",
                                            QKeySequence("Ctrl+Shift+I"),
                                            this,
                                            &MainWindow::insertMatchFromSelection);

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

  if (QMenu* menu = menuBar()->addMenu("Help"))
  {
    menu->addAction("About", this, &MainWindow::about);
    menu->addAction("About Qt", qApp, &QApplication::aboutQt);
  }

#ifndef NDEBUG
  if (QMenu* menu = menuBar()->addMenu("Debug"))
  {
    menu->addAction("Run debug proc", this, &MainWindow::debugProc);
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
  disconnect(m_undoStack, nullptr, this, nullptr);
  m_undoStack->clear();
}

QSettings& MainWindow::settings() const
{
  return *m_settings;
}

QUndoStack& MainWindow::undoStack() const
{
  return *m_undoStack;
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

    m_undoStack->setActive();

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

void MainWindow::about()
{
  if (!m_aboutDialog)
  {
    m_aboutDialog = new QDialog(this);
    m_aboutDialog->setWindowTitle("About | Digidub");

    auto* layout = new QVBoxLayout(m_aboutDialog);

    if (auto* row = new QHBoxLayout)
    {
      row->setSpacing(8);
      auto* pic = new QLabel(this);
      pic->setPixmap(QPixmap(":/images/ShogunGekomon.png"));
      row->addWidget(pic);

      row->addWidget(new QLabel(QString("This is Digidub v%1.").arg(qApp->applicationVersion())),
                     1,
                     Qt::AlignVCenter);

      layout->addLayout(row);
    }

    auto* btn = new QPushButton("Ok");
    connect(btn, &QPushButton::clicked, m_aboutDialog, &QDialog::close);
    layout->addWidget(btn, 0, Qt::AlignHCenter);
  }

  m_aboutDialog->show();
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
  m_undoStack->setClean();
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
    m_undoStack->resetClean();
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

void MainWindow::insertMatchFromSelection()
{
  if (!m_matchEditorWidget)
  {
    return;
  }

  auto [r1, r2] = m_matchEditorWidget->selectedRanges();

  if (!r1 || !r2)
  {
    QMessageBox::information(this, "Bad", "Bad selection.\nCannot create match from this.");
    return;
  }

  VideoMatch match;
  match.a = m_primaryMedia->convertFrameRangeToTimeSegment(r1.first, r1.last);
  match.b = m_secondaryMedia->convertFrameRangeToTimeSegment(r2.first, r2.last);

  // TODO: crop match if it overlaps with another one

  MatchObject* obj = m_project->createMatch(match);
  m_undoStack->push(new AddMatch(*obj, *m_project));

  m_matchEditorWidget->setCurrentMatchObject(obj);
}

void MainWindow::deleteCurrentMatch()
{
  MatchObject* mob = m_matchEditorWidget->currentMatchObject();

  MatchObject* prev = mob->previous();
  const int64_t dprev = prev ? mob->distanceTo(*prev) : std::numeric_limits<int64_t>::max();
  MatchObject* next = mob->next();
  const int64_t dnext = next ? mob->distanceTo(*next) : std::numeric_limits<int64_t>::max();

  if (prev || next)
  {
    m_matchEditorWidget->setCurrentMatchObject(dprev < dnext ? prev : next);
  }

  m_undoStack->push(new RemoveMatch(*mob, *m_project));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
  if (m_undoStack->isActive() && !m_undoStack->isClean())
  {
    int btn = QMessageBox::question(this,
                                    "Exit",
                                    "Unsaved changes will be lost. Continue ?",
                                    QMessageBox::Ok | QMessageBox::Cancel,
                                    QMessageBox::Cancel);

    if (btn != QMessageBox::Ok)
    {
      event->setAccepted(false);
      return;
    }
  }

  settings().setValue(WINDOW_GEOM_KEY, saveGeometry());
  QMainWindow::closeEvent(event);
}

void MainWindow::refreshUi()
{
  m_actions.openProject->setEnabled(!m_project);
  m_actions.saveProject->setEnabled(m_project && !m_undoStack->isClean());
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
    if (!m_undoStack->isClean())
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

  if (!m_project->matches().empty())
  {
    m_matchEditorWidget->setCurrentMatchObject(m_project->matches().front());
    refreshUi();
  }
}

void MainWindow::updateLastSaveDir(const QString& filePath)
{
  if (!filePath.isEmpty())
  {
    settings().setValue(LAST_SAVE_DIR_KEY, QFileInfo(filePath).absolutePath());
  }
}

void MainWindow::findMatchAfter(MatchObject& matchObject, std::optional<int64_t> requiredTimestamp)
{
  MatchObject* next_match = matchObject.next();

  const int64_t start = matchObject.value().a.end();
  const int64_t end = next_match ? next_match->value().a.start()
                                 : int64_t(m_primaryMedia->duration() * 1000);

  const auto srcsegment = TimeSegment(start, end);
  findMatch(srcsegment, requiredTimestamp);
}

void MainWindow::findMatchBefore(MatchObject& matchObject, std::optional<int64_t> requiredTimestamp)
{
  MatchObject* prev_match = matchObject.previous();
  const int64_t end = matchObject.value().a.start();
  const int64_t start = prev_match ? prev_match->value().a.end() : 0;

  const auto srcsegment = TimeSegment(start, end);
  findMatch(srcsegment, requiredTimestamp);
}

void MainWindow::findMatchAfterCurrentMatch()
{
  if (!m_matchEditorWidget || !m_matchEditorWidget->currentMatchObject())
  {
    return;
  }

  findMatchAfter(*m_matchEditorWidget->currentMatchObject());
}

void MainWindow::findMatchBeforeCurrentMatch()
{
  if (!m_matchEditorWidget || !m_matchEditorWidget->currentMatchObject())
  {
    return;
  }

  findMatchBefore(*m_matchEditorWidget->currentMatchObject());
}

void MainWindow::findMatch(const TimeSegment& withinSegment,
                           std::optional<int64_t> requiredTimestamp)
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

  detector.segmentA = withinSegment;
  // Note: on pourrait essayer de restreindre la plage de recherche dans la
  // deuxième vidéo, mais jusqu'à présent il n'y a jamais vraiment eu besoin.

  std::vector<VideoMatch> matches = detector.run();

  if (matches.empty())
  {
    QMessageBox::information(this, "Failed", "No match could be found.");
    return;
  }

  VideoMatch match;

  if (requiredTimestamp.has_value())
  {
    // find a match that contains the given timestamp
    const int64_t ts = *requiredTimestamp;

    auto it = std::find_if(matches.begin(), matches.end(), [ts](const VideoMatch& e) {
      return e.a.contains(ts);
    });

    if (it == matches.end())
    {
      QMessageBox::information(this,
                               "Failed",
                               "Could not find any match containing the specified frame.");
      return;
    }

    match = *it;
  }
  else
  {
    // find the longest match

    auto it = std::max_element(matches.begin(),
                               matches.end(),
                               [](const VideoMatch& a, const VideoMatch& b) {
                                 return a.a.duration() < b.a.duration();
                               });

    match = *it;
  }

  MatchObject* obj = m_project->createMatch(match);
  m_undoStack->push(new AddMatch(*obj, *m_project));

  m_matchEditorWidget->setCurrentMatchObject(obj);
}

void MainWindow::findMatchContaining(int64_t pos)
{
  const std::vector<MatchObject*>& allmatches = m_project->matches();

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
    findMatchBefore(**it, pos);
  }
  else
  {
    findMatchAfter(**std::prev(it), pos);
  }
}

void MainWindow::debugProc()
{
  openFile("C:\\work\\digidub\\ignore\\ds1e03.txt");
}
