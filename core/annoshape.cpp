#include "annoshape.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTransform>
#include <QUuid>

#include <cmath>

namespace {

QString sourceToString(ShapeSource source)
{
    switch (source) {
    case ShapeSource::Tracked:
        return "tracked";
    case ShapeSource::Interpolated:
        return "interpolated";
    case ShapeSource::Manual:
    default:
        return "manual";
    }
}

ShapeSource sourceFromString(const QString &s)
{
    if (s == "tracked")
        return ShapeSource::Tracked;
    if (s == "interpolated")
        return ShapeSource::Interpolated;
    return ShapeSource::Manual;
}

// Axis-aligned bounds of a point set. QRectF::united() cannot be used here: a
// zero-size rect around one point counts as null, and united() drops null
// rectangles, so unioning point rects yields an empty result.
QRectF boundsOf(const QVector<QPointF> &points)
{
    if (points.isEmpty())
        return QRectF();

    double minX = points.first().x();
    double maxX = minX;
    double minY = points.first().y();
    double maxY = minY;

    for (const QPointF &p : points) {
        minX = qMin(minX, p.x());
        maxX = qMax(maxX, p.x());
        minY = qMin(minY, p.y());
        maxY = qMax(maxY, p.y());
    }
    return QRectF(minX, minY, maxX - minX, maxY - minY);
}

int clampVisibility(int raw)
{
    if (raw < 0)
        return static_cast<int>(PointVisibility::NotLabeled);
    if (raw > 2)
        return static_cast<int>(PointVisibility::Visible);
    return raw;
}

} // namespace

ShapeTypeFlag shapeTypeToFlag(ShapeType type)
{
    switch (type) {
    case ShapeType::Polygon:
        return PolygonShapeFlag;
    case ShapeType::Polyline:
        return PolylineShapeFlag;
    case ShapeType::Keypoint:
        return KeypointShapeFlag;
    case ShapeType::Skeleton:
        return SkeletonShapeFlag;
    case ShapeType::Rect:
    default:
        return RectShapeFlag;
    }
}

QString shapeTypeToString(ShapeType type)
{
    switch (type) {
    case ShapeType::Polygon:
        return "polygon";
    case ShapeType::Polyline:
        return "polyline";
    case ShapeType::Keypoint:
        return "keypoint";
    case ShapeType::Skeleton:
        return "skeleton";
    case ShapeType::Rect:
    default:
        return "rect";
    }
}

ShapeType shapeTypeFromString(const QString &s)
{
    if (s == "polygon")
        return ShapeType::Polygon;
    if (s == "polyline")
        return ShapeType::Polyline;
    if (s == "keypoint")
        return ShapeType::Keypoint;
    if (s == "skeleton")
        return ShapeType::Skeleton;
    return ShapeType::Rect;
}

void AnnoShape::initIdentity()
{
    if (m_id.isEmpty())
        m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
}

AnnoShape AnnoShape::makeRect(const QRectF &rect, int labelId)
{
    AnnoShape shape;
    shape.m_type = ShapeType::Rect;
    shape.setRect(rect);
    shape.m_labelId = labelId;
    shape.initIdentity();
    return shape;
}

AnnoShape AnnoShape::makePolygon(const QVector<QPointF> &points, int labelId)
{
    AnnoShape shape;
    shape.m_type = ShapeType::Polygon;
    shape.setPoints(points);
    shape.m_labelId = labelId;
    shape.initIdentity();
    return shape;
}

AnnoShape AnnoShape::makePolyline(const QVector<QPointF> &points, int labelId)
{
    AnnoShape shape;
    shape.m_type = ShapeType::Polyline;
    shape.setPoints(points);
    shape.m_labelId = labelId;
    shape.initIdentity();
    return shape;
}

AnnoShape AnnoShape::makeKeypoint(const QPointF &point, int labelId)
{
    AnnoShape shape;
    shape.m_type = ShapeType::Keypoint;
    shape.setPoints({point});
    shape.m_labelId = labelId;
    shape.initIdentity();
    return shape;
}

AnnoShape AnnoShape::makeSkeleton(const QVector<QPointF> &points, int labelId,
                                  const QVector<int> &visibilityFlags)
{
    AnnoShape shape;
    shape.m_type = ShapeType::Skeleton;
    shape.setPoints(points);
    if (!visibilityFlags.isEmpty())
        shape.setVisibilityFlags(visibilityFlags);
    shape.m_labelId = labelId;
    shape.initIdentity();
    return shape;
}

void AnnoShape::setPoints(const QVector<QPointF> &points)
{
    m_points = points;

    // Keep the visibility vector in lock-step with the geometry; new points are
    // visible until the user says otherwise.
    const int visibleFlag = static_cast<int>(PointVisibility::Visible);
    if (m_visibility.size() > points.size())
        m_visibility.resize(points.size());
    while (m_visibility.size() < points.size())
        m_visibility.append(visibleFlag);
}

void AnnoShape::setVisibilityFlags(const QVector<int> &flags)
{
    const int visibleFlag = static_cast<int>(PointVisibility::Visible);
    m_visibility.clear();
    m_visibility.reserve(m_points.size());
    for (int i = 0; i < m_points.size(); ++i)
        m_visibility.append(i < flags.size() ? clampVisibility(flags.at(i)) : visibleFlag);
}

PointVisibility AnnoShape::visibilityAt(int index) const
{
    if (index < 0 || index >= m_visibility.size())
        return PointVisibility::Visible;
    return static_cast<PointVisibility>(m_visibility.at(index));
}

void AnnoShape::setVisibilityAt(int index, PointVisibility visibility)
{
    if (index < 0 || index >= m_points.size())
        return;
    while (m_visibility.size() < m_points.size())
        m_visibility.append(static_cast<int>(PointVisibility::Visible));
    m_visibility[index] = static_cast<int>(visibility);
}

void AnnoShape::cycleVisibilityAt(int index)
{
    switch (visibilityAt(index)) {
    case PointVisibility::Visible:
        setVisibilityAt(index, PointVisibility::Occluded);
        break;
    case PointVisibility::Occluded:
        setVisibilityAt(index, PointVisibility::NotLabeled);
        break;
    case PointVisibility::NotLabeled:
        setVisibilityAt(index, PointVisibility::Visible);
        break;
    }
}

int AnnoShape::labeledPointCount() const
{
    int count = 0;
    for (int i = 0; i < m_points.size(); ++i) {
        if (visibilityAt(i) != PointVisibility::NotLabeled)
            ++count;
    }
    return count;
}

QRectF AnnoShape::rect() const
{
    if (m_points.size() < 2)
        return QRectF();
    return QRectF(m_points.at(0), m_points.at(1)).normalized();
}

void AnnoShape::setRect(const QRectF &rect)
{
    setPoints({rect.topLeft(), rect.bottomRight()});
}

bool AnnoShape::isRotated() const
{
    return m_type == ShapeType::Rect && !qFuzzyIsNull(m_rotation);
}

QVector<QPointF> AnnoShape::rotatedRectCorners() const
{
    const QRectF base = rect();
    if (base.isNull())
        return {};

    if (!isRotated())
        return {base.topLeft(), base.topRight(), base.bottomRight(), base.bottomLeft()};

    QTransform transform;
    transform.translate(base.center().x(), base.center().y());
    transform.rotate(m_rotation);
    transform.translate(-base.center().x(), -base.center().y());

    return {transform.map(base.topLeft()), transform.map(base.topRight()),
            transform.map(base.bottomRight()), transform.map(base.bottomLeft())};
}

QRectF AnnoShape::boundingRect() const
{
    if (m_type == ShapeType::Rect)
        return isRotated() ? boundsOf(rotatedRectCorners()) : rect();

    // For point-bearing shapes, unlabeled points carry no position information
    // and must not drag the bounds towards the origin.
    QVector<QPointF> labeled;
    labeled.reserve(m_points.size());
    for (int i = 0; i < m_points.size(); ++i) {
        if (visibilityAt(i) != PointVisibility::NotLabeled)
            labeled.append(m_points.at(i));
    }
    return boundsOf(labeled);
}

QJsonObject AnnoShape::toJson() const
{
    QJsonArray points;
    for (const QPointF &p : m_points)
        points.append(QJsonArray{p.x(), p.y()});

    QJsonArray visibility;
    for (int flag : m_visibility)
        visibility.append(flag);

    QJsonObject obj;
    obj["id"] = m_id;
    obj["type"] = shapeTypeToString(m_type);
    obj["points"] = points;
    obj["visibility"] = visibility;
    obj["labelId"] = m_labelId;
    obj["trackId"] = m_trackId;
    obj["source"] = sourceToString(m_source);
    if (!qFuzzyIsNull(m_rotation))
        obj["rotation"] = m_rotation;
    if (!m_attributes.isEmpty())
        obj["attributes"] = QJsonObject::fromVariantMap(m_attributes);
    return obj;
}

AnnoShape AnnoShape::fromJson(const QJsonObject &obj)
{
    AnnoShape shape;
    shape.m_type = shapeTypeFromString(obj["type"].toString());

    QVector<QPointF> points;
    const QJsonArray pointsArray = obj["points"].toArray();
    points.reserve(pointsArray.size());
    for (const QJsonValue &v : pointsArray) {
        const QJsonArray pair = v.toArray();
        if (pair.size() == 2)
            points.append(QPointF(pair.at(0).toDouble(), pair.at(1).toDouble()));
    }
    shape.setPoints(points);

    // Projects written before visibility flags existed simply have every point
    // visible, which setPoints() already arranged.
    if (obj.contains("visibility")) {
        QVector<int> flags;
        const QJsonArray visibilityArray = obj["visibility"].toArray();
        flags.reserve(visibilityArray.size());
        for (const QJsonValue &v : visibilityArray)
            flags.append(v.toInt(static_cast<int>(PointVisibility::Visible)));
        shape.setVisibilityFlags(flags);
    }

    shape.m_labelId = obj["labelId"].toInt(-1);
    shape.m_trackId = obj["trackId"].toInt(-1);
    shape.m_source = sourceFromString(obj["source"].toString());
    shape.m_rotation = obj["rotation"].toDouble(0.0);
    shape.m_attributes = obj["attributes"].toObject().toVariantMap();
    shape.m_id = obj["id"].toString();
    shape.initIdentity();
    return shape;
}
