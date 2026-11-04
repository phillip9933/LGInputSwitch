#pragma once
#include "update_install.h"
#include <QtNetwork>
#include <QtWidgets>
#include <functional>

class UpdatePanel : public QWidget {
    QNetworkAccessManager network_;
    QNetworkAccessManager *transport_;
    QSettings *settings_;
    QPointer<QNetworkReply> reply_;
    QCheckBox *automatic_;
    QLabel *status_;
    QProgressBar *progress_;
    QPushButton *check_, *download_, *install_, *later_, *cancel_, *notes_;
    UpdateOffer offer_;
    std::unique_ptr<QTemporaryDir> cache_;
    QString downloaded_;
    bool preview_ = false, working_ = false, stopping_ = false;
    QString installed_;
    void state(const QString &message, bool working = false);
    void fetch(const QUrl &url, qint64 limit, std::function<void(QByteArray, QString)> done,
               int redirects = 0, bool binary = false);
    void download();
    void install();
    bool linuxManaged() const;
  public:
    explicit UpdatePanel(bool preview, QWidget *parent = nullptr, QNetworkAccessManager *transport = nullptr,
                         QSettings *settings = nullptr);
    std::function<void(const QString &)> notify;
    std::function<bool()> prepareInstall;
    std::function<void()> quitForUpdate;
    std::function<void(bool)> installBusy;
    void check(bool manual = true);
};
