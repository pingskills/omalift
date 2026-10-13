#include "liftprovider.h"

#include <QMutexLocker>

QImage LiftProvider::requestImage(const QString &id, QSize *size, const QSize &) {
    QMutexLocker locker(&m_mutex);
    const QImage image = id.startsWith(QLatin1String("lifted")) ? m_lifted : m_original;
    if (size)
        *size = image.size();
    return image;
}

void LiftProvider::setOriginal(const QImage &image) {
    QMutexLocker locker(&m_mutex);
    m_original = image;
}

void LiftProvider::setLifted(const QImage &image) {
    QMutexLocker locker(&m_mutex);
    m_lifted = image;
}
