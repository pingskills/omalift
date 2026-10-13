#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include "backend.h"
#include "key.h"
#include "liftprovider.h"

namespace {

const QColor kGreen(30, 180, 40);
const QColor kShirt(200, 40, 40);

// A flat green backdrop with a block of colour standing in for the person.
QImage scene(const QSize &size, const QRect &subject, const QColor &subjectColor = kShirt,
             const QColor &backdrop = kGreen) {
    QImage image(size, QImage::Format_ARGB32);
    image.fill(backdrop);
    QPainter painter(&image);
    painter.fillRect(subject, subjectColor);
    return image;
}

key::Screen greenScreen() {
    key::Screen screen;
    screen.color = kGreen;
    screen.coverage = 1.0;
    screen.found = true;
    return screen;
}

// What the key makes of a single pixel on a green screen.
QColor keyed(const QColor &pixel, const key::Settings &settings = key::Settings()) {
    QImage image(1, 1, QImage::Format_ARGB32);
    image.fill(pixel);
    return key::lift(image, greenScreen(), settings).pixelColor(0, 0);
}

}  // namespace

class Tests : public QObject {
    Q_OBJECT

private slots:
    void findsTheScreenBehindThePerson() {
        const key::Screen screen = key::detectScreen(scene({400, 300}, {150, 80, 100, 220}));
        QVERIFY(screen.found);
        QCOMPARE(screen.color, kGreen);
        QVERIFY2(qAbs(screen.coverage - 0.82) < 0.03, qPrintable(QString::number(screen.coverage)));
    }

    void findsAScreenThatStopsShortOfTheFrame() {
        // A cloth hung on a wall: the room shows all round it.
        QImage photo(400, 300, QImage::Format_ARGB32);
        photo.fill(QColor(150, 150, 150));
        QPainter painter(&photo);
        painter.fillRect(QRect(60, 30, 280, 270), kGreen);
        painter.fillRect(QRect(170, 120, 60, 180), kShirt);
        painter.end();
        const key::Screen screen = key::detectScreen(photo);
        QVERIFY(screen.found);
        QCOMPARE(screen.color, kGreen);
    }

    void takesTheUsualGreenNotTheBrightSpot() {
        QImage image = scene({400, 300}, {150, 80, 100, 220});
        QPainter painter(&image);
        painter.fillRect(QRect(0, 0, 90, 300), QColor(120, 255, 130));
        painter.end();
        QCOMPARE(key::detectScreen(image).color, kGreen);
    }

    void findsNoScreenInAnOrdinaryPhoto_data() {
        QTest::addColumn<QColor>("backdrop");
        QTest::newRow("blue table") << QColor(29, 78, 137);
        QTest::newRow("grey wall") << QColor(150, 150, 150);
        QTest::newRow("olive wall") << QColor(110, 120, 90);
    }
    void findsNoScreenInAnOrdinaryPhoto() {
        QFETCH(QColor, backdrop);
        const QImage photo = scene({400, 300}, {150, 80, 100, 220}, kShirt, backdrop);
        const key::Screen screen = key::detectScreen(photo);
        QVERIFY(!screen.found);
        // Without a screen nothing is removed, and nothing is offered to save.
        QCOMPARE(key::lift(photo, screen, key::Settings()).pixelColor(5, 5), backdrop);
        QVERIFY(key::cutout(photo, screen, key::Settings()).isNull());
    }

    void findsNoScreenWhenGreenIsOnlyInOneCorner() {
        QImage photo = scene({400, 300}, {150, 80, 100, 220}, kShirt, QColor(150, 150, 150));
        QPainter painter(&photo);
        painter.fillRect(QRect(0, 0, 60, 60), kGreen);
        painter.end();
        QVERIFY(!key::detectScreen(photo).found);
    }

    void removesTheScreenAndLeavesTheSubjectAlone() {
        const QImage lifted = key::lift(scene({400, 300}, {150, 80, 100, 220}), greenScreen(), key::Settings());
        QCOMPARE(lifted.format(), QImage::Format_ARGB32);
        QCOMPARE(qAlpha(lifted.pixel(10, 10)), 0);
        QCOMPARE(qAlpha(lifted.pixel(390, 290)), 0);
        QCOMPARE(lifted.pixelColor(200, 200), kShirt);
    }

    void removesShadowsAndCreasesOnTheCloth() {
        QCOMPARE(keyed(QColor(18, 108, 24)).alpha(), 0);
    }

    void keepsDarkHairDespiteAHintOfGreen() {
        QCOMPARE(keyed(QColor(20, 26, 18)).alpha(), 255);
    }

    void givesAnEdgeItsOwnColourBackWithoutAGreenFringe() {
        // Three parts white shirt to one part screen, as on a soft edge.
        const QColor edge = keyed(QColor(187, 225, 190));
        QVERIFY2(edge.alpha() > 100 && edge.alpha() < 255, qPrintable(QString::number(edge.alpha())));
        for (const int value : {edge.red(), edge.green(), edge.blue()})
            QVERIFY2(qAbs(value - 240) <= 12, qPrintable(edge.name()));
        QVERIFY(edge.green() <= qMax(edge.red(), edge.blue()));
    }

    void takesReflectedGreenOffTheSubjectOnlyAsFarAsAsked() {
        const QColor tinted(150, 170, 140);
        QCOMPARE(keyed(tinted), QColor(150, 150, 140));

        key::Settings off;
        off.despill = 0.0;
        QCOMPARE(keyed(tinted, off), tinted);

        key::Settings half;
        half.despill = 0.5;
        QCOMPARE(keyed(tinted, half), QColor(150, 160, 140));
    }

    void strengthDecidesHowWeakAGreenStillCounts() {
        // About a quarter as green as the screen.
        const QColor weak(100, 135, 100);
        key::Settings gentle;
        gentle.strength = 0.0;
        QCOMPARE(keyed(weak, gentle).alpha(), 255);
        key::Settings firm;
        firm.strength = 1.0;
        QCOMPARE(keyed(weak, firm).alpha(), 0);
    }

    void hardEdgesHaveNothingInBetween() {
        key::Settings hard;
        hard.softness = 0.0;
        for (int green = 100; green <= 250; green += 10) {
            const int alpha = keyed(QColor(100, green, 100), hard).alpha();
            QVERIFY2(alpha == 0 || alpha == 255, qPrintable(QString::number(alpha)));
        }
    }

    void clearsSpecksButKeepsTheSubjectAndASmallBall() {
        QImage lifted(1000, 1000, QImage::Format_ARGB32);
        lifted.fill(Qt::transparent);
        QPainter painter(&lifted);
        painter.fillRect(QRect(300, 300, 400, 600), kShirt);
        painter.fillRect(QRect(50, 50, 3, 3), kShirt);
        painter.fillRect(QRect(900, 100, 12, 12), Qt::white);
        painter.end();

        key::removeSpecks(lifted);
        QCOMPARE(qAlpha(lifted.pixel(51, 51)), 0);
        QCOMPARE(lifted.pixelColor(500, 600), kShirt);
        // A ball in the air is small, but it is not noise.
        QCOMPARE(lifted.pixelColor(905, 105), QColor(Qt::white));
    }

    void suggestsCroppingToABackdropThatDoesNotFillTheFrame() {
        QImage photo(400, 300, QImage::Format_ARGB32);
        photo.fill(QColor(150, 150, 150));
        QPainter painter(&photo);
        painter.fillRect(QRect(80, 30, 240, 270), kGreen);
        painter.fillRect(QRect(170, 120, 60, 180), kShirt);
        painter.end();

        const QRectF crop = key::screenExtent(photo, greenScreen());
        QVERIFY2(qAbs(crop.left() - 0.2) < 0.02, qPrintable(QString::number(crop.left())));
        QVERIFY2(qAbs(crop.right() - 0.8) < 0.02, qPrintable(QString::number(crop.right())));
        QVERIFY2(qAbs(crop.top() - 0.1) < 0.02, qPrintable(QString::number(crop.top())));
        QCOMPARE(crop.bottom(), 1.0);

        // Cropped there, the room around the backdrop is not in the cutout.
        const QImage lifted = key::cutout(photo, greenScreen(), key::Settings(), crop);
        QCOMPARE(lifted.size(), QSize(64, 182));
        QCOMPARE(lifted.pixelColor(30, 100), kShirt);
    }

    void keepsTheWholePhotoWhenTheBackdropFillsIt() {
        QCOMPARE(key::screenExtent(scene({400, 300}, {150, 80, 100, 220}), greenScreen()), QRectF(0, 0, 1, 1));
    }

    void trimsTheCutoutToTheSubject() {
        const QImage lifted = key::cutout(scene({400, 300}, {150, 80, 100, 150}), greenScreen(), key::Settings());
        // The subject plus a two pixel margin all round.
        QCOMPARE(lifted.size(), QSize(104, 154));
        QCOMPARE(qAlpha(lifted.pixel(0, 0)), 0);
        QCOMPARE(lifted.pixelColor(50, 75), kShirt);
    }

    void hasNothingToSaveWhenThePhotoIsAllScreen() {
        QImage empty(200, 200, QImage::Format_ARGB32);
        empty.fill(kGreen);
        QVERIFY(key::cutout(empty, greenScreen(), key::Settings()).isNull());
    }

    void readsTheAccentFromAnOmarchyTheme() {
        QTemporaryDir dir;
        const QString path = dir.filePath("colors.toml");
        const auto accent = [&](const QByteArray &contents) {
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                return QString();
            file.write(contents);
            file.close();
            return Backend::accentFromColorsFile(path, "#123456");
        };
        QCOMPARE(accent("# theme\nbackground = \"#000000\"\naccent = \"#7aa2f7\"\n"), QString("#7aa2f7"));
        QCOMPARE(accent("accent='#ff8800'\n"), QString("#ff8800"));
        QCOMPARE(accent("accent = \"not a colour\"\n"), QString("#123456"));
        QCOMPARE(accent("background = \"#000000\"\n"), QString("#123456"));
        QCOMPARE(Backend::accentFromColorsFile(dir.filePath("missing.toml"), "#123456"), QString("#123456"));
        QCOMPARE(Backend::foregroundFor("#ffd400"), QString("black"));
        QCOMPARE(Backend::foregroundFor("#14213d"), QString("white"));
    }

    void namesTheCutoutAfterThePhoto() {
        QCOMPARE(Backend::cutoutPathFor("/photos/alois.pointing.JPG"), QString("/photos/alois.pointing-cutout.png"));
    }

    void opensAPhotoAndSavesItsCutout() {
        QTemporaryDir dir;
        const QString photo = dir.filePath("jeff serving.jpg");
        QVERIFY(scene({800, 600}, {300, 150, 200, 450}).save(photo, "JPG", 95));

        auto *provider = new LiftProvider();
        Backend backend(provider);
        QSignalSpy previews(&backend, &Backend::previewChanged);
        QSignalSpy saves(&backend, &Backend::saved);

        QVERIFY(backend.load(QUrl::fromLocalFile(photo)));
        QVERIFY(backend.screenFound());
        QCOMPARE(backend.imageSize(), QSize(800, 600));
        QCOMPARE(backend.suggestedSaveUrl().toLocalFile(), dir.filePath("jeff serving-cutout.png"));
        // The keyed preview arrives a moment after the photo itself.
        QTRY_COMPARE(previews.count(), 2);
        QSize size;
        QCOMPARE(qAlpha(provider->requestImage("lifted/2", &size, QSize()).pixel(20, 20)), 0);
        QCOMPARE(size, QSize(800, 600));
        QCOMPARE(qAlpha(provider->requestImage("original/2", &size, QSize()).pixel(20, 20)), 255);

        // A name typed without an extension still gets a PNG.
        backend.save(QUrl::fromLocalFile(dir.filePath("out")));
        QVERIFY(backend.busy());
        QTRY_COMPARE(saves.count(), 1);
        QVERIFY(!backend.busy());
        QCOMPARE(saves.first().first().toString(), dir.filePath("out.png"));
        const QImage cutout(dir.filePath("out.png"));
        QVERIFY(cutout.hasAlphaChannel());
        QVERIFY2(qAbs(cutout.width() - 204) <= 6 && qAbs(cutout.height() - 452) <= 6,
                 qPrintable(QString("%1x%2").arg(cutout.width()).arg(cutout.height())));
        QVERIFY(backend.status().startsWith("Saved out.png"));
        delete provider;
    }

    void slidersRekeyThePreviewAndResetPutsThemBack() {
        QTemporaryDir dir;
        const QString photo = dir.filePath("photo.png");
        QVERIFY(scene({400, 300}, {150, 80, 100, 220}).save(photo));
        auto *provider = new LiftProvider();
        Backend backend(provider);
        QSignalSpy previews(&backend, &Backend::previewChanged);
        QVERIFY(backend.load(QUrl::fromLocalFile(photo)));
        QTRY_COMPARE(previews.count(), 2);

        backend.setStrength(0.9);
        backend.setStrength(0.9);
        backend.setCrop(QRectF(0.25, 0.1, 0.5, 0.9));
        QTRY_COMPARE(previews.count(), 3);
        QCOMPARE(backend.crop(), QRectF(0.25, 0.1, 0.5, 0.9));
        // Dragged outside the photo, the crop stops at its edge.
        backend.setCrop(QRectF(-0.5, 0.1, 1.0, 2.0));
        QCOMPARE(backend.crop(), QRectF(0, 0.1, 0.5, 0.9));

        backend.reset();
        QCOMPARE(backend.strength(), key::Settings().strength);
        QCOMPARE(backend.crop(), QRectF(0, 0, 1, 1));
        delete provider;
    }

    void reportsAFileThatIsNotAPhoto() {
        QTemporaryDir dir;
        QFile file(dir.filePath("notes.jpg"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not a picture");
        file.close();
        auto *provider = new LiftProvider();
        Backend backend(provider);
        QSignalSpy failures(&backend, &Backend::failed);
        QVERIFY(!backend.load(QUrl::fromLocalFile(file.fileName())));
        QCOMPARE(failures.count(), 1);
        QVERIFY(!backend.hasImage());
        QVERIFY(backend.status().startsWith("Could not open notes.jpg"));
        delete provider;
    }

    void theWindowLoadsAndShowsAPhoto() {
        QTemporaryDir dir;
        // OMALIFT_PHOTO swaps in a real photo, and OMALIFT_SCREENSHOT keeps a
        // picture of the window, for looking at the result by eye.
        QString photo = qEnvironmentVariable("OMALIFT_PHOTO");
        if (photo.isEmpty()) {
            photo = dir.filePath("photo.png");
            QVERIFY(scene({800, 600}, {300, 150, 200, 450}).save(photo));
        }

        QQuickStyle::setStyle("Material");
        auto *provider = new LiftProvider();
        Backend backend(provider);
        QStringList problems;
        QQmlApplicationEngine engine;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError> &warnings) {
            for (const QQmlError &warning : warnings)
                problems << warning.toString();
        });
        engine.addImageProvider("lift", provider);
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl("qrc:/Main.qml"));
        QCOMPARE(engine.rootObjects().size(), 1);

        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QCOMPARE(window->title(), QString("omalift"));
        QVERIFY(QTest::qWaitForWindowExposed(window));

        QSignalSpy previews(&backend, &Backend::previewChanged);
        QVERIFY(backend.load(QUrl::fromLocalFile(photo)));
        QTRY_COMPARE(previews.count(), 2);
        QCOMPARE(window->title(), QString("omalift — ") + QFileInfo(photo).fileName());
        QCOMPARE(window->property("ready").toBool(), true);

        QTest::qWait(200);
        const QImage shot = window->grabWindow();
        QVERIFY(!shot.isNull());
        if (qEnvironmentVariableIsSet("OMALIFT_SCREENSHOT"))
            QVERIFY(shot.save(qEnvironmentVariable("OMALIFT_SCREENSHOT")));
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
    }
};

QTEST_MAIN(Tests)
#include "tests.moc"
