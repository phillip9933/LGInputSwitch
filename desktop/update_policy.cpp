#include "update_policy.h"

namespace {
const QString repository = "https://github.com/phillip9933/LGInputSwitch";
bool Digest(QString value) {
    return QRegularExpression("^[0-9a-f]{64}$").match(value).hasMatch();
}
}
bool ValidUpdateVersion(const QString &version) {
    if (!QRegularExpression("^(0|[1-9][0-9]{0,4})\\.(0|[1-9][0-9]{0,4})\\.(0|[1-9][0-9]{0,4})$").match(version).hasMatch())
        return false;
    const auto parts = version.split('.');
    for (const auto &part : parts) if (part.toUInt() > 65535) return false;
    return true;
}
QString UpdateAssetName(const QString &version, bool windows) {
    return windows ? "LGInputSwitch-" + version + "-Windows-x64.exe"
                   : "lginputswitch-" + version + "-1-x86_64.pkg.tar.zst";
}
bool AllowedUpdateUrl(const QUrl &url) {
    return url.isValid() && url.scheme() == "https" && url.userInfo().isEmpty() && url.fragment().isEmpty() &&
           (url.port(-1) == -1 || url.port() == 443) &&
           QStringList{"api.github.com", "github.com", "release-assets.githubusercontent.com",
                       "objects.githubusercontent.com", "github-releases.githubusercontent.com"}.contains(url.host());
}
UpdateOffer SelectUpdate(const QByteArray &json, const QString &installed, bool windows, QString &error) {
    error.clear();
    if (!ValidUpdateVersion(installed) || json.size() > 2 * 1024 * 1024) {
        error = "Update metadata exceeds supported limits."; return {};
    }
    const auto doc = QJsonDocument::fromJson(json);
    if (!doc.isArray() || doc.array().size() > 100) { error = "GitHub returned invalid update metadata."; return {}; }
    UpdateOffer best;
    QSet<QString> seen;
    for (const auto &item : doc.array()) {
        const auto release = item.toObject();
        const auto version = release["tag_name"].toString();
        if (!ValidUpdateVersion(version) || release["draft"].toBool(true) || release["prerelease"].toBool(true)) continue;
        if (seen.contains(version)) { error = "GitHub returned duplicate release versions."; return {}; }
        seen.insert(version);
        if (QVersionNumber::fromString(version) <= QVersionNumber::fromString(installed)) continue;
        if (best.available() && QVersionNumber::fromString(version) <= QVersionNumber::fromString(best.version)) continue;
        const auto name = UpdateAssetName(version, windows);
        const auto prefix = repository + "/releases/download/" + version + "/";
        const auto assets = release["assets"].toArray();
        if (assets.size() > 64) { error = "Release asset list is too large."; return {}; }
        UpdateOffer offer;
        offer.version = version; offer.name = name; offer.page = repository + "/releases/tag/" + version;
        offer.notes = release["body"].toString().left(32768);
        int binaries = 0, manifests = 0;
        for (const auto &entry : assets) {
            const auto asset = entry.toObject();
            const auto assetName = asset["name"].toString();
            if (assetName != name && assetName != "SHA256SUMS") continue;
            if (asset["state"] != "uploaded" || asset["browser_download_url"] != prefix + assetName) {
                error = "Release download URL does not match this repository and version."; return {};
            }
            const auto digest = asset["digest"].toString();
            const double size = asset["size"].toDouble(-1);
            if (!digest.startsWith("sha256:") || !Digest(digest.mid(7)) || size < 1 ||
                size > (assetName == name ? 128 * 1024 * 1024 : 65536) || size != qint64(size)) {
                error = "Release download is missing valid size or checksum metadata."; return {};
            }
            if (assetName == name) {
                ++binaries; offer.url = prefix + name; offer.sha256 = digest.mid(7); offer.size = qint64(size);
            } else {
                ++manifests; offer.checksumUrl = prefix + assetName; offer.checksumSha256 = digest.mid(7);
            }
        }
        if (!binaries || !manifests) continue; // Older ZIP releases are manual downloads only.
        if (binaries != 1 || manifests != 1) { error = "Release downloads are ambiguous."; return {}; }
        best = offer;
    }
    return best;
}
bool VerifyUpdateChecksums(const UpdateOffer &offer, const QByteArray &checksums, QString &error) {
    error.clear();
    if (checksums.size() > 65536 || QString::fromLatin1(QCryptographicHash::hash(checksums, QCryptographicHash::Sha256).toHex()) != offer.checksumSha256) {
        error = "The checksum file did not match GitHub’s release metadata."; return false;
    }
    int matches = 0;
    QSet<QString> names;
    for (const auto &raw : checksums.split('\n')) {
        const auto line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const auto match = QRegularExpression("^([0-9a-f]{64})  ([A-Za-z0-9_.-]+)$").match(line);
        if (!match.hasMatch() || names.contains(match.captured(2))) {
            error = "The release checksum file is malformed or ambiguous."; return false;
        }
        names.insert(match.captured(2));
        if (match.captured(2) == offer.name) {
            if (match.captured(1) != offer.sha256) { error = "Release checksums disagree."; return false; }
            ++matches;
        }
    }
    if (matches != 1) error = "The selected download is missing from SHA256SUMS.";
    return matches == 1;
}
QString UpdateFileHash(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}
bool VerifyUpdateFile(const QString &path, const QString &sha256, qint64 expectedSize, QString &error) {
    error.clear();
    const QFileInfo info(path);
    if (!Digest(sha256) || !info.isFile() || info.isSymLink() || info.size() != expectedSize || UpdateFileHash(path) != sha256) {
        error = "The downloaded update failed its size or SHA256 check."; return false;
    }
    return true;
}
bool AutomaticUpdateDue(bool enabled, qint64 lastCheck, qint64 now) {
    return enabled && (lastCheck <= 0 || (now >= lastCheck && now - lastCheck >= 86400));
}
