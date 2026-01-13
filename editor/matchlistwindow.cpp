#include "matchlistwindow.h"

#include "commands.h"
#include "window.h"

#include "mediaobject.h"
#include "project.h"

#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <QVBoxLayout>

#include <QStringList>
#include <QTime>
#include <QUndoStack>
#include <QVariant>

#include <QKeyEvent>

#include <QDebug>

#include <algorithm>

MatchListWindow::MatchListWindow(DubbingProject& project, MainWindow& window, QWidget* parent)
    : QWidget(parent, Qt::Tool)
    , m_project(project)
    , m_window(window)
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
        m_window.undoStack().push(new RemoveMatch(*mob, m_project));
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
          m_window.findMatchBefore(*mob);
        }
        else
        {
          m_window.findMatchAfter(*mob);
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

  const std::vector<MatchObject*>& matches = m_project.matches();

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
