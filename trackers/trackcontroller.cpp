#include "trackcontroller.h"
#include "cvimageconvert.h"

#include "../core/annotationcontroller.h"
#include "../core/annoshape.h"
#include "../core/frameannotations.h"
#include "../core/project.h"

#include <QTimer>

namespace {
constexpr int kTrackIntervalMs = 50;
}

TrackController::TrackController(Project *project, AnnotationController *annotations, QObject *parent)
    : QObject(parent)
    , m_project(project)
    , m_annotations(annotations)
    , m_timer(new QTimer(this))
{
    connect(m_timer, &QTimer::timeout, this, &TrackController::onTimerTick);
}

bool TrackController::setModelPaths(const QString &backboneModelPath, const QString &neckheadModelPath, QString *error)
{
    return m_tracker.ensureInitialized(backboneModelPath, neckheadModelPath, error);
}

bool TrackController::isTracking() const
{
    return m_timer->isActive();
}

void TrackController::startTracking(int shapeIndex)
{
    if (!m_project->hasSource())
        return;

    // Starting a second run without stopping the first would nest undo macros
    // and leave the old one open forever.
    if (isTracking())
        stop();

    const int frameIndex = m_project->currentIndex();
    const QVector<AnnoShape> shapes = m_project->resolvedShapes(frameIndex);
    if (shapeIndex < 0 || shapeIndex >= shapes.size())
        return;

    const AnnoShape shape = shapes.at(shapeIndex);
    if (shape.type() != ShapeType::Rect) {
        emit statusChanged(QStringLiteral("Only boxes can be tracked."));
        return;
    }

    const QImage frame = m_project->currentFrame();
    if (frame.isNull()) {
        emit statusChanged(QStringLiteral("Stopped: current frame could not be decoded"));
        return;
    }

    // Initialise the tracker before touching the project, so a failure leaves no
    // half-created track behind.
    QString error;
    if (!m_tracker.start(qImageToBgrMat(frame), shape.rect().toRect(), &error)) {
        emit statusChanged(error);
        return;
    }

    // Everything the run produces lands on one track, so a shape drawn as a
    // plain box becomes a track before the first step.
    m_trackId = m_annotations->promoteToTrack(frameIndex, shapeIndex);
    if (m_trackId < 0) {
        emit statusChanged(QStringLiteral("Could not start a track for that shape."));
        return;
    }
    m_labelId = shape.labelId();

    // The run's per-frame keyframes collapse into one undo step; without this a
    // 500-frame run would need 500 presses of Ctrl+Z to take back.
    m_annotations->beginCompound(QStringLiteral("Track %1").arg(m_trackId));
    m_compoundOpen = true;

    m_timer->start(kTrackIntervalMs);
    emit trackingStarted(m_trackId);
    emit statusChanged(QStringLiteral("Tracking on track %1...").arg(m_trackId));
}

void TrackController::stop()
{
    const bool wasRunning = m_timer->isActive() || m_compoundOpen;
    m_timer->stop();

    if (m_compoundOpen) {
        m_annotations->endCompound();
        m_compoundOpen = false;
    }

    // Only announce a stop for a run that was actually going, so pressing Stop
    // while idle does not churn the UI state.
    if (wasRunning)
        emit trackingStopped();

    emit statusChanged("Stopped");
}

void TrackController::onTimerTick()
{
    stepForward();
}

void TrackController::stepForward()
{
    if (!m_project->next()) {
        stop();
        emit statusChanged("Stopped: end of frames");
        return;
    }

    const QImage frame = m_project->currentFrame();
    if (frame.isNull()) {
        stop();
        emit frameAdvanced(m_project->currentIndex());
        emit statusChanged(QStringLiteral("Stopped: frame could not be decoded"));
        return;
    }

    const auto result = m_tracker.update(qImageToBgrMat(frame));

    if (!result) {
        stop();
        emit frameAdvanced(m_project->currentIndex());
        emit statusChanged("Stopped: lost track");
        return;
    }

    AnnoShape shape = AnnoShape::makeRect(result->box, m_labelId);
    shape.setSource(ShapeSource::Tracked);
    m_annotations->setTrackKeyframe(m_trackId, m_project->currentIndex(), shape,
                                    QStringLiteral("Track step"));

    emit frameAdvanced(m_project->currentIndex());

    if (result->score < kAutoStopScoreThreshold) {
        stop();
        emit statusChanged(QString("Stopped: low confidence (%1)").arg(result->score, 0, 'f', 2));
    }
}
