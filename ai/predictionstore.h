#ifndef PREDICTIONSTORE_H
#define PREDICTIONSTORE_H

#include "prediction.h"

#include <QList>
#include <QMap>
#include <QObject>
#include <QVector>

// Model output for the whole project, held apart from the annotations.
//
// The separation is the point: predictions are drawn differently, filtered by a
// confidence threshold, and thrown away wholesale without touching the undo
// stack. Once accepted they leave here and become annotations, which is the only
// path by which a model can affect the saved project.
//
// Not persisted. A prediction that survives a restart would be indistinguishable
// from work, and the model can always be re-run.
class PredictionStore : public QObject
{
    Q_OBJECT
public:
    explicit PredictionStore(QObject *parent = nullptr);

    // Replaces whatever this frame had. Assigns ids to entries that arrive
    // without one.
    void setFramePredictions(int frame, const QVector<Prediction> &predictions);

    // Everything on a frame, ignoring the threshold. Accept-all uses this only
    // via visiblePredictions() — accepting something the user cannot see would
    // be a nasty surprise.
    QVector<Prediction> allPredictions(int frame) const;

    // Everything on a frame scoring at or above the current threshold.
    QVector<Prediction> visiblePredictions(int frame) const;

    // Nothing when no prediction on that frame has the id.
    const Prediction *prediction(int frame, const QString &id) const;

    bool remove(int frame, const QString &id);
    void clearFrame(int frame);
    void clearAll();

    bool isEmpty() const { return m_byFrame.isEmpty(); }
    int totalCount() const;
    int visibleCount(int frame) const;
    int totalVisibleCount() const;
    QList<int> frames() const { return m_byFrame.keys(); }

    // Predictions below this score are hidden and cannot be accepted. Applies
    // across the whole project, so raising it after a batch run prunes
    // everything at once.
    double scoreThreshold() const { return m_scoreThreshold; }
    void setScoreThreshold(double threshold);

signals:
    void framePredictionsChanged(int frame);
    void scoreThresholdChanged(double threshold);

    // Everything went away at once — cheaper for listeners than one signal per
    // frame after a batch run over a long video.
    void storeCleared();

private:
    QMap<int, QVector<Prediction>> m_byFrame;
    double m_scoreThreshold = 0.25;
    int m_nextId = 0;
};

#endif // PREDICTIONSTORE_H
