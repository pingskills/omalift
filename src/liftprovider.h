#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickImageProvider>

// Hands the photo and its keyed preview to QML as image://lift/original/<n>
// and image://lift/lifted/<n>. The number only exists to make QML reload.
class LiftProvider : public QQuickImageProvider {
public:
    LiftProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

    void setOriginal(const QImage &image);
    void setLifted(const QImage &image);

private:
    QMutex m_mutex;
    QImage m_original;
    QImage m_lifted;
};
