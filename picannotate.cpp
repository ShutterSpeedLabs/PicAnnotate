#include "picannotate.h"
#include "ui_picannotate.h"

#include "core/annotationcontroller.h"
#include "core/annoshape.h"
#include "core/frameannotations.h"
#include "io/datasetimporter.h"
#include "io/formatregistry.h"
#include "trackers/trackcontroller.h"
#include "ui/annotationgraphicsview.h"
#include "ui/exportdatasetdialog.h"
#include "ui/framenavigator.h"
#include "ui/importdatasetdialog.h"
#include "ui/labelpanel.h"
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
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
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
{
    ui->setupUi(this);

    setupDocks();
    labelPanel->setLabelSchema(&project.labelSchema());

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

    // Frames and Tracking are rarely both needed, so they share one slot.
    tabifyDockWidget(ui->dockFiles, trackDock);
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
    for (QDockWidget *dock : {labelDock, ui->dockAnnotations, ui->dockFiles, trackDock})
        ui->menuPanels->addAction(dock->toggleViewAction());

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
}

void PicAnnotate::refreshTrackPanel()
{
    trackPanel->refresh(&project.tracks(), &project.labelSchema(), project.currentIndex());
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
        this, "Select Folder", QDir::homePath(), QFileDialog::ShowDirsOnly);

    if (folderPath.isEmpty()) {
        qDebug() << "No folder selected.";
        return;
    }

    QString error;
    if (!project.openFolder(folderPath, &error)) {
        QMessageBox::warning(this, "Open Folder", error);
        return;
    }

    onSourceOpened();
}

void PicAnnotate::on_actionOpen_Image_triggered()
{
    const QString fileName = QFileDialog::getOpenFileName(this, "Select a File", QDir::currentPath());
    if (fileName.isEmpty()) {
        qDebug() << "No file selected";
        return;
    }

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
        this, "Select a Video", QDir::homePath(), "Video Files (*.mp4 *.avi *.mov *.mkv)");
    if (fileName.isEmpty()) {
        qDebug() << "No video selected";
        return;
    }

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
        this, "Select Annotation JSON", QDir::homePath(), "Annotation Files (*.json)");
    if (annotationPath.isEmpty()) {
        qDebug() << "No annotation file selected";
        return;
    }

    const QString videoPath = QFileDialog::getOpenFileName(
        this, "Select Matching Video", QDir::homePath(), "Video Files (*.mp4 *.avi *.mov *.mkv)");
    if (videoPath.isEmpty()) {
        qDebug() << "No video selected";
        return;
    }

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
        this, "Save Project", QDir::homePath(), "PicAnnotate Project (*.json)");
    if (filePath.isEmpty())
        return;

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

