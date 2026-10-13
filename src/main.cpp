// omalift — lifts a person off a photo: instantly off a green screen, or off
// any background with a model. Qt Quick (QML) UI.

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>
#include <QUrl>

#include "backend.h"
#include "download.h"
#include "key.h"
#include "liftprovider.h"
#include "matte.h"

namespace {

const char kUsage[] =
    "Usage:\n"
    "  omalift [photo]                      Open the window, optionally on a photo\n"
    "  omalift --export photo...            Cut green screen photos out\n"
    "  omalift --export --fast photo...     Cut out photos with any background\n"
    "  omalift --export --best photo...     The same, slower, with finer edges\n"
    "\n"
    "Each cutout is written beside its photo as <name>-cutout.png. --fast and\n"
    "--best download their model the first time they are used.\n";

// Waits for a model to arrive, reporting on stderr as it comes.
bool fetchModel(const matte::Model &model, QTextStream &err) {
    if (matte::installed(model))
        return true;
    err << "Downloading the " << model.label << " model (" << matte::sizeLabel(model) << ")...\n";
    err.flush();

    ModelDownload download;
    QEventLoop loop;
    bool fetched = false;
    int reported = 0;
    QObject::connect(&download, &ModelDownload::progress, [&](qint64 received, qint64 total) {
        const int tenth = total > 0 ? int(received * 10 / total) : 0;
        if (tenth > reported) {
            reported = tenth;
            err << "  " << tenth * 10 << "%\n";
            err.flush();
        }
    });
    QObject::connect(&download, &ModelDownload::finished, [&](const QString &, bool ok, const QString &error) {
        fetched = ok;
        if (!ok)
            err << error << '\n';
        loop.quit();
    });
    download.start(model);
    loop.exec();
    return fetched;
}

// Cuts photos out without a window. With no model, each photo must have a
// green screen and gets the settings the window opens on.
int exportPhotos(const QStringList &paths, const matte::Model *model) {
    QTextStream out(stdout);
    QTextStream err(stderr);

    matte::Session session;
    if (model) {
        QString error;
        if (!fetchModel(*model, err))
            return 1;
        if (!session.open(matte::modelPath(*model), &error)) {
            err << "Could not load the " << model->label << " model: " << error << '\n';
            return 1;
        }
    }

    int failures = 0;
    for (const QString &path : paths) {
        QString error;
        const QImage photo = Backend::readPhoto(path, &error);
        if (photo.isNull()) {
            err << path << ": " << error << '\n';
            ++failures;
            continue;
        }

        QImage lifted;
        if (model) {
            const QImage mask = session.mask(photo, &error);
            if (mask.isNull()) {
                err << path << ": " << error << '\n';
                ++failures;
                continue;
            }
            lifted = matte::cutout(photo, mask);
        } else {
            const key::Screen screen = key::detectScreen(photo);
            if (!screen.found) {
                err << path << ": no green screen found in this photo (try --fast or --best)\n";
                ++failures;
                continue;
            }
            lifted = key::cutout(photo, screen, key::Settings(), key::screenExtent(photo, screen));
        }

        const QString target = Backend::cutoutPathFor(path);
        if (lifted.isNull() || !lifted.save(target, "PNG")) {
            err << path << ": could not write " << target << '\n';
            ++failures;
            continue;
        }
        out << target << " (" << lifted.width() << " x " << lifted.height() << ")\n";
        out.flush();
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
        QStringList paths = app.arguments().mid(2);
        const matte::Model *model = nullptr;
        if (!paths.isEmpty() && paths.first().startsWith(QLatin1String("--"))) {
            model = matte::byId(paths.takeFirst().mid(2));
            if (!model) {
                QTextStream(stderr) << kUsage;
                return 1;
            }
        }
        if (paths.isEmpty()) {
            QTextStream(stderr) << kUsage;
            return 1;
        }
        return exportPhotos(paths, model);
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
