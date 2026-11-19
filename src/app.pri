# Sources shared by the launcher and the tests. Paths are relative to this
# file so the same list can be included from other build directories.

QT += core gui widgets svg sql
CONFIG += c++17
INCLUDEPATH += $$PWD

SOURCES += $$PWD/mainwindow.cpp \
           $$PWD/categorypane.cpp \
           $$PWD/catwidget.cpp \
           $$PWD/aboutdialog.cpp \
           $$PWD/desktopentry.cpp \
           $$PWD/desktoppickerdialog.cpp \
           $$PWD/configpaths.cpp \
           $$PWD/platformpaths.cpp \
           $$PWD/configfile.cpp \
           $$PWD/commandline.cpp \
           $$PWD/debug.cpp \
           $$PWD/contextmenucontroller.cpp \
           $$PWD/editorcontroller.cpp \
           $$PWD/launchcontroller.cpp \
           $$PWD/iconpickerdialog.cpp \
           $$PWD/launcherfilterproxy.cpp \
           $$PWD/importers/steam/steamimporter.cpp \
           $$PWD/importers/steam/steampickerdialog.cpp \
           $$PWD/importers/heroic/heroicimporter.cpp \
           $$PWD/importers/heroic/heroicpickerdialog.cpp \
           $$PWD/importers/itchio/itchimporter.cpp \
           $$PWD/importers/itchio/itchpickerdialog.cpp \
           $$PWD/importers/retroarch/retroarchimporter.cpp \
           $$PWD/importers/retroarch/retroarchpickerdialog.cpp \
           $$PWD/importers/prism/prismimporter.cpp \
           $$PWD/importers/prism/prismpickerdialog.cpp \
           $$PWD/importers/importcontroller.cpp

HEADERS += $$PWD/include/mainwindow.h \
           $$PWD/include/categorypane.h \
           $$PWD/include/catwidget.h \
           $$PWD/include/appitem.h \
           $$PWD/include/launchermodel.h \
           $$PWD/include/aboutdialog.h \
           $$PWD/include/desktopentry.h \
           $$PWD/include/desktoppickerdialog.h \
           $$PWD/include/configpaths.h \
           $$PWD/include/platformpaths.h \
           $$PWD/include/configfile.h \
           $$PWD/include/commandline.h \
           $$PWD/include/debug.h \
           $$PWD/contextmenucontroller.h \
           $$PWD/editorcontroller.h \
           $$PWD/launchcontroller.h \
           $$PWD/include/iconpickerdialog.h \
           $$PWD/include/launcherfilterproxy.h \
           $$PWD/importers/steam/steamimporter.h \
           $$PWD/importers/steam/steampickerdialog.h \
           $$PWD/importers/heroic/heroicimporter.h \
           $$PWD/importers/heroic/heroicpickerdialog.h \
           $$PWD/importers/itchio/itchimporter.h \
           $$PWD/importers/itchio/itchpickerdialog.h \
           $$PWD/importers/retroarch/retroarchimporter.h \
           $$PWD/importers/retroarch/retroarchpickerdialog.h \
           $$PWD/importers/prism/prismimporter.h \
           $$PWD/importers/prism/prismpickerdialog.h \
           $$PWD/importers/importcontroller.h

FORMS += $$PWD/../smollauncher_UI.ui \
         $$PWD/aboutdialog.ui \
         $$PWD/iconpickerdialog.ui \
         $$PWD/desktoppickerdialog.ui \
         $$PWD/importers/steam/steampickerdialog.ui \
         $$PWD/importers/heroic/heroicpickerdialog.ui \
         $$PWD/importers/itchio/itchpickerdialog.ui \
         $$PWD/importers/retroarch/retroarchpickerdialog.ui \
         $$PWD/importers/prism/prismpickerdialog.ui

TRANSLATIONS += $$PWD/../i18n/smollauncher_ru.ts
CONFIG += lrelease embed_translations

RESOURCES += $$PWD/../resources.qrc
