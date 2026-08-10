#include "picannotate.h"
#include "ui_picannotate.h"

#include "ai/autoannotator.h"
#include "ai/clipcontroller.h"
#include "ai/predictioncontroller.h"
#include "ai/predictionstore.h"
#include "ai/samcontroller.h"
#include "core/annotationcontroller.h"
#include "core/annoshape.h"
#include "core/frameannotations.h"
#include "io/datasetimporter.h"
#include "io/formatregistry.h"
#include "trackers/trackcontroller.h"
#include "ui/annotationgraphicsview.h"
#include "ui/datasetwizard.h"
#include "ui/exportdatasetdialog.h"
#include "ui/framenavigator.h"
#include "ui/importdatasetdialog.h"
#include "ui/labelpanel.h"
#include "ui/modelmanagerdialog.h"
#include "ui/predictionpanel.h"
#include "ui/recentpaths.h"
#include "ui/theme.h"
#include "ui/trackpanel.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QUndoStack>

#include <utility>

namespace {
QString shapeTypeName(ShapeType type)
{
    switch (type) {
    case ShapeType::Rect:
        return "RectBox";
    case ShapeType::Polygon:
        return "Polygon";
    case ShapeType::Polyline:
        return "Polyline";
    case ShapeType::Keypoint:
        return "Keypoint";
    case ShapeType::Skeleton:
        return "Skeleton";
    }
    return "Shape";
}
} // namespace

PicAnnotate::PicAnnotate(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::PicAnnotate)
    , annotations(new AnnotationController(&project, this))
    , labelPanel(new LabelPanel(this))
    , frameNavigator(new FrameNavigator(this))
    , trackPanel(new TrackPanel(this))
    , trackController(new TrackController(&project, annotations, this))
    , predictionStore(new PredictionStore(this))
    , predictionController(new PredictionController(&project, annotations, predictionStore, this))
    , autoAnnotator(new AutoAnnotator(&project, predictionStore, this))
    , predictionPanel(new PredictionPanel(this))
    , samController(new SamController(&project, this))
    , clipController(new ClipController(&project, annotations, this))
{
    ui->setupUi(this);

    setupDocks();
    labelPanel->setLabelSchema(&project.labelSchema());
    predictionPanel->setStore(predictionStore);
    predictionPanel->setLabelSchema(&project.labelSchema());

    // Undo/redo come from the command stack itself, so their text tracks the
    // action that would be undone ("Undo Add polygon").
    QAction *undoAction = annotations->createUndoAction(this);
    QAction *redoAction = annotations->createRedoAction(this);
    ui->menuEdit->insertAction(ui->menuEdit->actions().value(0), undoAction);
    ui->menuEdit->insertAction(ui->menuEdit->actions().value(1), redoAction);
    ui->menuEdit->insertSeparator(ui->menuEdit->actions().value(2));
    addAction(undoAction);
    addAction(redoAction);

    ui->toolBarFile->addSeparator();
    ui->toolBarFile->addAction(undoAction);
    ui->toolBarFile->addAction(redoAction);
    ui->toolBarFile->addSeparator();
    ui->toolBarFile->addAction(ui->actionImport_Dataset);
    ui->toolBarFile->addAction(ui->actionExport_Dataset);

    for (QAction *action : {ui->actionRectBox, ui->actionSquare, ui->actionPolyline,
                            ui->actionPolygone, ui->actionKeypoint, ui->actionSketlton}) {
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, &PicAnnotate::onDrawToolTriggered);
    }

    connect(ui->graphicsView, &AnnotationGraphicsView::rectDrawn, this, &PicAnnotate::onRectDrawn);
    connect(ui->graphicsView, &AnnotationGraphicsView::multiPointShapeDrawn, this, &PicAnnotate::onMultiPointShapeDrawn);
    connect(ui->graphicsView, &AnnotationGraphicsView::shapeGeometryChanged, this, &PicAnnotate::onShapeGeometryChanged);
    connect(ui->graphicsView, &AnnotationGraphicsView::shapeRotationChanged, this, &PicAnnotate::onShapeRotationChanged);
    connect(ui->graphicsView, &AnnotationGraphicsView::multiPointShapeGeometryChanged, this, &PicAnnotate::onMultiPointShapeGeometryChanged);
    connect(ui->graphicsView, &AnnotationGraphicsView::shapeVisibilityChanged, this, &PicAnnotate::onShapeVisibilityChanged);
    connect(ui->graphicsView, &AnnotationGraphicsView::shapeDeleteRequested, this, &PicAnnotate::onShapeDeleteRequested);
    connect(annotations, &AnnotationController::frameAnnotationsChanged, this, &PicAnnotate::onFrameAnnotationsChanged);
    connect(annotations, &AnnotationController::tracksChanged, this, &PicAnnotate::onTracksChanged);
    connect(labelPanel, &LabelPanel::currentLabelChanged, this, &PicAnnotate::onCurrentLabelChanged);
    // Class colours and names are baked into the drawn shapes and the annotation
    // list, so both have to be rebuilt when the schema changes.
    connect(labelPanel, &LabelPanel::labelsChanged, this, &PicAnnotate::refreshShapes);
    connect(labelPanel, &LabelPanel::removeClassRequested, this, &PicAnnotate::onRemoveClassRequested);
    connect(annotations, &AnnotationController::labelSchemaChanged, this, &PicAnnotate::onLabelSchemaChanged);
    connect(frameNavigator, &FrameNavigator::frameRequested, this, &PicAnnotate::onFrameRequested);
    connect(trackPanel, &TrackPanel::startTrackingRequested, this, &PicAnnotate::onStartTrackingRequested);
    connect(trackPanel, &TrackPanel::stopTrackingRequested, trackController, &TrackController::stop);
    connect(trackPanel, &TrackPanel::frameRequested, this, &PicAnnotate::onFrameRequested);
    connect(trackPanel, &TrackPanel::trackSelected, this, &PicAnnotate::onTrackSelected);
    connect(trackPanel, &TrackPanel::deleteTrackRequested, this, &PicAnnotate::onDeleteTrackRequested);
    connect(trackPanel, &TrackPanel::deleteKeyframeRequested, this, &PicAnnotate::onDeleteKeyframeRequested);
    connect(trackPanel, &TrackPanel::endTrackRequested, this, &PicAnnotate::onEndTrackRequested);
    connect(trackPanel, &TrackPanel::toggleOutsideRequested, this, &PicAnnotate::onToggleOutsideRequested);
    connect(trackPanel, &TrackPanel::toggleOccludedRequested, this, &PicAnnotate::onToggleOccludedRequested);
    connect(trackController, &TrackController::frameAdvanced, this, &PicAnnotate::displayCurrentFrame);
    connect(trackController, &TrackController::statusChanged, trackPanel, &TrackPanel::setStatusText);
    connect(trackController, &TrackController::trackingStarted, this, &PicAnnotate::onTrackingStarted);
    connect(trackController, &TrackController::trackingStopped, this, &PicAnnotate::onTrackingStopped);

    connect(predictionPanel, &PredictionPanel::runOnFrameRequested,
            this, &PicAnnotate::onRunModelOnFrameRequested);
    connect(predictionPanel, &PredictionPanel::runOnAllFramesRequested,
            this, &PicAnnotate::onRunModelOnAllFramesRequested);
    connect(predictionPanel, &PredictionPanel::cancelRunRequested,
            autoAnnotator, &AutoAnnotator::cancel);
    connect(predictionPanel, &PredictionPanel::acceptRequested,
            this, &PicAnnotate::onAcceptPredictionRequested);
    connect(predictionPanel, &PredictionPanel::rejectRequested,
            this, &PicAnnotate::onRejectPredictionRequested);
    connect(predictionPanel, &PredictionPanel::acceptFrameRequested,
            this, &PicAnnotate::onAcceptFramePredictionsRequested);
    connect(predictionPanel, &PredictionPanel::rejectFrameRequested,
            predictionController, &PredictionController::rejectFrame);
    connect(predictionPanel, &PredictionPanel::acceptAllRequested,
            this, &PicAnnotate::onAcceptAllPredictionsRequested);
    connect(predictionPanel, &PredictionPanel::rejectAllRequested,
            this, &PicAnnotate::onRejectAllPredictionsRequested);
    connect(predictionPanel, &PredictionPanel::scoreThresholdChanged,
            predictionStore, &PredictionStore::setScoreThreshold);
    connect(predictionPanel, &PredictionPanel::taskChanged,
            autoAnnotator, &AutoAnnotator::setTask);
    connect(predictionPanel, &PredictionPanel::createMissingClassesChanged, this,
            [this](bool enabled) { predictionController->options().createMissingClasses = enabled; });
    connect(predictionPanel, &PredictionPanel::predictionSelected,
            ui->graphicsView, &AnnotationGraphicsView::setHighlightedPrediction);

    connect(autoAnnotator, &AutoAnnotator::started, predictionPanel, &PredictionPanel::onRunStarted);
    connect(autoAnnotator, &AutoAnnotator::started, this,
            [this] { setModelRunUiActive(true); });
    connect(autoAnnotator, &AutoAnnotator::progress, predictionPanel,
            &PredictionPanel::onRunProgress);
    connect(autoAnnotator, &AutoAnnotator::finished, this,
            &PicAnnotate::onPredictionRunFinished);

    connect(predictionStore, &PredictionStore::framePredictionsChanged,
            this, &PicAnnotate::onPredictionsChangedForFrame);
    connect(predictionStore, &PredictionStore::storeCleared,
            this, &PicAnnotate::refreshPredictions);
    connect(predictionStore, &PredictionStore::scoreThresholdChanged, this,
            [this](double) { refreshPredictions(); });

    ui->menuAI->insertAction(ui->actionModel_Manager, ui->actionDetect_On_Frame);
    ui->menuAI->insertAction(ui->actionModel_Manager, ui->actionDetect_On_All_Frames);
    ui->menuAI->insertAction(ui->actionModel_Manager, ui->actionAccept_All_Predictions);
    ui->menuAI->insertAction(ui->actionModel_Manager, ui->actionClear_Predictions);
    ui->menuAI->insertSeparator(ui->actionModel_Manager);

    connect(ui->actionDetect_On_Frame, &QAction::triggered,
            this, &PicAnnotate::onRunModelOnFrameRequested);
    connect(ui->actionDetect_On_All_Frames, &QAction::triggered,
            this, &PicAnnotate::onRunModelOnAllFramesRequested);
    connect(ui->actionAccept_All_Predictions, &QAction::triggered,
            this, &PicAnnotate::onAcceptAllPredictionsRequested);
    connect(ui->actionClear_Predictions, &QAction::triggered,
            this, &PicAnnotate::onRejectAllPredictionsRequested);

    // SAM is a draw tool rather than a menu command: it stays on while the user
    // works through a frame, so it belongs beside the rectangle and polygon
    // tools and has to be mutually exclusive with them.
    ui->actionSAM_Segment->setCheckable(true);
    ui->menuAnnotate->addSeparator();
    ui->menuAnnotate->addAction(ui->actionSAM_Segment);
    ui->toolBarTools->insertAction(ui->actionPrevious_Frame, ui->actionSAM_Segment);
    ui->toolBarTools->insertSeparator(ui->actionPrevious_Frame);
    connect(ui->actionSAM_Segment, &QAction::toggled, this, &PicAnnotate::onSamToolToggled);

    connect(ui->graphicsView, &AnnotationGraphicsView::samPromptChanged,
            this, &PicAnnotate::onSamPromptChanged);
    connect(ui->graphicsView, &AnnotationGraphicsView::samCommitRequested,
            this, &PicAnnotate::onSamCommitRequested);
    connect(ui->graphicsView, &AnnotationGraphicsView::samCancelled,
            this, &PicAnnotate::onSamCancelled);

    connect(samController, &SamController::previewChanged, this,
            [this](const QVector<QPointF> &polygon, float) {
                ui->graphicsView->setSamPreview(polygon);
            });
    connect(samController, &SamController::previewCleared, this,
            [this] { ui->graphicsView->setSamPreview({}); });
    connect(samController, &SamController::statusChanged, this, [this](const QString &text) {
        if (!text.isEmpty())
            statusBar()->showMessage(text);
        else
            statusBar()->clearMessage();
    });
    connect(samController, &SamController::activeChanged, this, &PicAnnotate::onSamActiveChanged);
    connect(samController, &SamController::failed, this, &PicAnnotate::onSamFailed);

    ui->menuAI->insertAction(ui->actionModel_Manager, ui->actionClassify_Shape);
    ui->menuAI->insertAction(ui->actionModel_Manager, ui->actionClassify_Unlabelled);
    ui->menuAI->insertSeparator(ui->actionModel_Manager);
    connect(ui->actionClassify_Shape, &QAction::triggered,
            this, &PicAnnotate::onClassifySelectedShapeTriggered);
    connect(ui->actionClassify_Unlabelled, &QAction::triggered,
            this, &PicAnnotate::onClassifyUnlabelledOnFrameTriggered);

    connect(clipController, &ClipController::statusChanged, this, [this](const QString &text) {
        if (text.isEmpty())
            statusBar()->clearMessage();
        else
            statusBar()->showMessage(text);
    });
    connect(clipController, &ClipController::finished, this, &PicAnnotate::onClipFinished);
    connect(clipController, &ClipController::failed, this, [this](const QString &message) {
        QMessageBox::warning(this, "Classify", message);
    });
    // Renaming or removing a class changes what CLIP should score against, so
    // the cached text embeddings have to go.
    connect(annotations, &AnnotationController::labelSchemaChanged,
            clipController, &ClipController::invalidateCandidates);
    connect(labelPanel, &LabelPanel::labelsChanged,
            clipController, &ClipController::invalidateCandidates);

    ui->listViewFiles->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->listViewFiles, &QListView::customContextMenuRequested,
            this, &PicAnnotate::onFileListContextMenuRequested);
    connect(ui->listViewFiles, &QListView::clicked, this, &PicAnnotate::onFileListItemClicked);

    ui->listViewClass->setModel(&annotationListModel);
    connect(ui->listViewClass, &QListView::clicked, this, &PicAnnotate::onAnnotationListItemClicked);

    setupPanelsMenu();

    // Snapshot the arrangement built above before any saved state is applied, so
    // Reset Layout always has a known-good target.
    defaultLayoutState = saveState();
    defaultLayoutGeometry = saveGeometry();
    restoreLayout();

    Theme::applyWindowChrome(this);
}

PicAnnotate::~PicAnnotate()
{
    delete ui;
}

void PicAnnotate::setupDocks()
{
    // The canvas expands without limit, so without a floor on the side panels
    // QMainWindow squeezes the dock column down to almost nothing. Conversely the
    // panels must be allowed to get short, or the sum of their minimum heights
    // pushes the window's minimum past the screen height.
    constexpr int kPanelMinWidth = 230;
    for (QWidget *panel : {static_cast<QWidget *>(labelPanel),
                           static_cast<QWidget *>(trackPanel),
                           static_cast<QWidget *>(predictionPanel),
                           static_cast<QWidget *>(ui->listViewClass),
                           static_cast<QWidget *>(ui->listViewFiles)}) {
        panel->setMinimumWidth(kPanelMinWidth);
    }
    ui->listViewClass->setMinimumHeight(60);
    ui->listViewFiles->setMinimumHeight(60);
    ui->graphicsView->setMinimumSize(320, 240);

    labelDock = new QDockWidget(QStringLiteral("Labels"), this);
    labelDock->setObjectName(QStringLiteral("dockLabels"));
    labelDock->setWidget(labelPanel);

    trackDock = new QDockWidget(QStringLiteral("Tracking"), this);
    trackDock->setObjectName(QStringLiteral("dockTracking"));
    trackDock->setWidget(trackPanel);

    predictionDock = new QDockWidget(QStringLiteral("Predictions"), this);
    predictionDock->setObjectName(QStringLiteral("dockPredictions"));
    predictionDock->setWidget(predictionPanel);

    navigatorDock = new QDockWidget(QStringLiteral("Navigator"), this);
    navigatorDock->setObjectName(QStringLiteral("dockNavigator"));
    navigatorDock->setWidget(frameNavigator);
    navigatorDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    navigatorDock->setTitleBarWidget(new QWidget(navigatorDock));

    // The frame navigator spans the full width, so it is added first and claims
    // the whole bottom edge rather than sitting beside the side panels.
    addDockWidget(Qt::BottomDockWidgetArea, navigatorDock);

    // Stack the right-hand panels top to bottom. splitDockWidget is what fixes
    // the old layout: every side panel is now owned by the dock manager instead
    // of two of them competing with the canvas for the central widget's space.
    addDockWidget(Qt::RightDockWidgetArea, labelDock);
    splitDockWidget(labelDock, ui->dockAnnotations, Qt::Vertical);
    splitDockWidget(ui->dockAnnotations, ui->dockFiles, Qt::Vertical);

    // Frames, Tracking and Predictions are rarely needed at once, so they share
    // one slot. Frames stays on top: it is what a new user needs first.
    tabifyDockWidget(ui->dockFiles, trackDock);
    tabifyDockWidget(trackDock, predictionDock);
    ui->dockFiles->raise();

    // resizeDocks() is only honoured once the window has been laid out, so the
    // initial panel sizes are applied after the event loop starts rather than
    // here, where the call is silently ignored.
    QTimer::singleShot(0, this, [this] {
        resizeDocks({labelDock, ui->dockAnnotations, ui->dockFiles},
                    {160, 220, 300}, Qt::Vertical);
        resizeDocks({labelDock}, {300}, Qt::Horizontal);
    });
}

void PicAnnotate::setupPanelsMenu()
{
    // Each dock contributes its own show/hide action, so a panel closed by
    // accident can always be brought back.
    for (QDockWidget *dock : {labelDock, ui->dockAnnotations, ui->dockFiles, trackDock,
                              predictionDock}) {
        ui->menuPanels->addAction(dock->toggleViewAction());
    }

    ui->menuPanels->addSeparator();
    ui->menuPanels->addAction(ui->toolBarFile->toggleViewAction());
    ui->menuPanels->addAction(ui->toolBarTools->toggleViewAction());
}

void PicAnnotate::saveLayout()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("MainWindow"));
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("state"), saveState());
    settings.endGroup();
}

void PicAnnotate::restoreLayout()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("MainWindow"));
    const QByteArray geometry = settings.value(QStringLiteral("geometry")).toByteArray();
    const QByteArray state = settings.value(QStringLiteral("state")).toByteArray();
    settings.endGroup();

    if (geometry.isEmpty()) {
        // First run: start maximised instead of trusting the designer's size,
        // which is not guaranteed to fit the panels on a small display. Set the
        // state rather than calling showMaximized(), since main() shows us.
        setWindowState(windowState() | Qt::WindowMaximized);
        return;
    }

    restoreGeometry(geometry);
    if (!state.isEmpty())
        restoreState(state);
}

void PicAnnotate::closeEvent(QCloseEvent *event)
{
    saveLayout();
    QMainWindow::closeEvent(event);
}

void PicAnnotate::displayCurrentFrame()
{
    ui->graphicsView->setImage(project.currentFrame());
    frameNavigator->setCurrentFrame(project.currentIndex());
    refreshShapes();

    // SAM's embedding belongs to one frame, so moving the playhead invalidates
    // it and starts encoding the new one.
    ui->graphicsView->clearSamPrompt();
    samController->onCurrentFrameChanged();
}

void PicAnnotate::refreshShapes()
{
    // The resolved view: shapes drawn on this frame plus every track that is
    // live here, interpolated included. Indices below match what the view shows,
    // which is what the edit handlers address.
    const QVector<AnnoShape> shapes = project.resolvedShapes(project.currentIndex());
    ui->graphicsView->setShapes(shapes, project.labelSchema());

    QStringList entries;
    entries.reserve(shapes.size());
    for (int i = 0; i < shapes.size(); ++i) {
        const AnnoShape &shape = shapes.at(i);
        const LabelClass *label = project.labelSchema().findClass(shape.labelId());

        QString suffix;
        if (shape.trackId() >= 0) {
            suffix = QString(" · track %1%2")
                         .arg(shape.trackId())
                         .arg(shape.source() == ShapeSource::Interpolated ? " (interp)" : " (key)");
        }

        entries << QString("%1: %2 — %3%4")
                       .arg(i)
                       .arg(shapeTypeName(shape.type()))
                       .arg(label ? label->name : "Unlabeled")
                       .arg(suffix);
    }
    annotationListModel.setStringList(entries);

    refreshTrackPanel();
    refreshPredictions();
}

void PicAnnotate::refreshTrackPanel()
{
    trackPanel->refresh(&project.tracks(), &project.labelSchema(), project.currentIndex());
}

void PicAnnotate::refreshPredictions()
{
    const int frameIndex = project.currentIndex();
    predictionPanel->setCurrentFrame(frameIndex);
    predictionPanel->refresh();

    ui->graphicsView->setPredictions(
        frameIndex >= 0 ? predictionStore->visiblePredictions(frameIndex) : QVector<Prediction>(),
        project.labelSchema());
}

void PicAnnotate::onFrameAnnotationsChanged(int frameIndex)
{
    // Undo can target a frame the user has navigated away from; only redraw when
    // the change touches what is on screen.
    if (frameIndex == project.currentIndex())
        refreshShapes();
}

void PicAnnotate::onTracksChanged()
{
    // A track edit can change any frame it spans, so there is no cheap test for
    // whether the current frame was affected.
    refreshShapes();
}

void PicAnnotate::onSourceOpened()
{
    fileListModel.setStringList(project.fileNames());
    ui->listViewFiles->setModel(&fileListModel);
    frameNavigator->setFrameCount(project.frameCount());
    frameNavigator->setPlaybackFps(project.frameRate());
    labelPanel->refresh();
    // History from the previous source would undo into annotations that no
    // longer have a frame to sit on.
    annotations->clearHistory();
    // Predictions are indexed by frame number, so keeping them across a source
    // change would paint the old video's boxes onto the new one.
    predictionStore->clearAll();
    ui->graphicsView->setActiveLabelId(labelPanel->currentLabelId());
    displayCurrentFrame();
}

void PicAnnotate::onCurrentLabelChanged(int labelId)
{
    ui->graphicsView->setActiveLabelId(labelId);
}

void PicAnnotate::onRemoveClassRequested(int labelId)
{
    const LabelClass *label = project.labelSchema().findClass(labelId);
    if (!label)
        return;

    const AnnotationController::LabelUsage usage = annotations->labelUsage(labelId);

    // Deleting a class in use destroys annotation work, so say exactly how much
    // before doing it. An unused class just goes.
    if (usage.isUsed()) {
        QStringList parts;
        if (usage.frameShapes > 0) {
            parts << QString("%1 shape%2 on %3 frame%4")
                         .arg(usage.frameShapes)
                         .arg(usage.frameShapes == 1 ? "" : "s")
                         .arg(usage.frames)
                         .arg(usage.frames == 1 ? "" : "s");
        }
        if (usage.tracks > 0) {
            parts << QString("%1 track%2").arg(usage.tracks).arg(usage.tracks == 1 ? "" : "s");
        }

        const auto reply = QMessageBox::warning(
            this, "Remove Class",
            QString("\"%1\" is used by %2.\n\nRemoving the class deletes them as well. "
                    "This can be undone.")
                .arg(label->name, parts.join(" and ")),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (reply != QMessageBox::Yes)
            return;
    }

    annotations->removeLabelClass(labelId);
}

void PicAnnotate::onLabelSchemaChanged()
{
    // Reached on undo/redo too, so the panel is rebuilt from the schema rather
    // than assumed to already match it.
    labelPanel->refresh();
    ui->graphicsView->setActiveLabelId(labelPanel->currentLabelId());
    refreshShapes();
}

void PicAnnotate::setTrackingUiActive(bool active)
{
    frameNavigator->setInteractionEnabled(!active);
    trackPanel->setTrackingActive(active);

    // The frame list and the step actions move the playhead too, and the tracker
    // assumes it advances one frame at a time.
    ui->listViewFiles->setEnabled(!active);
    ui->actionNext_Frame->setEnabled(!active);
    ui->actionPrevious_Frame->setEnabled(!active);
}

void PicAnnotate::onTrackingStarted(int trackId)
{
    Q_UNUSED(trackId);
    setTrackingUiActive(true);
}

void PicAnnotate::onTrackingStopped()
{
    setTrackingUiActive(false);
}

void PicAnnotate::setModelRunUiActive(bool active)
{
    // A batch run drives the decoder frame by frame, so anything that also moves
    // the playhead has to stand down — same contract as a tracking run.
    frameNavigator->setInteractionEnabled(!active);
    ui->listViewFiles->setEnabled(!active);
    ui->actionNext_Frame->setEnabled(!active);
    ui->actionPrevious_Frame->setEnabled(!active);
    ui->actionDetect_On_Frame->setEnabled(!active);
    ui->actionDetect_On_All_Frames->setEnabled(!active);
    trackPanel->setTrackingActive(active);
}

void PicAnnotate::onRunModelOnFrameRequested()
{
    if (!project.hasSource()) {
        QMessageBox::information(this, "Run Model", "Open a source first.");
        return;
    }

    autoAnnotator->setTask(predictionPanel->selectedTask());

    QString error;
    if (!autoAnnotator->runOnFrame(project.currentIndex(), &error))
        QMessageBox::warning(this, "Run Model", error);
}

void PicAnnotate::onRunModelOnAllFramesRequested()
{
    if (!project.hasSource()) {
        QMessageBox::information(this, "Run Model", "Open a source first.");
        return;
    }

    const int frameCount = project.frameCount();

    // On a long video this is minutes of CPU, so say how long the queue is
    // before starting rather than appearing to hang.
    if (frameCount > 50) {
        const auto reply = QMessageBox::question(
            this, "Run Model",
            QString("Run the model over all %1 frames?\n\n"
                    "This runs on the CPU and can take a while. You can cancel it from "
                    "the Predictions panel, and nothing is written to your annotations "
                    "until you accept the results.")
                .arg(frameCount),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
        if (reply != QMessageBox::Yes)
            return;
    }

    QList<int> frames;
    frames.reserve(frameCount);
    for (int i = 0; i < frameCount; ++i)
        frames.append(i);

    autoAnnotator->setTask(predictionPanel->selectedTask());

    QString error;
    if (!autoAnnotator->runOnFrames(frames, &error))
        QMessageBox::warning(this, "Run Model", error);
}

void PicAnnotate::onPredictionRunFinished(int framesProcessed, int totalDetections,
                                          const QString &error)
{
    setModelRunUiActive(false);
    predictionPanel->onRunFinished(framesProcessed, totalDetections, error);

    // The run may have left the decoder on a different frame than the one on
    // screen; put the display back in step and show whatever landed here.
    displayCurrentFrame();

    if (!error.isEmpty())
        QMessageBox::warning(this, "Run Model", error);
}

void PicAnnotate::onPredictionsChangedForFrame(int frameIndex)
{
    // A batch run touches frames the user is not looking at; only redraw the
    // canvas when the change is visible.
    if (frameIndex == project.currentIndex())
        refreshPredictions();
    else
        predictionPanel->refresh();
}

void PicAnnotate::onAcceptPredictionRequested(int frameIndex, const QString &predictionId)
{
    if (!predictionController->acceptOne(frameIndex, predictionId)) {
        QMessageBox::information(
            this, "Accept Prediction",
            "That prediction could not be accepted. Its class has no match in the label "
            "schema, and \"Create missing classes on accept\" is off.");
    }
}

void PicAnnotate::onRejectPredictionRequested(int frameIndex, const QString &predictionId)
{
    predictionController->rejectOne(frameIndex, predictionId);
}

void PicAnnotate::onAcceptFramePredictionsRequested(int frameIndex)
{
    const int visible = predictionStore->visibleCount(frameIndex);
    const int taken = predictionController->acceptFrame(frameIndex);

    if (taken < visible) {
        QMessageBox::information(
            this, "Accept Predictions",
            QString("Accepted %1 of %2. The rest have classes with no match in the label "
                    "schema, and \"Create missing classes on accept\" is off.")
                .arg(taken)
                .arg(visible));
    }
}

void PicAnnotate::onAcceptAllPredictionsRequested()
{
    const int visible = predictionStore->totalVisibleCount();
    if (visible == 0) {
        QMessageBox::information(this, "Accept Predictions", "There are no predictions to accept.");
        return;
    }

    const auto reply = QMessageBox::question(
        this, "Accept Predictions",
        QString("Accept %1 prediction(s) across %2 frame(s) as real annotations?\n\n"
                "This is a single undo step.")
            .arg(visible)
            .arg(predictionStore->frames().size()),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
    if (reply != QMessageBox::Yes)
        return;

    const int taken = predictionController->acceptAll();
    if (taken < visible) {
        QMessageBox::information(
            this, "Accept Predictions",
            QString("Accepted %1 of %2. The rest have classes with no match in the label "
                    "schema, and \"Create missing classes on accept\" is off.")
                .arg(taken)
                .arg(visible));
    }
}

void PicAnnotate::onSamToolToggled(bool checked)
{
    if (checked) {
        // Same exclusive group as the manual draw tools.
        for (QAction *other : {ui->actionRectBox, ui->actionSquare, ui->actionPolyline,
                               ui->actionPolygone, ui->actionKeypoint, ui->actionSketlton}) {
            other->setChecked(false);
        }

        if (!project.hasSource()) {
            QMessageBox::information(this, "Segment Anything", "Open a source first.");
            ui->actionSAM_Segment->setChecked(false);
            return;
        }
    }

    ui->graphicsView->setDrawMode(checked ? AnnotationGraphicsView::DrawMode::Sam
                                          : AnnotationGraphicsView::DrawMode::None);
    samController->setActive(checked);
}

void PicAnnotate::onSamActiveChanged(bool active)
{
    // The controller turns itself off when the models fail to load, so the
    // action follows it rather than the other way round.
    if (ui->actionSAM_Segment->isChecked() != active) {
        QSignalBlocker blocker(ui->actionSAM_Segment);
        ui->actionSAM_Segment->setChecked(active);
        if (!active)
            ui->graphicsView->setDrawMode(AnnotationGraphicsView::DrawMode::None);
    }
}

void PicAnnotate::onSamPromptChanged(const QVector<QPointF> &positivePoints,
                                     const QVector<QPointF> &negativePoints, const QRectF &box)
{
    SamSegmenter::Prompt prompt;
    prompt.positivePoints = positivePoints;
    prompt.negativePoints = negativePoints;
    prompt.box = box;
    samController->setPrompt(prompt);
}

void PicAnnotate::onSamCommitRequested()
{
    const QVector<QPointF> polygon = samController->previewPolygon();
    if (polygon.size() < 3)
        return;

    const int labelId = labelPanel->currentLabelId();
    if (labelId < 0) {
        QMessageBox::information(this, "Segment Anything",
                                 "Select a label in the Labels panel first — a mask has to be "
                                 "assigned to a class.");
        return;
    }

    AnnoShape shape = AnnoShape::makePolygon(polygon, labelId);
    // Tagged as model output even though the user placed every prompt: the
    // geometry came from SAM, and an audit of the dataset should say so.
    shape.setSource(ShapeSource::Predicted);
    shape.setAttribute(QLatin1String(kConfidenceAttribute),
                       static_cast<double>(samController->previewScore()));
    shape.setAttribute(QLatin1String(kModelAttribute), QStringLiteral("sam"));

    annotations->addShape(project.currentIndex(), shape);

    // Clear the prompt so the next object starts clean rather than refining the
    // one just committed.
    ui->graphicsView->clearSamPrompt();
    samController->clearPrompt();
}

void PicAnnotate::onSamCancelled()
{
    samController->clearPrompt();
}

void PicAnnotate::onSamFailed(const QString &message)
{
    QMessageBox::warning(this, "Segment Anything", message);
}

void PicAnnotate::onClassifySelectedShapeTriggered()
{
    if (!project.hasSource()) {
        QMessageBox::information(this, "Classify", "Open a source first.");
        return;
    }

    const int shapeIndex = ui->graphicsView->selectedShapeIndex();
    if (shapeIndex < 0) {
        QMessageBox::information(this, "Classify",
                                 "Select a shape on the canvas or in the annotation list first.");
        return;
    }

    QString error;
    if (!clipController->classifyShapes(project.currentIndex(), {shapeIndex}, &error))
        QMessageBox::warning(this, "Classify", error);
}

void PicAnnotate::onClassifyUnlabelledOnFrameTriggered()
{
    if (!project.hasSource()) {
        QMessageBox::information(this, "Classify", "Open a source first.");
        return;
    }

    QString error;
    if (!clipController->classifyFrame(project.currentIndex(), /*onlyUnlabelled=*/true, &error))
        QMessageBox::information(this, "Classify", error);
}

void PicAnnotate::onClipFinished(int classified, int skipped, const QString &error)
{
    if (!error.isEmpty()) {
        QMessageBox::warning(this, "Classify", error);
        return;
    }

    QString summary = QString("Classified %1 shape%2.")
                          .arg(classified)
                          .arg(classified == 1 ? "" : "s");
    if (skipped > 0) {
        summary += QString("\n\n%1 left unchanged — either already correct, or CLIP was not "
                           "confident enough to pick a class.")
                       .arg(skipped);
    }
    statusBar()->showMessage(summary.section('\n', 0, 0), 5000);

    if (skipped > 0)
        QMessageBox::information(this, "Classify", summary);
}

void PicAnnotate::onRejectAllPredictionsRequested()
{
    if (predictionStore->isEmpty())
        return;

    const auto reply = QMessageBox::question(
        this, "Clear Predictions",
        QString("Discard all %1 prediction(s)?\n\nYour annotations are not affected.")
            .arg(predictionStore->totalCount()),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
    if (reply != QMessageBox::Yes)
        return;

    predictionController->rejectAll();
}

void PicAnnotate::showIoReport(const QString &title, const IoReport &report)
{
    QMessageBox box(this);
    box.setWindowTitle(title);

    if (!report.ok) {
        box.setIcon(QMessageBox::Warning);
        box.setText(report.error);
    } else {
        box.setIcon(report.warnings.isEmpty() ? QMessageBox::Information : QMessageBox::Warning);
        box.setText(QStringLiteral("%1 frames, %2 shapes.").arg(report.frames).arg(report.shapes));
        box.setDetailedText(report.toText());
    }

    box.exec();
}

void PicAnnotate::on_actionOpen_Folder_triggered()
{
    const QString folderPath = QFileDialog::getExistingDirectory(
        this, "Select Folder", RecentPaths::dir(RecentPaths::Images), QFileDialog::ShowDirsOnly);

    if (folderPath.isEmpty()) {
        qDebug() << "No folder selected.";
        return;
    }
    RecentPaths::remember(RecentPaths::Images, folderPath);

    QString error;
    if (!project.openFolder(folderPath, &error)) {
        QMessageBox::warning(this, "Open Folder", error);
        return;
    }

    onSourceOpened();
}

void PicAnnotate::on_actionOpen_Image_triggered()
{
    const QString fileName = QFileDialog::getOpenFileName(this, "Select a File",
                                                          RecentPaths::dir(RecentPaths::Images));
    if (fileName.isEmpty()) {
        qDebug() << "No file selected";
        return;
    }
    RecentPaths::remember(RecentPaths::Images, fileName);

    QString error;
    if (!project.openImage(fileName, &error)) {
        QMessageBox::critical(this, "Error", error);
        return;
    }

    onSourceOpened();
}

void PicAnnotate::on_actionOpen_Video_triggered()
{
    const QString fileName = QFileDialog::getOpenFileName(
        this, "Select a Video", RecentPaths::dir(RecentPaths::Videos),
        "Video Files (*.mp4 *.avi *.mov *.mkv)");
    if (fileName.isEmpty()) {
        qDebug() << "No video selected";
        return;
    }
    RecentPaths::remember(RecentPaths::Videos, fileName);

    QString error;
    if (!project.openVideo(fileName, &error)) {
        QMessageBox::critical(this, "Open Video", error);
        return;
    }

    onSourceOpened();
}

void PicAnnotate::on_actionOpen_Annotation_triggered()
{
    const QString annotationPath = QFileDialog::getOpenFileName(
        this, "Select Annotation JSON", RecentPaths::dir(RecentPaths::Annotations),
        "Annotation Files (*.json)");
    if (annotationPath.isEmpty()) {
        qDebug() << "No annotation file selected";
        return;
    }
    RecentPaths::remember(RecentPaths::Annotations, annotationPath);

    // The matching video usually sits beside its annotations, so offer that
    // folder first and only fall back to wherever videos were last picked.
    const QString videoPath = QFileDialog::getOpenFileName(
        this, "Select Matching Video", QFileInfo(annotationPath).absolutePath(),
        "Video Files (*.mp4 *.avi *.mov *.mkv)");
    if (videoPath.isEmpty()) {
        qDebug() << "No video selected";
        return;
    }
    RecentPaths::remember(RecentPaths::Videos, videoPath);

    QString error;
    if (!project.openDataset(videoPath, annotationPath, &error)) {
        QMessageBox::critical(this, "Open Annotation", error);
        return;
    }

    onSourceOpened();
}

void PicAnnotate::on_actionImport_Dataset_triggered()
{
    ImportDatasetDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const IDatasetFormat *format = dialog.selectedFormat();
    if (!format)
        return;

    const ImportOptions options = dialog.options();

    ImportedDataset dataset;
    IoReport report = format->importDataset(options, &dataset);
    if (report.ok) {
        // The read succeeded; applying it can still fail if the images cannot be
        // matched to frames, so carry the reader's warnings into the result.
        const QStringList readWarnings = report.warnings;
        report = DatasetImporter::apply(project, dataset, options.replaceExisting,
                                        options.groupTracks);
        report.warnings = readWarnings + report.warnings;
    }

    if (report.ok) {
        onSourceOpened();
        labelPanel->refresh();
    }

    showIoReport(QStringLiteral("Import %1").arg(format->displayName()), report);
}

void PicAnnotate::on_actionExport_Dataset_triggered()
{
    if (!project.hasSource()) {
        QMessageBox::information(this, "Export", "Open a source first.");
        return;
    }
    if (project.totalShapeCount() == 0) {
        QMessageBox::information(this, "Export", "There are no annotations to export.");
        return;
    }

    ExportDatasetDialog dialog(project.isVideoSource(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const IDatasetFormat *format = dialog.selectedFormat();
    if (!format)
        return;

    const IoReport report = format->exportDataset(project, dialog.options());
    showIoReport(QStringLiteral("Export %1").arg(format->displayName()), report);
}

void PicAnnotate::on_actionNext_Frame_triggered()
{
    if (project.next())
        displayCurrentFrame();
}

void PicAnnotate::on_actionPrevious_Frame_triggered()
{
    if (project.previous())
        displayCurrentFrame();
}

void PicAnnotate::onFrameRequested(int index)
{
    if (project.goToFrame(index))
        displayCurrentFrame();
}

void PicAnnotate::on_actionExit_triggered()
{
    // close() rather than quit(), so closeEvent() runs and the layout is saved.
    close();
}

void PicAnnotate::on_actionReset_Layout_triggered()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("MainWindow"));
    settings.remove(QStringLiteral("geometry"));
    settings.remove(QStringLiteral("state"));
    settings.endGroup();

    if (!defaultLayoutGeometry.isEmpty())
        restoreGeometry(defaultLayoutGeometry);
    if (!defaultLayoutState.isEmpty())
        restoreState(defaultLayoutState);
    showMaximized();
}

void PicAnnotate::onDrawToolTriggered()
{
    auto *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;

    for (QAction *other : {ui->actionRectBox, ui->actionSquare, ui->actionPolyline,
                           ui->actionPolygone, ui->actionKeypoint, ui->actionSketlton}) {
        if (other != action)
            other->setChecked(false);
    }

    // SAM is in the same exclusive group even though it is wired separately;
    // unchecking it here also shuts the controller down through its toggled().
    if (action->isChecked() && ui->actionSAM_Segment->isChecked())
        ui->actionSAM_Segment->setChecked(false);

    auto mode = AnnotationGraphicsView::DrawMode::None;
    if (action->isChecked()) {
        if (action == ui->actionRectBox)
            mode = AnnotationGraphicsView::DrawMode::Rect;
        else if (action == ui->actionSquare)
            mode = AnnotationGraphicsView::DrawMode::Square;
        else if (action == ui->actionPolyline)
            mode = AnnotationGraphicsView::DrawMode::Polyline;
        else if (action == ui->actionPolygone)
            mode = AnnotationGraphicsView::DrawMode::Polygon;
        else if (action == ui->actionKeypoint)
            mode = AnnotationGraphicsView::DrawMode::Keypoint;
        else if (action == ui->actionSketlton)
            mode = AnnotationGraphicsView::DrawMode::Skeleton;
    }

    ui->graphicsView->setDrawMode(mode);
}

void PicAnnotate::on_actionZoom_In_triggered()
{
    ui->graphicsView->zoomBy(1.25);
}

void PicAnnotate::on_actionZoom_Out_triggered()
{
    ui->graphicsView->zoomBy(0.8);
}

void PicAnnotate::on_actionFit_Window_triggered()
{
    ui->graphicsView->fitImageInView();
}

void PicAnnotate::on_actionShow_Keypoint_Names_toggled(bool checked)
{
    ui->graphicsView->setShowPointNames(checked);
}

void PicAnnotate::on_actionSave_triggered()
{
    const QString filePath = QFileDialog::getSaveFileName(
        this, "Save Project", RecentPaths::dir(RecentPaths::Projects),
        "PicAnnotate Project (*.json)");
    if (filePath.isEmpty())
        return;
    RecentPaths::remember(RecentPaths::Projects, filePath);

    QString error;
    if (!project.save(filePath, &error))
        QMessageBox::warning(this, "Save", error);
}

void PicAnnotate::on_actionSave_Directory_triggered()
{
    if (project.sourceDirectory().isEmpty()) {
        QMessageBox::information(this, "Save", "Open a folder or image first.");
        return;
    }

    const QString filePath = QDir(project.sourceDirectory()).filePath("annotations.json");
    QString error;
    if (project.save(filePath, &error))
        QMessageBox::information(this, "Save", "Saved to " + filePath);
    else
        QMessageBox::warning(this, "Save", error);
}

void PicAnnotate::on_actionClear_Frame_Annotations_triggered()
{
    if (!project.hasSource()) {
        QMessageBox::information(this, "Delete Annotations", "Open a source first.");
        return;
    }

    const int frameIndex = project.currentIndex();
    if (!project.hasAnnotations(frameIndex)) {
        QMessageBox::information(this, "Delete Annotations", "This frame has no annotations.");
        return;
    }

    const auto reply = QMessageBox::question(
        this, "Delete Annotations",
        QString("Delete all annotations on frame %1?").arg(frameIndex + 1),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;

    annotations->clearFrame(frameIndex);
}

void PicAnnotate::on_actionModel_Manager_triggered()
{
    ModelManagerDialog dialog(this);
    dialog.exec();
}

void PicAnnotate::on_actionCreate_Dataset_triggered()
{
    DatasetWizard wizard(this);
    if (wizard.exec() != QDialog::Accepted)
        return;

    const QString imageDirectory = wizard.imageDirectory();
    if (imageDirectory.isEmpty())
        return;

    QString error;
    if (!project.openFolder(imageDirectory, &error)) {
        QMessageBox::warning(this, "Create Dataset",
                             QString("The frames were extracted to\n%1\n\nbut the folder could "
                                     "not be opened: %2")
                                 .arg(imageDirectory, error));
        return;
    }

    onSourceOpened();
    statusBar()->showMessage(QString("Dataset ready — %1 frame(s) from %2 source(s).")
                                 .arg(wizard.report().framesWritten)
                                 .arg(wizard.report().sourcesProcessed),
                             8000);

    if (wizard.shouldAutoAnnotate()) {
        // Straight into the existing batch path, so the results land in the
        // prediction layer and still have to be reviewed before they count.
        predictionDock->raise();
        onRunModelOnAllFramesRequested();
    }
}

void PicAnnotate::onFileListContextMenuRequested(const QPoint &pos)
{
    const QModelIndex index = ui->listViewFiles->indexAt(pos);
    if (!index.isValid())
        return;

    const int frameIndex = index.row();

    QMenu menu(this);
    QAction *deleteAction = menu.addAction("Delete Annotations for This Frame");
    deleteAction->setEnabled(project.hasAnnotations(frameIndex));

    if (menu.exec(ui->listViewFiles->viewport()->mapToGlobal(pos)) != deleteAction)
        return;

    const auto reply = QMessageBox::question(
        this, "Delete Annotations",
        QString("Delete all annotations on frame %1?").arg(frameIndex + 1),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;

    annotations->clearFrame(frameIndex);
}

void PicAnnotate::onFileListItemClicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    if (project.goToFrame(index.row()))
        displayCurrentFrame();
}

void PicAnnotate::onAnnotationListItemClicked(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    ui->graphicsView->selectShape(index.row());
}

void PicAnnotate::onRectDrawn(const QRectF &imageRect)
{
    if (!project.hasSource())
        return;

    const int labelId = labelPanel->currentLabelId();
    annotations->addShape(project.currentIndex(), AnnoShape::makeRect(imageRect, labelId));
}

void PicAnnotate::onMultiPointShapeDrawn(ShapeType type, const QVector<QPointF> &points)
{
    if (!project.hasSource() || points.isEmpty())
        return;

    const int labelId = labelPanel->currentLabelId();
    AnnoShape shape;
    switch (type) {
    case ShapeType::Polygon:
        shape = AnnoShape::makePolygon(points, labelId);
        break;
    case ShapeType::Polyline:
        shape = AnnoShape::makePolyline(points, labelId);
        break;
    case ShapeType::Keypoint:
        shape = AnnoShape::makeKeypoint(points.first(), labelId);
        break;
    case ShapeType::Skeleton:
        shape = AnnoShape::makeSkeleton(points, labelId);
        break;
    case ShapeType::Rect:
        return;
    }

    annotations->addShape(project.currentIndex(), shape);
}

void PicAnnotate::onShapeGeometryChanged(int shapeIndex, const QRectF &imageRect)
{
    const QVector<AnnoShape> shapes = project.resolvedShapes(project.currentIndex());
    if (shapeIndex < 0 || shapeIndex >= shapes.size())
        return;

    AnnoShape shape = shapes.at(shapeIndex);
    shape.setRect(imageRect);
    // editShape() writes to a track keyframe or to the frame's own shape list,
    // whichever this index resolves to.
    annotations->editShape(project.currentIndex(), shapeIndex, shape,
                           QStringLiteral("Move/resize box"));
}

void PicAnnotate::onShapeRotationChanged(int shapeIndex, double degrees)
{
    const QVector<AnnoShape> shapes = project.resolvedShapes(project.currentIndex());
    if (shapeIndex < 0 || shapeIndex >= shapes.size())
        return;

    AnnoShape shape = shapes.at(shapeIndex);
    shape.setRotation(degrees);
    annotations->editShape(project.currentIndex(), shapeIndex, shape,
                           QStringLiteral("Rotate box"));
}

void PicAnnotate::onMultiPointShapeGeometryChanged(int shapeIndex, const QVector<QPointF> &points)
{
    const QVector<AnnoShape> shapes = project.resolvedShapes(project.currentIndex());
    if (shapeIndex < 0 || shapeIndex >= shapes.size())
        return;

    AnnoShape shape = shapes.at(shapeIndex);
    shape.setPoints(points);
    annotations->editShape(project.currentIndex(), shapeIndex, shape,
                           QStringLiteral("Move point"));
}

void PicAnnotate::onShapeVisibilityChanged(int shapeIndex, const QVector<int> &visibility)
{
    const QVector<AnnoShape> shapes = project.resolvedShapes(project.currentIndex());
    if (shapeIndex < 0 || shapeIndex >= shapes.size())
        return;

    AnnoShape shape = shapes.at(shapeIndex);
    shape.setVisibilityFlags(visibility);
    annotations->editShape(project.currentIndex(), shapeIndex, shape,
                           QStringLiteral("Change point visibility"));
}

void PicAnnotate::onShapeDeleteRequested(int shapeIndex)
{
    annotations->deleteShape(project.currentIndex(), shapeIndex);
}

void PicAnnotate::onStartTrackingRequested()
{
    const int shapeIndex = ui->graphicsView->selectedShapeIndex();
    if (shapeIndex < 0) {
        QMessageBox::information(this, "Track", "Select a shape first.");
        return;
    }

    // Stop playback up front rather than relying on trackingStarted to do it, so
    // the playback timer cannot fire once more between here and the run starting.
    frameNavigator->setInteractionEnabled(false);

    const QString modelDir = QCoreApplication::applicationDirPath() + "/models/";
    QString error;
    if (!trackController->setModelPaths(modelDir + "nanotrack_backbone_sim.onnx",
                                         modelDir + "nanotrack_head_sim.onnx", &error)) {
        setTrackingUiActive(false);
        QMessageBox::warning(this, "Track", error);
        return;
    }

    trackController->startTracking(shapeIndex);

    // startTracking() rejects unsuitable shapes and failed tracker init without
    // ever emitting trackingStarted, so the lock has to be released here.
    if (!trackController->isTracking())
        setTrackingUiActive(false);
}

void PicAnnotate::onTrackSelected(int trackId)
{
    // Mirror the panel's selection in the canvas so the two stay in step.
    const QVector<AnnoShape> shapes = project.resolvedShapes(project.currentIndex());
    for (int i = 0; i < shapes.size(); ++i) {
        if (shapes.at(i).trackId() == trackId) {
            ui->graphicsView->selectShape(i);
            return;
        }
    }
}

void PicAnnotate::onDeleteTrackRequested(int trackId)
{
    const AnnoTrack *track = project.tracks().track(trackId);
    if (!track)
        return;

    const auto reply = QMessageBox::question(
        this, "Delete Track",
        QString("Delete track %1 and all %2 of its keyframes?")
            .arg(trackId).arg(track->keyframeCount()),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;

    annotations->deleteTrack(trackId);
}

void PicAnnotate::onDeleteKeyframeRequested(int trackId, int frameIndex)
{
    annotations->removeTrackKeyframe(trackId, frameIndex);
}

void PicAnnotate::onEndTrackRequested(int trackId, int frameIndex)
{
    annotations->endTrackAt(trackId, frameIndex);
}

void PicAnnotate::onToggleOutsideRequested(int trackId, int frameIndex)
{
    const AnnoTrack *track = project.tracks().track(trackId);
    if (!track)
        return;

    const bool alreadyOutside = track->hasKeyframeAt(frameIndex)
                             && track->keyframes().value(frameIndex).outside;
    annotations->setTrackOutside(trackId, frameIndex, !alreadyOutside);
}

void PicAnnotate::onToggleOccludedRequested(int trackId, int frameIndex)
{
    const AnnoTrack *track = project.tracks().track(trackId);
    if (!track)
        return;

    const bool alreadyOccluded = track->hasKeyframeAt(frameIndex)
                              && track->keyframes().value(frameIndex).occluded;
    annotations->setTrackOccluded(trackId, frameIndex, !alreadyOccluded);
}

