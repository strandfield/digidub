// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#ifndef VFPARSER_H
#define VFPARSER_H

#include <QMap>
#include <QString>
#include <QStringList>

class VideoFilter
{
public:
  QString name;
  QMap<QString, QString> args;

public:
  explicit VideoFilter(const QString& filterName)
      : name(filterName)
  {}
};

class VideoFilters
{
public:
  std::vector<VideoFilter> filters;
};

inline VideoFilters vfparse(const QString& text)
{
  VideoFilters result;

  QStringList filterstrs = text.split(',', Qt::SkipEmptyParts);
  for (const QString& fstr : filterstrs)
  {
    int i = fstr.indexOf('=');
    if (i == -1)
    {
      result.filters.emplace_back(fstr);
      continue;
    }

    result.filters.emplace_back(fstr.left(i).simplified());
    VideoFilter& filter = result.filters.back();

    const QStringList argstrs = fstr.mid(i + 1).simplified().split(':', Qt::SkipEmptyParts);

    for (const QString& astr : argstrs)
    {
      i = astr.indexOf('=');
      if (i == -1)
      {
        filter.args[astr] = QString();
      }
      else
      {
        filter.args[astr.left(i)] = astr.mid(i + 1);
      }
    }
  }

  return result;
}

#endif // VFPARSER_H
