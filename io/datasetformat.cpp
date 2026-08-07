#include "datasetformat.h"

QString datasetTaskToString(DatasetTask task)
{
    switch (task) {
    case DatasetTask::OrientedDetection:
        return "obb";
    case DatasetTask::Segmentation:
        return "segmentation";
    case DatasetTask::Keypoints:
        return "keypoints";
    case DatasetTask::Detection:
    default:
        return "detection";
    }
}

QString datasetTaskDisplayName(DatasetTask task)
{
    switch (task) {
    case DatasetTask::OrientedDetection:
        return QStringLiteral("Oriented detection (rotated boxes)");
    case DatasetTask::Segmentation:
        return QStringLiteral("Instance segmentation (polygons)");
    case DatasetTask::Keypoints:
        return QStringLiteral("Keypoints / pose (skeletons)");
    case DatasetTask::Detection:
    default:
        return QStringLiteral("Object detection (boxes)");
    }
}

void IoReport::warn(const QString &message)
{
    // A malformed dataset can produce one warning per annotation, which is
    // useless in a dialog. Keep the first N and count the rest.
    if (warnings.size() < kMaxWarnings) {
        if (!warnings.contains(message))
            warnings.append(message);
        return;
    }

    if (warnings.size() == kMaxWarnings)
        warnings.append(QStringLiteral("... further warnings suppressed."));
}

void IoReport::fail(const QString &message)
{
    ok = false;
    error = message;
}

QString IoReport::toText() const
{
    if (!ok)
        return error.isEmpty() ? QStringLiteral("Operation failed.") : error;

    QStringList lines;
    lines << QStringLiteral("Frames: %1").arg(frames);
    lines << QStringLiteral("Shapes: %1").arg(shapes);
    if (tracks > 0)
        lines << QStringLiteral("Tracks: %1").arg(tracks);
    if (skipped > 0)
        lines << QStringLiteral("Skipped: %1").arg(skipped);

    if (!outputs.isEmpty()) {
        lines << QString();
        lines << QStringLiteral("Wrote:");
        for (const QString &output : outputs)
            lines << QStringLiteral("  %1").arg(output);
    }

    if (!warnings.isEmpty()) {
        lines << QString();
        lines << QStringLiteral("Warnings:");
        for (const QString &warning : warnings)
            lines << QStringLiteral("  - %1").arg(warning);
    }

    return lines.join('\n');
}
