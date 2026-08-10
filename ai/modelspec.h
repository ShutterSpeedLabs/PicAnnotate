#ifndef MODELSPEC_H
#define MODELSPEC_H

#include "inferencetypes.h"

#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

class QJsonObject;

// What a model is for. This drives which parts of the UI will offer it, so a
// SAM encoder never shows up in the "detector" combo.
enum class ModelTask {
    Detection,      // boxes only
    Segmentation,   // boxes plus instance masks
    SamEncoder,     // image -> embedding
    SamDecoder,     // embedding + prompts -> mask
    ClipImage,      // image -> embedding
    ClipText,       // token ids -> embedding
    Classification, // crop -> class scores over a fixed label set
    Unknown
};

QString modelTaskToString(ModelTask task);
QString modelTaskDisplayName(ModelTask task);
ModelTask modelTaskFromString(const QString &s);

// Which inference stack loads the file. YOLO goes through cv::dnn (already
// linked, and it handles those graphs well); SAM and CLIP need ONNX Runtime
// because their graphs use dynamic shapes and ops cv::dnn does not implement.
enum class ModelRuntime {
    OpenCvDnn,
    OnnxRuntime
};

QString modelRuntimeToString(ModelRuntime runtime);
QString modelRuntimeDisplayName(ModelRuntime runtime);
ModelRuntime modelRuntimeFromString(const QString &s);

// A named class list a model's output indices refer to (COCO-80, ImageNet-1k).
// Stored alongside the models rather than hard-coded, so adding a model trained
// on a different label set is a data change and not a code change.
struct LabelSet
{
    QString id;
    QString displayName;
    QStringList classNames;

    bool isEmpty() const { return classNames.isEmpty(); }

    QJsonObject toJson() const;
    static LabelSet fromJson(const QJsonObject &obj);
};

// Everything needed to find, fetch, verify and run one model file.
//
// This is a *description*, not a loaded model: a spec exists whether or not the
// file is on disk, which is what lets the Model Manager list models the user
// could have and offer to download them.
struct ModelSpec
{
    QString id;               // stable key, e.g. "yolov8n-seg"
    QString displayName;
    QString description;

    ModelTask task = ModelTask::Unknown;
    ModelRuntime runtime = ModelRuntime::OnnxRuntime;

    QString fileName;         // expected on-disk name, e.g. "yolov8n-seg.onnx"
    QString downloadUrl;      // empty when the model can only be supplied by hand
    QString sha256;           // lower-case hex; empty disables verification
    qint64 downloadBytes = 0; // for the progress bar and the confirmation prompt

    // What to do when there is no download URL — usually the one-line export
    // command that produces this exact file. Most of these models are published
    // as PyTorch checkpoints and have no canonical ONNX build to point at, so
    // telling the user how to make one beats a link that rots.
    QString sourceHint;

    // Fixed network input, or an invalid size when the graph is dynamic.
    QSize inputSize;

    // Id of the LabelSet the model's class indices refer to. Empty for models
    // that emit no classes (SAM, CLIP encoders).
    QString labelSetId;

    // Pairs an encoder with its decoder. SAM is two files that must come from
    // the same export, and mixing a MobileSAM encoder with a ViT-B decoder
    // produces silent garbage rather than an error, so the pairing is declared.
    QString companionId;

    // Redistribution matters here: Ultralytics YOLO weights are AGPL-3.0, so the
    // manager shows the licence before downloading anything.
    QString license;
    QString licenseUrl;

    // True when the app ships the file itself and no download is needed.
    bool bundled = false;

    bool isValid() const { return !id.isEmpty() && !fileName.isEmpty(); }
    bool isDownloadable() const { return !downloadUrl.isEmpty(); }

    QJsonObject toJson() const;
    static ModelSpec fromJson(const QJsonObject &obj);
};

#endif // MODELSPEC_H
