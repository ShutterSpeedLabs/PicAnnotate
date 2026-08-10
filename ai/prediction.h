#ifndef PREDICTION_H
#define PREDICTION_H

#include "../core/annoshape.h"

#include <QString>

// One model output waiting for a human decision.
//
// Deliberately not an AnnoShape. A prediction is a *proposal*: it carries a
// confidence, it knows which model made it and what that model called the
// object, and it has not yet been mapped onto the project's label schema.
// Writing model output straight into annotations loses all of that, and with it
// any way to review a batch run — which is the whole point of running one.
struct Prediction
{
    QString id;              // stable within a session, for accept/reject by id

    ShapeType type = ShapeType::Rect;
    QVector<QPointF> points; // rect corners, or the polygon ring
    QRectF box;              // always set, even for a polygon: the panel lists by box

    float score = 0.0f;

    // What the *model* called it, and the index in the model's own label set.
    QString modelClassName;
    int modelClassIndex = -1;

    // Resolved project class, or -1 when the model's class has no counterpart in
    // the schema yet. Filled in on accept.
    int labelId = -1;

    QString modelId;         // which model produced this

    bool hasPolygon() const { return type == ShapeType::Polygon && points.size() >= 3; }

    // The shape this becomes when accepted, tagged with its provenance so the
    // dataset can still be audited for how much a human actually drew.
    AnnoShape toShape(int resolvedLabelId) const;
};

#endif // PREDICTION_H
