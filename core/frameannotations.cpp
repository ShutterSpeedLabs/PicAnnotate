#include "frameannotations.h"

#include <QJsonArray>
#include <QJsonObject>

namespace {

QString statusToString(ReviewStatus status)
{
    switch (status) {
    case ReviewStatus::InProgress:
        return "in_progress";
    case ReviewStatus::Completed:
        return "completed";
    case ReviewStatus::Skipped:
        return "skipped";
    case ReviewStatus::Unlabeled:
    default:
        return "unlabeled";
    }
}

ReviewStatus statusFromString(const QString &s)
{
    if (s == "in_progress")
        return ReviewStatus::InProgress;
    if (s == "completed")
        return ReviewStatus::Completed;
    if (s == "skipped")
        return ReviewStatus::Skipped;
    return ReviewStatus::Unlabeled;
}

} // namespace

void FrameAnnotations::addShape(const AnnoShape &shape)
{
    m_shapes.append(shape);
}

void FrameAnnotations::insertShape(int index, const AnnoShape &shape)
{
    m_shapes.insert(qBound(0, index, m_shapes.size()), shape);
}

bool FrameAnnotations::removeShapeAt(int index)
{
    if (index < 0 || index >= m_shapes.size())
        return false;
    m_shapes.removeAt(index);
    return true;
}

bool FrameAnnotations::replaceShapeAt(int index, const AnnoShape &shape)
{
    if (index < 0 || index >= m_shapes.size())
        return false;
    m_shapes[index] = shape;
    return true;
}

const AnnoShape *FrameAnnotations::shapeAt(int index) const
{
    if (index < 0 || index >= m_shapes.size())
        return nullptr;
    return &m_shapes.at(index);
}

int FrameAnnotations::indexOfShapeId(const QString &id) const
{
    if (id.isEmpty())
        return -1;
    for (int i = 0; i < m_shapes.size(); ++i) {
        if (m_shapes.at(i).id() == id)
            return i;
    }
    return -1;
}

QJsonObject FrameAnnotations::toJson() const
{
    QJsonArray shapesArray;
    for (const AnnoShape &shape : m_shapes)
        shapesArray.append(shape.toJson());

    QJsonObject obj;
    obj["status"] = statusToString(m_status);
    obj["shapes"] = shapesArray;
    if (hasImageSize()) {
        obj["imageWidth"] = m_imageSize.width();
        obj["imageHeight"] = m_imageSize.height();
    }
    return obj;
}

FrameAnnotations FrameAnnotations::fromJson(const QJsonObject &obj)
{
    FrameAnnotations frame;
    frame.m_status = statusFromString(obj["status"].toString());

    const int width = obj["imageWidth"].toInt(0);
    const int height = obj["imageHeight"].toInt(0);
    if (width > 0 && height > 0)
        frame.m_imageSize = QSize(width, height);

    const QJsonArray shapesArray = obj["shapes"].toArray();
    for (const QJsonValue &v : shapesArray)
        frame.m_shapes.append(AnnoShape::fromJson(v.toObject()));
    return frame;
}
