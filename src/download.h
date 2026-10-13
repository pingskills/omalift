#pragma once

#include <QCryptographicHash>
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

#include "matte.h"

class QNetworkReply;

// Fetches one model file. It is written beside its final name and only moved
// into place once the checksum matches, so a half-finished or tampered
// download can never be mistaken for a model.
class ModelDownload : public QObject {
    Q_OBJECT

public:
    explicit ModelDownload(QObject *parent = nullptr);
    ~ModelDownload() override;

    bool running() const { return !m_reply.isNull(); }
    void start(const matte::Model &model);
    void cancel();

signals:
    void progress(qint64 received, qint64 total);
    // `error` is empty on success, and also when the download was cancelled.
    void finished(const QString &modelId, bool ok, const QString &error);

private:
    void finish(bool ok, const QString &error);

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    matte::Model m_model;
    QFile m_part;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    bool m_cancelled = false;
};
