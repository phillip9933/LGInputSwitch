#pragma once
#include <QtCore>

QString MonitorIdentity(const QByteArray &edid);
QString MonitorInputProfileFromEdid(const QByteArray &edid);
QString MonitorNameFromEdid(const QByteArray &edid); // local UI only
QJsonObject MonitorEdidHardware(const QByteArray &edid);
QJsonObject SafeMonitorHardware(const QJsonObject &hardware);
