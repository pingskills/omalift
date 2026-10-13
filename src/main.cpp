// omalift — lifts a person off a green screen photo. Qt Quick (QML) UI.

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>
#include <QUrl>

#include "backend.h"
#include "key.h"
#include "liftprovider.h"

namespace {

const char kUsage[] =
    "Usage:\n"
    "  omalift [photo]             Open the window, optionally on a photo\n"
    "  omalift --export photo...   Write <name>-cutout.png beside each photo\n";

// Cuts photos out without a window, with the settings the window opens on.
int exportPhotos(const QStringList &paths) {
    QTextStream out(stdout);
    QTextStream err(stderr);
    int failures = 0;
    for (const QString &path : paths) {
        QString error;
        const QImage photo = Backend::readPhoto(path, &error);
        if (photo.isNull()) {
            err << path << ": " << error << '\n';
            ++failures;
            continue;
        }
        const key::Screen screen = key::detectScreen(photo);
        if (!screen.found) {
            err << path << ": no green screen found in this photo\n";
            ++failures;
            continue;
        }
        const QImage lifted = key::cutout(photo, screen, key::Settings(), key::screenExtent(photo, screen));
        const QString target = Backend::cutoutPathFor(path);
        if (lifted.isNull() || !lifted.save(target, "PNG")) {
            err << path << ": could not write " << target << '\n';
            ++failures;
            continue;
        }
        out << target << " (" << lifted.width() << " x " << lifted.height() << ")\n";
    }
    return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char *argv[]) {
    const QString first = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    if (first == QLatin1String("--help") || first == QLatin1String("-h")) {
        QTextStream(stdout) << kUsage;
        return 0;
    }
    if (first == QLatin1String("--export")) {
        // No display needed, so this also works over SSH and in scripts.
        QCoreApplication app(argc, argv);
        const QStringList paths = app.arguments().mid(2);
        if (paths.isEmpty()) {
            QTextStream(stderr) << kUsage;
            return 1;
        }
        return exportPhotos(paths);
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("omalift");

    // Associates the window with omalift.desktop so the compositor (Wayland
    // app_id = this name) and taskbars pick up the installed icon.
    app.setDesktopFileName("omalift");
    app.setWindowIcon(QIcon::fromTheme("omalift"));

    QQuickStyle::setStyle("Material");

    auto *provider = new LiftProvider();
    Backend backend(provider, &app);

    QQmlApplicationEngine engine;
    // The engine takes ownership of the image provider.
    engine.addImageProvider("lift", provider);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return -1;

    const QStringList args = app.arguments();
    if (args.size() > 1)
        backend.load(QUrl::fromLocalFile(QFileInfo(args.at(1)).absoluteFilePath()));

    return app.exec();
}
