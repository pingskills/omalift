#pragma once

#include <QFileSystemWatcher>
#include <QImage>
#include <QObject>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QUrl>

#include <QHash>

#include <memory>

#include "download.h"
#include "key.h"
#include "matte.h"

class LiftProvider;

// The bridge between QML and the cutout code. Holds the photo and shows a
// screen-sized preview of its cutout, made one of three ways: "screen" keys
// out a green screen as the sliders move; "fast" and "best" run a model that
// copes with any background, once per photo.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source NOTIFY imageChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imageChanged)
    Q_PROPERTY(bool screenFound READ screenFound NOTIFY imageChanged)
    Q_PROPERTY(QSize imageSize READ imageSize NOTIFY imageChanged)
    Q_PROPERTY(int revision READ revision NOTIFY previewChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY stateChanged)
    // A cutout is on show and can be saved.
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    // The model for this mode is running on the photo.
    Q_PROPERTY(bool working READ working NOTIFY stateChanged)
    Q_PROPERTY(bool modelInstalled READ modelInstalled NOTIFY stateChanged)
    Q_PROPERTY(QString modelSize READ modelSize NOTIFY stateChanged)
    Q_PROPERTY(bool downloading READ downloading NOTIFY stateChanged)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(double strength READ strength WRITE setStrength NOTIFY settingsChanged)
    Q_PROPERTY(double softness READ softness WRITE setSoftness NOTIFY settingsChanged)
    Q_PROPERTY(double despill READ despill WRITE setDespill NOTIFY settingsChanged)
    Q_PROPERTY(QRectF crop READ crop WRITE setCrop NOTIFY cropChanged)
    Q_PROPERTY(QString themeAccent READ themeAccent NOTIFY themeAccentChanged)
    Q_PROPERTY(QString themeAccentForeground READ themeAccentForeground NOTIFY themeAccentChanged)

public:
    explicit Backend(LiftProvider *provider, QObject *parent = nullptr);
    ~Backend() override;

    QUrl source() const { return m_source; }
    bool hasImage() const { return !m_image.isNull(); }
    bool screenFound() const { return m_screen.found; }
    QSize imageSize() const { return m_image.size(); }
    int revision() const { return m_revision; }
    QString mode() const { return m_mode; }
    bool ready() const;
    bool working() const { return m_mode != QLatin1String("screen") && (m_mattingId == m_mode || m_queuedId == m_mode); }
    bool modelInstalled() const;
    QString modelSize() const;
    bool downloading() const { return m_download.running() && m_downloadId == m_mode; }
    double downloadProgress() const { return m_downloadProgress; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    double strength() const { return m_settings.strength; }
    double softness() const { return m_settings.softness; }
    double despill() const { return m_settings.despill; }
    QRectF crop() const { return m_crop; }
    QString themeAccent() const { return m_themeAccent; }
    QString themeAccentForeground() const;

    void setStrength(double value);
    void setSoftness(double value);
    void setDespill(double value);
    void setCrop(const QRectF &crop);
    void setMode(const QString &mode);

    // The accent from an omarchy colors.toml, or the fallback when the file is
    // missing or has no usable accent, which keeps omalift working elsewhere.
    static QString accentFromColorsFile(const QString &path, const QString &fallback);
    // "black" or "white", whichever stays legible on the given color.
    static QString foregroundFor(const QString &color);

    // Reads a photo the right way up, whatever its EXIF orientation says.
    static QImage readPhoto(const QString &path, QString *error = nullptr);
    // "<name>-cutout.png" beside the photo.
    static QString cutoutPathFor(const QString &photoPath);

    Q_INVOKABLE bool load(const QUrl &url);
    Q_INVOKABLE QUrl suggestedSaveUrl() const;
    Q_INVOKABLE void save(const QUrl &destination);
    // Back to the sliders and crop the photo was opened with.
    Q_INVOKABLE void reset();
    // Fetch the model the current mode needs. The photo is cut out as soon
    // as it arrives.
    Q_INVOKABLE void downloadModel();
    Q_INVOKABLE void cancelDownload();

signals:
    void imageChanged();
    void previewChanged();
    void stateChanged();
    void downloadProgressChanged();
    void busyChanged();
    void statusChanged();
    void settingsChanged();
    void cropChanged();
    void themeAccentChanged();
    void saved(const QString &path);
    void failed(const QString &message);

private:
    void setStatus(const QString &status);
    void setBusy(bool busy);
    void changeSetting(double &setting, double value);
    void refreshPreview();
    void enterMode();
    void startMatte(const QString &modelId);
    QRectF defaultCrop() const;
    void loadThemeAccent();
    void watchTheme();

    LiftProvider *m_provider;
    QUrl m_source;
    QImage m_image;
    QImage m_preview;
    key::Screen m_screen;
    key::Settings m_settings;
    QRectF m_crop{0, 0, 1, 1};
    QRectF m_suggestedCrop{0, 0, 1, 1};
    int m_revision = 0;
    // Bumped on every load, so work finishing for an earlier photo is dropped.
    int m_generation = 0;
    bool m_previewRunning = false;
    bool m_previewStale = false;
    bool m_busy = false;
    QString m_mode = QStringLiteral("screen");
    // The mask each model made for this photo, so switching between Fast and
    // Best after both have run costs nothing.
    QHash<QString, QImage> m_masks;
    // Only one model runs at a time; a second request waits its turn.
    QString m_mattingId;
    QString m_queuedId;
    std::shared_ptr<matte::Session> m_session;
    QString m_sessionId;
    ModelDownload m_download;
    QString m_downloadId;
    double m_downloadProgress = 0.0;
    QString m_status;
    QString m_themeAccent;
    QFileSystemWatcher m_themeWatcher;
};
