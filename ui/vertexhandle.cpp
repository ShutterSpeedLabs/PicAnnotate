#include "vertexhandle.h"
#include "multipointshapeitem.h"

#include <QGraphicsSceneMouseEvent>
#include <QPen>

namespace {
constexpr qreal kHandleRadius = 4.0;
}

VertexHandle::VertexHandle(int index, MultiPointShapeItem *owner)
    : QGraphicsEllipseItem(-kHandleRadius, -kHandleRadius, kHandleRadius * 2, kHandleRadius * 2, owner)
    , m_index(index)
    , m_owner(owner)
{
    setFlag(QGraphicsItem::ItemIsMovable, true);
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
    setBrush(Qt::white);
    setPen(QPen(Qt::black, 1));
    setZValue(1);
}

void VertexHandle::setVisibilityState(PointVisibility visibility, const QColor &shapeColor)
{
    switch (visibility) {
    case PointVisibility::Visible:
        setBrush(Qt::white);
        setPen(QPen(Qt::black, 1));
        setToolTip(QStringLiteral("Visible (v=2) — right-click to change"));
        break;
    case PointVisibility::Occluded:
        // Hollow, shape-coloured outline: annotated but hidden in the image.
        setBrush(Qt::NoBrush);
        setPen(QPen(shapeColor, 2));
        setToolTip(QStringLiteral("Occluded (v=1) — right-click to change"));
        break;
    case PointVisibility::NotLabeled:
        setBrush(QColor(120, 120, 120, 90));
        setPen(QPen(QColor(90, 90, 90), 1, Qt::DotLine));
        setToolTip(QStringLiteral("Not labeled (v=0) — right-click to change"));
        break;
    }
}

QVariant VertexHandle::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionHasChanged)
        m_owner->handleMoved(m_index, value.toPointF());
    return QGraphicsEllipseItem::itemChange(change, value);
}

void VertexHandle::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    // Right-click cycles visible -> occluded -> not labeled, which is the fastest
    // way through a skeleton where only some joints are in frame.
    if (event->button() == Qt::RightButton) {
        m_owner->cycleVisibility(m_index);
        event->accept();
        return;
    }
    QGraphicsEllipseItem::mousePressEvent(event);
}

void VertexHandle::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsEllipseItem::mouseReleaseEvent(event);
    m_owner->commitGeometry();
}
