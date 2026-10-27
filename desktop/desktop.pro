QT += widgets concurrent
CONFIG += c++17
TARGET = LGInputSwitchDesktop
SOURCES += main.cpp platform.cpp ../core/input_switcher.cpp ../platform/linux/ddcutil_backend.cpp
unix:!macx:exists(/usr/include/KF6/KGlobalAccel/KGlobalAccel) {
    QT += dbus
    DEFINES += LG_HAVE_KGLOBALACCEL
    INCLUDEPATH += /usr/include/KF6/KGlobalAccel
    LIBS += -lKF6GlobalAccel
}
