#ifndef ANNOSHAPE_H
#define ANNOSHAPE_H

#include <QFlags>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVariantMap>
#include <QVector>

class QJsonObject;

enum class ShapeType {
    Rect,
    Polygon,
    Polyline,
    Keypoint,
    Skeleton
};

// Bitmask mirror of ShapeType, so a LabelClass can declare which geometries it
// may be used with the way a Label Studio control tag binds a label set to one
// region type (RectangleLabels, PolygonLabels, KeyPointLabels, ...).
enum ShapeTypeFlag {
    NoShapeTypes       = 0x00,
    RectShapeFlag      = 0x01,
    PolygonShapeFlag   = 0x02,
    PolylineShapeFlag  = 0x04,
    KeypointShapeFlag  = 0x08,
    SkeletonShapeFlag  = 0x10,
    AllShapeTypes      = 0x1f
};
Q_DECLARE_FLAGS(ShapeTypeFlags, ShapeTypeFlag)
Q_DECLARE_OPERATORS_FOR_FLAGS(ShapeTypeFlags)

ShapeTypeFlag shapeTypeToFlag(ShapeType type);
QString shapeTypeToString(ShapeType type);
ShapeType shapeTypeFromString(const QString &s);

// COCO's keypoint visibility convention (the `v` in [x, y, v] triplets), reused
// for every point-bearing shape so exporters have a single source of truth.
enum class PointVisibility {
    NotLabeled = 0, // not annotated at all
    Occluded   = 1, // annotated but hidden in the image
    Visible    = 2
};

enum class ShapeSource {
    Manual,
    Tracked,
    Interpolated,

    // Came from a model and was accepted by the user. Kept distinct from Manual
    // so a dataset can still be audited for how much of it a human actually
    // drew — a distinction that disappears the moment predictions are written
    // in as ordinary shapes.
    Predicted
};

// Attribute key holding a model's confidence on a shape accepted from a
// prediction. Stored on the shape rather than alongside it so it survives the
// project save/load round trip.
inline constexpr const char *kConfidenceAttribute = "confidence";

// Attribute key recording which model produced a shape.
inline constexpr const char *kModelAttribute = "model";

class AnnoShape
{
public:
    AnnoShape() = default;

    static AnnoShape makeRect(const QRectF &rect, int labelId);
    static AnnoShape makePolygon(const QVector<QPointF> &points, int labelId);
    static AnnoShape makePolyline(const QVector<QPointF> &points, int labelId);
    static AnnoShape makeKeypoint(const QPointF &point, int labelId);
    static AnnoShape makeSkeleton(const QVector<QPointF> &points, int labelId,
                                  const QVector<int> &visibilityFlags = QVector<int>());

    ShapeType type() const { return m_type; }
    void setType(ShapeType type) { m_type = type; }

    QVector<QPointF> points() const { return m_points; }
    void setPoints(const QVector<QPointF> &points);

    QRectF rect() const;
    void setRect(const QRectF &rect);

    int labelId() const { return m_labelId; }
    void setLabelId(int labelId) { m_labelId = labelId; }

    int trackId() const { return m_trackId; }
    void setTrackId(int trackId) { m_trackId = trackId; }

    ShapeSource source() const { return m_source; }
    void setSource(ShapeSource source) { m_source = source; }

    // Stable identity, needed so an export/import round trip can re-attach a
    // region to the same annotation instead of creating a duplicate.
    QString id() const { return m_id; }
    void setId(const QString &id) { m_id = id; }

    // Per-point visibility. Always the same length as points(); points added
    // without an explicit flag default to Visible.
    QVector<int> visibilityFlags() const { return m_visibility; }
    void setVisibilityFlags(const QVector<int> &flags);
    PointVisibility visibilityAt(int index) const;
    void setVisibilityAt(int index, PointVisibility visibility);
    void cycleVisibilityAt(int index);
    int labeledPointCount() const;

    // Clockwise rotation in degrees about the rect centre. Only meaningful for
    // ShapeType::Rect; other types keep it at 0.
    double rotation() const { return m_rotation; }
    void setRotation(double degrees) { m_rotation = degrees; }
    bool isRotated() const;
    QVector<QPointF> rotatedRectCorners() const;

    QVariantMap attributes() const { return m_attributes; }
    void setAttributes(const QVariantMap &attributes) { m_attributes = attributes; }
    void setAttribute(const QString &key, const QVariant &value) { m_attributes.insert(key, value); }

    // Axis-aligned bounds over every point, rotation-aware for rects. This is
    // what the COCO/YOLO writers use for `bbox`.
    QRectF boundingRect() const;

    QJsonObject toJson() const;
    static AnnoShape fromJson(const QJsonObject &obj);

private:
    void initIdentity();

    ShapeType m_type = ShapeType::Rect;
    QVector<QPointF> m_points;
    QVector<int> m_visibility;
    int m_labelId = -1;
    int m_trackId = -1;
    ShapeSource m_source = ShapeSource::Manual;
    QString m_id;
    double m_rotation = 0.0;
    QVariantMap m_attributes;
};

#endif // ANNOSHAPE_H
