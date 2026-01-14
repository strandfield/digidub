// Copyright (C) 2025 Vincent Chambrin
// This file is part of the 'digidub' project.
// For conditions of distribution and use, see copyright notice in LICENSE.

#include "appsettings.h"
#include "window.h"

#include <QApplication>
#include <QVersionNumber>

#include <QFileInfo>

int main(int argc, char *argv[])
{
  QApplication::setOrganizationName("Analogman Software");
  //QApplication::setOrganizationDomain("mysoft.com");
  QApplication::setApplicationName("DigiDub");
  QCoreApplication::setApplicationVersion(
      QVersionNumber(DIGIDUB_VERSION_MAJOR, DIGIDUB_VERSION_MINOR).toString());

  QApplication::setStyle("fusion");

  QApplication app{argc, argv};

  auto* settings = new AppSettings(&app);

  MainWindow window;
  window.show();

  if (app.arguments().size() > 1)
  {
    for (int i(1); i < app.arguments().size(); ++i)
    {
      if (QFileInfo::exists(app.arguments().at(i)))
      {
        window.openFile(app.arguments().at(i));
      }
    }
  }

  return app.exec();
}
