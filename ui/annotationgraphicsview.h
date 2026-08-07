#ifndef ANNOTATIONGRAPHICSVIEW_H
#define ANNOTATIONGRAPHICSVIEW_H

#include <QGraphicsView>

#include "../core/annoshape.h"
#include "../core/labelschema.h"

class QGraphicsPixmapItem;
class QGraphicsPathItem;
class RectShapeItem;
class MultiPointShapeItem;

class AnnotationGraphicsView : public QGraphicsView
{
    Q_OBJECT
public:
    enum class DrawMode {
        None,
        Rect,
        Square,
        Polygon,
        Polyline,
        Keypoint,
        Skeleton
    };

    explicit AnnotationGraphicsView(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void setShapes(const QVector<AnnoShape> &shapes, const LabelSchema &schema);

    // Scales the image to fill the viewport and re-enables auto-fit, so later
    // window resizes keep it fitted.
    void fitImageInView();

    // Zooming is an explicit choice, so it turns auto-fit off and the chosen
    // scale survives a resize.
    void zoomBy(double factor);

    void setDrawMode(DrawMode mode);
    DrawMode drawMode() const { return m_drawMode; }

    // Label whose keypoint template new skeletons are placed from, so a skeleton
    // starts with the right number of named points for its class.
    void setActiveLabelId(int labelId) { m_activeLabelId = labelId; }

    void setShowPointNames(bool show);
    bool showPointNames() const { return m_showPointNames; }

    int selectedShapeIndex() const;
    void selectShape(int shapeIndex);

signals:
    void rectDrawn(const QRectF &imageRect);
    void multiPointShapeDrawn(ShapeType type, const QVector<QPointF> &points);
    void shapeGeometryChanged(int shapeIndex, const QRectF &imageRect);
    void shapeRotationChanged(int shapeIndex, double degrees);
    void multiPointShapeGeometryChanged(int shapeIndex, const QVector<QPointF> &points);
    void shapeVisibilityChanged(int shapeIndex, const QVector<int> &visibility);
    void shapeDeleteRequested(int shapeIndex);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QRectF rubberBandRect(const QPointF &start, const QPointF &current) const;
    void updatePendingPathItem(const QPointF &cursorPos);
    void finishPendingShape();
    void cancelPendingShape();
    void clearShapeItems();

    QGraphicsScene *m_scene;
    QGraphicsPixmapItem *m_imageItem;
    QVector<RectShapeItem *> m_rectShapeItems;
    QVector<MultiPointShapeItem *> m_multiPointShapeItems;

    DrawMode m_drawMode = DrawMode::None;
    QGraphicsRectItem *m_rubberBand = nullptr;
    QPointF m_rubberBandStart;

    QVector<QPointF> m_pendingPoints;
    QGraphicsPathItem *m_pendingPathItem = nullptr;

    // Copy of the schema from the last setShapes(), needed when a new skeleton is
    // placed and its template has to be resolved from the active label.
    LabelSchema m_schema;
    int m_activeLabelId = -1;
    bool m_showPointNames = false;

    // While set, the image is refitted on every resize. The first fit happens
    // during construction when the viewport is still tiny, so without this the
    // image stays at that initial scale once the window opens for real.
    bool m_autoFit = true;
};

#endif // ANNOTATIONGRAPHICSVIEW_H
