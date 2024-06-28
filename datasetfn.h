#ifndef DATASETFN_H
#define DATASETFN_H

#include "picannotate.h"
#include "ui_picannotate.h"
#include "yaml_cpp.h"
#include <QString>

int loadDataset(YoloConfig *config, Ui::PicAnnotate* ui, QString selectedFolder);
int readImagesInFolder(QString folderPath, Ui::PicAnnotate* ui);
extern int numberOfTestImages;
extern int numberOfTrainImages;
extern int numberOfValImages;

extern int numberOfTestLabels;
extern int numberOfTrainLabels;
extern int numberOfValLabels;

#endif // DATASETFN_H
