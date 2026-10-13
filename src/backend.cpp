#include "backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QStandardPaths>
#include <QTextStream>
#include <QtConcurrent>

#include "liftprovider.h"

namespace {

const QString kDefaultAccent = QStringLiteral("#1fb141");
// The preview is keyed at this size, so the sliders respond on any photo.
const int kPreviewSide = 1600;

QString omarchyCurrentDir() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/omarchy/current");
}

QString omarchyColorsPath() {
    return omarchyCurrentDir() + QStringLiteral("/theme/colors.toml");
}

struct Saved {
    bool ok = false;
    QSize size;
};

}  // namespace

Backend::Backend(LiftProvider *provider, QObject *parent)
    : QObject(parent), m_provider(provider), m_themeAccent(kDefaultAccent) {
    loadThemeAccent();
    watchTheme();
    const auto themeChanged = [this] {
        loadThemeAccent();
        watchTheme();
    };
    connect(&m_themeWatcher, &QFileSystemWatcher::fileChanged, this, themeChanged);
    connect(&m_themeWatcher, &QFileSystemWatcher::directoryChanged, this, themeChanged);
}

Backend::~Backend() = default;

QString Backend::accentFromColorsFile(const QString &path, const QString &fallback) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return fallback;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        const int equals = line.indexOf(QLatin1Char('='));
        if (line.startsWith(QLatin1Char('#')) || equals < 0
                || line.left(equals).trimmed() != QStringLiteral("accent"))
            continue;

        QString value = line.mid(equals + 1).trimmed();
        if (value.size() >= 2 && (value.front() == QLatin1Char('"') || value.front() == QLatin1Char('\''))
                && value.back() == value.front())
            value = value.mid(1, value.size() - 2);
        return QColor::fromString(value).isValid() ? value : fallback;
    }
    return fallback;
}

QString Backend::foregroundFor(const QString &color) {
    const QColor parsed = QColor::fromString(color);
    if (!parsed.isValid())
        return QStringLiteral("black");
    const double luminance = 0.299 * parsed.redF() + 0.587 * parsed.greenF() + 0.114 * parsed.blueF();
    return luminance < 0.5 ? QStringLiteral("white") : QStringLiteral("black");
}

QString Backend::themeAccentForeground() const {
    return foregroundFor(m_themeAccent);
}

void Backend::loadThemeAccent() {
    const QString accent = accentFromColorsFile(omarchyColorsPath(), kDefaultAccent);
    if (accent == m_themeAccent)
        return;
    m_themeAccent = accent;
    emit themeAccentChanged();
}

// Switching theme replaces the directory rather than editing the file, so the
// watches are laid again each time anything under it changes.
void Backend::watchTheme() {
    const QStringList watched = m_themeWatcher.files() + m_themeWatcher.directories();
    if (!watched.isEmpty())
        m_themeWatcher.removePaths(watched);
    for (const QString &path : {omarchyCurrentDir(), omarchyCurrentDir() + QStringLiteral("/theme"), omarchyColorsPath()})
        if (QFileInfo::exists(path))
            m_themeWatcher.addPath(path);
}

QImage Backend::readPhoto(const QString &path, QString *error) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull() && error)
        *error = reader.errorString();
    return image;
}

QString Backend::cutoutPathFor(const QString &photoPath) {
    const QFileInfo file(photoPath);
    return file.dir().filePath(file.completeBaseName() + QStringLiteral("-cutout.png"));
}

void Backend::setStatus(const QString &status) {
    if (status == m_status)
        return;
    m_status = status;
    emit statusChanged();
}

void Backend::setBusy(bool busy) {
    if (busy == m_busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

bool Backend::load(const QUrl &url) {
    const QString path = url.toLocalFile();
    QString error;
    const QImage image = readPhoto(path, &error);
    if (image.isNull()) {
        const QString message = QStringLiteral("Could not open %1: %2").arg(QFileInfo(path).fileName(), error);
        setStatus(message);
        emit failed(message);
        return false;
    }

    ++m_generation;
    m_source = url;
    m_image = image;
    m_preview = image.width() > kPreviewSide || image.height() > kPreviewSide
        ? image.scaled(QSize(kPreviewSide, kPreviewSide), Qt::KeepAspectRatio, Qt::SmoothTransformation)
        : image;
    m_screen = key::detectScreen(m_preview);
    m_settings = key::Settings();
    m_suggestedCrop = key::screenExtent(m_preview, m_screen);
    m_crop = m_suggestedCrop;

    m_provider->setOriginal(m_preview);
    // Until the first key lands the photo itself is shown.
    m_provider->setLifted(m_preview);
    ++m_revision;

    emit imageChanged();
    emit settingsChanged();
    emit cropChanged();
    emit previewChanged();

    if (m_screen.found) {
        setStatus(QStringLiteral("%1 × %2").arg(image.width()).arg(image.height()));
        refreshPreview();
    } else {
        setStatus(QStringLiteral("No green screen found in this photo."));
    }
    return true;
}

void Backend::changeSetting(double &setting, double value) {
    value = qBound(0.0, value, 1.0);
    if (qFuzzyCompare(setting + 1.0, value + 1.0))
        return;
    setting = value;
    emit settingsChanged();
    refreshPreview();
}

void Backend::setStrength(double value) { changeSetting(m_settings.strength, value); }
void Backend::setSoftness(double value) { changeSetting(m_settings.softness, value); }
void Backend::setDespill(double value) { changeSetting(m_settings.despill, value); }

void Backend::setCrop(const QRectF &crop) {
    const QRectF bounded = crop.normalized().intersected(QRectF(0, 0, 1, 1));
    if (bounded == m_crop || bounded.isEmpty())
        return;
    m_crop = bounded;
    emit cropChanged();
}

void Backend::reset() {
    m_settings = key::Settings();
    emit settingsChanged();
    setCrop(m_suggestedCrop);
    refreshPreview();
}

// One key runs at a time. A slider that moves while one is running just marks
// the result stale, and the latest settings are keyed as soon as it lands.
void Backend::refreshPreview() {
    if (!hasImage() || !m_screen.found)
        return;
    if (m_previewRunning) {
        m_previewStale = true;
        return;
    }
    m_previewRunning = true;
    m_previewStale = false;

    const int generation = m_generation;
    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        m_previewRunning = false;
        if (generation == m_generation) {
            m_provider->setLifted(watcher->result());
            ++m_revision;
            emit previewChanged();
        }
        if (m_previewStale || generation != m_generation)
            refreshPreview();
    });
    watcher->setFuture(QtConcurrent::run([preview = m_preview, screen = m_screen, settings = m_settings] {
        QImage lifted = key::lift(preview, screen, settings);
        key::removeSpecks(lifted);
        return lifted;
    }));
}

QUrl Backend::suggestedSaveUrl() const {
    return hasImage() ? QUrl::fromLocalFile(cutoutPathFor(m_source.toLocalFile())) : QUrl();
}

void Backend::save(const QUrl &destination) {
    if (!hasImage() || !m_screen.found || m_busy)
        return;

    QString path = destination.toLocalFile();
    // A cutout needs transparency, so it is a PNG whatever name was typed.
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
        path += QStringLiteral(".png");

    setBusy(true);
    setStatus(QStringLiteral("Saving…"));

    auto *watcher = new QFutureWatcher<Saved>(this);
    connect(watcher, &QFutureWatcher<Saved>::finished, this, [this, watcher, path] {
        watcher->deleteLater();
        const Saved result = watcher->result();
        setBusy(false);
        if (!result.ok) {
            const QString message = result.size.isEmpty()
                ? QStringLiteral("Nothing is left of the photo with these settings.")
                : QStringLiteral("Could not write %1.").arg(path);
            setStatus(message);
            emit failed(message);
            return;
        }
        setStatus(QStringLiteral("Saved %1 (%2 × %3)")
                      .arg(QFileInfo(path).fileName())
                      .arg(result.size.width())
                      .arg(result.size.height()));
        emit saved(path);
    });
    watcher->setFuture(QtConcurrent::run([image = m_image, screen = m_screen, settings = m_settings, crop = m_crop, path] {
        Saved result;
        const QImage lifted = key::cutout(image, screen, settings, crop);
        if (lifted.isNull())
            return result;
        result.size = lifted.size();
        result.ok = lifted.save(path, "PNG");
        return result;
    }));
}
