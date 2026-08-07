#include "annotationgraphicsview.h"
#include "multipointshapeitem.h"
#include "rectshapeitem.h"
#include "theme.h"

#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPen>

AnnotationGraphicsView::AnnotationGraphicsView(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
    , m_imageItem(new QGraphicsPixmapItem)
{
    m_scene->addItem(m_imageItem);
    setScene(m_scene);
    setDragMode(QGraphicsView::NoDrag);
    setRenderHint(QPainter::Antialiasing);

    // Set on the scene as well as via the stylesheet: the area outside the image
    // is painted by the scene, not the viewport, so both need the canvas colour
    // or the letterboxing around a fitted image shows through in a different tone.
    m_scene->setBackgroundBrush(Theme::Color::canvas());
    setBackgroundBrush(Theme::Color::canvas());
    setFrameShape(QFrame::NoFrame);
}

void AnnotationGraphicsView::setImage(const QImage &image)
{
    m_imageItem->setPixmap(QPixmap::fromImage(image));
    m_scene->setSceneRect(image.rect());
    fitImageInView();
}

void AnnotationGraphicsView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_autoFit)
        fitInView(m_imageItem, Qt::KeepAspectRatio);
}

void AnnotationGraphicsView::zoomBy(double factor)
{
    m_autoFit = false;
    scale(factor, factor);
}

void AnnotationGraphicsView::clearShapeItems()
{
    // Deferred deletion, deliberately not delete. setShapes() is reached from a
    // shape item's own mouse handler — a drag commit or a visibility toggle emits
    // a signal that ends up rebuilding the scene — so deleting synchronously
    // would free both the object still executing on the stack and the sender of
    // the signal that got us here. The items are pulled out of the scene
    // immediately; only the destruction waits for the event loop.
    for (RectShapeItem *item : m_rectShapeItems) {
        m_scene->removeItem(item);
        item->deleteLater();
    }
    m_rectShapeItems.clear();

    for (MultiPointShapeItem *item : m_multiPointShapeItems) {
        m_scene->removeItem(item);
        item->deleteLater();
    }
    m_multiPointShapeItems.clear();
}

void AnnotationGraphicsView::setShapes(const QVector<AnnoShape> &shapes, const LabelSchema &schema)
{
    clearShapeItems();
    m_schema = schema;

    for (int i = 0; i < shapes.size(); ++i) {
        const AnnoShape &shape = shapes.at(i);
        const LabelClass *label = schema.findClass(shape.labelId());
        const QColor color = label ? label->color : QColor(Qt::yellow);

        const bool interpolated = shape.source() == ShapeSource::Interpolated;

        if (shape.type() == ShapeType::Rect) {
            auto *item = new RectShapeItem(shape.rect(), i, color, interpolated, shape.rotation());
            connect(item, &RectShapeItem::geometryChanged, this, &AnnotationGraphicsView::shapeGeometryChanged);
            connect(item, &RectShapeItem::rotationChanged, this, &AnnotationGraphicsView::shapeRotationChanged);
            m_scene->addItem(item);
            m_rectShapeItems.append(item);
            continue;
        }

        MultiPointShapeItem::Config config;
        config.closed = shape.type() == ShapeType::Polygon;
        config.interpolated = interpolated;
        if (shape.type() == ShapeType::Skeleton) {
            // Edges and names come from the class's own template, so projects
            // with non-COCO skeletons draw correctly.
            const KeypointTemplate tmpl = schema.templateForClass(shape.labelId());
            config.edges = tmpl.edges;
            config.pointNames = tmpl.pointNames;
            config.showPointNames = m_showPointNames;
        }

        auto *item = new MultiPointShapeItem(shape.points(), shape.visibilityFlags(), i, color, config);
        connect(item, &MultiPointShapeItem::geometryChanged,
                this, &AnnotationGraphicsView::multiPointShapeGeometryChanged);
        connect(item, &MultiPointShapeItem::visibilityChanged,
                this, &AnnotationGraphicsView::shapeVisibilityChanged);
        m_scene->addItem(item);
        m_multiPointShapeItems.append(item);
    }
}

void AnnotationGraphicsView::setShowPointNames(bool show)
{
    m_showPointNames = show;
    for (MultiPointShapeItem *item : m_multiPointShapeItems)
        item->setShowPointNames(show);
}

void AnnotationGraphicsView::fitImageInView()
{
    m_autoFit = true;
    if (!m_imageItem->pixmap().isNull())
        fitInView(m_imageItem, Qt::KeepAspectRatio);
}

void AnnotationGraphicsView::setDrawMode(DrawMode mode)
{
    cancelPendingShape();
    m_drawMode = mode;
    setCursor(mode == DrawMode::None ? Qt::ArrowCursor : Qt::CrossCursor);
}

QRectF AnnotationGraphicsView::rubberBandRect(const QPointF &start, const QPointF &current) const
{
    QRectF rect(start, current);
    if (m_drawMode == DrawMode::Square) {
        const qreal side = qMax(qAbs(rect.width()), qAbs(rect.height()));
        rect.setWidth(rect.width() < 0 ? -side : side);
        rect.setHeight(rect.height() < 0 ? -side : side);
    }
    return rect.normalized();
}

void AnnotationGraphicsView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        switch (m_drawMode) {
        case DrawMode::Rect:
        case DrawMode::Square:
            m_rubberBandStart = mapToScene(event->pos());
            m_rubberBand = new QGraphicsRectItem(QRectF(m_rubberBandStart, QSizeF(0, 0)));
            m_rubberBand->setPen(QPen(Qt::yellow, 1, Qt::DashLine));
            m_scene->addItem(m_rubberBand);
            return;
        case DrawMode::Polygon:
        case DrawMode::Polyline:
            m_pendingPoints.append(mapToScene(event->pos()));
            updatePendingPathItem(mapToScene(event->pos()));
            return;
        case DrawMode::Keypoint:
            emit multiPointShapeDrawn(ShapeType::Keypoint, {mapToScene(event->pos())});
            return;
        case DrawMode::Skeleton: {
            const KeypointTemplate tmpl = m_schema.templateForClass(m_activeLabelId);
            emit multiPointShapeDrawn(ShapeType::Skeleton, tmpl.layoutAt(mapToScene(event->pos())));
            return;
        }
        case DrawMode::None:
            break;
        }
    }
    QGraphicsView::mousePressEvent(event);
}

void AnnotationGraphicsView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_rubberBand) {
        m_rubberBand->setRect(rubberBandRect(m_rubberBandStart, mapToScene(event->pos())));
        return;
    }
    if (!m_pendingPoints.isEmpty()) {
        updatePendingPathItem(mapToScene(event->pos()));
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void AnnotationGraphicsView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_rubberBand) {
        const QRectF finalRect = m_rubberBand->rect();
        m_scene->removeItem(m_rubberBand);
        delete m_rubberBand;
        m_rubberBand = nullptr;

        if (finalRect.width() > 3 && finalRect.height() > 3)
            emit rectDrawn(finalRect);
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void AnnotationGraphicsView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if ((m_drawMode == DrawMode::Polygon || m_drawMode == DrawMode::Polyline) && !m_pendingPoints.isEmpty()) {
        finishPendingShape();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void AnnotationGraphicsView::updatePendingPathItem(const QPointF &cursorPos)
{
    if (m_pendingPoints.isEmpty())
        return;

    if (!m_pendingPathItem) {
        m_pendingPathItem = new QGraphicsPathItem();
        m_pendingPathItem->setPen(QPen(Qt::yellow, 1, Qt::DashLine));
        m_scene->addItem(m_pendingPathItem);
    }

    QPainterPath path(m_pendingPoints.first());
    for (int i = 1; i < m_pendingPoints.size(); ++i)
        path.lineTo(m_pendingPoints.at(i));
    path.lineTo(cursorPos);

    m_pendingPathItem->setPath(path);
}

void AnnotationGraphicsView::finishPendingShape()
{
    if (m_pendingPoints.size() >= 2) {
        const ShapeType type = m_drawMode == DrawMode::Polygon ? ShapeType::Polygon : ShapeType::Polyline;
        emit multiPointShapeDrawn(type, m_pendingPoints);
    }
    cancelPendingShape();
}

void AnnotationGraphicsView::cancelPendingShape()
{
    if (m_pendingPathItem) {
        m_scene->removeItem(m_pendingPathItem);
        delete m_pendingPathItem;
        m_pendingPathItem = nullptr;
    }
    m_pendingPoints.clear();
}

void AnnotationGraphicsView::keyPressEvent(QKeyEvent *event)
{
    if (!m_pendingPoints.isEmpty()) {
        if (event->key() == Qt::Key_Escape) {
            cancelPendingShape();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            finishPendingShape();
            return;
        }
    }

    if (event->key() == Qt::Key_Delete) {
        for (RectShapeItem *item : m_rectShapeItems) {
            if (item->isSelected()) {
                emit shapeDeleteRequested(item->shapeIndex());
                return;
            }
        }
        for (MultiPointShapeItem *item : m_multiPointShapeItems) {
            if (item->isSelected()) {
                emit shapeDeleteRequested(item->shapeIndex());
                return;
            }
        }
    }
    QGraphicsView::keyPressEvent(event);
}

int AnnotationGraphicsView::selectedShapeIndex() const
{
    for (RectShapeItem *item : m_rectShapeItems) {
        if (item->isSelected())
            return item->shapeIndex();
    }
    for (MultiPointShapeItem *item : m_multiPointShapeItems) {
        if (item->isSelected())
            return item->shapeIndex();
    }
    return -1;
}

void AnnotationGraphicsView::selectShape(int shapeIndex)
{
    m_scene->clearSelection();

    for (RectShapeItem *item : m_rectShapeItems) {
        if (item->shapeIndex() == shapeIndex) {
            item->setSelected(true);
            centerOn(item);
        }
    }
    for (MultiPointShapeItem *item : m_multiPointShapeItems) {
        if (item->shapeIndex() == shapeIndex) {
            item->setSelected(true);
            centerOn(item);
        }
    }
}
