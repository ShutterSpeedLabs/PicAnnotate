#ifndef MODELDOWNLOAD_H
#define MODELDOWNLOAD_H

#include "modelspec.h"

#include <QCryptographicHash>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;

// One model file being fetched.
//
// The bytes are streamed straight to a QSaveFile and hashed as they arrive, so a
// 400MB encoder never sits in memory and a truncated or tampered download is
// caught before it is committed — a half-written .onnx that loads and then emits
// nonsense is far worse than a download that plainly failed.
class ModelDownload : public QObject
{
    Q_OBJECT
public:
    ModelDownload(const ModelSpec &spec, const QString &destinationPath,
                  QNetworkAccessManager *network, QObject *parent = nullptr);
    ~ModelDownload() override;

    const ModelSpec &spec() const { return m_spec; }
    QString destinationPath() const { return m_destinationPath; }

    void start();
    void cancel();

    bool isFinished() const { return m_finished; }
    bool wasCancelled() const { return m_cancelled; }

signals:
    // `total` is -1 while the server has not said how big the file is.
    void progress(qint64 received, qint64 total);

    // Exactly once per download, whatever the outcome. `error` is empty on
    // success and carries a user-facing reason otherwise.
    void finished(bool ok, const QString &error);

private slots:
    void onReadyRead();
    void onDownloadProgress(qint64 received, qint64 total);
    void onReplyFinished();

private:
    void fail(const QString &error);
    void cleanUpPartialFile();

    ModelSpec m_spec;
    QString m_destinationPath;
    QNetworkAccessManager *m_network;
    QNetworkReply *m_reply = nullptr;
    QSaveFile *m_file = nullptr;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};

    bool m_finished = false;
    bool m_cancelled = false;
};

#endif // MODELDOWNLOAD_H
