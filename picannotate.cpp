#include "picannotate.h"
#include "ui_picannotate.h"
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <opencv2/opencv.hpp>
#include <QImage>
#include <string>
#include "globalVar.h"

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
    , ui(new Ui::PicAnnotate)
{
    ui->setupUi(this);
    scene->addItem(imageItem);
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
    folderPath = QFileDialog::getExistingDirectory(nullptr, "Select Folder", QDir::homePath(), QFileDialog::ShowDirsOnly);
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
    directory.setNameFilters(nameFilters);

    // Get file list
    fileList = directory.entryList();

    // Get number of files in folder
    FilesInFolder = fileList.size();
    qDebug() << "Files in folder :" << FilesInFolder;

    // Set first image file name from folder
    imageFileName = fileList.at(0);

    // Set absolute path of image file
    absoluteFilePath = directory.absoluteFilePath(imageFileName);

    // Load first image
    pixmap.load(absoluteFilePath);
<<<<<<< HEAD
    zoomScale = 1;
    pixmap = pixmap.scaled(pixmap.width() * zoomScale, pixmap.height() * zoomScale);
=======

    // Set image to be displayed
>>>>>>> 889e6378989480427973fe13880a8232307ef168
    imageItem->setPixmap(pixmap);

    // Set scene and view
    ui->graphicsView->setScene(scene);
    ui->graphicsView->fitInView(imageItem, Qt::KeepAspectRatio);

    // Set current file number to 1
    CurrentFileNumber = 1;

    // Set model for files ListView
    model.setStringList(fileList);
    ui->listViewFiles->setModel(&model);
    ui->lblFilesInFolder->setText(std::to_string(FilesInFolder).c_str());
    ui->lblCurrentFileNumber->setText(std::to_string(CurrentFileNumber).c_str());
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
        if (fileName.isEmpty()) {
            qDebug() << "No file selected";
        }

        // Process the selected file
#ifdef QT_DEBUG
        qDebug() << "Selected file:" << fileName;
#endif

        pixmap.load(fileName);
        imageItem->setPixmap(pixmap);
        ui->graphicsView->setScene(scene);
        ui->graphicsView->fitInView(imageItem, Qt::KeepAspectRatio);

    } catch (const std::exception& e) {
        // Handle exceptions
        QMessageBox::critical(nullptr, "Error", e.what());
    } catch (...) {
        // Handle any other exceptions
        QMessageBox::critical(nullptr, "Error", "An unknown error occurred.");
    }
}

/**
 * @brief Display the next image in the folder.
 *
 * This function is called when the "Next" button is clicked.
 * If the current image is the last one in the folder, it does nothing.
 * Otherwise, it loads the next image in the folder, sets it as the scene
 * of the graphics view, and updates the current file number.
 */
void PicAnnotate::on_next_button_clicked()
{
    if(CurrentFileNumber<FilesInFolder && (FilesInFolder>1)){
        // Get the file name of the next image
        imageFileName = fileList.at(CurrentFileNumber);
        // Set the absolute path of the next image
        absoluteFilePath = directory.absoluteFilePath(imageFileName);
<<<<<<< HEAD
        zoomScale = 1;
        pixmap = pixmap.scaled(pixmap.width() * zoomScale, pixmap.height() * zoomScale);
=======
        // Load the next image
>>>>>>> 889e6378989480427973fe13880a8232307ef168
        pixmap.load(absoluteFilePath);
        // Set the image as the scene of the graphics view
        imageItem->setPixmap(pixmap);
        ui->graphicsView->setScene(scene);
        // Zoom the image to fit the view, keeping the aspect ratio
        ui->graphicsView->fitInView(imageItem, Qt::KeepAspectRatio);
<<<<<<< HEAD
        ui->lblCurrentFileNumber->setText(std::to_string(CurrentFileNumber).c_str());
        ui->listViewFiles->setCurrentIndex(model.index(CurrentFileNumber, 0));
=======
        // Update the current file number
>>>>>>> 889e6378989480427973fe13880a8232307ef168
        CurrentFileNumber++;
    }
}

}


/**
 * @brief Display the previous image in the folder.
 *
 * This function is called when the "Previous" button is clicked.
 * If the current image is the first one in the folder, it does nothing.
 * Otherwise, it loads the previous image in the folder, sets it as the scene
 * of the graphics view, and updates the current file number.
 */
void PicAnnotate::on_pre_button_clicked()
{
    if((CurrentFileNumber<=FilesInFolder) && (FilesInFolder>1) && (CurrentFileNumber>=1)){
        // Decrease file number
        CurrentFileNumber--;
        // Get the file name of the previous image
        imageFileName = fileList.at(CurrentFileNumber);
        // Set the absolute path of the previous image
        absoluteFilePath = directory.absoluteFilePath(imageFileName);
<<<<<<< HEAD
        zoomScale = 1;
        pixmap = pixmap.scaled(pixmap.width() * zoomScale, pixmap.height() * zoomScale);
        pixmap.load(absoluteFilePath);
      // ui->graphicsView->set
=======
        // Load the previous image
        pixmap.load(absoluteFilePath);
        // Set the image as the scene of the graphics view
>>>>>>> 889e6378989480427973fe13880a8232307ef168
        imageItem->setPixmap(pixmap);
        ui->graphicsView->setScene(scene);
        // Zoom the image to fit the view, keeping the aspect ratio
        ui->graphicsView->fitInView(imageItem, Qt::KeepAspectRatio);
        ui->lblCurrentFileNumber->setText(std::to_string(CurrentFileNumber).c_str());
        ui->listViewFiles->setCurrentIndex(model.index(CurrentFileNumber, 0));
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
    CurrentFileNumber = index.row();
    imageFileName = fileList.at(CurrentFileNumber);
    absoluteFilePath = directory.absoluteFilePath(imageFileName);
    pixmap.load(absoluteFilePath);
    imageItem->setPixmap(pixmap);
    ui->graphicsView->setScene(scene);
    ui->graphicsView->fitInView(imageItem, Qt::KeepAspectRatio);
    ui->lblCurrentFileNumber->setText(std::to_string(CurrentFileNumber).c_str());
}


void PicAnnotate::on_listViewFiles_doubleClicked(const QModelIndex &index)
{

}


void PicAnnotate::on_ZoomIn_clicked()
{
    #ifdef QT_DEBUG
        qDebug() << "zoomScale: " << zoomScale;
        qDebug() << "CurrentFileNumber: " << CurrentFileNumber;
    #endif
    if(CurrentFileNumber>=1 && zoomScale<2){
        zoomScale = zoomScale + 0.1;
        pixmapScale = pixmap.scaled(pixmap.width() * zoomScale, pixmap.height() * zoomScale);
        imageItem->setPixmap(pixmapScale);
        imageItem->setScale(zoomScale);
        // Update the view to reflect the changes
        imageItem->update();

        // Set the center point of the image item
       // imageItem->setTransform(QTransform().scale(zoomScale).translate(imageItem->boundingRect().width() / 2, imageItem->boundingRect().height() / 2));
       // ui->graphicsView->setScene(scene);
    }
}


void PicAnnotate::on_ZoomOut_clicked()
{
    #ifdef QT_DEBUG
        qDebug() << "zoomScale: " << zoomScale;
        qDebug() << "CurrentFileNumber: " << CurrentFileNumber;
    #endif
    if(CurrentFileNumber>=1 && zoomScale<=2.2 && zoomScale>=1.1){
        zoomScale = zoomScale - 0.1;
        pixmapScale = pixmap.scaled(pixmap.width() * zoomScale, pixmap.height() * zoomScale);
        imageItem->setPixmap(pixmapScale);
        imageItem->setScale(zoomScale);
        // Update the view to reflect the changes
        imageItem->update();

        // Set the center point of the image item
      // imageItem->setTransform(QTransform().scale(zoomScale).translate(imageItem->boundingRect().width() / 2, imageItem->boundingRect().height() / 2));

      //  ui->graphicsView->setScene(scene);
    }
}

