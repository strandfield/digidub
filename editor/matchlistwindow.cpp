#include "matchlistwindow.h"

#include "mediaobject.h"
#include "project.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <QVBoxLayout>

#include <QStringList>
#include <QTime>
#include <QVariant>

#include <QKeyEvent>

#include <QDebug>

#include <algorithm>

MatchListWindow::MatchListWindow(DubbingProject& project, QWidget* parent)
    : QWidget(parent, Qt::Tool)
    , m_project(project)
{
  setWindowTitle("Match list");

  if (auto* layout = new QVBoxLayout(this))
  {
    layout->addWidget(m_matchListWidget = new QTreeWidget(this));
    m_matchListWidget->setContextMenuPolicy(Qt::NoContextMenu);

    m_matchListWidget->setColumnCount(4);
    m_matchListWidget->setHeaderLabels(QStringList() << "0@Start"
                                                     << "0@End"
                                                     << "1@Start"
                                                     << "1@End");
    m_matchListWidget->setIndentation(0);

    m_matchListWidget->installEventFilter(this);
  }

  connect(m_matchListWidget,
          &QTreeWidget::itemDoubleClicked,
          this,
          &MatchListWindow::onItemDoubleClicked);

  setupConnectionsTo(&m_project);

  resetMatchList();
}

MatchListWindow::~MatchListWindow() {}

DubbingProject& MatchListWindow::project() const
{
  return m_project;
}

void MatchListWindow::setVideoDuration(int64_t msecs)
{
  m_videoDuration = msecs;
}

static MatchObject* getMatchObject(const QTreeWidgetItem* item)
{
  if (!item)
  {
    return nullptr;
  }

  return qobject_cast<MatchObject*>(qvariant_cast<QObject*>(item->data(0, Qt::UserRole)));
}

void MatchListWindow::onItemDoubleClicked(QTreeWidgetItem* item)
{
  MatchObject* mob = getMatchObject(item);
  if (mob)
  {
    Q_EMIT matchDoubleClicked(mob);
  }
}

void MatchListWindow::onMatchAdded(MatchObject* mob)
{
  // pour l'instant:
  resetMatchList();
}

void MatchListWindow::onMatchRemoved(MatchObject* mob)
{
  // pour l'instant:
  resetMatchList();
}

void MatchListWindow::onMatchChanged(MatchObject* mob)
{
  for (int i(0); i < m_matchListWidget->topLevelItemCount(); ++i)
  {
    QTreeWidgetItem* item = m_matchListWidget->topLevelItem(i);

    if (getMatchObject(item) != mob)
    {
      continue;
    }

    fill(item, mob);

    return;
  }
}

bool MatchListWindow::eventFilter(QObject* watched, QEvent* event)
{
  if (watched != m_matchListWidget)
  {
    return false;
  }

  if (event->type() == QEvent::KeyPress)
  {
    auto* kev = static_cast<QKeyEvent*>(event);
    if (kev->key() == Qt::Key_Delete)
    {
      MatchObject* mob = getSelectedMatchObject();
      if (mob)
      {
        m_project.removeMatch(mob);
        mob->deleteLater();
        return true;
      }
    }

    if ((kev->key() == Qt::Key_Up || kev->key() == Qt::Key_Down)
        && (kev->modifiers() & (Qt::ControlModifier | Qt::AltModifier)))
    {
      MatchObject* mob = getSelectedMatchObject();
      if (mob)
      {
        if (kev->key() == Qt::Key_Up)
        {
          findMatchBefore(*mob);
        }
        else
        {
          findMatchAfter(*mob);
        }

        return true;
      }
    }
  }

  return false;
}

void MatchListWindow::closeEvent(QCloseEvent* ev)
{
  QWidget::closeEvent(ev);
  Q_EMIT closed();
}

void MatchListWindow::setupConnectionsTo(DubbingProject* project)
{
  connect(project, &DubbingProject::matchAdded, this, &MatchListWindow::onMatchAdded);
  connect(project, &DubbingProject::matchRemoved, this, &MatchListWindow::onMatchRemoved);
  connect(project, &DubbingProject::matchChanged, this, &MatchListWindow::onMatchChanged);
}

void MatchListWindow::resetMatchList()
{
  m_matchListWidget->clear();

  std::vector<MatchObject*> matches = m_project.matches();
  ::sort(matches);

  for (MatchObject* m : matches)
  {
    auto* item = new QTreeWidgetItem;
    fill(item, m);
    m_matchListWidget->addTopLevelItem(item);
  }
}

void MatchListWindow::fill(QTreeWidgetItem* item, MatchObject* m)
{
  item->setFlags(item->flags() | Qt::ItemNeverHasChildren);
  item->setData(0, Qt::UserRole, QVariant::fromValue((QObject*) m));

  item->setData(0, Qt::DisplayRole, Duration(m->value().a.start()).toString(Duration::HHMMSSzzz));
  item->setData(1, Qt::DisplayRole, Duration(m->value().a.end()).toString(Duration::HHMMSSzzz));
  item->setData(2, Qt::DisplayRole, Duration(m->value().b.start()).toString(Duration::HHMMSSzzz));
  item->setData(3, Qt::DisplayRole, Duration(m->value().b.end()).toString(Duration::HHMMSSzzz));

  // item->setData(0, Qt::EditRole, QTime::fromMSecsSinceStartOfDay(m->value().a.start()));
  // item->setData(1, Qt::EditRole, QTime::fromMSecsSinceStartOfDay(m->value().a.end()));
  // item->setData(2, Qt::EditRole, QTime::fromMSecsSinceStartOfDay(m->value().b.start()));
  // item->setData(3, Qt::EditRole, QTime::fromMSecsSinceStartOfDay(m->value().b.end()));
}

MatchObject* MatchListWindow::getSelectedMatchObject() const
{
  QTreeWidgetItem* selected = m_matchListWidget->currentItem();
  return getMatchObject(selected);
}

void MatchListWindow::findMatchAfter(MatchObject& matchObject)
{
  MatchObject* next_match = nullptr;
  {
    std::vector<MatchObject*> matches = m_project.matches();
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
    if (!m_videoDuration)
    {
      return;
    }

    end = m_videoDuration;
  }

  const auto srcsegment = TimeSegment(start, end);
  Q_EMIT findMatchRequested(srcsegment,
                            TimeSegment(matchObject.value().b.end(),
                                        matchObject.value().b.end() + srcsegment.duration()));
}

void MatchListWindow::findMatchBefore(MatchObject& matchObject)
{
  MatchObject* prev_match = nullptr;
  {
    std::vector<MatchObject*> matches = m_project.matches();
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
  Q_EMIT findMatchRequested(srcsegment,
                            TimeSegment(matchObject.value().b.start() - srcsegment.duration(),
                                        srcsegment.duration()));
}
