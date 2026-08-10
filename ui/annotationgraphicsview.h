#ifndef ANNOTATIONGRAPHICSVIEW_H
#define ANNOTATIONGRAPHICSVIEW_H

#include <QGraphicsView>

#include "../ai/prediction.h"
#include "../core/annoshape.h"
#include "../core/labelschema.h"

class QGraphicsItem;
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
        Skeleton,

        // Click-to-segment. Unlike the others this mode does not produce a shape
        // directly: it accumulates a prompt, something else turns that into a
        // mask, and the result is committed explicitly.
        Sam
    };

    explicit AnnotationGraphicsView(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void setShapes(const QVector<AnnoShape> &shapes, const LabelSchema &schema);

    // Model output, drawn dashed and translucent underneath the real shapes and
    // deliberately not editable. A prediction is a proposal, so it must be
    // visibly different from an annotation and must not be draggable — dragging
    // one would imply an edit that has nowhere to be stored.
    void setPredictions(const QVector<Prediction> &predictions, const LabelSchema &schema);
    void clearPredictions();

    // Draws one prediction emphasised, following the panel's selection.
    void setHighlightedPrediction(const QString &predictionId);

    // ---- SAM prompting ------------------------------------------------------

    // The live mask for the current prompt. Empty hides the preview.
    void setSamPreview(const QVector<QPointF> &polygon);

    // Drops the accumulated prompt and the preview without emitting anything.
    void clearSamPrompt();
    bool hasSamPrompt() const;

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

    // The SAM prompt changed and a new mask should be computed. Coordinates are
    // in image pixels.
    void samPromptChanged(const QVector<QPointF> &positivePoints,
                          const QVector<QPointF> &negativePoints, const QRectF &box);

    // The user accepted the previewed mask (Enter, or double-click).
    void samCommitRequested();

    // The user abandoned the prompt (Escape).
    void samCancelled();

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
    void clearPredictionItems();
    void rebuildPredictionItems();

    // ---- SAM prompting ------------------------------------------------------
    bool handleSamPress(QMouseEvent *event);
    bool handleSamRelease(QMouseEvent *event);
    void rebuildSamItems();
    void clearSamItems();
    void emitSamPrompt();

    QGraphicsScene *m_scene;
    QGraphicsPixmapItem *m_imageItem;
    QVector<RectShapeItem *> m_rectShapeItems;
    QVector<MultiPointShapeItem *> m_multiPointShapeItems;

    // Predictions are plain path/text items rather than shape items: they carry
    // no handles, no drag behaviour and no index into the annotation list.
    QVector<QGraphicsItem *> m_predictionItems;
    QVector<Prediction> m_predictions;
    QString m_highlightedPredictionId;

    // ---- SAM prompting ------------------------------------------------------
    QVector<QPointF> m_samPositive;
    QVector<QPointF> m_samNegative;
    QRectF m_samBox;
    QVector<QPointF> m_samPreview;
    QVector<QGraphicsItem *> m_samItems;

    // A press only becomes a point on release, and only if the mouse barely
    // moved: the same gesture starts a box drag, and deciding at press time
    // would make every box also drop a stray point.
    QPointF m_samPressPos;
    bool m_samPressWasNegative = false;
    bool m_samDragging = false;

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
