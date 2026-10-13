#include <QCryptographicHash>
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
#include "download.h"
#include "key.h"
#include "liftprovider.h"
#include "matte.h"

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

    QTemporaryDir m_models;

    // A model that is really a small local file, so downloading can be
    // tested without the network.
    matte::Model localModel(const QByteArray &contents, const QByteArray &claimedContents) {
        const QString source = m_models.filePath("source.bin");
        QFile file(source);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(contents);
        file.close();
        matte::Model model;
        model.id = "local";
        model.label = "Local";
        model.file = "local.onnx";
        model.url = QUrl::fromLocalFile(source);
        model.sha256 = QCryptographicHash::hash(claimedContents, QCryptographicHash::Sha256).toHex();
        model.bytes = claimedContents.size();
        return model;
    }

private slots:
    // Every test starts with no models installed, wherever it is run.
    void init() {
        QDir(m_models.path()).removeRecursively();
        QDir().mkpath(m_models.path());
        qputenv("OMALIFT_MODEL_DIR", m_models.path().toLocal8Bit());
    }

    void knowsItsTwoModels() {
        QCOMPARE(matte::byId("fast"), &matte::fast());
        QCOMPARE(matte::byId("best"), &matte::best());
        QVERIFY(!matte::byId("screen"));
        QCOMPARE(matte::sizeLabel(matte::fast()), QString("214 MB"));
        QCOMPARE(matte::sizeLabel(matte::best()), QString("928 MB"));
        QCOMPARE(matte::modelPath(matte::fast()), m_models.filePath("birefnet-general-lite.onnx"));
        QVERIFY(matte::fast().url.toString().startsWith("https://github.com/danielgatis/rembg/releases/"));
    }

    void onlyCountsAWholeModelAsInstalled() {
        QVERIFY(!matte::installed(matte::fast()));
        QFile file(matte::modelPath(matte::fast()));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("cut short");
        file.close();
        QVERIFY(!matte::installed(matte::fast()));
        QVERIFY(file.resize(matte::fast().bytes));
        QVERIFY(matte::installed(matte::fast()));
        QVERIFY(!matte::installed(matte::best()));
    }

    void preparesAPhotoTheWayTheModelExpects() {
        QImage photo(40, 20, QImage::Format_ARGB32);
        photo.fill(QColor(255, 0, 0));
        const std::vector<float> tensor = matte::toTensor(photo);
        const int plane = matte::kSide * matte::kSide;
        QCOMPARE(int(tensor.size()), 3 * plane);
        // Red, green and blue planes in that order, each normalised.
        QVERIFY(qAbs(tensor[5] - (1.0f - 0.485f) / 0.229f) < 1e-4f);
        QVERIFY(qAbs(tensor[plane + 5] - (0.0f - 0.456f) / 0.224f) < 1e-4f);
        QVERIFY(qAbs(tensor[2 * plane + 5] - (0.0f - 0.406f) / 0.225f) < 1e-4f);

        // A dim photo is stretched to full brightness first.
        photo.fill(QColor(100, 50, 0));
        const std::vector<float> dim = matte::toTensor(photo);
        QVERIFY(qAbs(dim[5] - (1.0f - 0.485f) / 0.229f) < 1e-4f);
        QVERIFY(qAbs(dim[plane + 5] - (0.5f - 0.456f) / 0.224f) < 1e-3f);
    }

    void turnsTheModelsScoresIntoAMaskTheSizeOfThePhoto() {
        // Left half surely background, right half surely person.
        const float scores[] = {-12.0f, 12.0f, -12.0f, 12.0f};
        const QImage mask = matte::toMask(scores, 2, 2, QSize(400, 300));
        QCOMPARE(mask.size(), QSize(400, 300));
        QCOMPARE(mask.format(), QImage::Format_Grayscale8);
        QCOMPARE(qGray(mask.pixel(20, 150)), 0);
        QCOMPARE(qGray(mask.pixel(380, 150)), 255);
        // An unsure model still gives a full-range mask.
        const float unsure[] = {-0.2f, 0.3f};
        const QImage stretched = matte::toMask(unsure, 2, 1, QSize(2, 1));
        QCOMPARE(qGray(stretched.pixel(0, 0)), 0);
        QCOMPARE(qGray(stretched.pixel(1, 0)), 255);
        QVERIFY(matte::toMask(nullptr, 2, 2, QSize(4, 4)).isNull());
    }

    void cutsThePhotoToItsMaskAndTrimsIt() {
        const QColor wall(150, 150, 150);
        const QImage photo = scene({400, 300}, {150, 80, 100, 150}, kShirt, wall);
        QImage mask(400, 300, QImage::Format_Grayscale8);
        mask.fill(0);
        QPainter painter(&mask);
        painter.fillRect(QRect(150, 80, 100, 150), Qt::white);
        painter.end();

        const QImage lifted = matte::withMask(photo, mask);
        QCOMPARE(qAlpha(lifted.pixel(10, 10)), 0);
        QCOMPARE(lifted.pixelColor(200, 150), kShirt);

        const QImage trimmed = matte::cutout(photo, mask);
        QCOMPARE(trimmed.size(), QSize(104, 154));
        QCOMPARE(trimmed.pixelColor(50, 75), kShirt);
        // The crop applies here too.
        QCOMPARE(matte::cutout(photo, mask, QRectF(0.5, 0, 0.5, 1)).size(), QSize(52, 154));

        // A mask made at another size is fitted to the photo.
        const QImage small = mask.scaled(200, 150);
        QCOMPARE(matte::withMask(photo, small).pixelColor(200, 150), kShirt);
        QCOMPARE(qAlpha(matte::withMask(photo, small).pixel(10, 10)), 0);

        mask.fill(0);
        QVERIFY(matte::cutout(photo, mask).isNull());
    }

    void downloadsAModelIntoPlace() {
        const matte::Model model = localModel("pretend model", "pretend model");
        ModelDownload download;
        QSignalSpy finished(&download, &ModelDownload::finished);
        QSignalSpy progress(&download, &ModelDownload::progress);
        download.start(model);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(0).toString(), QString("local"));
        QCOMPARE(finished.first().at(1).toBool(), true);
        QVERIFY(progress.count() >= 1);
        QVERIFY(matte::installed(model));
        QVERIFY(!QFile::exists(matte::modelPath(model) + ".part"));
        QVERIFY(!download.running());
    }

    void throwsAwayADownloadThatFailsItsChecksum() {
        const matte::Model model = localModel("tampered with", "pretend model");
        ModelDownload download;
        QSignalSpy finished(&download, &ModelDownload::finished);
        download.start(model);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(1).toBool(), false);
        QVERIFY(finished.first().at(2).toString().contains("checksum"));
        QVERIFY(!QFile::exists(matte::modelPath(model)));
        QVERIFY(!QFile::exists(matte::modelPath(model) + ".part"));
    }

    void reportsADownloadThatCannotBeFetched() {
        matte::Model model = localModel("pretend model", "pretend model");
        model.url = QUrl::fromLocalFile(m_models.filePath("not-there.bin"));
        ModelDownload download;
        QSignalSpy finished(&download, &ModelDownload::finished);
        download.start(model);
        QTRY_COMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(1).toBool(), false);
        QVERIFY(finished.first().at(2).toString().startsWith("The download failed"));
        QVERIFY(!QFile::exists(matte::modelPath(model)));
    }

    void offersAModelForAPhotoWithNoGreenScreen() {
        QTemporaryDir dir;
        const QString photo = dir.filePath("at the table.png");
        QVERIFY(scene({400, 300}, {150, 80, 100, 220}, kShirt, QColor(29, 78, 137)).save(photo));
        auto *provider = new LiftProvider();
        Backend backend(provider);
        QVERIFY(backend.load(QUrl::fromLocalFile(photo)));

        // Nothing is downloaded without being asked for.
        QCOMPARE(backend.mode(), QString("fast"));
        QVERIFY(!backend.ready());
        QVERIFY(!backend.working());
        QVERIFY(!backend.modelInstalled());
        QVERIFY(!backend.downloading());
        QCOMPARE(backend.modelSize(), QString("214 MB"));
        QCOMPARE(backend.crop(), QRectF(0, 0, 1, 1));

        backend.setMode("best");
        QCOMPARE(backend.modelSize(), QString("928 MB"));
        backend.setMode("nonsense");
        QCOMPARE(backend.mode(), QString("best"));

        backend.setMode("screen");
        QVERIFY(!backend.ready());
        QVERIFY(backend.modelInstalled());
        QVERIFY(backend.status().contains("Try Fast or Best"));
        // Nothing to save in any of the three.
        QSignalSpy saves(&backend, &Backend::saved);
        backend.save(QUrl::fromLocalFile(dir.filePath("out.png")));
        QVERIFY(!backend.busy());
        QVERIFY(!QFile::exists(dir.filePath("out.png")));
        delete provider;
    }

    void keysAGreenScreenPhotoWithoutAnyModel() {
        QTemporaryDir dir;
        const QString photo = dir.filePath("photo.png");
        QVERIFY(scene({400, 300}, {150, 80, 100, 220}).save(photo));
        auto *provider = new LiftProvider();
        Backend backend(provider);
        QVERIFY(backend.load(QUrl::fromLocalFile(photo)));
        QCOMPARE(backend.mode(), QString("screen"));
        QVERIFY(backend.ready());
        delete provider;
    }

    // Runs the real models. They are far too big to keep with the tests, so
    // this needs OMALIFT_TEST_MODELS (a folder holding them) and
    // OMALIFT_TEST_PORTRAIT (a photo of one person, roughly centred).
    void cutsAPersonOutOfAnOrdinaryPhoto_data() {
        QTest::addColumn<QString>("mode");
        QTest::newRow("fast") << "fast";
        QTest::newRow("best") << "best";
    }
    void cutsAPersonOutOfAnOrdinaryPhoto() {
        QFETCH(QString, mode);
        const QString models = qEnvironmentVariable("OMALIFT_TEST_MODELS");
        const QString portrait = qEnvironmentVariable("OMALIFT_TEST_PORTRAIT");
        if (models.isEmpty() || portrait.isEmpty())
            QSKIP("Set OMALIFT_TEST_MODELS and OMALIFT_TEST_PORTRAIT to run the real models.");
        qputenv("OMALIFT_MODEL_DIR", models.toLocal8Bit());
        if (!matte::installed(*matte::byId(mode)))
            QSKIP("That model is not in OMALIFT_TEST_MODELS.");

        QTemporaryDir dir;
        auto *provider = new LiftProvider();
        Backend backend(provider);
        QSignalSpy saves(&backend, &Backend::saved);
        QVERIFY(backend.load(QUrl::fromLocalFile(portrait)));
        QVERIFY(!backend.screenFound());
        backend.setMode(mode);
        QCOMPARE(backend.mode(), mode);
        QVERIFY(backend.working());
        QVERIFY(!backend.ready());
        QTRY_VERIFY_WITH_TIMEOUT(backend.ready(), 300000);
        QVERIFY(!backend.working());

        const QSize size = backend.imageSize();
        const QImage preview = provider->requestImage("lifted/0", nullptr, QSize());
        QCOMPARE(preview.size(), size.scaled(1600, 1600, Qt::KeepAspectRatio).boundedTo(size));
        QCOMPARE(qAlpha(preview.pixel(preview.width() / 2, preview.height() / 2)), 255);
        QCOMPARE(qAlpha(preview.pixel(2, 2)), 0);

        backend.save(QUrl::fromLocalFile(dir.filePath("out.png")));
        QTRY_COMPARE_WITH_TIMEOUT(saves.count(), 1, 30000);
        const QImage cutout(dir.filePath("out.png"));
        QVERIFY(cutout.hasAlphaChannel());
        QVERIFY(cutout.width() <= size.width() && cutout.height() <= size.height());
        QVERIFY(cutout.width() > size.width() / 3 && cutout.height() > size.height() / 3);
        if (qEnvironmentVariableIsSet("OMALIFT_TEST_KEEP"))
            QVERIFY(cutout.save(qEnvironmentVariable("OMALIFT_TEST_KEEP") + "/" + mode + ".png"));
        delete provider;
    }

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
        // A line along the top, as a crop leaves on the edge of the cloth.
        painter.fillRect(QRect(0, 0, 1000, 2), kShirt);
        painter.end();

        key::removeSpecks(lifted);
        QCOMPARE(qAlpha(lifted.pixel(51, 51)), 0);
        QCOMPARE(qAlpha(lifted.pixel(500, 1)), 0);
        QCOMPARE(key::subjectBounds(lifted, 0), QRect(300, 100, 612, 800));
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

    void leavesNoLineWhereTheCropMeetsABlurredClothEdge() {
        // The same hung cloth, but photographed: its edges are soft.
        QImage photo(800, 600, QImage::Format_ARGB32);
        photo.fill(QColor(150, 150, 150));
        QPainter painter(&photo);
        painter.fillRect(QRect(160, 60, 480, 540), kGreen);
        painter.end();
        photo = photo.scaled(200, 150, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                    .scaled(800, 600, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QPainter person(&photo);
        person.fillRect(QRect(340, 240, 120, 360), kShirt);
        person.end();

        const key::Screen screen = key::detectScreen(photo);
        QVERIFY(screen.found);
        const QImage lifted = key::cutout(photo, screen, key::Settings(), key::screenExtent(photo, screen));
        // Trimmed to the person, with nothing left along the top or sides.
        QCOMPARE(lifted.size(), QSize(124, 362));
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
