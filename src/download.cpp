#include "download.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>

ModelDownload::ModelDownload(QObject *parent) : QObject(parent) {}

ModelDownload::~ModelDownload() {
    cancel();
}

void ModelDownload::start(const matte::Model &model) {
    if (running())
        return;

    m_model = model;
    m_cancelled = false;
    m_hash.reset();

    const QString path = matte::modelPath(model);
    QDir().mkpath(QFileInfo(path).path());
    m_part.setFileName(path + QStringLiteral(".part"));
    if (!m_part.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit finished(model.id, false, QStringLiteral("Could not write to %1.").arg(m_part.fileName()));
        return;
    }

    QNetworkRequest request(model.url);
    // The release link redirects to wherever the file is actually stored.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network.get(request);

    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        const QByteArray chunk = m_reply->readAll();
        m_hash.addData(chunk);
        m_part.write(chunk);
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        emit progress(received, total > 0 ? total : m_model.bytes);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        const QNetworkReply::NetworkError problem = m_reply->error();
        const QString message = m_reply->errorString();
        m_reply->deleteLater();
        m_reply.clear();
        if (m_cancelled)
            return finish(false, QString());
        if (problem != QNetworkReply::NoError)
            return finish(false, QStringLiteral("The download failed: %1").arg(message));
        if (m_hash.result().toHex() != m_model.sha256)
            return finish(false, QStringLiteral("The download did not match its checksum, so it was thrown away."));
        finish(true, QString());
    });
}

void ModelDownload::cancel() {
    if (!running())
        return;
    m_cancelled = true;
    m_reply->abort();
}

void ModelDownload::finish(bool ok, const QString &error) {
    m_part.close();
    QString message = error;
    if (ok) {
        const QString path = matte::modelPath(m_model);
        QFile::remove(path);
        if (!m_part.rename(path)) {
            ok = false;
            message = QStringLiteral("Could not save the model to %1.").arg(path);
        }
    }
    if (!ok)
        m_part.remove();
    emit finished(m_model.id, ok, message);
}
