#include "modelspec.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>

namespace {

// Task <-> string in one table, so the two directions cannot drift apart.
struct TaskName
{
    ModelTask task;
    const char *key;
};

const TaskName kTaskNames[] = {
    {ModelTask::Detection,      "detection"},
    {ModelTask::Segmentation,   "segmentation"},
    {ModelTask::SamEncoder,     "sam-encoder"},
    {ModelTask::SamDecoder,     "sam-decoder"},
    {ModelTask::ClipImage,      "clip-image"},
    {ModelTask::ClipText,       "clip-text"},
    {ModelTask::Classification, "classification"},
};

} // namespace

QString modelTaskToString(ModelTask task)
{
    for (const TaskName &entry : kTaskNames) {
        if (entry.task == task)
            return QString::fromLatin1(entry.key);
    }
    return QStringLiteral("unknown");
}

QString modelTaskDisplayName(ModelTask task)
{
    switch (task) {
    case ModelTask::Detection:      return QObject::tr("Object detection");
    case ModelTask::Segmentation:   return QObject::tr("Instance segmentation");
    case ModelTask::SamEncoder:     return QObject::tr("SAM image encoder");
    case ModelTask::SamDecoder:     return QObject::tr("SAM mask decoder");
    case ModelTask::ClipImage:      return QObject::tr("CLIP image encoder");
    case ModelTask::ClipText:       return QObject::tr("CLIP text encoder");
    case ModelTask::Classification: return QObject::tr("Classification");
    case ModelTask::Unknown:        break;
    }
    return QObject::tr("Unknown");
}

ModelTask modelTaskFromString(const QString &s)
{
    const QString key = s.trimmed().toLower();
    for (const TaskName &entry : kTaskNames) {
        if (key == QLatin1String(entry.key))
            return entry.task;
    }
    return ModelTask::Unknown;
}

QString modelRuntimeToString(ModelRuntime runtime)
{
    return runtime == ModelRuntime::OpenCvDnn ? QStringLiteral("opencv-dnn")
                                              : QStringLiteral("onnxruntime");
}

QString modelRuntimeDisplayName(ModelRuntime runtime)
{
    return runtime == ModelRuntime::OpenCvDnn ? QObject::tr("OpenCV DNN")
                                              : QObject::tr("ONNX Runtime");
}

ModelRuntime modelRuntimeFromString(const QString &s)
{
    const QString key = s.trimmed().toLower();
    if (key == QLatin1String("opencv-dnn") || key == QLatin1String("opencv")
        || key == QLatin1String("dnn")) {
        return ModelRuntime::OpenCvDnn;
    }
    return ModelRuntime::OnnxRuntime;
}

QJsonObject LabelSet::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("displayName"), displayName);

    QJsonArray names;
    for (const QString &name : classNames)
        names.append(name);
    obj.insert(QStringLiteral("classes"), names);
    return obj;
}

LabelSet LabelSet::fromJson(const QJsonObject &obj)
{
    LabelSet set;
    set.id = obj.value(QStringLiteral("id")).toString();
    set.displayName = obj.value(QStringLiteral("displayName")).toString(set.id);

    const QJsonArray names = obj.value(QStringLiteral("classes")).toArray();
    set.classNames.reserve(names.size());
    for (const QJsonValue &value : names)
        set.classNames.append(value.toString());

    return set;
}

QJsonObject ModelSpec::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("displayName"), displayName);
    if (!description.isEmpty())
        obj.insert(QStringLiteral("description"), description);
    obj.insert(QStringLiteral("task"), modelTaskToString(task));
    obj.insert(QStringLiteral("runtime"), modelRuntimeToString(runtime));
    obj.insert(QStringLiteral("fileName"), fileName);
    if (!downloadUrl.isEmpty())
        obj.insert(QStringLiteral("url"), downloadUrl);
    if (!sha256.isEmpty())
        obj.insert(QStringLiteral("sha256"), sha256);
    if (downloadBytes > 0)
        obj.insert(QStringLiteral("bytes"), static_cast<double>(downloadBytes));
    if (!sourceHint.isEmpty())
        obj.insert(QStringLiteral("sourceHint"), sourceHint);
    if (inputSize.isValid()) {
        obj.insert(QStringLiteral("inputWidth"), inputSize.width());
        obj.insert(QStringLiteral("inputHeight"), inputSize.height());
    }
    if (!labelSetId.isEmpty())
        obj.insert(QStringLiteral("labelSet"), labelSetId);
    if (!companionId.isEmpty())
        obj.insert(QStringLiteral("companion"), companionId);
    if (!license.isEmpty())
        obj.insert(QStringLiteral("license"), license);
    if (!licenseUrl.isEmpty())
        obj.insert(QStringLiteral("licenseUrl"), licenseUrl);
    if (bundled)
        obj.insert(QStringLiteral("bundled"), true);
    return obj;
}

ModelSpec ModelSpec::fromJson(const QJsonObject &obj)
{
    ModelSpec spec;
    spec.id = obj.value(QStringLiteral("id")).toString();
    spec.displayName = obj.value(QStringLiteral("displayName")).toString(spec.id);
    spec.description = obj.value(QStringLiteral("description")).toString();
    spec.task = modelTaskFromString(obj.value(QStringLiteral("task")).toString());
    spec.runtime = modelRuntimeFromString(obj.value(QStringLiteral("runtime")).toString());
    spec.fileName = obj.value(QStringLiteral("fileName")).toString();
    spec.downloadUrl = obj.value(QStringLiteral("url")).toString();
    spec.sha256 = obj.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    spec.downloadBytes = static_cast<qint64>(obj.value(QStringLiteral("bytes")).toDouble());
    spec.sourceHint = obj.value(QStringLiteral("sourceHint")).toString();

    const int width = obj.value(QStringLiteral("inputWidth")).toInt();
    const int height = obj.value(QStringLiteral("inputHeight")).toInt();
    if (width > 0 && height > 0)
        spec.inputSize = QSize(width, height);

    spec.labelSetId = obj.value(QStringLiteral("labelSet")).toString();
    spec.companionId = obj.value(QStringLiteral("companion")).toString();
    spec.license = obj.value(QStringLiteral("license")).toString();
    spec.licenseUrl = obj.value(QStringLiteral("licenseUrl")).toString();
    spec.bundled = obj.value(QStringLiteral("bundled")).toBool(false);

    // A spec with no explicit file name still needs one to look for on disk.
    if (spec.fileName.isEmpty() && !spec.id.isEmpty())
        spec.fileName = spec.id + QStringLiteral(".onnx");

    return spec;
}
