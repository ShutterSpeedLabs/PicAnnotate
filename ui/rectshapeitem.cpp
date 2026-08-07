#include "rectshapeitem.h"

#include <QCursor>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPen>
#include <QStyleOptionGraphicsItem>

#include <cmath>

RectShapeItem::RectShapeItem(const QRectF &rect, int shapeIndex, const QColor &color,
                             bool interpolated, double rotation, QGraphicsItem *parent)
    : QGraphicsRectItem(rect, parent)
    , m_shapeIndex(shapeIndex)
{
    setFlag(QGraphicsItem::ItemIsSelectable, true);
    setAcceptHoverEvents(true);

    // The item's transform carries the angle, so every hit test below can work in
    // the unrotated frame and stay simple.
    setTransformOriginPoint(rect.center());
    setRotation(rotation);

    if (interpolated) {
        setPen(QPen(color, 1, Qt::DashLine));
        setBrush(Qt::NoBrush);
        setToolTip(QStringLiteral("Interpolated between keyframes — edit to pin a keyframe here"));
        return;
    }

    QColor fillColor = color;
    fillColor.setAlpha(50);
    setPen(QPen(color, 2));
    setBrush(fillColor);
}

QPointF RectShapeItem::rotationHandlePos() const
{
    const QRectF r = rect();
    return QPointF(r.center().x(), r.top() - kRotateHandleOffset);
}

QRectF RectShapeItem::boundingRect() const
{
    // Room for the rotation handle and its stem, which sit outside the box.
    const qreal margin = kRotateHandleOffset + kRotateHandleRadius + 2.0;
    return QGraphicsRectItem::boundingRect().adjusted(-margin, -margin, margin, margin);
}

QPainterPath RectShapeItem::shape() const
{
    // Without the handle in the shape, clicks on it fall through to the scene and
    // rotation becomes unreachable.
    QPainterPath path;
    path.addRect(rect());

    const QPointF handle = rotationHandlePos();
    path.addEllipse(handle, kRotateHandleRadius + 3.0, kRotateHandleRadius + 3.0);
    return path;
}

void RectShapeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    QGraphicsRectItem::paint(painter, option, widget);

    // Only on the selected box: drawn for every box at once it is just clutter.
    if (!(option->state & QStyle::State_Selected))
        return;

    const QColor handleColor = pen().color();
    const QPointF handle = rotationHandlePos();

    painter->save();
    painter->setPen(QPen(handleColor, 1, Qt::DotLine));
    painter->drawLine(QPointF(rect().center().x(), rect().top()), handle);
    painter->setPen(QPen(handleColor, 1));
    painter->setBrush(Qt::white);
    painter->drawEllipse(handle, kRotateHandleRadius, kRotateHandleRadius);
    painter->restore();
}

RectShapeItem::DragMode RectShapeItem::hitTest(const QPointF &pos) const
{
    // The rotation handle wins over everything, including the corner nearest it.
    if (isSelected()) {
        const QPointF toHandle = pos - rotationHandlePos();
        if (std::hypot(toHandle.x(), toHandle.y()) <= kRotateHandleRadius + 4.0)
            return DragMode::Rotate;
    }

    const QRectF r = rect();
    const bool nearLeft = qAbs(pos.x() - r.left()) <= kHandleMargin;
    const bool nearRight = qAbs(pos.x() - r.right()) <= kHandleMargin;
    const bool nearTop = qAbs(pos.y() - r.top()) <= kHandleMargin;
    const bool nearBottom = qAbs(pos.y() - r.bottom()) <= kHandleMargin;

    if (nearLeft && nearTop)
        return DragMode::ResizeTopLeft;
    if (nearRight && nearTop)
        return DragMode::ResizeTopRight;
    if (nearLeft && nearBottom)
        return DragMode::ResizeBottomLeft;
    if (nearRight && nearBottom)
        return DragMode::ResizeBottomRight;
    if (r.contains(pos))
        return DragMode::Move;
    return DragMode::None;
}

Qt::CursorShape RectShapeItem::cursorForDragMode(DragMode mode)
{
    switch (mode) {
    case DragMode::ResizeTopLeft:
    case DragMode::ResizeBottomRight:
        return Qt::SizeFDiagCursor;
    case DragMode::ResizeTopRight:
    case DragMode::ResizeBottomLeft:
        return Qt::SizeBDiagCursor;
    case DragMode::Move:
        return Qt::SizeAllCursor;
    case DragMode::Rotate:
        return Qt::CrossCursor;
    default:
        return Qt::ArrowCursor;
    }
}

void RectShapeItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    setSelected(true);
    m_dragMode = hitTest(event->pos());
    m_dragStartRect = rect();
    m_dragStartPos = event->pos();
    m_dragStartRotation = rotation();
    event->accept();
}

void RectShapeItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_dragMode == DragMode::None)
        return;

    if (m_dragMode == DragMode::Rotate) {
        // Measured in scene space on purpose: item coordinates rotate with the
        // item, so using them here would feed the rotation back into itself.
        const QPointF centre = mapToScene(rect().center());
        const QPointF delta = event->scenePos() - centre;
        if (qFuzzyIsNull(delta.x()) && qFuzzyIsNull(delta.y()))
            return;

        // The handle sits above the centre at zero rotation, hence the +90.
        double angle = std::atan2(delta.y(), delta.x()) * 180.0 / M_PI + 90.0;
        if (event->modifiers() & Qt::ShiftModifier)
            angle = qRound(angle / kRotationSnapDegrees) * kRotationSnapDegrees;

        while (angle < 0.0)
            angle += 360.0;
        while (angle >= 360.0)
            angle -= 360.0;

        setTransformOriginPoint(rect().center());
        setRotation(angle);
        return;
    }

    const QPointF delta = event->pos() - m_dragStartPos;
    QRectF newRect = m_dragStartRect;

    switch (m_dragMode) {
    case DragMode::Move:
        newRect.translate(delta);
        break;
    case DragMode::ResizeTopLeft:
        newRect.setTopLeft(m_dragStartRect.topLeft() + delta);
        break;
    case DragMode::ResizeTopRight:
        newRect.setTopRight(m_dragStartRect.topRight() + delta);
        break;
    case DragMode::ResizeBottomLeft:
        newRect.setBottomLeft(m_dragStartRect.bottomLeft() + delta);
        break;
    case DragMode::ResizeBottomRight:
        newRect.setBottomRight(m_dragStartRect.bottomRight() + delta);
        break;
    default:
        break;
    }

    prepareGeometryChange();
    setRect(newRect.normalized());
    // Keep rotating about the box's own middle as it is resized.
    setTransformOriginPoint(rect().center());
}

void RectShapeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    Q_UNUSED(event);
    const DragMode finished = m_dragMode;
    m_dragMode = DragMode::None;

    // Emitted last, and after m_dragMode is already cleared: the handler rebuilds
    // the scene, so nothing may touch this object afterwards.
    if (finished == DragMode::Rotate) {
        if (!qFuzzyCompare(rotation() + 1.0, m_dragStartRotation + 1.0))
            emit rotationChanged(m_shapeIndex, rotation());
        return;
    }

    if (finished != DragMode::None && rect() != m_dragStartRect)
        emit geometryChanged(m_shapeIndex, rect());
}

void RectShapeItem::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    setCursor(cursorForDragMode(hitTest(event->pos())));
    QGraphicsRectItem::hoverMoveEvent(event);
}
