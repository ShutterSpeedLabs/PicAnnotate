#include "labelclass.h"

#include <QJsonObject>

QJsonObject LabelClass::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["name"] = name;
    obj["color"] = color.name();
    obj["allowedTypes"] = static_cast<int>(allowedTypes);
    if (!keypointTemplateId.isEmpty())
        obj["keypointTemplateId"] = keypointTemplateId;
    return obj;
}

LabelClass LabelClass::fromJson(const QJsonObject &obj)
{
    LabelClass label;
    label.id = obj["id"].toInt(-1);
    label.name = obj["name"].toString();
    label.color = QColor(obj["color"].toString());
    // Projects saved before per-class type restrictions existed allowed every
    // geometry, so that is the right default when the field is absent.
    label.allowedTypes = ShapeTypeFlags(obj["allowedTypes"].toInt(AllShapeTypes));
    label.keypointTemplateId = obj["keypointTemplateId"].toString();
    return label;
}
