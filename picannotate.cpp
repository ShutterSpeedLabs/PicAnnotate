#include "picannotate.h"
#include "datasetfn.h"
#include "ui_picannotate.h"
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <opencv2/opencv.hpp>
#include <QImage>
#include <string>
#include <QStandardItemModel>
#include <QStandardItem>
#include "globalVar.h"
#include <QRegularExpression>
#include <QInputDialog>
#include <QRandomGenerator>

/**
 * Constructor for the PicAnnotate class.
 *
 * @param parent a pointer to the parent widget
 *
 * @return N/A
 *
 * @throws N/A
 */
PicAnnotate::PicAnnotate(QWidget *parent)
    : QMainWindow(parent)
    , imageItem(nullptr)
    , scene(nullptr)
    , isDrawing(false)
    , currentClassId(0)
    , ui(new Ui::PicAnnotate)
{
    ui->setupUi(this);

    scene = new QGraphicsScene(this);
    imageItem = new QGraphicsPixmapItem();
    scene->addItem(imageItem);
    ui->graphicsView->setScene(scene);

    // Install event filter on the graphics view
    ui->graphicsView->viewport()->installEventFilter(this);
    connect(&classModel, &QStandardItemModel::itemChanged, this, &PicAnnotate::updateAnnotations);
}
/**
 * Destructor for the PicAnnotate class.
 */
PicAnnotate::~PicAnnotate()
{
    delete ui;
}

/**
 * Handles the "Open Folder" action in the PicAnnotate class.
 *
 * @param None
 *
 * @return None
 *
 * @throws None
 */
void PicAnnotate::on_actionOpen_Folder_triggered()
{
    QString folderPath = QFileDialog::getExistingDirectory(nullptr, "Select Folder", QDir::homePath(), QFileDialog::ShowDirsOnly);
    if (!folderPath.isEmpty()) {
// Do something with the selected folder path
#ifdef QT_DEBUG
        qDebug() << "Selected folder path:" << folderPath;
#endif

    } else {
#ifdef QT_DEBUG
        qDebug() << "No folder selected.";
#endif
    }

    // Set filters for images
    nameFilters << "*.png" << "*.jpg" << "*.bmp"; // Add more image formats if needed

    // Set directory path and filters
     directory.setPath(folderPath);
     directory.setNameFilters( nameFilters);

    // Get file list
     fileList =  directory.entryList();

    // Get number of files in folder
     FilesInFolder =  fileList.size();
    qDebug() << "Files in folder :" <<  FilesInFolder;

    // Set first image file name from folder
     imageFileName =  fileList.at(0);

    // Set absolute path of image file
     absoluteFilePath =  directory.absoluteFilePath( imageFileName);

    // Load first image
    pixmap.load( absoluteFilePath);
     zoomScale = 1;
    pixmap = pixmap.scaled(pixmap.width() *  zoomScale, pixmap.height() *  zoomScale);

    // Set image to be displayed
    imageItem->setPixmap(pixmap);

    // Set scene and view
    ui->graphicsView->setScene(scene);
    ui->graphicsView->fitInView(imageItem, Qt::KeepAspectRatio);

    // Set current file number to 1
     CurrentFileNumber = 1;

    // Set model for files ListView
     model.setStringList( fileList);
    ui->listViewFiles->setModel(& model);
    ui->lblFilesInFolder->setText(std::to_string( FilesInFolder).c_str());
    ui->lblCurrentFileNumber->setText(std::to_string( CurrentFileNumber).c_str());
}


/**
 * @brief Opens an image file and displays it in the graphics view
 * 
 * When the user selects this menu item, a file dialog appears that allows the
 * user to select an image file. If the user selects a file and confirms the
 * file dialog, the selected file is opened and displayed in the graphics
 * view. If the user cancels the file dialog, a message is printed to the debug
 * output indicating that no file was selected.
 */
void PicAnnotate::on_actionOpen_Image_triggered()
{
    try {
        // Browse for a file
         fileName = QFileDialog::getOpenFileName(this, "Select a File", QDir::currentPath());

        // If the user cancels the file dialog, fileName will be an empty string
        if ( fileName.isEmpty()) {
            qDebug() << "No file selected";
        }
        else{
            // Process the selected file
            #ifdef QT_DEBUG
                    qDebug() << "Selected file:" <<  fileName;
            #endif

            pixmap.load( fileName);
            imageItem->setPixmap(pixmap);
            ui->graphicsView->setScene(scene);
            ui->graphicsView->fitInView(imageItem, Qt::KeepAspectRatio);
        }
    } catch (const std::exception& e) {
        // Handle exceptions
        QMessageBox::critical(nullptr, "Error", e.what());
    } catch (...) {
        // Handle any other exceptions
        QMessageBox::critical(nullptr, "Error", "An unknown error occurred.");
    }
}


/**
 * Handle the triggering of the exit action.
 */
void PicAnnotate::on_actionExit_triggered()
{
     QApplication::quit();
}


void PicAnnotate::on_listViewFiles_clicked(const QModelIndex &index)
{
    if (!index.isValid()) return;

    int selectedIndex = index.row();
    if (selectedIndex >= 0 && selectedIndex < fileList.size()) {
        CurrentFileNumber = selectedIndex + 1;
        loadImageAndAnnotations(selectedIndex);
    }
}


void PicAnnotate::on_actionRead_YAML_triggered()
{
    zoomScale = 1;
    try {
        // Select the dataset folder
        QString datasetFolder = QFileDialog::getExistingDirectory(this, "Select Dataset Folder", QDir::homePath());
        if (datasetFolder.isEmpty()) {
            QMessageBox::information(this, "Information", "No folder selected");
            return;
        }

        // Find all YAML files in the dataset folder
        QDir dir(datasetFolder);
        QStringList yamlFilters;
        yamlFilters << "*.yaml" << "*.yml";
        QFileInfoList yamlFiles = dir.entryInfoList(yamlFilters, QDir::Files);

        if (yamlFiles.isEmpty()) {
            QMessageBox::warning(this, "Warning", "No YAML files found in the selected folder");
            return;
        }

        // Let user select which YAML file to use
        QStringList yamlFileNames;
        for (const auto& fileInfo : yamlFiles) {
            yamlFileNames << fileInfo.fileName();
        }

        bool ok;
        QString selectedYamlFile = QInputDialog::getItem(this, "Select YAML File",
                                                         "Choose a YAML file:",
                                                         yamlFileNames, 0, false, &ok);
        if (!ok || selectedYamlFile.isEmpty()) {
            QMessageBox::information(this, "Information", "No YAML file selected");
            return;
        }

        fileName = dir.filePath(selectedYamlFile);
        qDebug() << "Selected YAML file:" << fileName;

        config = readYoloConfig(fileName.toStdString());
        initializeColorMap();

        // Check if folders exist and classes are defined
        QStringList availableFolders;
        if (QDir(QString::fromStdString(config.trainImagesPath)).exists())
        {
            availableFolders << "train";
        }
        if (QDir(QString::fromStdString(config.testImagesPath)).exists())
        {
            availableFolders << "test";
        }
        if (QDir(QString::fromStdString(config.valImagesPath)).exists())
        {
            availableFolders << "Val";
        }

        if (availableFolders.isEmpty()) {
            QMessageBox::warning(this, "Missing Folders", "No valid image folders found in the dataset.");
            return;
        }

        if (config.classNames.empty()) {
            QMessageBox::warning(this, "Missing Classes", "No classes defined in the YAML file.");
            return;
        }

        // Let user select which folder to open
        QString selectedFolder = QInputDialog::getItem(this, "Select Folder",
                                                       "Choose a folder to open images from:",
                                                       availableFolders, 0, false, &ok);
        if (!ok || selectedFolder.isEmpty()) {
            QMessageBox::information(this, "Information", "No folder selected");
            return;
        }

        // Set the current working directory based on user selection
        if (selectedFolder == "train") {
            directory.setPath(QString::fromStdString(config.trainImagesPath));
        } else if (selectedFolder == "test") {
            directory.setPath(QString::fromStdString(config.testImagesPath));
        } else if (selectedFolder == "val") {
            directory.setPath(QString::fromStdString(config.valImagesPath));
        }

        // Setup class list
        classModel.clear();
        for (const auto& className : config.classNames) {
            QStandardItem* item = new QStandardItem(QString::fromStdString(className));
            item->setCheckable(true);
            item->setCheckState(Qt::Checked);
            classModel.appendRow(item);
        }
        ui->listViewClass->setModel(&classModel);
        ui->lblTotalnc->setText(QString::number(config.numClasses));

        connect(&classModel, &QStandardItemModel::itemChanged, this, &PicAnnotate::updateAnnotations);

        // Load dataset
        loadStatus = loadDataset(&config, ui, selectedFolder);
        if (loadStatus < 0) {
            QMessageBox::warning(this, "Warning", "Dataset not loaded properly");
            return;
        }

        // Load first image and its annotations
        loadImageAndAnnotations(0);

    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error", e.what());
    } catch (...) {
        QMessageBox::critical(this, "Error", "An unknown error occurred.");
    }
}


void PicAnnotate::initializeColorMap()
{
    QRandomGenerator *rng = QRandomGenerator::global();
    for (int i = 0; i < config.classNames.size(); ++i) {
        classColorMap[i] = QColor::fromHsv(rng->bounded(360), 200 + rng->bounded(55), 200 + rng->bounded(55));
    }
}


void PicAnnotate::updateAnnotatedPixmap()
{
    annotatedPixmap = originalPixmap.copy();
    QPainter painter(&annotatedPixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    for (const BoundingBox &box : boundingBoxes) {
        painter.setPen(QPen(classColorMap[box.classId], 2));
        painter.drawRect(box.rect);

        // Draw class name
        QColor textColor = classColorMap[box.classId].lightness() > 128 ? Qt::black : Qt::white;
        painter.setPen(textColor);
        painter.setFont(QFont("Arial", 10));
        painter.drawText(box.rect, Qt::AlignTop | Qt::AlignLeft,
                         QString::fromStdString(config.classNames[box.classId]));
    }

    imageItem->setPixmap(annotatedPixmap);
}

void PicAnnotate::updateView()
{
    QPixmap scaledPixmap = annotatedPixmap.scaled(annotatedPixmap.width() * zoomScale,
                                                  annotatedPixmap.height() * zoomScale,
                                                  Qt::KeepAspectRatio,
                                                  Qt::SmoothTransformation);
    imageItem->setPixmap(scaledPixmap);
    imageItem->setScale( zoomScale);

    ui->graphicsView->setScene(scene);
    ui->graphicsView->fitInView(imageItem, Qt::KeepAspectRatio);
    imageItem->update();
}

void PicAnnotate::updateViewZoom()
{
    QPixmap scaledPixmap = annotatedPixmap.scaled(annotatedPixmap.width() * zoomScale,
                                                  annotatedPixmap.height() * zoomScale);
    imageItem->setPixmap(scaledPixmap);
    imageItem->setScale( zoomScale);
    scene->setSceneRect(QRectF(0,0,scaledPixmap.width(), scaledPixmap.height()));
    imageItem->update();
}

void PicAnnotate::loadImageAndAnnotations(int index)
{
    if (index < 0 || index >= fileList.size()) return;

    imageFileName = fileList.at(index);
    absoluteFilePath = directory.absoluteFilePath(imageFileName);

    originalPixmap.load(absoluteFilePath);
    if (originalPixmap.isNull()) {
        qDebug() << "Failed to load image:" << absoluteFilePath;
        return;
    }

    // Clear existing bounding boxes
    boundingBoxes.clear();

    // Load annotations from label file
    QString labelPath = absoluteFilePath;
    labelPath.replace("/images/", "/labels/");
    labelPath.replace(QRegularExpression("\\.(jpg|jpeg|png|bmp)$"), ".txt");

    QFile file(labelPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        while (!in.atEnd()) {
            QString line = in.readLine();
            QStringList parts = line.split(" ");
            if (parts.size() == 5) {
                int classId = parts[0].toInt();
                float centerX = parts[1].toFloat() * originalPixmap.width();
                float centerY = parts[2].toFloat() * originalPixmap.height();
                float width = parts[3].toFloat() * originalPixmap.width();
                float height = parts[4].toFloat() * originalPixmap.height();

                QRectF rect(centerX - width/2, centerY - height/2, width, height);
                BoundingBox box;
                box.classId = classId;
                box.rect = rect;
                boundingBoxes.append(box);
            }
        }
        file.close();
    }

    updateAnnotatedPixmap();
    updateView();

    CurrentFileNumber = index + 1;
    ui->lblCurrentFileNumber->setText(QString::number(CurrentFileNumber));
    ui->listViewFiles->setCurrentIndex(model.index(index, 0));
}

void PicAnnotate::on_next_button_clicked()
{
    zoomScale = 1;
    imageItem->setScale( zoomScale);
    scene->setSceneRect(QRectF(0,0,annotatedPixmap.width(), annotatedPixmap.height()));
    imageItem->update();
    if (CurrentFileNumber < FilesInFolder && (FilesInFolder > 1)) {
        loadImageAndAnnotations(CurrentFileNumber);
    }
}

void PicAnnotate::on_pre_button_clicked()
{
    zoomScale = 1;
    imageItem->setScale( zoomScale);
    scene->setSceneRect(QRectF(0,0,annotatedPixmap.width(), annotatedPixmap.height()));
    imageItem->update();

    if (CurrentFileNumber > 1 && FilesInFolder > 1) {
        loadImageAndAnnotations(CurrentFileNumber - 2);
    }
}

void PicAnnotate::on_ZoomIn_clicked()
{
    if (CurrentFileNumber >= 1 && zoomScale < 2) {
        zoomScale = zoomScale + 0.1;
        updateViewZoom();
    }
}

void PicAnnotate::on_ZoomOut_clicked()
{
    if (CurrentFileNumber >= 1 && zoomScale <= 2.2 && zoomScale >= 1.1) {
        zoomScale = zoomScale - 0.1;
        updateViewZoom();
    }
}

void PicAnnotate::updateAnnotations()
{
    QList<BoundingBox> visibleBoxes;
    for (const BoundingBox &box : boundingBoxes) {
        QModelIndex index = classModel.index(box.classId, 0);
        if (index.isValid() && classModel.data(index, Qt::CheckStateRole) == Qt::Checked) {
            visibleBoxes.append(box);
        }
    }

    annotatedPixmap = originalPixmap.copy();
    QPainter painter(&annotatedPixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    for (const BoundingBox &box : visibleBoxes) {
        painter.setPen(QPen(classColorMap[box.classId], 2));
        painter.drawRect(box.rect);

        QColor textColor = classColorMap[box.classId].lightness() > 128 ? Qt::black : Qt::white;
        painter.setPen(textColor);
        painter.setFont(QFont("Arial", 10));
        painter.drawText(box.rect, Qt::AlignTop | Qt::AlignLeft,
                         QString::fromStdString(config.classNames[box.classId]));
    }

    updateView();
}


void PicAnnotate::drawTemporaryRect()
{
    QPixmap tempPixmap = annotatedPixmap.copy();
    QPainter painter(&tempPixmap);
    painter.setPen(QPen(classColorMap[currentClassId], 2));
    painter.drawRect(QRectF(startPoint, endPoint));
    imageItem->setPixmap(tempPixmap);
}

void PicAnnotate::finalizeBoundingBox()
{
    QRectF rect(startPoint, endPoint);
    rect = rect.normalized();

    BoundingBox newBox;
    newBox.classId = currentClassId;
    newBox.rect = rect;
    boundingBoxes.append(newBox);

    updateAnnotatedPixmap();
    updateView();
    saveBoundingBoxes();
}

void PicAnnotate::on_listViewClass_clicked(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    QStyleOptionViewItem option;
    option.initFrom(ui->listViewClass);
    option.rect = ui->listViewClass->visualRect(index);

    QRect checkBoxRect = ui->listViewClass->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &option, ui->listViewClass);

    // Get the position of the mouse click
    QPoint mousePos = ui->listViewClass->mapFromGlobal(QCursor::pos());

    if (checkBoxRect.contains(mousePos)) {
        // Checkbox area was clicked
        QStandardItem* item = classModel.itemFromIndex(index);
        if (item->isCheckable()) {
            Qt::CheckState newState = (item->checkState() == Qt::Checked) ? Qt::Unchecked : Qt::Checked;
            item->setCheckState(newState);
            updateAnnotations();
        }
    }

    // Set the current class ID regardless of where the click occurred
    currentClassId = index.row();

    // Optionally, you can update the UI to show which class is currently selected
    ui->listViewClass->setCurrentIndex(index);
}

bool PicAnnotate::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == ui->graphicsView->viewport()) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            QPointF scenePos = ui->graphicsView->mapToScene(mouseEvent->pos());
            if (mouseEvent->button() == Qt::LeftButton) {
                onMousePressed(scenePos);
            } else if (mouseEvent->button() == Qt::RightButton) {
                deleteBoundingBox(scenePos);
            }
            return true;
        }
        case QEvent::MouseMove: {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            QPointF scenePos = ui->graphicsView->mapToScene(mouseEvent->pos());
            onMouseMoved(scenePos);
            return true;
        }
        case QEvent::MouseButtonRelease: {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            QPointF scenePos = ui->graphicsView->mapToScene(mouseEvent->pos());
            onMouseReleased(scenePos);
            return true;
        }
        default:
            return false;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void PicAnnotate::onMousePressed(QPointF point)
{
    if (isDrawing) return;
    startPoint = point;
    isDrawing = true;
}

void PicAnnotate::onMouseMoved(QPointF point)
{
    if (!isDrawing) return;
    endPoint = point;
    drawTemporaryRect();
}

void PicAnnotate::onMouseReleased(QPointF point)
{
    if (!isDrawing) return;
    endPoint = point;
    isDrawing = false;
    finalizeBoundingBox();
}
void PicAnnotate::deleteBoundingBox(const QPointF &clickPoint)
{
    for (int i = boundingBoxes.size() - 1; i >= 0; --i) {
        if (boundingBoxes[i].rect.contains(clickPoint)) {
            boundingBoxes.removeAt(i);
            updateAnnotatedPixmap();
            updateView();
            saveBoundingBoxes();
            break;
        }
    }
}

void PicAnnotate::saveBoundingBoxes()
{
    QString labelPath = absoluteFilePath;
    labelPath.replace("/images/", "/labels/");
    labelPath.replace(QRegularExpression("\\.(jpg|jpeg|png|bmp)$"), ".txt");
    QFile file(labelPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        for (const BoundingBox &box : boundingBoxes) {
            float center_x = (box.rect.left() + box.rect.right()) / (2.0 * annotatedPixmap.width());
            float center_y = (box.rect.top() + box.rect.bottom()) / (2.0 * annotatedPixmap.height());
            float width = box.rect.width() / annotatedPixmap.width();
            float height = box.rect.height() / annotatedPixmap.height();
            out << QString("%1 %2 %3 %4 %5\n").arg(box.classId).arg(center_x).arg(center_y).arg(width).arg(height);
        }
        file.close();
    }
}
