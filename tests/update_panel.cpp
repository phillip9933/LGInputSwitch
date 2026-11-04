#include "../desktop/updates.h"
#include <iostream>

namespace {
void check(bool value, const char *message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
struct Response {
    QByteArray body;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    QString path;
};
class Reply : public QNetworkReply {
    QByteArray body_;
    qint64 offset_ = 0;
  public:
    Reply(const QNetworkRequest &request, Response response, QObject *parent)
        : QNetworkReply(parent), body_(response.body) {
        setRequest(request); setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this, response] {
            if (isFinished()) return;
            if (response.error != NoError) setError(response.error, "Synthetic network failure");
            emit readyRead();
            if (!isFinished()) { setFinished(true); emit finished(); }
        });
    }
    void abort() override {
        if (isFinished()) return;
        setError(OperationCanceledError, "Cancelled"); setFinished(true); emit finished();
    }
    qint64 bytesAvailable() const override { return body_.size() - offset_ + QIODevice::bytesAvailable(); }
    bool isSequential() const override { return true; }
  protected:
    qint64 readData(char *buffer, qint64 maximum) override {
        const auto size = qMin(maximum, body_.size() - offset_);
        if (!size) return -1;
        memcpy(buffer, body_.constData() + offset_, size); offset_ += size; return size;
    }
};
class Network : public QNetworkAccessManager {
  public:
    QList<Response> responses;
    int requests = 0;
  protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        ++requests;
        check(!responses.isEmpty(), "Unexpected update request");
        const auto response = responses.takeFirst();
        check(response.path.isEmpty() || request.url().path().endsWith(response.path), "Wrong update resource requested");
        return new Reply(request, response, this);
    }
};
void finish(UpdatePanel &panel) {
    QElapsedTimer timer; timer.start();
    while (!panel.findChild<QPushButton *>("checkUpdates")->isEnabled() && timer.elapsed() < 2000)
        QCoreApplication::processEvents();
    check(panel.findChild<QPushButton *>("checkUpdates")->isEnabled(), "Update request did not finish");
}
QString hash(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
}

void TestUpdatePanel() {
    QTemporaryDir temporary;
    QSettings settings(temporary.filePath("updates.ini"), QSettings::IniFormat);
    Network network;
    const auto originalVersion = QCoreApplication::applicationVersion();
    QCoreApplication::setApplicationVersion("1.3.2");
    UpdatePanel panel(false, nullptr, &network, &settings);
    auto *status = panel.findChild<QLabel *>("updateStatus");
    network.responses = {{{}, QNetworkReply::ConnectionRefusedError}};
    panel.check(false);
    check(!settings.contains("updates/lastCheck"), "Update check throttled before a response");
    finish(panel);
    check(!settings.contains("updates/lastCheck") && status->text().contains("Could not reach"),
          "Offline update check counted as a successful daily check");
    network.responses = {{"[]"}};
    panel.check(false); finish(panel);
    check(network.requests == 2 && settings.value("updates/lastCheck").toLongLong() > 0,
          "Automatic check did not retry after failure");
    panel.check(false);
    check(network.requests == 2, "Successful check did not enforce the daily interval");
    settings.remove("updates/lastCheck");
    const QString version = "1.3.3";
#ifdef Q_OS_WIN
    constexpr bool windows = true;
#else
    constexpr bool windows = false;
#endif
    const auto name = UpdateAssetName(version, windows);
    const QByteArray binary("synthetic update bytes");
    const QByteArray manifest = (hash(binary) + "  " + name + "\n").toUtf8();
    const auto prefix = "https://github.com/phillip9933/LGInputSwitch/releases/download/" + version + "/";
    auto asset = [&](const QString &file, const QByteArray &bytes) {
        return QJsonObject{{"name", file}, {"size", bytes.size()}, {"state", "uploaded"},
                           {"digest", "sha256:" + hash(bytes)}, {"browser_download_url", prefix + file}};
    };
    const auto release = QJsonDocument(QJsonArray{QJsonObject{
        {"tag_name", version}, {"draft", false}, {"prerelease", false},
        {"assets", QJsonArray{asset(name, binary), asset("SHA256SUMS", manifest)}}}}).toJson();
    network.responses = {{release}, {"tampered manifest"}};
    panel.check(false); finish(panel);
    check(!settings.contains("updates/lastCheck") && status->text().contains("checksum"),
          "Invalid checksum manifest counted as a successful check");
    network.responses = {{release}, {manifest, QNetworkReply::NoError, "SHA256SUMS"}};
    panel.check(false); finish(panel);
    check(settings.contains("updates/lastCheck") && status->text().contains("available"),
          "Verified release was not offered");
    network.responses = {{{}, QNetworkReply::ConnectionRefusedError}};
    panel.check(true); finish(panel);
    check(!panel.findChild<QPushButton *>("downloadUpdate")->isHidden(),
          "Failed check discarded a previously verified release offer");
    network.responses = {{binary, QNetworkReply::NoError, name}};
    panel.findChild<QPushButton *>("downloadUpdate")->click(); finish(panel);
    auto *install = panel.findChild<QPushButton *>("installUpdate");
    check(!install->isHidden() && status->text().contains("ready to install"), "Verified update not retained for installation");
    // Force the daily check due, then manually check too. Neither may remove
    // the ready binary or replace its matching release/checksum metadata.
    settings.setValue("updates/lastCheck", 1);
    const auto requests = network.requests;
    panel.check(false); panel.check(true);
    check(network.requests == requests && !install->isHidden() && install->isEnabled() &&
              status->text().contains("1.3.3 is verified"), "A check discarded a ready update");
    QCoreApplication::setApplicationVersion(originalVersion);
}
