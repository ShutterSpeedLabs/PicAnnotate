#include "clipcontroller.h"

#include "../core/annotationcontroller.h"
#include "../core/project.h"
#include "../trackers/cvimageconvert.h"

#include <QCryptographicHash>
#include <QThread>

void ClipWorker::configure(const QVector<ClipClassifier::Candidate> &candidates)
{
    QString error;
    if (!m_classifier.isReady() && !m_classifier.load(&error)) {
        emit configured(false, error);
        return;
    }
    if (!m_classifier.setCandidates(candidates, &error)) {
        emit configured(false, error);
        return;
    }
    emit configured(true, QString());
}

void ClipWorker::classify(quint64 requestId, const cv::Mat &image, const QVector<QRectF> &boxes,
                          const QVector<int> &shapeIndices)
{
    QVector<ClipAssignment> assignments;
    QString error;

    for (int i = 0; i < boxes.size(); ++i) {
        const QVector<ClipClassifier::Score> scores =
            m_classifier.classifyRegion(image, boxes.at(i), &error);
        if (scores.isEmpty())
            continue;

        ClipAssignment assignment;
        assignment.shapeIndex = shapeIndices.value(i, -1);
        assignment.labelId = scores.first().labelId;
        assignment.className = scores.first().className;
        assignment.probability = scores.first().probability;
        assignments.append(assignment);
    }

    // A per-region failure is not fatal for the batch; only report an error when
    // nothing at all came back.
    emit classified(requestId, assignments, assignments.isEmpty() ? error : QString());
}

void ClipWorker::release()
{
    m_classifier.unload();
}

ClipController::ClipController(Project *project, AnnotationController *annotations,
                               QObject *parent)
    : QObject(parent)
    , m_project(project)
    , m_annotations(annotations)
{
    qRegisterMetaType<cv::Mat>("cv::Mat");
    qRegisterMetaType<ClipAssignment>("ClipAssignment");
    qRegisterMetaType<QVector<ClipAssignment>>("QVector<ClipAssignment>");
    qRegisterMetaType<ClipClassifier::Candidate>("ClipClassifier::Candidate");
    qRegisterMetaType<QVector<ClipClassifier::Candidate>>("QVector<ClipClassifier::Candidate>");
    qRegisterMetaType<QVector<QRectF>>("QVector<QRectF>");
    qRegisterMetaType<QVector<int>>("QVector<int>");
}

ClipController::~ClipController()
{
    if (m_thread) {
        m_thread->quit();
        if (!m_thread->wait(10000))
            m_thread->terminate();
    }
}

void ClipController::ensureWorker()
{
    if (m_worker)
        return;

    m_thread = new QThread(this);
    m_worker = new ClipWorker;
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &ClipWorker::configured, this, &ClipController::onConfigured);
    connect(m_worker, &ClipWorker::classified, this, &ClipController::onClassified);

    m_thread->start();
}

QVector<ClipClassifier::Candidate> ClipController::buildCandidates() const
{
    QVector<ClipClassifier::Candidate> candidates;
    for (const LabelClass &label : m_project->labelSchema().classes()) {
        if (label.name.trimmed().isEmpty())
            continue;
        candidates.append({label.id, label.name});
    }
    return candidates;
}

QString ClipController::candidateFingerprint() const
{
    // Ids and names both matter: renaming a class changes its embedding, and
    // removing one changes which ids the results can refer to.
    QCryptographicHash hash(QCryptographicHash::Md5);
    for (const LabelClass &label : m_project->labelSchema().classes()) {
        hash.addData(QByteArray::number(label.id));
        hash.addData(label.name.toUtf8());
    }
    return QString::fromLatin1(hash.result().toHex());
}

void ClipController::invalidateCandidates()
{
    m_configuredFingerprint.clear();
}

void ClipController::setMinimumConfidence(double confidence)
{
    m_minimumConfidence = qBound(0.0, confidence, 1.0);
}

bool ClipController::classifyFrame(int frameIndex, bool onlyUnlabelled, QString *error)
{
    const QVector<AnnoShape> shapes = m_project->resolvedShapes(frameIndex);

    QList<int> indices;
    for (int i = 0; i < shapes.size(); ++i) {
        if (onlyUnlabelled && shapes.at(i).labelId() >= 0)
            continue;
        indices.append(i);
    }

    if (indices.isEmpty()) {
        if (error) {
            *error = onlyUnlabelled
                         ? tr("Every shape on this frame already has a class.")
                         : tr("There are no shapes on this frame to classify.");
        }
        return false;
    }

    return classifyShapes(frameIndex, indices, error);
}

bool ClipController::classifyShapes(int frameIndex, const QList<int> &shapeIndices, QString *error)
{
    if (isBusy()) {
        if (error)
            *error = tr("A classification run is already in progress.");
        return false;
    }
    if (shapeIndices.isEmpty()) {
        if (error)
            *error = tr("No shapes were selected.");
        return false;
    }
    if (buildCandidates().isEmpty()) {
        if (error) {
            *error = tr("There are no classes to choose between. Add some in the Labels "
                        "panel first — zero-shot classification scores a region against "
                        "your own class names.");
        }
        return false;
    }

    ensureWorker();

    m_pendingFrame = frameIndex;
    m_pendingShapes = shapeIndices;
    m_hasPending = true;

    const QString fingerprint = candidateFingerprint();
    if (fingerprint == m_configuredFingerprint)
        return dispatch(error);

    m_configuring = true;
    m_configuredFingerprint = fingerprint;
    emit statusChanged(tr("Embedding class names..."));
    QMetaObject::invokeMethod(m_worker, "configure", Qt::QueuedConnection,
                              Q_ARG(QVector<ClipClassifier::Candidate>, buildCandidates()));
    return true;
}

void ClipController::onConfigured(bool ok, const QString &error)
{
    m_configuring = false;

    if (!ok) {
        // Do not remember a fingerprint that failed, or the next run would skip
        // configuring and classify against nothing.
        m_configuredFingerprint.clear();
        m_hasPending = false;
        m_pendingShapes.clear();
        emit statusChanged(QString());
        emit failed(error);
        return;
    }

    QString dispatchError;
    if (!dispatch(&dispatchError) && !dispatchError.isEmpty())
        emit failed(dispatchError);
}

bool ClipController::dispatch(QString *error)
{
    if (!m_hasPending)
        return false;

    const QImage frame = m_project->frameAt(m_pendingFrame);
    if (frame.isNull()) {
        m_hasPending = false;
        if (error)
            *error = tr("This frame could not be decoded.");
        return false;
    }

    const QVector<AnnoShape> shapes = m_project->resolvedShapes(m_pendingFrame);

    QVector<QRectF> boxes;
    QVector<int> indices;
    for (int index : m_pendingShapes) {
        if (index < 0 || index >= shapes.size())
            continue;
        const QRectF box = shapes.at(index).boundingRect();
        if (box.width() < 2.0 || box.height() < 2.0)
            continue;
        boxes.append(box);
        indices.append(index);
    }

    if (boxes.isEmpty()) {
        m_hasPending = false;
        if (error)
            *error = tr("None of the selected shapes are big enough to classify.");
        return false;
    }

    m_inFlight = true;
    m_hasPending = false;
    emit statusChanged(tr("Classifying %1 region(s)...").arg(boxes.size()));

    QMetaObject::invokeMethod(m_worker, "classify", Qt::QueuedConnection,
                              Q_ARG(quint64, m_nextRequestId++),
                              Q_ARG(cv::Mat, qImageToBgrMat(frame)),
                              Q_ARG(QVector<QRectF>, boxes), Q_ARG(QVector<int>, indices));
    return true;
}

void ClipController::onClassified(quint64 requestId, const QVector<ClipAssignment> &assignments,
                                  const QString &error)
{
    Q_UNUSED(requestId);
    m_inFlight = false;
    emit statusChanged(QString());

    if (assignments.isEmpty()) {
        emit finished(0, 0, error);
        if (!error.isEmpty())
            emit failed(error);
        return;
    }

    const int frameIndex = m_pendingFrame;
    const QVector<AnnoShape> shapes = m_project->resolvedShapes(frameIndex);

    int applied = 0;
    int skipped = 0;

    m_annotations->beginCompound(
        tr("Classify %1 shape(s) with CLIP").arg(assignments.size()));

    for (const ClipAssignment &assignment : assignments) {
        if (assignment.probability < m_minimumConfidence || assignment.labelId < 0) {
            ++skipped;
            continue;
        }
        if (assignment.shapeIndex < 0 || assignment.shapeIndex >= shapes.size()) {
            ++skipped;
            continue;
        }

        AnnoShape shape = shapes.at(assignment.shapeIndex);
        if (shape.labelId() == assignment.labelId) {
            // Already right; writing it again would add a no-op undo entry.
            ++skipped;
            continue;
        }

        shape.setLabelId(assignment.labelId);
        shape.setAttribute(QStringLiteral("clipConfidence"),
                           static_cast<double>(assignment.probability));

        m_annotations->editShape(frameIndex, assignment.shapeIndex, shape,
                                 tr("Classify as %1").arg(assignment.className));
        ++applied;
    }

    m_annotations->endCompound();

    emit finished(applied, skipped, QString());
}
