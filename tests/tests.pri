QT += core gui testlib
CONFIG += console testcase c++17
CONFIG -= app_bundle debug_and_release
TEMPLATE = app

INCLUDEPATH += $$PWD/../src

lessThan(QT_MAJOR_VERSION, 6): error("SmolLauncher requires Qt 6.7 or newer")
equals(QT_MAJOR_VERSION, 6):lessThan(QT_MINOR_VERSION, 7): \
    error("SmolLauncher requires Qt 6.7 or newer")
