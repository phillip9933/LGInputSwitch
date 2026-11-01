#include "../desktop/update_policy.h"
#include <iostream>

static void check(bool condition, const char *message) { if (!condition) { std::cerr << message << '\n'; std::exit(1); } }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto hash = QString::fromLatin1(QCryptographicHash::hash("executable", QCryptographicHash::Sha256).toHex());
    const QString tag = "1.3.3", name = UpdateAssetName(tag, true);
    const QByteArray checksums = (hash + "  " + name + "\n").toUtf8();
    const auto manifestHash = QString::fromLatin1(QCryptographicHash::hash(checksums, QCryptographicHash::Sha256).toHex());
    const auto prefix = "https://github.com/phillip9933/LGInputSwitch/releases/download/" + tag + "/";
    QJsonObject binary{{"name", name}, {"size", 10}, {"state", "uploaded"}, {"digest", "sha256:" + hash}, {"browser_download_url", prefix + name}};
    QJsonObject manifest{{"name", "SHA256SUMS"}, {"size", checksums.size()}, {"state", "uploaded"}, {"digest", "sha256:" + manifestHash}, {"browser_download_url", prefix + "SHA256SUMS"}};
    QJsonObject release{{"tag_name", tag}, {"draft", false}, {"prerelease", false}, {"assets", QJsonArray{binary, manifest}}, {"body", "Fixture"}};
    auto select = [](QJsonObject release, QString &error) { return SelectUpdate(QJsonDocument(QJsonArray{release}).toJson(), "1.3.2", true, error); };
    QString error;
    const auto offer = select(release, error);
    check(error.isEmpty() && offer.version == tag && offer.sha256 == hash, "Matching release was not selected");
    check(VerifyUpdateChecksums(offer, checksums, error), "Matching checksums were rejected");
    check(!VerifyUpdateChecksums(offer, checksums + 'x', error), "Tampered manifest accepted");
    auto changed = offer;
    const auto duplicate = checksums + checksums;
    changed.checksumSha256 = QString::fromLatin1(QCryptographicHash::hash(duplicate, QCryptographicHash::Sha256).toHex());
    check(!VerifyUpdateChecksums(changed, duplicate, error), "Duplicate checksum entry accepted");
    const auto disagreement = (QString(64, '0') + "  " + name + "\n").toUtf8();
    changed.checksumSha256 = QString::fromLatin1(QCryptographicHash::hash(disagreement, QCryptographicHash::Sha256).toHex());
    check(!VerifyUpdateChecksums(changed, disagreement, error), "Conflicting GitHub and manifest digests accepted");
    auto linuxBinary = binary;
    linuxBinary["name"] = UpdateAssetName(tag, false);
    linuxBinary["browser_download_url"] = prefix + UpdateAssetName(tag, false);
    auto linuxRelease = release; linuxRelease["assets"] = QJsonArray{linuxBinary, manifest};
    check(SelectUpdate(QJsonDocument(QJsonArray{linuxRelease}).toJson(), "1.3.2", false, error).name == UpdateAssetName(tag, false), "Linux package was not selected");
    check(!select(linuxRelease, error).available(), "Linux package offered to Windows");
    auto bad = release; bad["tag_name"] = "1.3.2"; check(!select(bad, error).available(), "Same-version update accepted");
    bad["tag_name"] = "1.2.9"; check(!select(bad, error).available(), "Downgrade accepted");
    bad = release; bad["prerelease"] = true; check(!select(bad, error).available(), "Prerelease accepted");
    bad = release; bad["draft"] = true; check(!select(bad, error).available(), "Draft accepted");
    bad = release; bad["tag_name"] = "v1.4.0"; check(!select(bad, error).available(), "Non-numeric tag accepted");
    auto asset = binary; asset["browser_download_url"] = "https://attacker.invalid/app.exe";
    bad = release; bad["assets"] = QJsonArray{asset, manifest}; check(!select(bad, error).available() && !error.isEmpty(), "Foreign asset URL accepted");
    asset = binary; asset["size"] = 1024.0 * 1024 * 1024;
    bad["assets"] = QJsonArray{asset, manifest}; check(!select(bad, error).available(), "Oversized executable accepted");
    asset = binary; asset.remove("digest"); bad["assets"] = QJsonArray{asset, manifest}; check(!select(bad, error).available(), "Missing checksum accepted");
    bad = release; bad["assets"] = QJsonArray{binary, binary, manifest}; check(!select(bad, error).available(), "Duplicate asset accepted");
    bad = release; bad["assets"] = QJsonArray{manifest}; check(!select(bad, error).available(), "Missing executable accepted");
    check(!SelectUpdate(QJsonDocument(QJsonArray{release, release}).toJson(), "1.3.2", true, error).available(), "Duplicate release accepted");
    check(!SelectUpdate(QByteArray(2 * 1024 * 1024 + 1, ' '), "1.3.2", true, error).available(), "Oversized response accepted");
    for (const auto &url : {"http://github.com/file", "https://evilgithub.com/file", "https://user@github.com/file", "https://github.com:444/file", "file:///tmp/file"})
        check(!AllowedUpdateUrl(QUrl(url)), "Unsafe transport URL accepted");
    check(AllowedUpdateUrl(QUrl("https://release-assets.githubusercontent.com/download?token=fixture")), "Valid GitHub redirect rejected");
    check(!ValidUpdateVersion("1.02.3") && !ValidUpdateVersion("65536.0.0") && !ValidUpdateVersion("1.3.3-beta"), "Invalid numeric version accepted");
    check(AutomaticUpdateDue(true, 1, 86401) && !AutomaticUpdateDue(false, 0, 86401) && !AutomaticUpdateDue(true, 100, 50), "Daily throttle is incorrect");
    QTemporaryDir dir; const auto path = dir.filePath("download.exe"); QFile file(path); check(file.open(QIODevice::WriteOnly), "Fixture unavailable"); file.write("executable"); file.close();
    check(VerifyUpdateFile(path, hash, 10, error), "Verified file rejected");
    check(!VerifyUpdateFile(path, hash, 9, error), "Wrong-size file accepted");
    check(file.open(QIODevice::Append), "Fixture unavailable"); file.write("x"); file.close();
    check(!VerifyUpdateFile(path, hash, 11, error), "Altered executable accepted");
    std::cout << "Update version, repository, URL, digest, bounds, throttle and tampering checks passed\n";
}
