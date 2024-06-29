#include "datasetfn.h"
#include "globalVar.h"
#include "picannotate.h"
#include "qdebug.h"
#include "qdir.h"
#include "qfileinfo.h"
#include "ui_picannotate.h"
#include "yaml_cpp.h"
#include <QString>


int numberOfTestImages = 0;
int numberOfTrainImages = 0;
int numberOfValImages = 0;

int numberOfTestLabels = 0;
int numberOfTrainLabels = 0;
int numberOfValLabels = 0;

int loadDataset(YoloConfig *config, Ui::PicAnnotate* ui, QString selectedFolder){
    QString imagePath;
    QString labelPath;

    if (selectedFolder == "train") {
        imagePath = QString::fromStdString(config->trainImagesPath);
        labelPath = QString::fromStdString(config->trainLabelsPath);
    } else if (selectedFolder == "test") {
        imagePath = QString::fromStdString(config->testImagesPath);
        labelPath = QString::fromStdString(config->testLabelsPath);
    } else if (selectedFolder == "val") {
        imagePath = QString::fromStdString(config->valImagesPath);
        labelPath = QString::fromStdString(config->valLabelsPath);
    } else {
        return -1; // Invalid folder selected
    }

    qDebug() << "Selected Image Path: " << imagePath;
    qDebug() << "Selected Label Path: " << labelPath;

    QStringList imageFilters;
    imageFilters << "*.jpg" << "*.png" << "*.jpeg" << "*.bmp";

    QDir imageDir(imagePath);
    QDir labelDir(labelPath);

    int numberOfImages = imageDir.entryList(imageFilters, QDir::Files).size();
    int numberOfLabels = labelDir.entryList(QStringList() << "*.txt", QDir::Files).size();

    qDebug() << "Number of Images in " << selectedFolder << ": " << numberOfImages;
    qDebug() << "Number of Labels in " << selectedFolder << ": " << numberOfLabels;

    if (numberOfImages != numberOfLabels) {
        qDebug() << "Warning: Number of images and labels do not match";
    }

    int readImgStatus = readImagesInFolder(imagePath, ui);

    return readImgStatus;
}


int readImagesInFolder(QString folderPath, Ui::PicAnnotate* ui){
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

    // Set model for files ListView
    model.setStringList(fileList);
    ui->listViewFiles->setModel(&model);
    ui->lblFilesInFolder->setText(std::to_string(FilesInFolder).c_str());
    ui->lblCurrentFileNumber->setText(std::to_string(CurrentFileNumber).c_str());
    return 1;
}


