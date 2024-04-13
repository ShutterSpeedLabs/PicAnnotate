#ifndef GLOBALVAR_H
#define GLOBALVAR_H
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
unsigned int FilesInFolder;
unsigned int CurrentFileNumber;
QStringListModel model;
QDir directory;


#endif // GLOBALVAR_H
