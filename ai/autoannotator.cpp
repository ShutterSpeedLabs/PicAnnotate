#include "autoannotator.h"

#include "modelmanager.h"
#include "predictionstore.h"

#include "../core/project.h"
#include "../trackers/cvimageconvert.h"

#include <QThread>

void DetectionWorker::configure(const ModelSpec &spec, const QString &modelPath,
                                const QStringList &classNames)
{
    QString error;
    const bool ok = m_detector.load(spec, modelPath, classNames, &error);
    emit configured(ok, error);
}

void DetectionWorker::detect(int frameIndex, const cv::Mat &image, YoloDetector::Options options)
{
    QString error;
    const QVector<Detection> detections = m_detector.detect(image, options, &error);
    emit detected(frameIndex, detections, error);
}

void DetectionWorker::release()
{
    m_detector.unload();
}

AutoAnnotator::AutoAnnotator(Project *project, PredictionStore *store, QObject *parent)
    : QObject(parent)
    , m_project(project)
    , m_store(store)
{
    // Queued connections copy their arguments through the metatype system, so
    // every type crossing the thread boundary has to be registered first.
    qRegisterMetaType<cv::Mat>("cv::Mat");
    qRegisterMetaType<Detection>("Detection");
    qRegisterMetaType<QVector<Detection>>("QVector<Detection>");
    qRegisterMetaType<YoloDetector::Options>("YoloDetector::Options");
    qRegisterMetaType<ModelSpec>("ModelSpec");
}

AutoAnnotator::~AutoAnnotator()
{
    if (m_thread) {
        m_thread->quit();
        // The worker only ever holds one frame at a time, so this returns
        // quickly; the timeout is a backstop against a wedged inference call
        // rather than an expected path.
        if (!m_thread->wait(5000))
            m_thread->terminate();
    }
}

void AutoAnnotator::setTask(ModelTask task)
{
    if (task != ModelTask::Detection && task != ModelTask::Segmentation)
        return;
    if (m_task == task)
        return;

    m_task = task;
    // Force a reconfigure: the loaded graph belongs to the other task.
    m_activeModelId.clear();
}

void AutoAnnotator::ensureWorker()
{
    if (m_worker)
        return;

    m_thread = new QThread(this);
    m_worker = new DetectionWorker;
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &DetectionWorker::configured, this, &AutoAnnotator::onConfigured);
    connect(m_worker, &DetectionWorker::detected, this, &AutoAnnotator::onDetected);

    m_thread->start();
}

bool AutoAnnotator::resolveModel(ModelSpec *spec, QString *path, QStringList *classNames,
                                 QString *error) const
{
    const ModelManager &manager = ModelManager::instance();

    const ModelSpec *selected = manager.selectedModel(m_task);
    if (!selected) {
        if (error) {
            *error = tr("No %1 model is registered. Open the Model Manager to add one.")
                         .arg(modelTaskDisplayName(m_task).toLower());
        }
        return false;
    }

    QString reason;
    if (!manager.isUsable(selected->id, &reason)) {
        if (error)
            *error = reason;
        return false;
    }

    *spec = *selected;
    *path = manager.localPath(selected->id);
    *classNames = manager.registry().classNamesFor(*selected);
    return true;
}

bool AutoAnnotator::runOnFrame(int frameIndex, QString *error)
{
    return runOnFrames({frameIndex}, error);
}

bool AutoAnnotator::runOnFrames(const QList<int> &frames, QString *error)
{
    if (m_running) {
        if (error)
            *error = tr("A model run is already in progress.");
        return false;
    }
    if (frames.isEmpty()) {
        if (error)
            *error = tr("No frames to run on.");
        return false;
    }
    if (!m_project->hasSource()) {
        if (error)
            *error = tr("Open a source first.");
        return false;
    }

    ModelSpec spec;
    QString path;
    QStringList classNames;
    if (!resolveModel(&spec, &path, &classNames, error))
        return false;

    ensureWorker();

    m_queue = frames;
    m_cursor = 0;
    m_detectionCount = 0;
    m_running = true;
    m_cancelled = false;

    emit started(m_queue.size());

    if (m_activeModelId == spec.id) {
        // Already loaded on the worker; go straight to the first frame.
        pumpNextFrame();
        return true;
    }

    m_awaitingConfigure = true;
    m_activeModelId = spec.id;
    QMetaObject::invokeMethod(m_worker, "configure", Qt::QueuedConnection,
                              Q_ARG(ModelSpec, spec), Q_ARG(QString, path),
                              Q_ARG(QStringList, classNames));
    return true;
}

void AutoAnnotator::onConfigured(bool ok, const QString &error)
{
    m_awaitingConfigure = false;

    if (!ok) {
        // Do not remember a model that failed to load, or the next run would
        // skip configuring and detect with nothing loaded.
        m_activeModelId.clear();
        stop(error);
        return;
    }

    if (m_cancelled) {
        stop(tr("Cancelled."));
        return;
    }

    pumpNextFrame();
}

void AutoAnnotator::pumpNextFrame()
{
    if (m_cancelled) {
        stop(tr("Cancelled."));
        return;
    }
    if (m_cursor >= m_queue.size()) {
        stop(QString());
        return;
    }

    const int frameIndex = m_queue.at(m_cursor);
    const QImage frame = m_project->frameAt(frameIndex);
    if (frame.isNull()) {
        // A frame that will not decode is not a reason to abandon the run; skip
        // it and record nothing for it.
        ++m_cursor;
        emit progress(m_cursor, m_queue.size(), m_detectionCount);
        pumpNextFrame();
        return;
    }

    const cv::Mat bgr = qImageToBgrMat(frame);
    QMetaObject::invokeMethod(m_worker, "detect", Qt::QueuedConnection,
                              Q_ARG(int, frameIndex), Q_ARG(cv::Mat, bgr),
                              Q_ARG(YoloDetector::Options, m_options));
}

void AutoAnnotator::onDetected(int frameIndex, const QVector<Detection> &detections,
                               const QString &error)
{
    if (!m_running)
        return;

    if (!error.isEmpty()) {
        stop(error);
        return;
    }

    QVector<Prediction> predictions;
    predictions.reserve(detections.size());
    for (const Detection &detection : detections) {
        Prediction prediction;
        prediction.box = detection.box;
        prediction.score = detection.score;
        prediction.modelClassIndex = detection.classIndex;
        prediction.modelClassName = detection.className;
        prediction.modelId = m_activeModelId;

        if (detection.hasPolygon()) {
            prediction.type = ShapeType::Polygon;
            prediction.points = detection.polygon;
        } else {
            prediction.type = ShapeType::Rect;
            prediction.points = {detection.box.topLeft(), detection.box.bottomRight()};
        }

        predictions.append(prediction);
    }

    m_store->setFramePredictions(frameIndex, predictions);
    m_detectionCount += predictions.size();

    ++m_cursor;
    emit progress(m_cursor, m_queue.size(), m_detectionCount);

    pumpNextFrame();
}

void AutoAnnotator::cancel()
{
    if (!m_running)
        return;

    m_cancelled = true;
    // The in-flight frame still comes back; pumpNextFrame() sees the flag and
    // stops there rather than trying to interrupt an inference mid-call.
}

void AutoAnnotator::stop(const QString &error)
{
    if (!m_running)
        return;

    m_running = false;
    m_awaitingConfigure = false;
    const int processed = m_cursor;
    const int detections = m_detectionCount;
    m_queue.clear();
    m_cursor = 0;

    emit finished(processed, detections, error);
}
