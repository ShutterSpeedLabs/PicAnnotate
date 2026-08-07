#include "multipointshapeitem.h"
#include "vertexhandle.h"

#include <QPainter>

MultiPointShapeItem::MultiPointShapeItem(const QVector<QPointF> &points, const QVector<int> &visibility,
                                         int shapeIndex, const QColor &color, const Config &config,
                                         QGraphicsItem *parent)
    : QGraphicsObject(parent)
    , m_shapeIndex(shapeIndex)
    , m_color(color)
    , m_config(config)
    , m_points(points)
    , m_visibility(visibility)
{
    setFlag(QGraphicsItem::ItemIsSelectable, true);

    while (m_visibility.size() < m_points.size())
        m_visibility.append(static_cast<int>(PointVisibility::Visible));

    for (int i = 0; i < points.size(); ++i) {
        auto *handle = new VertexHandle(i, this);
        handle->setPos(points.at(i));
        m_handles.append(handle);
    }
    applyHandleStyles();
}

PointVisibility MultiPointShapeItem::visibilityAt(int index) const
{
    if (index < 0 || index >= m_visibility.size())
        return PointVisibility::Visible;
    return static_cast<PointVisibility>(m_visibility.at(index));
}

void MultiPointShapeItem::applyHandleStyles()
{
    for (int i = 0; i < m_handles.size(); ++i) {
        m_handles.at(i)->setVisibilityState(visibilityAt(i), m_color);
        if (i < m_config.pointNames.size()) {
            m_handles.at(i)->setToolTip(QStringLiteral("%1 — %2")
                                            .arg(m_config.pointNames.at(i),
                                                 m_handles.at(i)->toolTip()));
        }
    }
}

QRectF MultiPointShapeItem::boundingRect() const
{
    if (m_points.isEmpty())
        return QRectF();

    // Tracked as min/max rather than via QRectF::united(), which discards the
    // null (zero-size) rects a single point produces and would leave the bounds
    // sitting on the first point only — clipping the rest of the shape.
    double minX = m_points.first().x();
    double maxX = minX;
    double minY = m_points.first().y();
    double maxY = minY;

    for (const QPointF &p : m_points) {
        minX = qMin(minX, p.x());
        maxX = qMax(maxX, p.x());
        minY = qMin(minY, p.y());
        maxY = qMax(maxY, p.y());
    }
    return QRectF(minX, minY, maxX - minX, maxY - minY).adjusted(-10, -10, 10, 10);
}

void MultiPointShapeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    if (m_points.size() < 2)
        return;

    const QPen solidPen(m_color, m_config.interpolated ? 1 : 2,
                        m_config.interpolated ? Qt::DashLine : Qt::SolidLine);
    // An edge touching an occluded joint is drawn dashed so the annotator can see
    // at a glance which parts of the pose are inferred rather than observed.
    QPen occludedPen(m_color, 1, Qt::DotLine);

    const auto isDrawable = [this](int index) {
        return visibilityAt(index) != PointVisibility::NotLabeled;
    };

    if (!m_config.edges.isEmpty()) {
        for (const auto &edge : m_config.edges) {
            if (edge.first >= m_points.size() || edge.second >= m_points.size())
                continue;
            if (!isDrawable(edge.first) || !isDrawable(edge.second))
                continue;

            const bool occluded = visibilityAt(edge.first) == PointVisibility::Occluded
                               || visibilityAt(edge.second) == PointVisibility::Occluded;
            painter->setPen(occluded ? occludedPen : solidPen);
            painter->drawLine(m_points.at(edge.first), m_points.at(edge.second));
        }
    } else {
        painter->setPen(solidPen);
        for (int i = 0; i + 1 < m_points.size(); ++i)
            painter->drawLine(m_points.at(i), m_points.at(i + 1));
        if (m_config.closed)
            painter->drawLine(m_points.last(), m_points.first());
    }

    if (m_config.showPointNames && !m_config.pointNames.isEmpty()) {
        QFont font = painter->font();
        font.setPointSizeF(7.0);
        painter->setFont(font);
        painter->setPen(m_color.darker(140));
        for (int i = 0; i < m_points.size() && i < m_config.pointNames.size(); ++i) {
            if (!isDrawable(i))
                continue;
            painter->drawText(m_points.at(i) + QPointF(6, -4), m_config.pointNames.at(i));
        }
    }
}

void MultiPointShapeItem::handleMoved(int index, const QPointF &newPos)
{
    if (index < 0 || index >= m_points.size())
        return;
    prepareGeometryChange();
    m_points[index] = newPos;
    update();
}

void MultiPointShapeItem::commitGeometry()
{
    emit geometryChanged(m_shapeIndex, m_points);
}

void MultiPointShapeItem::setShowPointNames(bool show)
{
    if (m_config.showPointNames == show)
        return;
    m_config.showPointNames = show;
    update();
}

void MultiPointShapeItem::cycleVisibility(int index)
{
    if (index < 0 || index >= m_visibility.size())
        return;

    switch (visibilityAt(index)) {
    case PointVisibility::Visible:
        m_visibility[index] = static_cast<int>(PointVisibility::Occluded);
        break;
    case PointVisibility::Occluded:
        m_visibility[index] = static_cast<int>(PointVisibility::NotLabeled);
        break;
    case PointVisibility::NotLabeled:
        m_visibility[index] = static_cast<int>(PointVisibility::Visible);
        break;
    }

    applyHandleStyles();
    update();
    emit visibilityChanged(m_shapeIndex, m_visibility);
}
