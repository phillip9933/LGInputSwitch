#include "updates.h"

namespace {
#ifdef Q_OS_WIN
constexpr bool windows = true;
#else
constexpr bool windows = false;
#endif
}
UpdatePanel::UpdatePanel(bool preview, QWidget *parent, QNetworkAccessManager *transport, QSettings *settings)
    : QWidget(parent), transport_(transport ? transport : &network_),
      settings_(settings ? settings : preview ? nullptr : new QSettings("LGInputSwitch", "LGInputSwitchDesktop", this)), preview_(preview),
      installed_(QCoreApplication::applicationVersion()) {
    setObjectName("updates");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 8, 0, 0);
    auto *title = new QLabel("Application updates");
    QFont bold = title->font(); bold.setBold(true); title->setFont(bold); layout->addWidget(title);
    automatic_ = new QCheckBox("Check GitHub for updates automatically");
    automatic_->setObjectName("automaticUpdates");
    automatic_->setChecked(preview || settings_->value("updates/automatic", true).toBool());
    layout->addWidget(automatic_);
    auto *hint = new QLabel("Checks once a day. Downloads and installation start only when you choose them.");
    hint->setWordWrap(true); layout->addWidget(hint);
    status_ = new QLabel("Version " + installed_); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true);
    status_->setObjectName("updateStatus"); layout->addWidget(status_);
    progress_ = new QProgressBar; progress_->setRange(0, 100); progress_->hide(); layout->addWidget(progress_);
    auto *buttons = new QGridLayout;
    check_ = new QPushButton("Check for updates"); download_ = new QPushButton("Download update");
    install_ = new QPushButton("Install and restart"); later_ = new QPushButton("Later");
    cancel_ = new QPushButton("Cancel download"); notes_ = new QPushButton("Release notes");
    check_->setObjectName("checkUpdates"); download_->setObjectName("downloadUpdate"); install_->setObjectName("installUpdate");
    buttons->addWidget(check_, 0, 0); buttons->addWidget(download_, 0, 1); buttons->addWidget(install_, 0, 2);
    buttons->addWidget(notes_, 1, 0); buttons->addWidget(later_, 1, 1); buttons->addWidget(cancel_, 1, 2);
    layout->addLayout(buttons);
    connect(automatic_, &QCheckBox::toggled, this, [this](bool enabled) {
        if (!preview_) { auto &s = *settings_; s.setValue("updates/automatic", enabled); }
    });
    connect(check_, &QPushButton::clicked, this, [this] { check(); });
    connect(download_, &QPushButton::clicked, this, [this] { download(); });
    connect(install_, &QPushButton::clicked, this, [this] { install(); });
    connect(notes_, &QPushButton::clicked, this, [this] {
        if (offer_.available()) QDesktopServices::openUrl(QUrl(offer_.page));
    });
    connect(later_, &QPushButton::clicked, this, [this] {
        if (!preview_) { auto &s = *settings_; s.setValue("updates/dismissed", offer_.version); }
        offer_ = {}; downloaded_.clear(); cache_.reset(); state("Reminder dismissed. A manual check can show it again.");
    });
    connect(cancel_, &QPushButton::clicked, this, [this] { if (reply_) reply_->abort(); });
    state("Version " + installed_);
    if (!preview) QTimer::singleShot(2500, this, [this] {
        CleanupWindowsUpdateFiles();
        auto &settings = *settings_;
        const auto result = settings.value("updates/result").toString();
        if (!result.isEmpty()) {
            settings.remove("updates/result"); state(result); if (notify) notify(result);
        }
        if (AutomaticUpdateDue(automatic_->isChecked(), settings.value("updates/lastCheck").toLongLong(), QDateTime::currentSecsSinceEpoch())) check(false);
    });
    if (!preview) {
        auto *timer = new QTimer(this);
        timer->setInterval(60 * 60 * 1000);
        connect(timer, &QTimer::timeout, this, [this] { check(false); });
        timer->start();
    }
}
void UpdatePanel::state(const QString &message, bool working) {
    working_ = working;
    status_->setText(message); status_->setAccessibleName(message);
    check_->setEnabled(!working); automatic_->setEnabled(!working);
    download_->setVisible(offer_.available() && downloaded_.isEmpty()); download_->setEnabled(!working);
    install_->setVisible(!downloaded_.isEmpty()); install_->setEnabled(!working);
    later_->setVisible(offer_.available()); later_->setEnabled(!working);
    notes_->setVisible(offer_.available()); notes_->setEnabled(!working);
    cancel_->setVisible(working && !stopping_); progress_->setVisible(working && !stopping_);
}
void UpdatePanel::fetch(const QUrl &url, qint64 limit, std::function<void(QByteArray, QString)> done, int redirects, bool binary) {
    if (!AllowedUpdateUrl(url) || redirects > 5) { done({}, "Update download left the allowed HTTPS hosts."); return; }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "LGInputSwitch/" + installed_);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    auto *reply = transport_->get(request); reply_ = reply;
    auto data = std::make_shared<QByteArray>(); auto exceeded = std::make_shared<bool>(false);
    auto *deadline = new QTimer(reply); deadline->setSingleShot(true);
    // Metadata stays bounded in time. Binary transfers can take longer on slow
    // links; the request's inactivity timeout still stops a stalled download.
    if (!binary) deadline->start(180000);
    connect(deadline, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::readyRead, this, [data, exceeded, reply, limit] {
        const auto chunk = reply->readAll();
        if (chunk.size() > limit - data->size()) { *exceeded = true; reply->abort(); return; }
        data->append(chunk);
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, limit](qint64 got, qint64 total) {
        if (total > limit || got > limit) { if (reply_) reply_->abort(); return; }
        progress_->setRange(0, total > 0 ? 100 : 0);
        if (total > 0) progress_->setValue(int(qMin<qint64>(100, got * 100 / total)));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, data, exceeded, done, limit, redirects, binary] {
        const auto tail = reply->readAll();
        if (tail.size() > limit - data->size()) *exceeded = true;
        else data->append(tail);
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        const auto next = reply->url().resolved(redirect);
        const auto networkError = reply->error(); reply->deleteLater(); reply_.clear();
        if (*exceeded) { done({}, "Update response exceeded its size limit."); return; }
        if (status == 403 || status == 429) { done({}, "GitHub temporarily limited update checks. Try again later."); return; }
        if (networkError != QNetworkReply::NoError) {
            done({}, networkError == QNetworkReply::OperationCanceledError ? "Update download cancelled or timed out." : "Could not reach GitHub. Check your connection and try again."); return;
        }
        if (status >= 300 && status < 400 && !redirect.isEmpty()) { fetch(next, limit, done, redirects + 1, binary); return; }
        if (status != 200) { done({}, "GitHub did not return the requested update."); return; }
        done(*data, {});
    });
}
void UpdatePanel::check(bool manual) {
    if (working_ || preview_) { if (preview_) state("Design preview · update checks are disabled."); return; }
    if (QSysInfo::buildCpuArchitecture() != "x86_64") { state("Use the release page for this architecture."); return; }
    if (!downloaded_.isEmpty()) {
        if (manual) state("Version " + offer_.version + " is verified and ready to install.");
        return;
    }
    auto &settings = *settings_;
    if (!manual && !AutomaticUpdateDue(automatic_->isChecked(), settings.value("updates/lastCheck").toLongLong(), QDateTime::currentSecsSinceEpoch())) return;
    state("Checking GitHub…", true);
    fetch(QUrl("https://api.github.com/repos/phillip9933/LGInputSwitch/releases?per_page=100"), 2 * 1024 * 1024,
        [this, manual](QByteArray data, QString error) {
        if (!error.isEmpty()) { state(error); return; }
        auto offer = SelectUpdate(data, installed_, windows, error);
        if (!error.isEmpty()) { state(error); return; }
        if (!offer.available()) {
            settings_->setValue("updates/lastCheck", QDateTime::currentSecsSinceEpoch());
            offer_ = {}; state("You have the latest compatible release (" + installed_ + ")."); return;
        }
        if (!manual && settings_->value("updates/dismissed").toString() == offer.version) {
            settings_->setValue("updates/lastCheck", QDateTime::currentSecsSinceEpoch());
            offer_ = {}; state("Version " + installed_); return;
        }
        fetch(QUrl(offer.checksumUrl), 65536, [this, offer, manual](QByteArray checksums, QString error) {
            if (error.isEmpty()) VerifyUpdateChecksums(offer, checksums, error);
            if (!error.isEmpty()) { state(error); return; }
            settings_->setValue("updates/lastCheck", QDateTime::currentSecsSinceEpoch());
            offer_ = offer; state("Version " + offer.version + " is available.");
            if (notify) notify("LG Input Switch " + offer.version + " is available. Open Preferences → Application updates.");
            Q_UNUSED(manual);
        });
    });
}
void UpdatePanel::download() {
    if (working_ || !offer_.available() || preview_) return;
    const auto offer = offer_; state("Downloading version " + offer.version + "…", true);
    fetch(QUrl(offer.url), offer.size, [this, offer](QByteArray bytes, QString error) {
        if (!error.isEmpty()) { state(error); return; }
        if (bytes.size() != offer.size || QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != offer.sha256) {
            state("The update failed its size or SHA256 check. Nothing was installed."); return;
        }
        cache_ = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/lginputswitch-update-XXXXXX");
        if (!cache_->isValid()) { state("Could not create the update cache."); return; }
        const auto path = cache_->filePath(offer.name); QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) { state("Could not save the downloaded update."); return; }
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        downloaded_ = path; state("Version " + offer.version + " is verified and ready to install.");
        if (notify) notify("Update downloaded. Choose Install and restart in Preferences when ready.");
    }, 0, true);
}
bool UpdatePanel::linuxManaged() const {
    if (QCoreApplication::applicationFilePath() != "/usr/bin/LGInputSwitchDesktop") return false;
    QProcess process; process.start("/usr/bin/pacman", {"-Qoq", QCoreApplication::applicationFilePath()});
    return process.waitForFinished(3000) && process.exitCode() == 0 && process.readAllStandardOutput().trimmed() == "lginputswitch";
}
void UpdatePanel::install() {
    if (working_ || downloaded_.isEmpty() || preview_) return;
    QString error;
    if (!VerifyUpdateFile(downloaded_, offer_.sha256, offer_.size, error)) { downloaded_.clear(); state(error); return; }
#ifdef Q_OS_WIN
    if (prepareInstall && !prepareInstall()) return;
    if (!StartWindowsUpdate(offer_, downloaded_, error)) { state(error); return; }
    stopping_ = true; state("Closing to install the update…", true);
    if (installBusy) installBusy(true);
    if (quitForUpdate) quitForUpdate();
#else
    if (!linuxManaged()) { state("Automatic installation requires the native Arch/CachyOS package. Use the release page for your installation."); return; }
    QProcess inspect; inspect.start("/usr/bin/pacman", {"-Qp", downloaded_});
    if (!inspect.waitForFinished(5000) || inspect.exitCode() != 0 ||
        inspect.readAllStandardOutput().trimmed() != ("lginputswitch " + offer_.version + "-1").toUtf8()) {
        state("The downloaded package name or version did not match this release."); return;
    }
    if (prepareInstall && !prepareInstall()) return;
    auto *process = new QProcess(this); stopping_ = true;
    if (installBusy) installBusy(true);
    state("Approve the system authorization prompt to install this package…", true);
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
        stopping_ = false; if (installBusy) installBusy(false);
        state("Could not start the package manager. Nothing was installed."); process->deleteLater();
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, process](int code, QProcess::ExitStatus status) {
        process->deleteLater(); stopping_ = false; if (installBusy) installBusy(false);
        if (status != QProcess::NormalExit || code != 0) { state("Package installation failed or was cancelled. Check system dependencies and try again."); return; }
        auto &settings = *settings_; settings.setValue("updates/result", "Updated to " + offer_.version + "."); settings.sync();
        if (QProcess::startDetached(QCoreApplication::applicationFilePath(), {"--restart-after", QString::number(QCoreApplication::applicationPid())})) {
            if (quitForUpdate) quitForUpdate();
        } else state("Update installed. Please restart LG Input Switch from your application menu.");
    });
    process->start("/usr/bin/pkexec", {"--disable-internal-agent", "/usr/bin/pacman", "-U", "--noconfirm", downloaded_});
#endif
}
