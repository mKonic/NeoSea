#include "app/app.h"
#include "app/appsettings.h"
#include "app/mainwindow.h"
#include "core/fonts.h"
#include "core/i18n.h"
#include "neosea_version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>

#include <cstdio>

using namespace neosea;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("neosea");
    app.setApplicationDisplayName(kAppName);
    app.setOrganizationName("neosea");
    app.setApplicationVersion(QStringLiteral("%1 (%2)").arg(kVersionName).arg(kBuild));

    QCommandLineParser cli;
    cli.addVersionOption();
    cli.addHelpOption();
    QCommandLineOption libraryOpt("library", "Use this library folder.", "dir");
    cli.addOption(libraryOpt);
    cli.process(app);

    // Two copies editing the same library is how words get eaten: the second
    // one brings the first to the front and leaves
    const QString runDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(runDir);
    QLockFile lock(QDir(runDir).filePath("neosea.lock"));
    lock.setStaleLockTime(0);
    const QString serverName = "neosea-" + qEnvironmentVariable("USER");
    if (!lock.tryLock(100)) {
        QLocalSocket s;
        s.connectToServer(serverName);
        if (s.waitForConnected(500)) {
            s.write("raise");
            s.waitForBytesWritten(500);
        }
        return 0;
    }

    I18n::load(resourcesDir() + "/locales",
               AppSettings::read().value("uiLanguage").toString(QLocale::system().name()));
    registerBundledFonts();
    QFont ui = app.font();
    ui.setPixelSize(13);
    app.setFont(ui);

    const QString dir = cli.isSet(libraryOpt) ? cli.value(libraryOpt) : AppSettings::libraryDir();
    App state(dir);
    std::fprintf(stderr, "neosea %s (%d), library %s\n", kVersionName, kBuild, qPrintable(dir));

    MainWindow w(&state);
    QLocalServer::removeServer(serverName);
    QLocalServer server;
    server.listen(serverName);
    QObject::connect(&server, &QLocalServer::newConnection, &w, [&] {
        QLocalSocket *s = server.nextPendingConnection();
        if (s) s->deleteLater();
        w.showNormal();
        w.raise();
        w.activateWindow();
    });
    w.show();
    return app.exec();
}
