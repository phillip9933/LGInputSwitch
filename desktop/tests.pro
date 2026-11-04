QT += widgets concurrent
CONFIG += c++17
TARGET = desktop_smoke
SOURCES += ../tests/desktop_smoke.cpp ../tests/update_panel.cpp platform.cpp ../core/input_switcher.cpp ../platform/linux/ddcutil_backend.cpp
unix:!macx:exists(/usr/include/KF6/KGlobalAccel/KGlobalAccel) {
    QT += dbus
    DEFINES += LG_HAVE_KGLOBALACCEL
    INCLUDEPATH += /usr/include/KF6/KGlobalAccel
    LIBS += -lKF6GlobalAccel
}

SOURCES += profiles.cpp diagnostics.cpp capabilities.cpp

SOURCES += monitor_inputs.cpp monitor_identity.cpp windows_monitors.cpp

QT += network
SOURCES += update_policy.cpp update_install.cpp updates.cpp
