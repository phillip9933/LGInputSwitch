QT += widgets concurrent
CONFIG += c++17
TARGET = desktop_smoke
SOURCES += ../tests/desktop_smoke.cpp platform.cpp ../core/input_switcher.cpp ../platform/linux/ddcutil_backend.cpp
