#define LG_DESKTOP_TEST
#include "../desktop/main.cpp"
#include <iostream>

static void check(bool value, const char *message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
static bool waitUntil(const std::function<bool()> &predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000)
    {
        QCoreApplication::processEvents();
        if (predicate())
            return true;
        QThread::msleep(5);
    }
    return false;
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName("LGInputSwitchTests");
    app.setApplicationName("Desktop");
    app.setStyle("Fusion");
    QTemporaryDir temporary;
    check(temporary.isValid(), "Temporary directory unavailable");
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, temporary.path());
    {
        DesktopWindow preview(true);
        preview.show();
        app.processEvents();
        auto *appearance = preview.findChild<QComboBox *>("appearance");
        auto *table = preview.findChild<QTableWidget *>("inputs");
        check(appearance && table && table->rowCount() == 4, "Inputs or appearance controls missing");
        appearance->setCurrentIndex(appearance->findData("dark"));
        app.processEvents();
        check(app.palette().color(QPalette::Window).lightness() < 60, "Dark palette not applied");
        appearance->setCurrentIndex(appearance->findData("light"));
        app.processEvents();
        check(app.palette().color(QPalette::Window).lightness() > 220, "Light palette not applied");
        check(table->isColumnHidden(2), "Advanced codes unexpectedly exposed by default");
        check(table->visualItemRect(table->item(3, 1)).bottom() <= table->viewport()->height(),
              "Last input is clipped");
        auto *status = preview.findChild<QLabel *>("status");
        for (auto *button : preview.findChildren<QPushButton *>())
            if (button->text() == "Switch")
            {
                button->click();
                break;
            }
        check(status->text().contains("preview", Qt::CaseInsensitive), "Preview attempted a live operation");
        preview.close();
    }
#ifndef Q_OS_WIN
    // A fake process provides discovery and records writes. No I2C hardware is used.
    const auto log = temporary.path() + "/commands";
    QFile fake(temporary.path() + "/ddcutil");
    check(fake.open(QIODevice::WriteOnly), "Could not create fake backend");
    fake.write(("#!/bin/sh\nif [ \"$1\" = detect ]; then\nprintf 'Display 1\\n   I2C bus: /dev/i2c-6\\n   "
                "Model: Synthetic LG\\n'\nelse\nprintf '%s\\n' \"$@\" > '" +
                log + "'\nfi\n")
                   .toUtf8());
    fake.close();
    fake.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("PATH", temporary.path().toUtf8());
    {
        DesktopWindow live(false);
        live.show();
        auto *monitors = live.findChild<QComboBox *>("monitors");
        check(waitUntil([&] { return monitors->count() == 1 && monitors->isEnabled(); }),
              "Asynchronous discovery did not finish");
        check(monitors->currentIndex() == -1, "Discovery silently selected an unconfigured target");
        monitors->setCurrentIndex(0);
        auto *table = live.findChild<QTableWidget *>("inputs");
        table->item(0, 2)->setText("0xD0");
        for (auto *button : live.findChildren<QPushButton *>())
            if (button->accessibleName() == "Save settings")
            {
                button->click();
                break;
            }
        check(live.findChild<QLabel *>("status")->text().contains("saved", Qt::CaseInsensitive),
              "Settings did not save");
        for (auto *button : live.findChildren<QPushButton *>())
            if (button->text() == "Switch")
            {
                button->click();
                break;
            }
        check(waitUntil([&] { return QFile::exists(log) && table->isEnabled(); }),
              "Asynchronous input send did not finish");
        QFile record(log);
        check(record.open(QIODevice::ReadOnly), "No backend command recorded");
        check(record.readAll() == "--bus\n6\nsetvcp\n0xf4\n0xd0\n--i2c-source-addr=0x50\n--noverify\n",
              "GUI sent wrong target or command");
        QString warning;
        auto saved = LoadDesktopConfig(warning);
        check(saved.display == 6 && saved.bus && saved.inputs[0].code == "0xD0",
              "Config roundtrip changed selected target/custom code");
    }
#endif
    std::cout << "Desktop theme, preview, layout and platform smoke checks passed\n";
}
