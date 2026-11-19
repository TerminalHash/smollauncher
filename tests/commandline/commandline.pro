include(../tests.pri)

TARGET = tst_commandline

SOURCES += ../tst_commandline.cpp \
           $$PWD/../../src/commandline.cpp

HEADERS += $$PWD/../../src/include/commandline.h
