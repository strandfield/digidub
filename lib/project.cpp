#include "project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <algorithm>
#include <set>

MatchObject::MatchObject(const QString& text, QObject* parent)
    : QObject(parent)
{
  QStringList parts = text.split('~', Qt::SkipEmptyParts);
  if (parts.size() != 2)
  {
    throw std::runtime_error("bad");
  }

  m_value.a = TimeSegment::fromString(parts.front());
  m_value.b = TimeSegment::fromString(parts.back());
}

MatchObject::MatchObject(const VideoMatch& val, QObject* parent)
    : QObject(parent)
    , m_value(val)
{}

DubbingProject* MatchObject::project() const
{
  return qobject_cast<DubbingProject*>(parent());
}

MatchObject* MatchObject::previous() const
{
  return m_previous;
}

MatchObject* MatchObject::next() const
{
  return m_next;
}

int64_t MatchObject::distanceTo(const MatchObject& other) const
{
  if (other.value().a.end() <= value().a.start())
  {
    return this->value().a.start() - other.value().a.end();
  }
  else
  {
    return other.value().a.start() - this->value().a.end();
  }
}

const VideoMatch& MatchObject::value() const
{
  return m_value;
}

void MatchObject::setValue(const VideoMatch& val)
{
  if (m_value != val)
  {
    m_value = val;
    Q_EMIT changed();
  }
}

QString MatchObject::toString() const
{
  return value().a.toString() + "~" + value().b.toString();
}

bool MatchObject::active() const
{
  return m_active;
}

void MatchObject::setActive(bool active)
{
  if (m_active != active)
  {
    m_active = active;
    Q_EMIT activeChanged();

    if (!m_active)
    {
      Q_EMIT deleted();
    }
  }
}

void MatchObject::setDeleted(bool deleted)
{
  setActive(!deleted);
}

void sort(std::vector<MatchObject*>& matches)
{
  std::sort(matches.begin(), matches.end(), [](const MatchObject* a, const MatchObject* b) {
    return a->value().a.start() < b->value().a.start();
  });
}

std::vector<VideoMatch> convert2vm(const std::vector<MatchObject*>& matches, bool isSorted)
{
  std::vector<VideoMatch> result;
  result.reserve(matches.size());
  for (const MatchObject* m : matches)
  {
    result.push_back(m->value());
  }

  if (!isSorted)
  {
    std::sort(result.begin(), result.end(), [](const VideoMatch& a, const VideoMatch& b) {
      return a.a.start() < b.a.start();
    });
  }

  return result;
}

DubbingProject::DubbingProject(QObject* parent)
    : QObject(parent)
{}

DubbingProject::DubbingProject(const QString& filePathOrTitle, QObject* parent)
    : QObject(parent)
{
  if (QFile::exists(filePathOrTitle))
  {
    load(filePathOrTitle);
  }
  else
  {
    m_projectTitle = filePathOrTitle;
  }
}

DubbingProject::DubbingProject(const QString& videoPath, const QString& audioPath, QObject* parent)
    : QObject(parent)
    , m_videoFilePath(videoPath)
    , m_audioSourceFilePath(audioPath)
{
  m_projectTitle = QFileInfo(videoPath).completeBaseName();
}

const QString& DubbingProject::projectTitle() const
{
  return m_projectTitle;
}

void DubbingProject::setProjectTitle(const QString& title)
{
  if (m_projectTitle != title)
  {
    m_projectTitle = title;
    Q_EMIT projectTitleChanged();
  }
}

const QString& DubbingProject::projectFilePath() const
{
  return m_projectFilePath;
}

void DubbingProject::setProjectFilePath(const QString& path)
{
  if (m_projectFilePath != path)
  {
    convertFilePathsToAbsolute();
    m_projectFilePath = path;
  }
}

int DubbingProject::convertFilePathsToAbsolute()
{
  if (projectFilePath().isEmpty())
  {
    return 0;
  }

  const QDir pd = projectDirectory();
  int converted = 0;

  for (QString* path :
       {&m_videoFilePath, &m_audioSourceFilePath, &m_subtitlesFilePath, &m_outputFilePath})
  {
    if (path->isEmpty())
      continue;

    QFileInfo info{*path};
    if (info.isRelative())
    {
      *path = pd.absoluteFilePath(*path);
      ++converted;
    }
  }

  return converted;
}

int DubbingProject::convertFilePathsToRelative()
{
  if (projectFilePath().isEmpty())
  {
    return 0;
  }

  const QDir pd = projectDirectory();
  int converted = 0;

  for (QString* path :
       {&m_videoFilePath, &m_audioSourceFilePath, &m_subtitlesFilePath, &m_outputFilePath})
  {
    if (path->isEmpty())
      continue;

    QFileInfo info{*path};
    if (info.isAbsolute())
    {
      QString relpath = pd.relativeFilePath(info.absoluteFilePath());
      if (!relpath.contains(".."))
      {
        *path = relpath;
        ++converted;
      }
    }
  }

  return converted;
}

QDir DubbingProject::projectDirectory() const
{
  return QFileInfo(projectFilePath()).absoluteDir();
}

bool DubbingProject::load(const QString& projectFilePath)
{
  QFile file{projectFilePath};
  if (!file.open(QIODevice::ReadOnly))
  {
    return false;
  }

  QByteArray line = file.readLine(1024);
  if (!line.startsWith("DIGIDUB PROJECT"))
  {
    qDebug() << "not a digidub project";
    return false;
  }

  line = file.readLine();
  if (!line.startsWith("VERSION "))
  {
    return false;
  }

  const int version = line.mid(8).toInt();
  if (version != 1)
  {
    qDebug() << "bad version";
    return false;
  }

  m_videoFilePath.clear();
  m_audioSourceFilePath.clear();
  m_outputFilePath.clear();
  m_subtitlesFilePath.clear();
  m_matches.clear();

  while (!file.atEnd())
  {
    line = file.readLine();

    if (line.startsWith("TITLE "))
    {
      m_projectTitle = QString::fromUtf8(line.mid(6).trimmed());
    }
    else if (line.startsWith("VIDEO "))
    {
      m_videoFilePath = QString::fromUtf8(line.mid(6).trimmed());
    }
    else if (line.startsWith("AUDIO "))
    {
      m_audioSourceFilePath = QString::fromUtf8(line.mid(6).trimmed());
    }
    else if (line.startsWith("OUTPUT "))
    {
      m_outputFilePath = QString::fromUtf8(line.mid(7).trimmed());
    }
    else if (line.startsWith("SUBTITLES "))
    {
      m_subtitlesFilePath = QString::fromUtf8(line.mid(10).trimmed());
    }
    else if (line.startsWith("MATCHES ")) // deprecated match list
    {
      const int n = line.mid(8).trimmed().toInt();

      for (int i(0); i < n; ++i)
      {
        line = file.readLine().trimmed();
        try
        {
          m_matches.push_back(new MatchObject(QString::fromUtf8(line), this));
          setupConnectionsTo(m_matches.back());
        } catch (const std::runtime_error&)
        {
          qDebug() << "failed to parser match: " << line;
          return false;
        }
      }
    }
    else if (line.startsWith("BEGIN MATCHLIST"))
    {
      while (!file.atEnd())
      {
        line = file.readLine().trimmed();
        if (line.startsWith("END MATCHLIST"))
        {
          break;
        }

        try
        {
          m_matches.push_back(new MatchObject(QString::fromUtf8(line), this));
          setupConnectionsTo(m_matches.back());
        } catch (const std::runtime_error&)
        {
          qDebug() << "failed to parser match: " << line;
          return false;
        }
      }
    }
    else if (!line.trimmed().isEmpty())
    {
      qDebug() << "ignoring non-emtpy line: " << line.trimmed();
    }
  }

  if (!m_matches.empty())
  {
    ::sort(m_matches);

    MatchObject* previous = nullptr;
    for (MatchObject* current : m_matches)
    {
      current->setActive(true);

      if (previous)
      {
        previous->m_next = current;
      }

      current->m_previous = previous;

      previous = current;
    }
  }

  if (std::exchange(m_projectFilePath, projectFilePath) != projectFilePath)
  {
    Q_EMIT projectFilePathChanged();
  }

  return true;
}

void DubbingProject::save(const QString& path)
{
  QFile file{path};
  if (!file.open(QIODevice::ReadWrite | QIODevice::Truncate))
  {
    return;
  }

  QTextStream stream{&file};
  dump(stream);
}

void DubbingProject::dump(QTextStream& stream)
{
  const std::vector<MatchObject*>& ms = matches();

  stream << "DIGIDUB PROJECT\n";
  stream << "VERSION 1\n";
  if (!projectTitle().isEmpty())
  {
    stream << "TITLE " << projectTitle() << "\n";
  }
  stream << "VIDEO " << videoFilePath() << "\n";
  stream << "AUDIO " << audioSourceFilePath() << "\n";
  if (!subtitlesFilePath().isEmpty())
  {
    stream << "SUBTITLES " << subtitlesFilePath() << "\n";
  }
  if (!m_outputFilePath.isEmpty())
  {
    stream << "OUTPUT " << outputFilePath() << "\n";
  }
  if (!ms.empty())
  {
    stream << "BEGIN MATCHLIST (" << ms.size() << ")"
           << "\n";
    for (const MatchObject* m : ms)
    {
      stream << m->toString() << "\n";
    }
    stream << "END MATCHLIST"
           << "\n";
  }
}

const QString& DubbingProject::videoFilePath() const
{
  return m_videoFilePath;
}

void DubbingProject::setVideoFilePath(const QString& path)
{
  m_videoFilePath = path;
}

const QString& DubbingProject::audioSourceFilePath() const
{
  return m_audioSourceFilePath;
}

void DubbingProject::setAudioSourceFilePath(const QString& path)
{
  m_audioSourceFilePath = path;
}

const QString& DubbingProject::subtitlesFilePath() const
{
  return m_subtitlesFilePath;
}

void DubbingProject::setSubtitlesFilePath(const QString& subtitlesFile)
{
  if (m_subtitlesFilePath != subtitlesFile)
  {
    m_subtitlesFilePath = subtitlesFile;
    Q_EMIT subtitlesFilePathChanged(subtitlesFile);
  }
}

const QString& DubbingProject::outputFilePath() const
{
  return m_outputFilePath;
}

void DubbingProject::setOutputFilePath(const QString& filePath)
{
  if (m_outputFilePath != filePath)
  {
    m_outputFilePath = filePath;
    Q_EMIT outputFilePathChanged(filePath);
  }
}

QString DubbingProject::resolvePath(const QString& filePath) const
{
  if (filePath.isEmpty())
  {
    return filePath;
  }

  QFileInfo info{filePath};
  if (info.isAbsolute())
  {
    return filePath;
  }
  else
  {
    info = QFileInfo(projectFilePath());
    return QFileInfo(info.absolutePath() + "/" + filePath).absoluteFilePath();
  }
}

MatchObject* DubbingProject::createMatch(const VideoMatch& val)
{
  auto* result = new MatchObject(val, this);
  setupConnectionsTo(result);
  return result;
}

void DubbingProject::addMatch(MatchObject* match)
{
  Q_ASSERT(std::find(m_matches.begin(), m_matches.end(), match) == m_matches.end());

  auto it = std::upper_bound(m_matches.begin(),
                             m_matches.end(),
                             match,
                             [](MatchObject* a, MatchObject* b) {
                               return a->value().a.start() < b->value().a.start();
                             });

  MatchObject* next = nullptr;
  MatchObject* previous = nullptr;

  if (it != m_matches.end())
  {
    next = *it;
  }

  if (it != m_matches.begin())
  {
    previous = *std::prev(it);
  }

  m_matches.insert(it, match);

  if (next)
  {
    next->m_previous = match;
    match->m_next = next;
  }

  if (previous)
  {
    previous->m_next = match;
    match->m_previous = previous;
  }

  match->setDeleted(false);

  Q_EMIT matchAdded(match);

  if (next)
  {
    Q_EMIT match->nextChanged();
    Q_EMIT next->previousChanged();
  }

  if (previous)
  {
    Q_EMIT match->previousChanged();
    Q_EMIT previous->nextChanged();
  }
}

void DubbingProject::removeMatch(MatchObject* match)
{
  if (match->project() != this)
  {
    Q_ASSERT(false && "trying to remove match from another DubbingProject");
    return;
  }

  if (!match->active())
  {
    qDebug() << "trying to remove match, but it is already inactive: " << match;
    return;
  }

  auto it = std::find(m_matches.begin(), m_matches.end(), match);

  MatchObject* prev = match->previous();
  MatchObject* next = match->next();

  if (prev)
  {
    prev->m_next = next;
  }

  if (next)
  {
    next->m_previous = prev;
  }

  match->m_previous = nullptr;
  match->m_next = nullptr;

  m_matches.erase(it);
  match->setDeleted();
  Q_EMIT matchRemoved(match);

  if (prev)
  {
    Q_EMIT match->previousChanged();
    Q_EMIT prev->nextChanged();
  }

  if (next)
  {
    Q_EMIT match->nextChanged();
    Q_EMIT next->previousChanged();
  }
}

// Returns the sorted list of match objects that are active.
const std::vector<MatchObject*>& DubbingProject::matches() const
{
  return m_matches;
}

void DubbingProject::addMatches(const std::vector<VideoMatch>& values)
{
  for (const VideoMatch& m : values)
  {
    MatchObject* obj = createMatch(m);
    addMatch(obj);
  }
}

std::vector<MatchObject*> DubbingProject::matchObjects() const
{
  const QList<MatchObject*> list = findChildren<MatchObject*>();
  return std::vector<MatchObject*>(list.begin(), list.end());
}

void DubbingProject::onMatchChanged()
{
  auto* const m = qobject_cast<MatchObject*>(sender());
  if (!m)
  {
    return;
  }

  Q_ASSERT(m->project() == this);

  const bool next_is_okay = !m->next() || m->next()->value().a.start() > m->value().a.start();
  const bool prev_is_okay = !m->previous()
                            || m->previous()->value().a.start() < m->value().a.start();

  if (next_is_okay && prev_is_okay)
  {
    Q_EMIT matchChanged(m);
    return;
  }

  std::set<MatchObject*> nextchanged, prevchanged;

  ::sort(m_matches);

  if (m_matches.front()->previous())
  {
    m_matches.front()->m_previous = nullptr;
    prevchanged.insert(m_matches.front());
  }

  if (m_matches.back()->next())
  {
    m_matches.back()->m_next = nullptr;
    nextchanged.insert(m_matches.back());
  }

  MatchObject* previous = nullptr;
  for (MatchObject* current : m_matches)
  {
    if (previous)
    {
      if (previous->m_next != current)
      {
        previous->m_next = current;
        nextchanged.insert(previous);
      }
    }

    if (current->m_previous != previous)
    {
      current->m_previous = previous;
      prevchanged.insert(current);
    }

    previous = current;
  }

  Q_EMIT matchChanged(m);

  for (MatchObject* match : prevchanged)
  {
    Q_EMIT match->previousChanged();
  }

  for (MatchObject* match : nextchanged)
  {
    Q_EMIT match->nextChanged();
  }
}

void DubbingProject::setupConnectionsTo(MatchObject* mobj)
{
  connect(mobj, &MatchObject::changed, this, &DubbingProject::onMatchChanged);
}
