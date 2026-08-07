#ifndef DATASETFORMAT_H
#define DATASETFORMAT_H

#include "../core/frameannotations.h"
#include "../core/labelschema.h"

#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class Project;

// The three annotation tasks the formats can carry. A format advertises which of
// these it supports, and the export dialog only offers the intersection.
enum class DatasetTask {
    Detection,
    OrientedDetection,   // angle-aware boxes; four corners rather than x/y/w/h
    Segmentation,
    Keypoints
};

// Every task, in the order the UI should offer them.
inline QVector<DatasetTask> allDatasetTasks()
{
    return {DatasetTask::Detection, DatasetTask::OrientedDetection,
            DatasetTask::Segmentation, DatasetTask::Keypoints};
}

QString datasetTaskToString(DatasetTask task);
QString datasetTaskDisplayName(DatasetTask task);

struct ExportOptions
{
    QString outputDir;
    DatasetTask task = DatasetTask::Detection;

    // Write the image files alongside the labels. Required for video sources,
    // which have no on-disk frames to point at.
    bool copyImages = true;

    // Emit entries for frames that have no shapes (negative/background samples).
    bool includeEmptyFrames = false;

    // Fraction of frames routed to a validation split, 0 for none. The split is
    // deterministic (every Nth frame) so re-exporting is reproducible.
    double valSplit = 0.0;
};

struct ImportOptions
{
    QString path;                  // annotation file, or dataset root directory
    bool replaceExisting = true;   // drop current annotations before applying

    // Fold annotations sharing a track id into one track per object, so a video
    // dataset comes back as tracks rather than as unrelated per-frame shapes.
    // Datasets with no track ids are unaffected either way.
    bool groupTracks = true;
};

struct IoReport
{
    bool ok = false;
    QString error;
    QStringList warnings;
    int frames = 0;
    int shapes = 0;
    int tracks = 0;
    int skipped = 0;
    QStringList outputs;

    void warn(const QString &message);
    void fail(const QString &message);
    QString toText() const;

    static constexpr int kMaxWarnings = 40;
};

// What an importer produces: annotations keyed by image file name, plus the
// label schema the dataset declared. Keeping this separate from Project means an
// importer never has to know how sources are opened.
struct ImportedDataset
{
    QString imageDirectory;
    QStringList imageFileNames;                    // dataset order, base names
    LabelSchema schema;
    QMap<QString, FrameAnnotations> byFileName;    // keyed by base name

    bool isEmpty() const { return byFileName.isEmpty(); }
};

class IDatasetFormat
{
public:
    virtual ~IDatasetFormat() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QString description() const { return QString(); }

    virtual bool supportsExport() const = 0;
    virtual bool supportsImport() const = 0;
    virtual QSet<DatasetTask> supportedTasks() const = 0;
    bool supportsTask(DatasetTask task) const { return supportedTasks().contains(task); }

    // True when import expects a directory rather than a single file.
    virtual bool importsDirectory() const = 0;
    virtual QString importFileFilter() const { return QString(); }

    virtual IoReport exportDataset(const Project &project, const ExportOptions &options) const = 0;
    virtual IoReport importDataset(const ImportOptions &options, ImportedDataset *out) const = 0;
};

#endif // DATASETFORMAT_H
