#ifndef YOLOFORMAT_H
#define YOLOFORMAT_H

#include "datasetformat.h"

// Ultralytics YOLO layout: one .txt per image plus a data.yaml describing the
// class list. All coordinates are normalised to the image size.
//
//   detect   -> cls cx cy w h
//   obb      -> cls x1 y1 x2 y2 x3 y3 x4 y4          (four corners, clockwise)
//   segment  -> cls x1 y1 x2 y2 ... (polygon ring)
//   pose     -> cls cx cy w h  px py v  px py v ...   (with kpt_shape: [K, 3])
//
// Pose datasets carry a single kpt_shape for the whole dataset, so skeletons
// whose template is shorter than the dataset's K are padded with unlabeled
// points and reported as a warning.
//
// OBB is export-only. An 8-value row is indistinguishable from a 4-point polygon
// on disk — the two tasks share a row shape — and data.yaml records no task, so
// guessing on import would silently mangle one of them.
class YoloFormat : public IDatasetFormat
{
public:
    QString id() const override { return QStringLiteral("yolo"); }
    QString displayName() const override { return QStringLiteral("YOLO (Ultralytics)"); }
    QString description() const override
    {
        return QStringLiteral("Normalised .txt labels plus data.yaml — detect, segment or pose.");
    }

    bool supportsExport() const override { return true; }
    bool supportsImport() const override { return true; }
    QSet<DatasetTask> supportedTasks() const override
    {
        return {DatasetTask::Detection, DatasetTask::OrientedDetection,
                DatasetTask::Segmentation, DatasetTask::Keypoints};
    }

    bool importsDirectory() const override { return true; }

    IoReport exportDataset(const Project &project, const ExportOptions &options) const override;
    IoReport importDataset(const ImportOptions &options, ImportedDataset *out) const override;
};

#endif // YOLOFORMAT_H
