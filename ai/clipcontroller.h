#ifndef CLIPCONTROLLER_H
#define CLIPCONTROLLER_H

#include "clipclassifier.h"

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QVector>

#include <opencv2/core.hpp>

class AnnotationController;
class Project;
class QThread;

// One shape's classification result, on its way back from the worker.
struct ClipAssignment
{
    int shapeIndex = -1;
    int labelId = -1;
    QString className;
    float probability = 0.0f;
};

Q_DECLARE_METATYPE(ClipAssignment)
Q_DECLARE_METATYPE(QVector<ClipAssignment>)
Q_DECLARE_METATYPE(ClipClassifier::Candidate)
Q_DECLARE_METATYPE(QVector<ClipClassifier::Candidate>)
Q_DECLARE_METATYPE(QVector<QRectF>)

class ClipWorker : public QObject
{
    Q_OBJECT
public slots:
    void configure(const QVector<ClipClassifier::Candidate> &candidates);
    void classify(quint64 requestId, const cv::Mat &image, const QVector<QRectF> &boxes,
                  const QVector<int> &shapeIndices);
    void release();

signals:
    void configured(bool ok, const QString &error);
    void classified(quint64 requestId, const QVector<ClipAssignment> &assignments,
                    const QString &error);

private:
    ClipClassifier m_classifier;
};

// Applies CLIP zero-shot classification to existing shapes.
//
// The candidates are the project's own classes, so this answers "which of *my*
// labels does this region look like" rather than handing back an ImageNet
// category nobody asked for. Text embeddings depend only on the class names, so
// they are recomputed when the schema changes and reused otherwise.
class ClipController : public QObject
{
    Q_OBJECT
public:
    ClipController(Project *project, AnnotationController *annotations,
                   QObject *parent = nullptr);
    ~ClipController() override;

    bool isBusy() const { return m_configuring || m_inFlight; }

    // Classifies the given shapes on a frame and writes the winning class onto
    // each, as one undo step. `shapeIndices` indexes the resolved view, the same
    // way the annotation list does.
    bool classifyShapes(int frameIndex, const QList<int> &shapeIndices, QString *error);

    // Every shape on the frame, or only the ones with no class yet.
    bool classifyFrame(int frameIndex, bool onlyUnlabelled, QString *error);

    // A result below this probability is left alone rather than guessed at.
    double minimumConfidence() const { return m_minimumConfidence; }
    void setMinimumConfidence(double confidence);

    // Forces the class embeddings to be rebuilt on the next run. Called when the
    // label schema changes.
    void invalidateCandidates();

signals:
    void statusChanged(const QString &text);
    void finished(int classified, int skipped, const QString &error);
    void failed(const QString &message);

private slots:
    void onConfigured(bool ok, const QString &error);
    void onClassified(quint64 requestId, const QVector<ClipAssignment> &assignments,
                      const QString &error);

private:
    void ensureWorker();

    // Candidate list built from the current schema, and a fingerprint of it so a
    // rename or a new class triggers a re-embed but a no-op does not.
    QVector<ClipClassifier::Candidate> buildCandidates() const;
    QString candidateFingerprint() const;

    bool dispatch(QString *error);

    Project *m_project;
    AnnotationController *m_annotations;

    QThread *m_thread = nullptr;
    ClipWorker *m_worker = nullptr;

    bool m_configuring = false;
    bool m_inFlight = false;
    QString m_configuredFingerprint;

    // The run waiting for the class embeddings to finish.
    int m_pendingFrame = -1;
    QList<int> m_pendingShapes;
    bool m_hasPending = false;

    quint64 m_nextRequestId = 1;
    double m_minimumConfidence = 0.25;
};

#endif // CLIPCONTROLLER_H
