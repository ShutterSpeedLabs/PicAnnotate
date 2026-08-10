#include "samcontroller.h"

#include "../core/project.h"
#include "../trackers/cvimageconvert.h"

#include <QThread>

void SamWorker::configure()
{
    QString error;
    QString warning;
    const bool ok = m_segmenter.load(&error, &warning);
    emit configured(ok, error, warning);
}

void SamWorker::encode(int frameIndex, const cv::Mat &image)
{
    QString error;
    const bool ok = m_segmenter.setImage(image, &error);
    emit encoded(frameIndex, ok, error);
}

void SamWorker::decode(quint64 requestId, const SamSegmenter::Prompt &prompt,
                       double polygonEpsilon)
{
    QString error;
    const std::optional<SamSegmenter::Result> result =
        m_segmenter.segment(prompt, polygonEpsilon, &error);

    if (!result) {
        emit decoded(requestId, false, {}, QRectF(), 0.0f, error);
        return;
    }
    emit decoded(requestId, true, result->polygon, result->box, result->score, QString());
}

void SamWorker::release()
{
    m_segmenter.unload();
}

SamController::SamController(Project *project, QObject *parent)
    : QObject(parent)
    , m_project(project)
{
    qRegisterMetaType<cv::Mat>("cv::Mat");
    qRegisterMetaType<SamSegmenter::Prompt>("SamSegmenter::Prompt");
    qRegisterMetaType<QVector<QPointF>>("QVector<QPointF>");
}

SamController::~SamController()
{
    if (m_thread) {
        m_thread->quit();
        // An encode can take a few seconds on a big ViT-B model, so give it
        // room before resorting to terminate().
        if (!m_thread->wait(10000))
            m_thread->terminate();
    }
}

void SamController::ensureWorker()
{
    if (m_worker)
        return;

    m_thread = new QThread(this);
    m_worker = new SamWorker;
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &SamWorker::configured, this, &SamController::onConfigured);
    connect(m_worker, &SamWorker::encoded, this, &SamController::onEncoded);
    connect(m_worker, &SamWorker::decoded, this, &SamController::onDecoded);

    m_thread->start();
}

void SamController::setActive(bool active)
{
    if (m_active == active)
        return;

    m_active = active;
    emit activeChanged(active);

    if (!active) {
        clearPrompt();
        emit statusChanged(QString());
        return;
    }

    ensureWorker();

    if (!m_configured) {
        if (!m_configuring) {
            m_configuring = true;
            emit statusChanged(tr("Loading SAM models..."));
            setBusy();
            QMetaObject::invokeMethod(m_worker, "configure", Qt::QueuedConnection);
        }
        return;
    }

    requestEncode();
}

void SamController::onConfigured(bool ok, const QString &error, const QString &warning)
{
    m_configuring = false;
    m_configured = ok;

    if (!ok) {
        m_active = false;
        emit activeChanged(false);
        emit statusChanged(QString());
        setBusy();
        emit failed(error);
        return;
    }

    if (!warning.isEmpty())
        emit failed(warning);

    if (m_active)
        requestEncode();
    else
        setBusy();
}

void SamController::requestEncode()
{
    if (!m_active || !m_configured || !m_project->hasSource())
        return;

    const int frameIndex = m_project->currentIndex();
    if (frameIndex < 0)
        return;

    // Already have it, or already getting it.
    if (frameIndex == m_encodedFrame || frameIndex == m_encodingFrame) {
        setBusy();
        return;
    }

    const QImage frame = m_project->currentFrame();
    if (frame.isNull()) {
        emit statusChanged(tr("This frame could not be decoded."));
        return;
    }

    m_encoding = true;
    m_encodingFrame = frameIndex;
    m_encodedFrame = -1;
    setBusy();
    emit statusChanged(tr("Encoding frame %1...").arg(frameIndex + 1));

    QMetaObject::invokeMethod(m_worker, "encode", Qt::QueuedConnection,
                              Q_ARG(int, frameIndex), Q_ARG(cv::Mat, qImageToBgrMat(frame)));
}

void SamController::onEncoded(int frameIndex, bool ok, const QString &error)
{
    m_encoding = false;
    m_encodingFrame = -1;

    if (!ok) {
        m_encodedFrame = -1;
        setBusy();
        emit statusChanged(QString());
        emit failed(error);
        return;
    }

    m_encodedFrame = frameIndex;

    // The playhead may have moved while the encode was running; if so the
    // embedding is already stale, so start the right one instead of pretending.
    if (m_active && m_project->currentIndex() != frameIndex) {
        requestEncode();
        return;
    }

    setBusy();
    emit statusChanged(tr("Ready — click to segment, Shift+click to exclude, Enter to accept."));

    // A prompt set while the encode was in flight is now runnable.
    if (!m_prompt.isEmpty())
        dispatchPrompt();
}

bool SamController::acceptsPrompts() const
{
    return m_active && m_configured && m_encodedFrame >= 0
           && m_encodedFrame == m_project->currentIndex();
}

void SamController::setPrompt(const SamSegmenter::Prompt &prompt)
{
    m_prompt = prompt;

    if (prompt.isEmpty()) {
        clearPrompt();
        return;
    }

    if (!acceptsPrompts()) {
        // Not encoded yet. onEncoded() picks the prompt back up.
        return;
    }

    dispatchPrompt();
}

void SamController::dispatchPrompt()
{
    if (m_decodeInFlight) {
        // Coalesce rather than queue: by the time a backlog drained, every
        // entry but the last would already be stale.
        m_queuedPrompt = m_prompt;
        return;
    }

    m_decodeInFlight = true;
    m_latestRequestId = m_nextRequestId++;
    setBusy();

    QMetaObject::invokeMethod(m_worker, "decode", Qt::QueuedConnection,
                              Q_ARG(quint64, m_latestRequestId),
                              Q_ARG(SamSegmenter::Prompt, m_prompt),
                              Q_ARG(double, m_polygonEpsilon));
}

void SamController::onDecoded(quint64 requestId, bool ok, const QVector<QPointF> &polygon,
                              const QRectF &box, float score, const QString &error)
{
    Q_UNUSED(box);
    m_decodeInFlight = false;

    // Ignore a result the user has already superseded.
    const bool stale = requestId != m_latestRequestId;

    if (m_queuedPrompt) {
        const SamSegmenter::Prompt queued = *m_queuedPrompt;
        m_queuedPrompt.reset();
        m_prompt = queued;
        dispatchPrompt();
        return;
    }

    setBusy();

    if (stale)
        return;

    if (!ok) {
        m_previewPolygon.clear();
        m_previewScore = 0.0f;
        emit previewCleared();
        if (!error.isEmpty())
            emit statusChanged(error);
        return;
    }

    m_previewPolygon = polygon;
    m_previewScore = score;
    emit previewChanged(polygon, score);
    emit statusChanged(tr("Mask score %1 — Enter to accept, Esc to clear.")
                           .arg(static_cast<double>(score), 0, 'f', 2));
}

void SamController::clearPrompt()
{
    m_prompt = SamSegmenter::Prompt();
    m_queuedPrompt.reset();
    // Bump the id so an in-flight decode's result is discarded when it lands.
    m_latestRequestId = m_nextRequestId++;

    if (!m_previewPolygon.isEmpty()) {
        m_previewPolygon.clear();
        m_previewScore = 0.0f;
        emit previewCleared();
    }
}

void SamController::onCurrentFrameChanged()
{
    if (!m_active)
        return;

    clearPrompt();
    requestEncode();
}

void SamController::setPolygonEpsilon(double epsilon)
{
    m_polygonEpsilon = qBound(0.0005, epsilon, 0.05);
}

void SamController::setBusy()
{
    const bool busy = isBusy();
    if (busy == m_lastBusy)
        return;
    m_lastBusy = busy;
    emit busyChanged(busy);
}
