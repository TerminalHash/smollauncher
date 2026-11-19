include(../tests.pri)

TARGET = tst_configfile

SOURCES += ../tst_configfile.cpp \
           $$PWD/../../src/configfile.cpp \
           $$PWD/../../src/commandline.cpp

HEADERS += $$PWD/../../src/include/configfile.h \
           $$PWD/../../src/include/commandline.h \
           $$PWD/../../src/include/appitem.h \
           $$PWD/../../src/include/launchermodel.h
