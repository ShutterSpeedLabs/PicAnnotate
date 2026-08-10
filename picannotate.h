#ifndef PICANNOTATE_H
#define PICANNOTATE_H

#include <QMainWindow>
#include <QPointF>
#include <QRectF>
#include <QStringListModel>
#include <QVector>

#include "core/annoshape.h"
#include "core/project.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class PicAnnotate;
}
QT_END_NAMESPACE

class AnnotationController;
class AutoAnnotator;
class ClipController;
class LabelPanel;
class FrameNavigator;
class PredictionController;
class PredictionPanel;
class PredictionStore;
class SamController;
class TrackPanel;
class TrackController;
class QDockWidget;
struct IoReport;

class PicAnnotate : public QMainWindow
{
    Q_OBJECT

public:
    PicAnnotate(QWidget *parent = nullptr);
    ~PicAnnotate();

protected:
    // Saves the window and dock arrangement on the way out. Exit goes through
    // close() so this runs for both the menu item and the title-bar button.
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_actionOpen_Folder_triggered();
    void on_actionOpen_Image_triggered();
    void on_actionOpen_Video_triggered();
    void on_actionOpen_Annotation_triggered();
    void on_actionImport_Dataset_triggered();
    void on_actionExport_Dataset_triggered();
    void on_actionNext_Frame_triggered();
    void on_actionPrevious_Frame_triggered();
    void on_actionExit_triggered();
    void on_actionZoom_In_triggered();
    void on_actionZoom_Out_triggered();
    void on_actionFit_Window_triggered();
    void on_actionReset_Layout_triggered();
    void on_actionShow_Keypoint_Names_toggled(bool checked);
    void on_actionSave_triggered();
    void on_actionSave_Directory_triggered();
    void on_actionClear_Frame_Annotations_triggered();
    void on_actionModel_Manager_triggered();
    void on_actionCreate_Dataset_triggered();

    void onDrawToolTriggered();
    void onRectDrawn(const QRectF &imageRect);
    void onMultiPointShapeDrawn(ShapeType type, const QVector<QPointF> &points);
    void onShapeGeometryChanged(int shapeIndex, const QRectF &imageRect);
    void onShapeRotationChanged(int shapeIndex, double degrees);
    void onMultiPointShapeGeometryChanged(int shapeIndex, const QVector<QPointF> &points);
    void onShapeVisibilityChanged(int shapeIndex, const QVector<int> &visibility);
    void onShapeDeleteRequested(int shapeIndex);
    void onFrameAnnotationsChanged(int frameIndex);
    void onTracksChanged();
    void onFrameRequested(int index);
    void onStartTrackingRequested();
    void onTrackSelected(int trackId);
    void onDeleteTrackRequested(int trackId);
    void onDeleteKeyframeRequested(int trackId, int frameIndex);
    void onEndTrackRequested(int trackId, int frameIndex);
    void onToggleOutsideRequested(int trackId, int frameIndex);
    void onToggleOccludedRequested(int trackId, int frameIndex);
    void onFileListContextMenuRequested(const QPoint &pos);
    void onFileListItemClicked(const QModelIndex &index);
    void onAnnotationListItemClicked(const QModelIndex &index);
    void onCurrentLabelChanged(int labelId);
    void onRemoveClassRequested(int labelId);
    void onLabelSchemaChanged();
    void onTrackingStarted(int trackId);
    void onTrackingStopped();

    // ---- model-assisted annotation -----------------------------------------
    void onRunModelOnFrameRequested();
    void onRunModelOnAllFramesRequested();
    void onPredictionRunFinished(int framesProcessed, int totalDetections, const QString &error);
    void onPredictionsChangedForFrame(int frameIndex);
    void onAcceptPredictionRequested(int frameIndex, const QString &predictionId);
    void onRejectPredictionRequested(int frameIndex, const QString &predictionId);
    void onAcceptFramePredictionsRequested(int frameIndex);
    void onAcceptAllPredictionsRequested();
    void onRejectAllPredictionsRequested();

    // ---- interactive SAM ----------------------------------------------------
    void onSamToolToggled(bool checked);
    void onSamPromptChanged(const QVector<QPointF> &positivePoints,
                            const QVector<QPointF> &negativePoints, const QRectF &box);
    void onSamCommitRequested();
    void onSamCancelled();
    void onSamActiveChanged(bool active);
    void onSamFailed(const QString &message);

    // ---- CLIP zero-shot classification --------------------------------------
    void onClassifySelectedShapeTriggered();
    void onClassifyUnlabelledOnFrameTriggered();
    void onClipFinished(int classified, int skipped, const QString &error);

private:
    void setupDocks();
    void setupPanelsMenu();
    void saveLayout();
    void restoreLayout();

    void displayCurrentFrame();
    void refreshShapes();
    void refreshTrackPanel();
    void refreshPredictions();

    // Locks out everything that would move the playhead or edit annotations
    // while a batch model run owns them.
    void setModelRunUiActive(bool active);

    // Locks out every control that would move the playhead while a tracking run
    // owns it — playback, scrubbing, the frame list and the step actions.
    void setTrackingUiActive(bool active);
    void onSourceOpened();
    void showIoReport(const QString &title, const IoReport &report);

    Ui::PicAnnotate *ui;
    Project project;
    QStringListModel fileListModel;
    QStringListModel annotationListModel;
    AnnotationController *annotations;
    LabelPanel *labelPanel;
    FrameNavigator *frameNavigator;
    TrackPanel *trackPanel;
    TrackController *trackController;

    PredictionStore *predictionStore;
    PredictionController *predictionController;
    AutoAnnotator *autoAnnotator;
    PredictionPanel *predictionPanel;
    SamController *samController;
    ClipController *clipController;

    QDockWidget *labelDock = nullptr;
    QDockWidget *trackDock = nullptr;
    QDockWidget *navigatorDock = nullptr;
    QDockWidget *predictionDock = nullptr;

    // Captured once the default arrangement is built, so Reset Layout has
    // something to go back to after the user has rearranged things.
    QByteArray defaultLayoutState;
    QByteArray defaultLayoutGeometry;
};
#endif // PICANNOTATE_H
