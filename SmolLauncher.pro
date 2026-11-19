include(src/app.pri)

TARGET = smollauncher
TEMPLATE = app

CONFIG  -= debug_and_release debug
CONFIG  += release

# Strip the binary; debug symbols aren't needed at runtime. "-s" is a
# GNU/ELF linker flag and is not valid for MSVC or Apple's linker.
unix:!macx {
    CONFIG += strip
    QMAKE_LFLAGS_RELEASE += -s
}

# QProcess::setChildProcessModifier() arrived in Qt 6.7 and is used to detach
# launched applications. The filter proxy's newer API is already guarded by version.
lessThan(QT_MAJOR_VERSION, 6): error("SmolLauncher requires Qt 6.7 or newer")
equals(QT_MAJOR_VERSION, 6):lessThan(QT_MINOR_VERSION, 7): \
    error("SmolLauncher requires Qt 6.7 or newer")

# toml++ is header-only. Install it (package "tomlplusplus") or add its include
# directory to INCLUDEPATH (vcpkg/Homebrew installs it outside the default path).
SOURCES += src/main.cpp
