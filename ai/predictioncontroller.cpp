#include "predictioncontroller.h"

#include "predictionstore.h"

#include "../core/annotationcontroller.h"
#include "../core/project.h"

PredictionController::PredictionController(Project *project, AnnotationController *annotations,
                                           PredictionStore *store, QObject *parent)
    : QObject(parent)
    , m_project(project)
    , m_annotations(annotations)
    , m_store(store)
{
}

int PredictionController::resolveLabelId(const QString &modelClassName)
{
    if (modelClassName.isEmpty())
        return -1;

    LabelSchema &schema = m_project->labelSchema();

    if (const LabelClass *existing = schema.findClassByName(modelClassName))
        return existing->id;

    if (!m_options.createMissingClasses)
        return -1;

    // ensureClass() is what every dataset importer uses, so a class created for
    // a detection is indistinguishable from one that arrived with a COCO file.
    const int id = schema.ensureClass(modelClassName);
    m_annotations->notifyLabelSchemaChanged();
    return id;
}

bool PredictionController::acceptOne(int frame, const QString &id)
{
    const Prediction *found = m_store->prediction(frame, id);
    if (!found)
        return false;

    // Copy before mutating the store: accepting removes the entry, which
    // invalidates the pointer.
    const Prediction prediction = *found;
    if (prediction.score < m_store->scoreThreshold())
        return false;

    const int labelId = resolveLabelId(prediction.modelClassName);
    if (labelId < 0)
        return false;

    Prediction toAccept = prediction;
    if (!m_options.preferPolygons)
        toAccept.type = ShapeType::Rect;

    m_annotations->addShape(frame, toAccept.toShape(labelId));
    m_store->remove(frame, id);

    emit accepted(1, 0);
    return true;
}

int PredictionController::acceptFrameInternal(int frame)
{
    const QVector<Prediction> visible = m_store->visiblePredictions(frame);
    if (visible.isEmpty())
        return 0;

    int taken = 0;
    for (const Prediction &prediction : visible) {
        const int labelId = resolveLabelId(prediction.modelClassName);
        if (labelId < 0)
            continue;

        Prediction toAccept = prediction;
        if (!m_options.preferPolygons)
            toAccept.type = ShapeType::Rect;

        m_annotations->addShape(frame, toAccept.toShape(labelId));
        m_store->remove(frame, prediction.id);
        ++taken;
    }

    return taken;
}

int PredictionController::acceptFrame(int frame)
{
    const int before = m_store->visibleCount(frame);
    if (before == 0)
        return 0;

    m_annotations->beginCompound(
        QStringLiteral("Accept %1 prediction(s) on frame %2").arg(before).arg(frame + 1));
    const int taken = acceptFrameInternal(frame);
    m_annotations->endCompound();

    emit accepted(taken, before - taken);
    return taken;
}

int PredictionController::acceptAll()
{
    const int before = m_store->totalVisibleCount();
    if (before == 0)
        return 0;

    // frames() is snapshotted first: accepting removes entries, which would
    // otherwise mutate the map being iterated.
    const QList<int> frames = m_store->frames();

    m_annotations->beginCompound(QStringLiteral("Accept %1 prediction(s)").arg(before));
    int taken = 0;
    for (int frame : frames)
        taken += acceptFrameInternal(frame);
    m_annotations->endCompound();

    emit accepted(taken, before - taken);
    return taken;
}

void PredictionController::rejectOne(int frame, const QString &id)
{
    m_store->remove(frame, id);
}

void PredictionController::rejectFrame(int frame)
{
    m_store->clearFrame(frame);
}

void PredictionController::rejectAll()
{
    m_store->clearAll();
}
