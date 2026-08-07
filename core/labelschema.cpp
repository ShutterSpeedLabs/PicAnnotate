#include "labelschema.h"

#include <QJsonArray>
#include <QJsonObject>

// Golden-angle hue stepping keeps consecutive classes far apart on the wheel,
// which matters more than hitting exact hues when one frame holds many labels.
// Value stays high and saturation moderate so every colour reads against the
// near-black canvas. Kept here rather than in ui/theme so core stays standalone.
QColor LabelSchema::suggestedColorForIndex(int index)
{
    const int hue = (index * 137 + 12) % 360;
    const int saturation = (index % 2 == 0) ? 165 : 200;
    const int value = (index % 3 == 0) ? 245 : 225;
    return QColor::fromHsv(hue, saturation, value);
}

int LabelSchema::addClass(const QString &name, const QColor &color)
{
    return addClass(name, color, AllShapeTypes);
}

int LabelSchema::addClass(const QString &name, const QColor &color, ShapeTypeFlags allowedTypes,
                          const QString &keypointTemplateId)
{
    LabelClass label;
    label.id = m_nextId++;
    label.name = name;
    label.color = color;
    label.allowedTypes = allowedTypes;
    label.keypointTemplateId = keypointTemplateId;
    m_classes.append(label);
    return label.id;
}

bool LabelSchema::removeClass(int id)
{
    for (int i = 0; i < m_classes.size(); ++i) {
        if (m_classes.at(i).id == id) {
            m_classes.removeAt(i);
            return true;
        }
    }
    return false;
}

bool LabelSchema::insertClassAt(int index, const LabelClass &label)
{
    if (label.id < 0 || findClass(label.id))
        return false;

    m_classes.insert(qBound(0, index, m_classes.size()), label);
    m_nextId = qMax(m_nextId, label.id + 1);
    return true;
}

bool LabelSchema::renameClass(int id, const QString &name)
{
    for (LabelClass &label : m_classes) {
        if (label.id == id) {
            label.name = name;
            return true;
        }
    }
    return false;
}

bool LabelSchema::setClassColor(int id, const QColor &color)
{
    for (LabelClass &label : m_classes) {
        if (label.id == id) {
            label.color = color;
            return true;
        }
    }
    return false;
}

bool LabelSchema::setClassAllowedTypes(int id, ShapeTypeFlags allowedTypes)
{
    for (LabelClass &label : m_classes) {
        if (label.id == id) {
            label.allowedTypes = allowedTypes;
            return true;
        }
    }
    return false;
}

bool LabelSchema::setClassKeypointTemplate(int id, const QString &templateId)
{
    for (LabelClass &label : m_classes) {
        if (label.id == id) {
            label.keypointTemplateId = templateId;
            return true;
        }
    }
    return false;
}

const LabelClass *LabelSchema::findClass(int id) const
{
    for (const LabelClass &label : m_classes) {
        if (label.id == id)
            return &label;
    }
    return nullptr;
}

const LabelClass *LabelSchema::findClassByName(const QString &name) const
{
    for (const LabelClass &label : m_classes) {
        if (label.name.compare(name, Qt::CaseInsensitive) == 0)
            return &label;
    }
    return nullptr;
}

int LabelSchema::ensureClass(const QString &name, ShapeTypeFlags allowedTypes,
                            const QString &keypointTemplateId)
{
    for (LabelClass &label : m_classes) {
        if (label.name.compare(name, Qt::CaseInsensitive) != 0)
            continue;

        // An existing class widens to accept the imported geometry rather than
        // rejecting it, and adopts a keypoint template if it had none.
        label.allowedTypes |= allowedTypes;
        if (label.keypointTemplateId.isEmpty() && !keypointTemplateId.isEmpty())
            label.keypointTemplateId = keypointTemplateId;
        return label.id;
    }

    return addClass(name, suggestedColorForIndex(m_classes.size()),
                    allowedTypes, keypointTemplateId);
}

QVector<int> LabelSchema::orderedClassIds() const
{
    QVector<int> ids;
    ids.reserve(m_classes.size());
    for (const LabelClass &label : m_classes)
        ids.append(label.id);
    return ids;
}

int LabelSchema::classIndex(int id) const
{
    for (int i = 0; i < m_classes.size(); ++i) {
        if (m_classes.at(i).id == id)
            return i;
    }
    return -1;
}

void LabelSchema::addKeypointTemplate(const KeypointTemplate &tmpl)
{
    if (!tmpl.isValid())
        return;

    for (KeypointTemplate &existing : m_keypointTemplates) {
        if (existing.id == tmpl.id) {
            existing = tmpl;
            return;
        }
    }
    m_keypointTemplates.append(tmpl);
}

const KeypointTemplate *LabelSchema::findKeypointTemplate(const QString &id) const
{
    for (const KeypointTemplate &tmpl : m_keypointTemplates) {
        if (tmpl.id == id)
            return &tmpl;
    }
    return nullptr;
}

KeypointTemplate LabelSchema::templateForClass(int labelId) const
{
    const LabelClass *label = findClass(labelId);
    if (label && !label->keypointTemplateId.isEmpty()) {
        if (const KeypointTemplate *tmpl = findKeypointTemplate(label->keypointTemplateId))
            return *tmpl;
    }
    return KeypointTemplate::coco17();
}

QJsonArray LabelSchema::toJson() const
{
    QJsonArray array;
    for (const LabelClass &label : m_classes)
        array.append(label.toJson());
    return array;
}

LabelSchema LabelSchema::fromJson(const QJsonArray &array)
{
    LabelSchema schema;
    int maxId = -1;
    for (const QJsonValue &v : array) {
        LabelClass label = LabelClass::fromJson(v.toObject());
        schema.m_classes.append(label);
        if (label.id > maxId)
            maxId = label.id;
    }
    schema.m_nextId = maxId + 1;
    return schema;
}

QJsonObject LabelSchema::toJsonObject() const
{
    QJsonArray templatesArray;
    for (const KeypointTemplate &tmpl : m_keypointTemplates)
        templatesArray.append(tmpl.toJson());

    QJsonObject obj;
    obj["classes"] = toJson();
    obj["keypointTemplates"] = templatesArray;
    return obj;
}

LabelSchema LabelSchema::fromJsonObject(const QJsonObject &obj)
{
    LabelSchema schema = fromJson(obj["classes"].toArray());

    const QJsonArray templatesArray = obj["keypointTemplates"].toArray();
    for (const QJsonValue &v : templatesArray)
        schema.addKeypointTemplate(KeypointTemplate::fromJson(v.toObject()));

    return schema;
}
