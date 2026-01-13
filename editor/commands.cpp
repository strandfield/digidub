#include "commands.h"

AddMatch::AddMatch(MatchObject& match, DubbingProject& project)
    : m_project(project)
    , m_match(&match)
{
  setText("Add match");
}

void AddMatch::redo()
{
  m_project.addMatch(m_match);
}

void AddMatch::undo()
{
  m_project.removeMatch(m_match);
}

RemoveMatch::RemoveMatch(MatchObject& match, DubbingProject& project)
    : m_project(project)
    , m_match(&match)
{
  setText("Remove match");
}

void RemoveMatch::redo()
{
  m_project.removeMatch(m_match);
}

void RemoveMatch::undo()
{
  m_project.addMatch(m_match);
}

EditMatch::EditMatch(MatchObject& match, const VideoMatch& value)
    : m_match(match)
    , m_prev_value(match.value())
    , m_new_value(value)
{
  setText("Edit match");
}

void EditMatch::redo()
{
  m_match.setValue(m_new_value);
}

void EditMatch::undo()
{
  m_match.setValue(m_prev_value);
}
