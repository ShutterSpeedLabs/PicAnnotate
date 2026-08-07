#ifndef MULTIPOINTSHAPEITEM_H
#define MULTIPOINTSHAPEITEM_H

#include <QColor>
#include <QGraphicsObject>
#include <QPair>
#include <QStringList>
#include <QVector>

#include "../core/annoshape.h"

class VertexHandle;

class MultiPointShapeItem : public QGraphicsObject
{
    Q_OBJECT
public:
    struct Config
    {
        bool closed = false;
        QVector<QPair<int, int>> edges;   // skeleton edges; empty for poly shapes
        QStringList pointNames;           // shown on hover, from the keypoint template
        bool showPointNames = false;

        // Produced by a track between keyframes: drawn thinner so it reads as
        // provisional next to a keyframed shape.
        bool interpolated = false;
    };

    MultiPointShapeItem(const QVector<QPointF> &points, const QVector<int> &visibility,
                        int shapeIndex, const QColor &color, const Config &config,
                        QGraphicsItem *parent = nullptr);

    int shapeIndex() const { return m_shapeIndex; }

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void handleMoved(int index, const QPointF &newPos);
    void commitGeometry();
    void cycleVisibility(int index);
    void setShowPointNames(bool show);

signals:
    void geometryChanged(int shapeIndex, const QVector<QPointF> &points);
    void visibilityChanged(int shapeIndex, const QVector<int> &visibility);

private:
    PointVisibility visibilityAt(int index) const;
    void applyHandleStyles();

    int m_shapeIndex;
    QColor m_color;
    Config m_config;
    QVector<VertexHandle *> m_handles;
    QVector<QPointF> m_points;
    QVector<int> m_visibility;
};

#endif // MULTIPOINTSHAPEITEM_H
