#ifndef VERTEXHANDLE_H
#define VERTEXHANDLE_H

#include <QGraphicsEllipseItem>

#include "../core/annoshape.h"

class MultiPointShapeItem;

class VertexHandle : public QGraphicsEllipseItem
{
public:
    VertexHandle(int index, MultiPointShapeItem *owner);

    int index() const { return m_index; }

    // Restyles the handle to show whether the point is visible, occluded, or not
    // labeled at all — the COCO `v` value the exporters read.
    void setVisibilityState(PointVisibility visibility, const QColor &shapeColor);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    int m_index;
    MultiPointShapeItem *m_owner;
};

#endif // VERTEXHANDLE_H
