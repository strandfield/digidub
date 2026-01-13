// Copyright (C) 2026 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#pragma once

#include "project.h"

#include <QUndoCommand>

class AddMatch : public QUndoCommand
{
public:
  AddMatch(MatchObject& match, DubbingProject& project);

  void redo() final;
  void undo() final;

private:
  DubbingProject& m_project;
  MatchObject* m_match;
};

class RemoveMatch : public QUndoCommand
{
public:
  RemoveMatch(MatchObject& match, DubbingProject& project);

  void redo() final;
  void undo() final;

private:
  DubbingProject& m_project;
  MatchObject* m_match;
};

class EditMatch : public QUndoCommand
{
public:
  EditMatch(MatchObject& match, const VideoMatch& value);

  void redo() final;
  void undo() final;

private:
  MatchObject& m_match;
  VideoMatch m_prev_value;
  VideoMatch m_new_value;
};
