QT += widgets concurrent dbus
CONFIG += c++17
DEFINES += LG_HAVE_KGLOBALACCEL
INCLUDEPATH += /usr/include/KF6/KGlobalAccel
LIBS += -lKF6GlobalAccel
TARGET = kde_window_probe
SOURCES += ../tests/kde_window_probe.cpp platform.cpp ../core/input_switcher.cpp ../platform/linux/ddcutil_backend.cpp

SOURCES += profiles.cpp diagnostics.cpp capabilities.cpp legacy_config.cpp

SOURCES += monitor_inputs.cpp monitor_identity.cpp windows_monitors.cpp

QT += network
SOURCES += update_policy.cpp update_install.cpp updates.cpp
