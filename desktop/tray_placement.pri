unix:!macx:system($$pkgConfigExecutable() --atleast-version=6.0 KWaylandClient) {
    CONFIG += link_pkgconfig
    PKGCONFIG += KWaylandClient
    DEFINES += LG_HAVE_PLASMA_TRAY_PLACEMENT
}
