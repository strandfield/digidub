
#include "cache.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

QString GetCacheDir()
{
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

void CreateCacheDir()
{
  const QString path = GetCacheDir();
  if (!QFileInfo::exists(path))
  {
    QDir().mkpath(path);
  }
}
