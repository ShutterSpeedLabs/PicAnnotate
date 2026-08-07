#ifndef VOCFORMAT_H
#define VOCFORMAT_H

#include "datasetformat.h"

// Pascal VOC: one XML file per image, holding the image size and a flat list of
// <object> entries with an axis-aligned <bndbox>.
//
// Detection only — the format has no way to express polygons, keypoints or an
// angle. Shapes that cannot be reduced to a box are reported rather than
// silently flattened.
class VocFormat : public IDatasetFormat
{
public:
    QString id() const override { return QStringLiteral("voc"); }
    QString displayName() const override { return QStringLiteral("Pascal VOC XML"); }
    QString description() const override
    {
        return QStringLiteral("One XML per image with axis-aligned boxes. Detection only.");
    }

    bool supportsExport() const override { return true; }
    bool supportsImport() const override { return true; }
    QSet<DatasetTask> supportedTasks() const override { return {DatasetTask::Detection}; }

    bool importsDirectory() const override { return true; }

    IoReport exportDataset(const Project &project, const ExportOptions &options) const override;
    IoReport importDataset(const ImportOptions &options, ImportedDataset *out) const override;
};

#endif // VOCFORMAT_H
