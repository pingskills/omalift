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

    connect(&m_download, &ModelDownload::progress, this, [this](qint64 received, qint64 total) {
        m_downloadProgress = total > 0 ? double(received) / total : 0.0;
        emit downloadProgressChanged();
    });
    connect(&m_download, &ModelDownload::finished, this, [this](const QString &modelId, bool ok, const QString &error) {
        if (!error.isEmpty()) {
            setStatus(error);
            emit failed(error);
        }
        emit stateChanged();
        // Carry straight on to what the download was for.
        if (ok && modelId == m_mode)
            enterMode();
    });
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
    m_masks.clear();
    m_queuedId.clear();
    // A green screen is keyed, which is instant. Anything else goes to a
    // model: whichever is already here, the quicker one first.
    if (m_screen.found)
        m_mode = QStringLiteral("screen");
    else
        m_mode = !matte::installed(matte::fast()) && matte::installed(matte::best()) ? matte::best().id : matte::fast().id;
    m_crop = defaultCrop();

    m_provider->setOriginal(m_preview);
    // Until the first key lands the photo itself is shown.
    m_provider->setLifted(m_preview);
    ++m_revision;

    emit imageChanged();
    emit settingsChanged();
    emit cropChanged();
    emit previewChanged();
    enterMode();
    return true;
}

bool Backend::ready() const {
    if (!hasImage())
        return false;
    return m_mode == QLatin1String("screen") ? m_screen.found : m_masks.contains(m_mode);
}

bool Backend::modelInstalled() const {
    const matte::Model *model = matte::byId(m_mode);
    return !model || matte::installed(*model);
}

QString Backend::modelSize() const {
    const matte::Model *model = matte::byId(m_mode);
    return model ? matte::sizeLabel(*model) : QString();
}

// A green screen photo is cropped to its cloth; a model looks at everything.
QRectF Backend::defaultCrop() const {
    return m_mode == QLatin1String("screen") ? m_suggestedCrop : QRectF(0, 0, 1, 1);
}

void Backend::setMode(const QString &mode) {
    if (mode == m_mode || (mode != QLatin1String("screen") && !matte::byId(mode)))
        return;
    m_mode = mode;
    setCrop(defaultCrop());
    enterMode();
}

// Shows whatever the current mode has for this photo, and sets off whatever
// it still needs.
void Backend::enterMode() {
    emit stateChanged();
    if (!hasImage())
        return;

    const QString size = QStringLiteral("%1 × %2").arg(m_image.width()).arg(m_image.height());
    if (m_mode == QLatin1String("screen")) {
        setStatus(m_screen.found ? size : QStringLiteral("No green screen found in this photo. Try Fast or Best."));
        refreshPreview();
        return;
    }
    if (m_masks.contains(m_mode)) {
        setStatus(size);
        refreshPreview();
        return;
    }
    // Nothing to show yet, so the photo itself stays up.
    m_provider->setLifted(m_preview);
    ++m_revision;
    emit previewChanged();
    if (modelInstalled())
        startMatte(m_mode);
    else if (!downloading())
        setStatus(size);
}

void Backend::startMatte(const QString &modelId) {
    const matte::Model *model = matte::byId(modelId);
    if (!model || !hasImage())
        return;
    if (!m_mattingId.isEmpty()) {
        if (m_mattingId != modelId)
            m_queuedId = modelId;
        return;
    }
    m_mattingId = modelId;
    setStatus(QStringLiteral("Cutting out… this can take up to a minute."));
    emit stateChanged();

    struct Matted {
        QImage mask;
        QString error;
        std::shared_ptr<matte::Session> session;
    };
    // A loaded model is kept for the next photo; loading one is slow.
    std::shared_ptr<matte::Session> session = m_sessionId == modelId ? m_session : nullptr;
    m_session.reset();
    m_sessionId.clear();

    const int generation = m_generation;
    auto *watcher = new QFutureWatcher<Matted>(this);
    connect(watcher, &QFutureWatcher<Matted>::finished, this, [this, watcher, generation, modelId] {
        watcher->deleteLater();
        const Matted result = watcher->result();
        m_mattingId.clear();
        m_session = result.session;
        m_sessionId = result.session ? modelId : QString();

        if (generation == m_generation) {
            if (result.mask.isNull()) {
                const QString message = QStringLiteral("Could not cut this photo out: %1").arg(result.error);
                setStatus(message);
                emit failed(message);
            } else {
                m_masks.insert(modelId, result.mask);
            }
        }
        const QString queued = m_queuedId;
        m_queuedId.clear();
        if (generation != m_generation) {
            // A different photo was opened meanwhile; start over for that one.
            enterMode();
        } else if (!queued.isEmpty() && !m_masks.contains(queued)) {
            if (modelId == m_mode && m_masks.contains(modelId))
                enterMode();
            startMatte(queued);
        } else if (modelId == m_mode && m_masks.contains(modelId)) {
            enterMode();
        } else {
            emit stateChanged();
        }
    });
    watcher->setFuture(QtConcurrent::run([image = m_image, path = matte::modelPath(*model), session]() mutable {
        Matted result;
        if (!session) {
            session = std::make_shared<matte::Session>();
            if (!session->open(path, &result.error))
                return result;
        }
        result.mask = session->mask(image, &result.error);
        result.session = session;
        return result;
    }));
}

void Backend::downloadModel() {
    const matte::Model *model = matte::byId(m_mode);
    if (!model || m_download.running() || matte::installed(*model))
        return;
    m_downloadId = model->id;
    m_downloadProgress = 0.0;
    emit downloadProgressChanged();
    setStatus(QStringLiteral("Downloading the %1 model (%2)…").arg(model->label, matte::sizeLabel(*model)));
    m_download.start(*model);
    emit stateChanged();
}

void Backend::cancelDownload() {
    if (!m_download.running())
        return;
    m_download.cancel();
    setStatus(QString());
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
    setCrop(defaultCrop());
    refreshPreview();
}

// One key runs at a time. A slider that moves while one is running just marks
// the result stale, and the latest settings are keyed as soon as it lands.
void Backend::refreshPreview() {
    if (!hasImage())
        return;
    if (m_mode != QLatin1String("screen")) {
        // A model's mask is already made; laying it over the preview is quick.
        if (!m_masks.contains(m_mode))
            return;
        m_provider->setLifted(matte::withMask(m_preview, m_masks.value(m_mode)));
        ++m_revision;
        emit previewChanged();
        return;
    }
    if (!m_screen.found)
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
        // Dropped if the photo or the mode changed while it was being keyed.
        if (generation == m_generation && m_mode == QLatin1String("screen")) {
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
    if (!ready() || m_busy)
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
    const bool keyed = m_mode == QLatin1String("screen");
    watcher->setFuture(QtConcurrent::run([image = m_image, screen = m_screen, settings = m_settings, crop = m_crop,
                                          mask = m_masks.value(m_mode), keyed, path] {
        Saved result;
        const QImage lifted = keyed ? key::cutout(image, screen, settings, crop) : matte::cutout(image, mask, crop);
        if (lifted.isNull())
            return result;
        result.size = lifted.size();
        result.ok = lifted.save(path, "PNG");
        return result;
    }));
}
