#include "annotationgraphicsview.h"
#include "multipointshapeitem.h"
#include "rectshapeitem.h"
#include "theme.h"

#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
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

void AnnotationGraphicsView::clearPredictionItems()
{
    // Plain delete is safe here, unlike in clearShapeItems(): these items are
    // not QObjects and take no mouse input, so no rebuild can be reached from
    // inside one of their own handlers.
    for (QGraphicsItem *item : m_predictionItems) {
        m_scene->removeItem(item);
        delete item;
    }
    m_predictionItems.clear();
}

void AnnotationGraphicsView::setPredictions(const QVector<Prediction> &predictions,
                                            const LabelSchema &schema)
{
    m_predictions = predictions;
    m_schema = schema;
    rebuildPredictionItems();
}

void AnnotationGraphicsView::clearPredictions()
{
    m_predictions.clear();
    m_highlightedPredictionId.clear();
    clearPredictionItems();
}

void AnnotationGraphicsView::setHighlightedPrediction(const QString &predictionId)
{
    if (m_highlightedPredictionId == predictionId)
        return;
    m_highlightedPredictionId = predictionId;
    rebuildPredictionItems();
}

void AnnotationGraphicsView::rebuildPredictionItems()
{
    clearPredictionItems();

    for (const Prediction &prediction : m_predictions) {
        // Colour by the class the prediction would become, so a proposal that
        // maps onto an existing class already looks like it belongs.
        const LabelClass *label = m_schema.findClassByName(prediction.modelClassName);
        const QColor base = label ? label->color : QColor(0xff, 0xa5, 0x00);
        const bool highlighted = !m_highlightedPredictionId.isEmpty()
                                 && prediction.id == m_highlightedPredictionId;

        QPainterPath path;
        if (prediction.hasPolygon()) {
            path.moveTo(prediction.points.first());
            for (int i = 1; i < prediction.points.size(); ++i)
                path.lineTo(prediction.points.at(i));
            path.closeSubpath();
        } else {
            path.addRect(prediction.box);
        }

        QPen pen(base, highlighted ? 3.0 : 1.6);
        pen.setStyle(highlighted ? Qt::SolidLine : Qt::DashLine);
        pen.setCosmetic(true);   // constant on-screen width at any zoom

        QColor fill = base;
        fill.setAlpha(highlighted ? 70 : 36);

        auto *pathItem = new QGraphicsPathItem(path);
        pathItem->setPen(pen);
        pathItem->setBrush(fill);
        // Below annotations, and transparent to the mouse so drawing a new shape
        // over a prediction still works.
        pathItem->setZValue(-0.5);
        pathItem->setAcceptedMouseButtons(Qt::NoButton);
        m_scene->addItem(pathItem);
        m_predictionItems.append(pathItem);

        auto *text = new QGraphicsSimpleTextItem(
            QStringLiteral("%1 %2")
                .arg(prediction.modelClassName.isEmpty() ? QStringLiteral("?")
                                                         : prediction.modelClassName)
                .arg(prediction.score, 0, 'f', 2));
        text->setBrush(base);
        // Ignore transformations so the caption stays readable when zoomed out;
        // otherwise it shrinks into an unreadable smear on a 4K frame.
        text->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        text->setPos(prediction.box.topLeft() - QPointF(0, 2));
        text->setZValue(-0.4);
        text->setAcceptedMouseButtons(Qt::NoButton);
        m_scene->addItem(text);
        m_predictionItems.append(text);
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

    // Leaving SAM mode throws away the half-built prompt: it means nothing to
    // any other tool, and leaving the markers on screen would be misleading.
    if (m_drawMode == DrawMode::Sam && mode != DrawMode::Sam)
        clearSamPrompt();

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

// ---------------------------------------------------------------------------
// SAM prompting
//
// Three gestures share the mouse: a click adds an include point, Shift+click or
// right-click adds an exclude point, and a drag draws a box. Click and drag are
// only distinguishable on release, so a press records where it started and the
// decision is made there.
// ---------------------------------------------------------------------------

bool AnnotationGraphicsView::handleSamPress(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton)
        return false;

    m_samPressPos = mapToScene(event->pos());
    m_samPressWasNegative = event->button() == Qt::RightButton
                            || event->modifiers().testFlag(Qt::ShiftModifier);
    m_samDragging = false;

    // Only a plain left press can grow into a box; the exclude gesture is always
    // a point, so there is nothing to rubber-band.
    if (event->button() == Qt::LeftButton && !m_samPressWasNegative) {
        m_rubberBandStart = m_samPressPos;
        m_rubberBand = new QGraphicsRectItem(QRectF(m_rubberBandStart, QSizeF(0, 0)));
        m_rubberBand->setPen(QPen(QColor(0x4f, 0xc3, 0xf7), 1, Qt::DashLine));
        m_scene->addItem(m_rubberBand);
    }
    return true;
}

bool AnnotationGraphicsView::handleSamRelease(QMouseEvent *event)
{
    QRectF dragged;
    if (m_rubberBand) {
        dragged = m_rubberBand->rect();
        m_scene->removeItem(m_rubberBand);
        delete m_rubberBand;
        m_rubberBand = nullptr;
    }

    const QPointF releasePos = mapToScene(event->pos());

    // A few pixels of travel is a click with a shaky hand, not a box.
    const bool isBox = dragged.width() > 6.0 && dragged.height() > 6.0;

    if (isBox) {
        m_samBox = dragged;
    } else if (m_samPressWasNegative) {
        m_samNegative.append(releasePos);
    } else {
        m_samPositive.append(releasePos);
    }

    m_samDragging = false;
    rebuildSamItems();
    emitSamPrompt();
    return true;
}

void AnnotationGraphicsView::emitSamPrompt()
{
    emit samPromptChanged(m_samPositive, m_samNegative, m_samBox);
}

bool AnnotationGraphicsView::hasSamPrompt() const
{
    return !m_samPositive.isEmpty() || !m_samNegative.isEmpty() || m_samBox.isValid();
}

void AnnotationGraphicsView::setSamPreview(const QVector<QPointF> &polygon)
{
    m_samPreview = polygon;
    rebuildSamItems();
}

void AnnotationGraphicsView::clearSamPrompt()
{
    m_samPositive.clear();
    m_samNegative.clear();
    m_samBox = QRectF();
    m_samPreview.clear();
    clearSamItems();
}

void AnnotationGraphicsView::clearSamItems()
{
    for (QGraphicsItem *item : m_samItems) {
        m_scene->removeItem(item);
        delete item;
    }
    m_samItems.clear();
}

void AnnotationGraphicsView::rebuildSamItems()
{
    clearSamItems();

    // The mask preview, drawn first so the prompt markers sit on top of it.
    if (m_samPreview.size() >= 3) {
        QPainterPath path(m_samPreview.first());
        for (int i = 1; i < m_samPreview.size(); ++i)
            path.lineTo(m_samPreview.at(i));
        path.closeSubpath();

        auto *item = new QGraphicsPathItem(path);
        QPen pen(QColor(0x4f, 0xc3, 0xf7), 2.0);
        pen.setCosmetic(true);
        item->setPen(pen);
        item->setBrush(QColor(0x4f, 0xc3, 0xf7, 80));
        item->setZValue(1.0);
        item->setAcceptedMouseButtons(Qt::NoButton);
        m_scene->addItem(item);
        m_samItems.append(item);
    }

    if (m_samBox.isValid()) {
        auto *item = new QGraphicsRectItem(m_samBox);
        QPen pen(QColor(0xff, 0xd5, 0x4f), 1.5, Qt::DashLine);
        pen.setCosmetic(true);
        item->setPen(pen);
        item->setBrush(Qt::NoBrush);
        item->setZValue(1.1);
        item->setAcceptedMouseButtons(Qt::NoButton);
        m_scene->addItem(item);
        m_samItems.append(item);
    }

    // Prompt markers ignore the view transform, so they stay a usable size when
    // zoomed out on a large frame instead of vanishing.
    const auto addMarker = [this](const QPointF &point, const QColor &fill) {
        constexpr qreal radius = 4.0;
        auto *dot = new QGraphicsEllipseItem(-radius, -radius, radius * 2, radius * 2);
        dot->setBrush(fill);
        dot->setPen(QPen(Qt::white, 1.5));
        dot->setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
        dot->setPos(point);
        dot->setZValue(1.2);
        dot->setAcceptedMouseButtons(Qt::NoButton);
        m_scene->addItem(dot);
        m_samItems.append(dot);
    };

    for (const QPointF &point : m_samPositive)
        addMarker(point, QColor(0x4c, 0xaf, 0x50));
    for (const QPointF &point : m_samNegative)
        addMarker(point, QColor(0xe5, 0x39, 0x35));
}

void AnnotationGraphicsView::mousePressEvent(QMouseEvent *event)
{
    if (m_drawMode == DrawMode::Sam) {
        if (handleSamPress(event))
            return;
    }

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
        case DrawMode::Sam:
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
        if (m_drawMode == DrawMode::Sam)
            m_samDragging = true;
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
    if (m_drawMode == DrawMode::Sam) {
        if (handleSamRelease(event))
            return;
    }

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
    if (m_drawMode == DrawMode::Sam && m_samPreview.size() >= 3) {
        // The press half of this double-click already added a point and asked
        // for a fresh mask; committing the preview we have is what the user
        // means by double-clicking an object.
        emit samCommitRequested();
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
    if (m_drawMode == DrawMode::Sam) {
        switch (event->key()) {
        case Qt::Key_Escape:
            clearSamPrompt();
            emit samCancelled();
            return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (m_samPreview.size() >= 3)
                emit samCommitRequested();
            return;
        case Qt::Key_Backspace:
            // Undo the last thing added, newest first: box, then excludes,
            // then includes — the reverse of the order they are usually given.
            if (m_samBox.isValid())
                m_samBox = QRectF();
            else if (!m_samNegative.isEmpty())
                m_samNegative.removeLast();
            else if (!m_samPositive.isEmpty())
                m_samPositive.removeLast();
            else
                return;

            m_samPreview.clear();
            rebuildSamItems();
            emitSamPrompt();
            return;
        default:
            break;
        }
    }

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
