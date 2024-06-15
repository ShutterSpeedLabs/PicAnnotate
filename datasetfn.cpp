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

int loadDataset(YoloConfig *config, QString fileName, Ui::PicAnnotate* ui){

    QString testFolderPath = QString::fromStdString(config->testFolder);
    QString valFolderPath = QString::fromStdString(config->valFolder);
    QString trainFolderPath = QString::fromStdString(config->trainFolder);

    qDebug() << "Test Folder: " << testFolderPath;
    qDebug() << "Val Folder: " << valFolderPath;
    qDebug() << "Train Folder: " << trainFolderPath;

    QFileInfo fileInfo(fileName);

    QString folder = fileInfo.path(); // Get the folder path
    QString fileName1 = fileInfo.fileName(); // Get the filename

    // Output the folder and filename
    qDebug() << "Folder: " << folder;
    qDebug() << "Filename: " << fileName1;

    // QString testImagePath = folder + QDir::cleanPath(testFolderPath);
    // QString trainImagePath = folder + QDir::cleanPath(trainFolderPath);
    // QString valImagePath = folder + QDir::cleanPath(valFolderPath);


    QString testImagePath = folder + testFolderPath.remove("..");
    QString trainImagePath = folder + trainFolderPath.remove("..");
    QString valImagePath = folder + valFolderPath.remove("..");

    QStringList testLabelFolderList = testImagePath.split(QDir::separator());
    QStringList trainLabelFolderList = trainImagePath.split(QDir::separator());
    QStringList valLabelFolderList = valImagePath.split(QDir::separator());

    qDebug() << "testLabelFolderList.first(): " << testLabelFolderList[testLabelFolderList.size() - 2];
    qDebug() << "trainLabelFolderList.first(): " << trainLabelFolderList[trainLabelFolderList.size() - 2];
    qDebug() << "valLabelFolderList.first(): " << valLabelFolderList[valLabelFolderList.size() - 2];

    QString testLabelPath = folder + '/' + testLabelFolderList[testLabelFolderList.size() - 2] + "/labels";
    QString trainLabelPath = folder + '/' + trainLabelFolderList[trainLabelFolderList.size() - 2] + "/labels";
    QString valLabelPath = folder + '/' + valLabelFolderList[valLabelFolderList.size() - 2] + "/labels";

    qDebug() << "testImagePath: " << testImagePath;
    qDebug() << "trainImagePath: " << trainImagePath;
    qDebug() << "valImagePath: " << valImagePath;

    qDebug() << "testLabelPath: " << testLabelPath;
    qDebug() << "trainLabelPath: " << trainLabelPath;
    qDebug() << "valLabelPath: " << valLabelPath;

    QStringList imageFilters;
    QStringList labelFilters;

    imageFilters << "*.jpg" << "*.png";
    labelFilters << "*.txt";

    QDir dir(testImagePath);

    if (dir.exists()) {
        // Get the list of JPG and PNG files in the folder
        QStringList jpgPngFiles = dir.entryList(imageFilters, QDir::Files);

        // Get the number of JPG and PNG files in the folder
        numberOfTestImages = jpgPngFiles.size();

        qDebug() << "Number of JPG and PNG files in folder:" << numberOfTestImages;
        qDebug() << "TEST Folder exists!";

    } else {
        qDebug() << "TEST Folder does not exist!";
    }

    dir = trainImagePath;

    if (dir.exists()) {
        // Get the list of JPG and PNG files in the folder
        QStringList jpgPngFiles = dir.entryList(imageFilters, QDir::Files);

        // Get the number of JPG and PNG files in the folder
        numberOfTrainImages = jpgPngFiles.size();

        qDebug() << "Number of JPG and PNG files in folder:" << numberOfTrainImages;
        qDebug() << "Train Folder exists!";
    } else {
        qDebug() << "Train Folder does not exist!";
    }

    dir = valImagePath;

    if (dir.exists()) {
        // Get the list of JPG and PNG files in the folder
        QStringList jpgPngFiles = dir.entryList(imageFilters, QDir::Files);

        // Get the number of JPG and PNG files in the folder
        numberOfValImages = jpgPngFiles.size();

        qDebug() << "Number of JPG and PNG files in folder:" << numberOfValImages;
        qDebug() << "Val Folder exists!";
    } else {
        qDebug() << "Val Folder does not exist!";
    }

    dir = testLabelPath;

    if (dir.exists()) {
        // Get the list of JPG and PNG files in the folder
        QStringList labelFiles = dir.entryList(labelFilters, QDir::Files);

        // Get the number of JPG and PNG files in the folder
        numberOfTestLabels = labelFiles.size();

        qDebug() << "test label files in folder:" << numberOfTestLabels;
        qDebug() << "test label Folder exists!";
    } else {
        qDebug() << "test label Folder does not exist!";
    }

    dir = trainLabelPath;

    if (dir.exists()) {
        // Get the list of JPG and PNG files in the folder
        QStringList labelFiles = dir.entryList(labelFilters, QDir::Files);

        // Get the number of JPG and PNG files in the folder
        numberOfTrainLabels = labelFiles.size();

        qDebug() << "Train label files in folder:" << numberOfTrainLabels;
        qDebug() << "Train label Folder exists!";
    } else {
        qDebug() << "Train label Folder does not exist!";
    }

    dir = valLabelPath;

    if (dir.exists()) {
        // Get the list of JPG and PNG files in the folder
        QStringList labelFiles = dir.entryList(labelFilters, QDir::Files);

        // Get the number of JPG and PNG files in the folder
        numberOfValLabels = labelFiles.size();

        qDebug() << "Val label files in folder:" << numberOfValLabels;
        qDebug() << "Val label Folder exists!";
    } else {
        qDebug() << "Val label Folder does not exist!";
    }

    int readImgStatus = readImagesInFolder( testImagePath, ui);


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
    return 0;
}


