QT += widgets concurrent
CONFIG += c++17
TARGET = LGInputSwitchDesktop
SOURCES += main.cpp platform.cpp ../core/input_switcher.cpp ../platform/linux/ddcutil_backend.cpp
