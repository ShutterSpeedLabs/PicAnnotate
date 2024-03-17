#include "picannotate.h"
#include "ui_picannotate.h"
#include <QDebug>
#include <QFileDialog>
#include <QMessageBox>
#include <opencv2/opencv.hpp>
#include <QImage>
#include "globalVar.h"

PicAnnotate::PicAnnotate(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::PicAnnotate)
{
    ui->setupUi(this);
    scene->addItem(imageItem);
}

PicAnnotate::~PicAnnotate()
{
    delete ui;
}

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

    nameFilters << "*.png" << "*.jpg" << "*.bmp"; // Add more image formats if needed
    directory.setPath(folderPath);
    directory.setNameFilters(nameFilters);
    fileList = directory.entryList();
    FilesInFolder = fileList.size();
    qDebug() << "Files in folder :" << FilesInFolder;
    imageFileName = fileList.at(0);
    absoluteFilePath = directory.absoluteFilePath(imageFileName);
    pixmap.load(absoluteFilePath);
    imageItem->setPixmap(pixmap);
    ui->graphicsView->setScene(scene);
    ui->graphicsView->fitInView(imageItem, Qt::KeepAspectRatio);
    CurrentFileNumber = 1;
    model.setStringList(fileList);
    ui->listViewFiles->setModel(&model);
}


void PicAnnotate::on_actionOpen_Image_triggered()
{
    try {
        // Browse for a file
        fileName = QFileDialog::getOpenFileName(this, "Select a File", QDir::currentPath());

        if (fileName.isEmpty()) {
            // If the user cancels the file dialog, fileName will be an empty string
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


void PicAnnotate::on_next_button_clicked()
{
    if(CurrentFileNumber<FilesInFolder && (FilesInFolder>1)){
        imageFileName = fileList.at(CurrentFileNumber);
        absoluteFilePath = directory.absoluteFilePath(imageFileName);
        pixmap.load(absoluteFilePath);
        imageItem->setPixmap(pixmap);
        ui->graphicsView->setScene(scene);
        ui->graphicsView->fitInView(imageItem, Qt::KeepAspectRatio);
        CurrentFileNumber++;
    }
}


void PicAnnotate::on_pre_button_clicked()
{
    if((CurrentFileNumber<=FilesInFolder) && (FilesInFolder>1) && (CurrentFileNumber>=1)){
        CurrentFileNumber--;
        imageFileName = fileList.at(CurrentFileNumber);
        absoluteFilePath = directory.absoluteFilePath(imageFileName);
        pixmap.load(absoluteFilePath);
        imageItem->setPixmap(pixmap);
        ui->graphicsView->setScene(scene);
        ui->graphicsView->fitInView(imageItem, Qt::KeepAspectRatio);

    }
}


void PicAnnotate::on_actionExit_triggered()
{
     QApplication::quit();
}

