#include "picannotate.h"
#include "ui_picannotate.h"
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <opencv2/opencv.hpp>
#include <QImage>
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

    // Set image to be displayed
    imageItem->setPixmap(pixmap);

    // Set scene and view
    ui->graphicsView->setScene(scene);
    ui->graphicsView->fitInView(imageItem, Qt::KeepAspectRatio);

    // Set current file number to 1
    CurrentFileNumber = 1;

    // Set model for files ListView
    model.setStringList(fileList);
    ui->listViewFiles->setModel(&model);
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
        // Load the next image
        pixmap.load(absoluteFilePath);
        // Set the image as the scene of the graphics view
        imageItem->setPixmap(pixmap);
        ui->graphicsView->setScene(scene);
        // Zoom the image to fit the view, keeping the aspect ratio
        ui->graphicsView->fitInView(imageItem, Qt::KeepAspectRatio);
        // Update the current file number
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
        // Load the previous image
        pixmap.load(absoluteFilePath);
        // Set the image as the scene of the graphics view
        imageItem->setPixmap(pixmap);
        ui->graphicsView->setScene(scene);
        // Zoom the image to fit the view, keeping the aspect ratio
        ui->graphicsView->fitInView(imageItem, Qt::KeepAspectRatio);

    }
}


/**
 * Handle the triggering of the exit action.
 */
void PicAnnotate::on_actionExit_triggered()
{
     QApplication::quit();
}

