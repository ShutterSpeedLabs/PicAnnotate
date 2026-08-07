#ifndef TRACKCONTROLLER_H
#define TRACKCONTROLLER_H

#include "objecttracker.h"

#include <QObject>
#include <QString>

class AnnotationController;
class Project;
class QTimer;

class TrackController : public QObject
{
    Q_OBJECT
public:
    TrackController(Project *project, AnnotationController *annotations, QObject *parent = nullptr);

    bool setModelPaths(const QString &backboneModelPath, const QString &neckheadModelPath, QString *error);

    // Follows the shape at `shapeIndex` on the current frame. A plain frame shape
    // is promoted to a track first, so the run produces keyframes on one track
    // rather than an unrelated shape per frame.
    void startTracking(int shapeIndex);
    void stop();
    bool isTracking() const;

    int activeTrackId() const { return m_trackId; }

signals:
    void frameAdvanced(int frameIndex);
    void statusChanged(const QString &text);

    // Let the rest of the UI lock out anything that would also move the playhead
    // while a run owns it.
    void trackingStarted(int trackId);
    void trackingStopped();

private slots:
    void onTimerTick();

private:
    void stepForward();

    Project *m_project;
    AnnotationController *m_annotations;
    ObjectTracker m_tracker;
    QTimer *m_timer;

    int m_trackId = -1;
    int m_labelId = -1;

    // True while the run's keyframes are being collected into one undo step.
    // Every exit path goes through stop(), which closes it.
    bool m_compoundOpen = false;

    static constexpr float kAutoStopScoreThreshold = 0.5f;
};

#endif // TRACKCONTROLLER_H
