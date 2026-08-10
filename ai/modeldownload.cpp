#include "modeldownload.h"

#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>

ModelDownload::ModelDownload(const ModelSpec &spec, const QString &destinationPath,
                             QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_spec(spec)
    , m_destinationPath(destinationPath)
    , m_network(network)
{
}

ModelDownload::~ModelDownload()
{
    cleanUpPartialFile();
}

void ModelDownload::start()
{
    if (m_finished)
        return;

    const QUrl url(m_spec.downloadUrl);
    if (!url.isValid() || url.scheme() != QLatin1String("https")) {
        // Model files are executable graphs in all but name. Fetching one over
        // plain HTTP would let anything on the path decide what the app runs.
        fail(tr("%1 is not a valid https:// download URL.").arg(m_spec.downloadUrl));
        return;
    }

    const QFileInfo info(m_destinationPath);
    if (!QDir().mkpath(info.absolutePath())) {
        fail(tr("Could not create the model folder %1.").arg(info.absolutePath()));
        return;
    }

    m_file = new QSaveFile(m_destinationPath, this);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(tr("Could not write to %1: %2").arg(m_destinationPath, m_file->errorString()));
        return;
    }

    QNetworkRequest request(url);
    // Release URLs on GitHub and Hugging Face both redirect to a CDN host.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PicAnnotate"));

    m_reply = m_network->get(request);
    m_reply->setParent(this);

    connect(m_reply, &QNetworkReply::readyRead, this, &ModelDownload::onReadyRead);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &ModelDownload::onDownloadProgress);
    connect(m_reply, &QNetworkReply::finished, this, &ModelDownload::onReplyFinished);
}

void ModelDownload::cancel()
{
    if (m_finished)
        return;

    m_cancelled = true;
    if (m_reply)
        m_reply->abort();   // lands in onReplyFinished with OperationCanceledError
    else
        fail(tr("Download cancelled."));
}

void ModelDownload::onReadyRead()
{
    if (!m_reply || !m_file)
        return;

    const QByteArray chunk = m_reply->readAll();
    if (chunk.isEmpty())
        return;

    if (m_file->write(chunk) != chunk.size()) {
        const QString reason = m_file->errorString();
        m_reply->abort();
        fail(tr("Could not write to %1: %2").arg(m_destinationPath, reason));
        return;
    }
    m_hash.addData(chunk);
}

void ModelDownload::onDownloadProgress(qint64 received, qint64 total)
{
    // Servers that omit Content-Length report -1; fall back to the size the
    // catalogue declared so the progress bar still means something.
    if (total <= 0 && m_spec.downloadBytes > 0)
        total = m_spec.downloadBytes;
    emit progress(received, total);
}

void ModelDownload::onReplyFinished()
{
    if (m_finished)
        return;

    if (!m_reply) {
        fail(tr("Download failed."));
        return;
    }

    if (m_reply->error() != QNetworkReply::NoError) {
        const QString reason = m_cancelled ? tr("Download cancelled.")
                                           : m_reply->errorString();
        fail(reason);
        return;
    }

    // Anything still buffered when finished() fires would otherwise be dropped,
    // producing a file that is short by a few KB and fails the hash check.
    onReadyRead();

    if (!m_spec.sha256.isEmpty()) {
        const QString actual = QString::fromLatin1(m_hash.result().toHex());
        if (actual != m_spec.sha256) {
            fail(tr("Checksum mismatch for %1.\n\nExpected SHA-256 %2\nbut the download hashed to %3.\n\n"
                    "The file was discarded. Either the catalogue entry is out of date or the "
                    "download was corrupted.")
                     .arg(m_spec.fileName, m_spec.sha256, actual));
            return;
        }
    }

    if (!m_file->commit()) {
        const QString reason = m_file->errorString();
        m_file = nullptr;   // commit() failing already closed and removed it
        fail(tr("Could not save %1: %2").arg(m_destinationPath, reason));
        return;
    }

    m_file = nullptr;
    m_finished = true;
    emit finished(true, QString());
}

void ModelDownload::fail(const QString &error)
{
    if (m_finished)
        return;

    cleanUpPartialFile();
    m_finished = true;
    emit finished(false, error);
}

void ModelDownload::cleanUpPartialFile()
{
    if (!m_file)
        return;

    // cancelWriting() means the real file on disk is never touched, so a failed
    // re-download cannot destroy a good copy that was already there.
    m_file->cancelWriting();
    m_file->deleteLater();
    m_file = nullptr;
}
