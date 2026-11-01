#pragma once
#include <QtCore>

struct UpdateOffer {
    QString version, name, url, page, notes, sha256, checksumUrl, checksumSha256;
    qint64 size = 0;
    bool available() const { return !version.isEmpty(); }
};
QString UpdateAssetName(const QString &version, bool windows);
bool ValidUpdateVersion(const QString &version);
bool AllowedUpdateUrl(const QUrl &url);
UpdateOffer SelectUpdate(const QByteArray &json, const QString &installed, bool windows, QString &error);
bool VerifyUpdateChecksums(const UpdateOffer &offer, const QByteArray &checksums, QString &error);
bool VerifyUpdateFile(const QString &path, const QString &sha256, qint64 expectedSize, QString &error);
bool AutomaticUpdateDue(bool enabled, qint64 lastCheck, qint64 now);
QString UpdateFileHash(const QString &path);
