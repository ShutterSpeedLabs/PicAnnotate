#include "qstandarditemmodel.h"
#include "yaml_cpp.h"
#include <QString>
#include <QPixmap>
#include <QDir>
#include <QStringListModel>

QString folderPath;
 QString fileName;
 QStringList nameFilters;
 QString absoluteFilePath;
 QString imageFileName;
 QImage CurrentImage;
 QStringList fileList;
 QStringList classList;
 unsigned int FilesInFolder;
 unsigned int CurrentFileNumber;
 QStringListModel model;
 QStandardItemModel classModel;
 int loadStatus;

 QDir directory;
 float zoomScale;
 YoloConfig config;

 QString testFolderPath;
 QString valFolderPath;
 QString trainFolderPath;
