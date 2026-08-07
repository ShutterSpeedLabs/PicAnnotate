#ifndef RECTSHAPEITEM_H
#define RECTSHAPEITEM_H

#include <QColor>
#include <QGraphicsRectItem>
#include <QObject>

class RectShapeItem : public QObject, public QGraphicsRectItem
{
    Q_OBJECT
public:
    // `interpolated` draws the box dashed and unfilled: it marks a frame the
    // annotator has not pinned down, produced by a track between keyframes.
    // `rotation` is clockwise degrees about the box centre.
    RectShapeItem(const QRectF &rect, int shapeIndex, const QColor &color,
                  bool interpolated = false, double rotation = 0.0,
                  QGraphicsItem *parent = nullptr);

    int shapeIndex() const { return m_shapeIndex; }

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

signals:
    void geometryChanged(int shapeIndex, const QRectF &rect);
    void rotationChanged(int shapeIndex, double degrees);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;

private:
    enum class DragMode {
        None,
        Move,
        Rotate,
        ResizeTopLeft,
        ResizeTopRight,
        ResizeBottomLeft,
        ResizeBottomRight
    };

    DragMode hitTest(const QPointF &pos) const;
    static Qt::CursorShape cursorForDragMode(DragMode mode);

    // Handle position in item (unrotated) coordinates: the item's own transform
    // carries the rotation, so all hit-testing stays in the upright frame.
    QPointF rotationHandlePos() const;

    int m_shapeIndex;
    DragMode m_dragMode = DragMode::None;
    QRectF m_dragStartRect;
    QPointF m_dragStartPos;
    double m_dragStartRotation = 0.0;

    static constexpr qreal kHandleMargin = 6.0;
    static constexpr qreal kRotateHandleOffset = 22.0;
    static constexpr qreal kRotateHandleRadius = 5.0;
    static constexpr double kRotationSnapDegrees = 15.0;
};

#endif // RECTSHAPEITEM_H
