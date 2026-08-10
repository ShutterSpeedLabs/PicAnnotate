#include "predictionstore.h"

AnnoShape Prediction::toShape(int resolvedLabelId) const
{
    AnnoShape shape = hasPolygon() ? AnnoShape::makePolygon(points, resolvedLabelId)
                                   : AnnoShape::makeRect(box, resolvedLabelId);

    shape.setSource(ShapeSource::Predicted);
    shape.setAttribute(QLatin1String(kConfidenceAttribute), static_cast<double>(score));
    if (!modelId.isEmpty())
        shape.setAttribute(QLatin1String(kModelAttribute), modelId);

    // Keep what the model called it even after the shape is bound to a project
    // class. The two can legitimately differ once someone renames a class, and
    // the original is the only record of what the model actually said.
    if (!modelClassName.isEmpty())
        shape.setAttribute(QStringLiteral("modelClass"), modelClassName);

    return shape;
}

PredictionStore::PredictionStore(QObject *parent)
    : QObject(parent)
{
}

void PredictionStore::setFramePredictions(int frame, const QVector<Prediction> &predictions)
{
    if (predictions.isEmpty()) {
        clearFrame(frame);
        return;
    }

    QVector<Prediction> stored = predictions;
    for (Prediction &prediction : stored) {
        if (prediction.id.isEmpty()) {
            prediction.id = QStringLiteral("p%1_%2").arg(frame).arg(m_nextId++);
        }
    }

    m_byFrame.insert(frame, stored);
    emit framePredictionsChanged(frame);
}

QVector<Prediction> PredictionStore::allPredictions(int frame) const
{
    return m_byFrame.value(frame);
}

QVector<Prediction> PredictionStore::visiblePredictions(int frame) const
{
    QVector<Prediction> result;
    for (const Prediction &prediction : m_byFrame.value(frame)) {
        if (prediction.score >= m_scoreThreshold)
            result.append(prediction);
    }
    return result;
}

const Prediction *PredictionStore::prediction(int frame, const QString &id) const
{
    const auto it = m_byFrame.constFind(frame);
    if (it == m_byFrame.constEnd())
        return nullptr;

    for (const Prediction &prediction : it.value()) {
        if (prediction.id == id)
            return &prediction;
    }
    return nullptr;
}

bool PredictionStore::remove(int frame, const QString &id)
{
    const auto it = m_byFrame.find(frame);
    if (it == m_byFrame.end())
        return false;

    QVector<Prediction> &predictions = it.value();
    for (int i = 0; i < predictions.size(); ++i) {
        if (predictions.at(i).id != id)
            continue;

        predictions.remove(i);
        if (predictions.isEmpty())
            m_byFrame.erase(it);
        emit framePredictionsChanged(frame);
        return true;
    }
    return false;
}

void PredictionStore::clearFrame(int frame)
{
    if (m_byFrame.remove(frame) > 0)
        emit framePredictionsChanged(frame);
}

void PredictionStore::clearAll()
{
    if (m_byFrame.isEmpty())
        return;

    m_byFrame.clear();
    emit storeCleared();
}

int PredictionStore::totalCount() const
{
    int count = 0;
    for (const QVector<Prediction> &predictions : m_byFrame)
        count += predictions.size();
    return count;
}

int PredictionStore::visibleCount(int frame) const
{
    int count = 0;
    for (const Prediction &prediction : m_byFrame.value(frame)) {
        if (prediction.score >= m_scoreThreshold)
            ++count;
    }
    return count;
}

int PredictionStore::totalVisibleCount() const
{
    int count = 0;
    for (auto it = m_byFrame.constBegin(); it != m_byFrame.constEnd(); ++it) {
        for (const Prediction &prediction : it.value()) {
            if (prediction.score >= m_scoreThreshold)
                ++count;
        }
    }
    return count;
}

void PredictionStore::setScoreThreshold(double threshold)
{
    const double clamped = qBound(0.0, threshold, 1.0);
    if (qFuzzyCompare(clamped + 1.0, m_scoreThreshold + 1.0))
        return;

    m_scoreThreshold = clamped;
    emit scoreThresholdChanged(clamped);
}
