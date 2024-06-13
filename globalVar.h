#ifndef GLOBALVAR_H
#define GLOBALVAR_H
    #include "qstandarditemmodel.h"
    #include "yaml_cpp.h"
    #include <QString>
    #include <QPixmap>
    #include <QDir>
    #include <QStringListModel>

    extern QString folderPath;
    extern QString fileName;
    extern QStringList nameFilters;
    extern QString absoluteFilePath;
    extern QString imageFileName;
    extern QImage CurrentImage;
    extern QStringList fileList;
    extern QStringList classList;
    extern unsigned int FilesInFolder;
    extern unsigned int CurrentFileNumber;
    extern QStringListModel model;
    extern QStandardItemModel classModel;
    extern int loadStatus;

    extern QDir directory;
    extern float zoomScale;
    extern YoloConfig config;

    extern QString testFolderPath;
    extern QString valFolderPath;
    extern QString trainFolderPath;

    extern QPixmap pixmap;



#endif // GLOBALVAR_H
