#ifndef COCOFORMAT_H
#define COCOFORMAT_H

#include "datasetformat.h"

// COCO JSON. One format spans all three tasks:
//   detection    -> annotations[].bbox
//   segmentation -> annotations[].segmentation as polygon rings
//   keypoints    -> annotations[].keypoints triplets + categories[].keypoints/skeleton
//
// RLE (`iscrowd: 1`) segmentation is recognised on import but skipped, since the
// project stores polygon segmentation only.
class CocoFormat : public IDatasetFormat
{
public:
    QString id() const override { return QStringLiteral("coco"); }
    QString displayName() const override { return QStringLiteral("COCO JSON"); }
    QString description() const override
    {
        return QStringLiteral("Boxes, polygon segmentation and keypoints in a single JSON file.");
    }

    bool supportsExport() const override { return true; }
    bool supportsImport() const override { return true; }
    QSet<DatasetTask> supportedTasks() const override
    {
        return {DatasetTask::Detection, DatasetTask::Segmentation, DatasetTask::Keypoints};
    }

    bool importsDirectory() const override { return false; }
    QString importFileFilter() const override { return QStringLiteral("COCO annotations (*.json)"); }

    IoReport exportDataset(const Project &project, const ExportOptions &options) const override;
    IoReport importDataset(const ImportOptions &options, ImportedDataset *out) const override;
};

#endif // COCOFORMAT_H
