QT += widgets dbus
CONFIG += c++17
DEFINES += LG_HAVE_KGLOBALACCEL
INCLUDEPATH += /usr/include/KF6/KGlobalAccel
LIBS += -lKF6GlobalAccel
TARGET = kde_shortcut_probe
SOURCES += ../tests/kde_shortcut_probe.cpp
