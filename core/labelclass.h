#ifndef LABELCLASS_H
#define LABELCLASS_H

#include "annoshape.h"

#include <QColor>
#include <QString>

class QJsonObject;

struct LabelClass
{
    int id = -1;
    QString name;
    QColor color;

    // Which geometries this class may be drawn with. Mirrors how Label Studio
    // binds a label set to one control tag; AllShapeTypes keeps the pre-existing
    // "any label on any shape" behaviour for older projects.
    ShapeTypeFlags allowedTypes = AllShapeTypes;

    // Skeleton layout used when this class is drawn as a Skeleton. Empty means
    // "fall back to the built-in COCO-17 template".
    QString keypointTemplateId;

    bool allows(ShapeType type) const { return allowedTypes.testFlag(shapeTypeToFlag(type)); }

    QJsonObject toJson() const;
    static LabelClass fromJson(const QJsonObject &obj);
};

#endif // LABELCLASS_H
