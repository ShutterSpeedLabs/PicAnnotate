#include "yoloformat.h"
#include "frameexporter.h"
#include "iogeometry.h"

#include "../core/imagefoldersource.h"
#include "../core/project.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

#include <algorithm>

namespace {

// --- data.yaml -------------------------------------------------------------
//
// A full YAML parser is overkill here: Ultralytics dataset files use a handful of
// scalar keys plus a class list in one of three shapes. Anything outside that
// subset is reported rather than silently misread.
struct DataYaml
{
    QString rootPath;
    QString trainEntry;
    QString valEntry;
    QStringList names;
    int keypointCount = 0;
    int keypointDims = 3;
};

QString stripInlineComment(const QString &line)
{
    const int hash = line.indexOf('#');
    return hash < 0 ? line : line.left(hash);
}

QStringList parseInlineList(const QString &raw)
{
    QString text = raw.trimmed();
    if (text.startsWith('[') && text.endsWith(']'))
        text = text.mid(1, text.size() - 2);

    QStringList items;
    for (const QString &part : text.split(',', Qt::SkipEmptyParts)) {
        QString item = part.trimmed();
        if ((item.startsWith('\'') && item.endsWith('\'')) || (item.startsWith('"') && item.endsWith('"')))
            item = item.mid(1, item.size() - 2);
        if (!item.isEmpty())
            items.append(item);
    }
    return items;
}

int indentOf(const QString &line)
{
    int indent = 0;
    while (indent < line.size() && line.at(indent) == QLatin1Char(' '))
        ++indent;
    return indent;
}

bool parseDataYaml(const QString &path, DataYaml *out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Could not read ") + path;
        return false;
    }

    QStringList rawLines;
    QTextStream stream(&file);
    while (!stream.atEnd())
        rawLines.append(stream.readLine());

    QMap<int, QString> indexedNames;
    QStringList listedNames;

    for (int i = 0; i < rawLines.size(); ++i) {
        const QString line = stripInlineComment(rawLines.at(i));
        if (line.trimmed().isEmpty())
            continue;
        if (indentOf(line) != 0)
            continue;

        const int colon = line.indexOf(':');
        if (colon < 0)
            continue;

        const QString key = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();

        if (key == QStringLiteral("path")) {
            out->rootPath = value;
        } else if (key == QStringLiteral("train")) {
            out->trainEntry = value;
        } else if (key == QStringLiteral("val")) {
            out->valEntry = value;
        } else if (key == QStringLiteral("kpt_shape")) {
            const QStringList dims = parseInlineList(value);
            if (dims.size() >= 1)
                out->keypointCount = dims.at(0).toInt();
            if (dims.size() >= 2)
                out->keypointDims = dims.at(1).toInt();
        } else if (key == QStringLiteral("names")) {
            if (!value.isEmpty()) {
                listedNames = parseInlineList(value);
                continue;
            }

            // Block form: consume the indented lines that follow.
            for (int j = i + 1; j < rawLines.size(); ++j) {
                const QString child = stripInlineComment(rawLines.at(j));
                if (child.trimmed().isEmpty())
                    continue;
                if (indentOf(child) == 0)
                    break;

                const QString trimmed = child.trimmed();
                if (trimmed.startsWith('-')) {
                    QString item = trimmed.mid(1).trimmed();
                    if ((item.startsWith('\'') && item.endsWith('\''))
                        || (item.startsWith('"') && item.endsWith('"'))) {
                        item = item.mid(1, item.size() - 2);
                    }
                    listedNames.append(item);
                    continue;
                }

                const int childColon = trimmed.indexOf(':');
                if (childColon < 0)
                    continue;

                bool ok = false;
                const int index = trimmed.left(childColon).trimmed().toInt(&ok);
                QString item = trimmed.mid(childColon + 1).trimmed();
                if ((item.startsWith('\'') && item.endsWith('\''))
                    || (item.startsWith('"') && item.endsWith('"'))) {
                    item = item.mid(1, item.size() - 2);
                }
                if (ok)
                    indexedNames.insert(index, item);
            }
        }
    }

    if (!indexedNames.isEmpty()) {
        // Fill gaps so class indices keep lining up with positions.
        const int highest = indexedNames.lastKey();
        for (int i = 0; i <= highest; ++i)
            out->names.append(indexedNames.value(i, QStringLiteral("class_%1").arg(i)));
    } else {
        out->names = listedNames;
    }

    if (out->names.isEmpty()) {
        if (error)
            *error = QStringLiteral("data.yaml has no usable \"names\" entry.");
        return false;
    }
    return true;
}

QString locateDataYaml(const QString &path)
{
    const QFileInfo info(path);
    if (info.isFile())
        return info.absoluteFilePath();

    const QDir dir(path);
    for (const QString &candidate : {QStringLiteral("data.yaml"), QStringLiteral("data.yml"),
                                     QStringLiteral("dataset.yaml"), QStringLiteral("dataset.yml")}) {
        if (dir.exists(candidate))
            return dir.absoluteFilePath(candidate);
    }
    return QString();
}

// Maps labels/train/x.txt -> images/train/x.<ext>, falling back to an image
// sitting next to the label file.
QString findImageForLabel(const QString &labelPath, const QDir &root)
{
    const QFileInfo labelInfo(labelPath);
    const QString stem = labelInfo.completeBaseName();

    QStringList candidateDirs;
    QString relativeDir = root.relativeFilePath(labelInfo.absolutePath());
    if (!relativeDir.startsWith(QStringLiteral(".."))) {
        QStringList parts = relativeDir.split('/', Qt::SkipEmptyParts);
        for (QString &part : parts) {
            if (part.compare(QStringLiteral("labels"), Qt::CaseInsensitive) == 0) {
                part = QStringLiteral("images");
                break;
            }
        }
        candidateDirs << root.absoluteFilePath(parts.join('/'));
    }
    candidateDirs << labelInfo.absolutePath();

    for (const QString &dirPath : candidateDirs) {
        const QDir dir(dirPath);
        for (const QString &filter : ImageFolderSource::supportedNameFilters()) {
            const QString extension = filter.mid(1); // "*.png" -> ".png"
            const QString candidate = dir.absoluteFilePath(stem + extension);
            if (QFileInfo::exists(candidate))
                return candidate;
        }
    }
    return QString();
}

QString normalizedPair(double x, double y)
{
    return IoGeometry::formatNormalized(IoGeometry::clamp01(x)) + QLatin1Char(' ')
         + IoGeometry::formatNormalized(IoGeometry::clamp01(y));
}

} // namespace

IoReport YoloFormat::exportDataset(const Project &project, const ExportOptions &options) const
{
    IoReport report;

    if (!project.hasSource()) {
        report.fail(QStringLiteral("Open a source before exporting."));
        return report;
    }
    if (options.outputDir.isEmpty()) {
        report.fail(QStringLiteral("No output directory selected."));
        return report;
    }

    const QList<int> frameIndices = FrameExporter::framesToExport(project, options);
    if (frameIndices.isEmpty()) {
        report.fail(QStringLiteral("Nothing to export: no annotated frames."));
        return report;
    }

    const LabelSchema &schema = project.labelSchema();
    if (schema.classes().isEmpty()) {
        report.fail(QStringLiteral("The project has no label classes to export."));
        return report;
    }

    if (!options.copyImages && project.isVideoSource()) {
        report.fail(QStringLiteral("A video source has no image files to point at. "
                                   "Enable \"copy images\" to export frames."));
        return report;
    }

    QDir outputRoot(options.outputDir);
    if (!outputRoot.mkpath(QStringLiteral("."))) {
        report.fail(QStringLiteral("Could not create output directory: ") + options.outputDir);
        return report;
    }

    // Pose datasets declare one kpt_shape for every class, so settle on K up
    // front by looking at the skeletons that are actually going to be written.
    int keypointCount = 0;
    if (options.task == DatasetTask::Keypoints) {
        QSet<int> distinctCounts;
        for (int frameIndex : frameIndices) {
            for (const AnnoShape &shape : project.resolvedShapes(frameIndex)) {
                if (shape.type() != ShapeType::Skeleton)
                    continue;
                const int count = schema.templateForClass(shape.labelId()).pointCount();
                distinctCounts.insert(count);
                keypointCount = qMax(keypointCount, count);
            }
        }

        if (keypointCount == 0) {
            report.fail(QStringLiteral("Pose export needs skeleton shapes, and none were found."));
            return report;
        }
        if (distinctCounts.size() > 1) {
            report.warn(QStringLiteral("Classes use keypoint templates of different lengths; "
                                       "kpt_shape is [%1, 3] and shorter skeletons are padded "
                                       "with unlabeled points.").arg(keypointCount));
        }
    }

    const bool split = options.valSplit > 0.0;
    QSet<QString> createdDirs;
    const auto ensureDir = [&](const QString &relative) -> bool {
        if (createdDirs.contains(relative))
            return true;
        if (!outputRoot.mkpath(relative)) {
            report.fail(QStringLiteral("Could not create directory: ") + relative);
            return false;
        }
        createdDirs.insert(relative);
        return true;
    };

    int trainCount = 0;
    int valCount = 0;

    for (int ordinal = 0; ordinal < frameIndices.size(); ++ordinal) {
        const int frameIndex = frameIndices.at(ordinal);
        const QSize imageSize = project.frameSize(frameIndex);
        if (!imageSize.isValid() || imageSize.isEmpty()) {
            report.warn(QStringLiteral("Frame %1 skipped: image size unknown.").arg(frameIndex));
            ++report.skipped;
            continue;
        }

        const double width = imageSize.width();
        const double height = imageSize.height();
        const bool validation = split && FrameExporter::isValidationFrame(ordinal, options.valSplit);
        const QString splitDir = FrameExporter::splitName(validation);

        QStringList rows;
        // The resolved view, so interpolated track frames get labels too.
        for (const AnnoShape &shape : project.resolvedShapes(frameIndex)) {
            const int classIndex = schema.classIndex(shape.labelId());
            if (classIndex < 0) {
                report.warn(QStringLiteral("Shape with no label skipped on frame %1.").arg(frameIndex));
                ++report.skipped;
                continue;
            }

            if (options.task == DatasetTask::Detection) {
                if (shape.type() == ShapeType::Polyline || shape.type() == ShapeType::Keypoint) {
                    report.warn(QStringLiteral("%1 shapes carry no box and were skipped for detect export.")
                                    .arg(shapeTypeToString(shape.type())));
                    ++report.skipped;
                    continue;
                }

                const QRectF box = IoGeometry::clampRectToImage(shape.boundingRect(), imageSize);
                if (!IoGeometry::isExportableRect(box)) {
                    ++report.skipped;
                    continue;
                }
                if (shape.isRotated()) {
                    report.warn(QStringLiteral("Rotated boxes were written as their axis-aligned "
                                               "bounds. Export the \"Oriented detection\" task to "
                                               "keep the angle."));
                }

                rows.append(QStringLiteral("%1 %2 %3 %4 %5")
                                .arg(classIndex)
                                .arg(IoGeometry::formatNormalized(IoGeometry::clamp01(box.center().x() / width)))
                                .arg(IoGeometry::formatNormalized(IoGeometry::clamp01(box.center().y() / height)))
                                .arg(IoGeometry::formatNormalized(IoGeometry::clamp01(box.width() / width)))
                                .arg(IoGeometry::formatNormalized(IoGeometry::clamp01(box.height() / height))));
                ++report.shapes;

            } else if (options.task == DatasetTask::OrientedDetection) {
                if (shape.type() != ShapeType::Rect) {
                    report.warn(QStringLiteral("%1 shapes were skipped for OBB export; only "
                                               "boxes carry an angle.")
                                    .arg(shapeTypeToString(shape.type())));
                    ++report.skipped;
                    continue;
                }

                if (!IoGeometry::isExportableRect(shape.rect())) {
                    ++report.skipped;
                    continue;
                }

                // Four corners, clockwise from the box's own top-left. Clamped
                // individually: a rotated box can poke past an edge at one corner
                // while the others stay inside.
                const QVector<QPointF> corners =
                    IoGeometry::clampPointsToImage(shape.rotatedRectCorners(), imageSize);
                if (corners.size() != 4) {
                    ++report.skipped;
                    continue;
                }

                QStringList parts{QString::number(classIndex)};
                for (const QPointF &p : corners)
                    parts.append(normalizedPair(p.x() / width, p.y() / height));
                rows.append(parts.join(' '));
                ++report.shapes;

            } else if (options.task == DatasetTask::Segmentation) {
                QVector<QPointF> ring;
                if (shape.type() == ShapeType::Polygon)
                    ring = IoGeometry::clampPointsToImage(shape.points(), imageSize);
                else if (shape.type() == ShapeType::Rect)
                    ring = IoGeometry::clampPointsToImage(shape.rotatedRectCorners(), imageSize);
                else {
                    report.warn(QStringLiteral("%1 shapes were skipped for segment export.")
                                    .arg(shapeTypeToString(shape.type())));
                    ++report.skipped;
                    continue;
                }

                if (ring.size() < 3) {
                    ++report.skipped;
                    continue;
                }

                QStringList parts{QString::number(classIndex)};
                for (const QPointF &p : ring)
                    parts.append(normalizedPair(p.x() / width, p.y() / height));
                rows.append(parts.join(' '));
                ++report.shapes;

            } else if (options.task == DatasetTask::Keypoints) {
                if (shape.type() != ShapeType::Skeleton) {
                    report.warn(QStringLiteral("%1 shapes were skipped for pose export.")
                                    .arg(shapeTypeToString(shape.type())));
                    ++report.skipped;
                    continue;
                }

                const QRectF box = IoGeometry::clampRectToImage(shape.boundingRect(), imageSize);
                if (!IoGeometry::isExportableRect(box)) {
                    ++report.skipped;
                    continue;
                }

                QStringList parts{QString::number(classIndex)};
                parts.append(IoGeometry::formatNormalized(IoGeometry::clamp01(box.center().x() / width)));
                parts.append(IoGeometry::formatNormalized(IoGeometry::clamp01(box.center().y() / height)));
                parts.append(IoGeometry::formatNormalized(IoGeometry::clamp01(box.width() / width)));
                parts.append(IoGeometry::formatNormalized(IoGeometry::clamp01(box.height() / height)));

                const QVector<QPointF> points = shape.points();
                for (int i = 0; i < keypointCount; ++i) {
                    const PointVisibility visibility =
                        i < points.size() ? shape.visibilityAt(i) : PointVisibility::NotLabeled;

                    if (i >= points.size() || visibility == PointVisibility::NotLabeled) {
                        parts.append(QStringLiteral("0 0 0"));
                        continue;
                    }
                    const QPointF p = points.at(i);
                    parts.append(normalizedPair(p.x() / width, p.y() / height));
                    parts.append(QString::number(static_cast<int>(visibility)));
                }
                rows.append(parts.join(' '));
                ++report.shapes;
            }
        }

        if (rows.isEmpty() && !options.includeEmptyFrames)
            continue;

        const QString imagesRelative = QStringLiteral("images/") + splitDir;
        const QString labelsRelative = QStringLiteral("labels/") + splitDir;
        if (!ensureDir(imagesRelative) || !ensureDir(labelsRelative))
            return report;

        const QString fileName = FrameExporter::imageFileName(project, frameIndex);
        if (options.copyImages) {
            const QString destPath =
                outputRoot.absoluteFilePath(imagesRelative + QLatin1Char('/') + fileName);
            QString copyError;
            if (!FrameExporter::writeFrameImage(project, frameIndex, destPath, &copyError)) {
                report.warn(copyError);
                ++report.skipped;
                continue;
            }
        }

        const QString labelPath = outputRoot.absoluteFilePath(
            labelsRelative + QLatin1Char('/') + QFileInfo(fileName).completeBaseName()
            + QStringLiteral(".txt"));

        QFile labelFile(labelPath);
        if (!labelFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            report.fail(QStringLiteral("Could not write ") + labelPath);
            return report;
        }
        QTextStream out(&labelFile);
        for (const QString &row : rows)
            out << row << '\n';
        labelFile.close();

        ++report.frames;
        if (validation)
            ++valCount;
        else
            ++trainCount;
    }

    if (report.frames == 0) {
        report.fail(QStringLiteral("Nothing was written — every frame was skipped."));
        return report;
    }

    // --- data.yaml -----------------------------------------------------------
    QStringList yaml;
    yaml << QStringLiteral("# Generated by PicAnnotate (%1)").arg(datasetTaskToString(options.task));
    yaml << QStringLiteral("path: %1").arg(QDir::toNativeSeparators(outputRoot.absolutePath()));
    yaml << QStringLiteral("train: images/train");

    if (valCount > 0) {
        yaml << QStringLiteral("val: images/val");
    } else {
        // Ultralytics requires a val key; pointing it at train is the usual
        // stand-in, but the user should know it is not a real holdout.
        yaml << QStringLiteral("val: images/train");
        report.warn(QStringLiteral("No validation split was produced, so data.yaml points "
                                   "\"val\" at the training images."));
    }

    yaml << QStringLiteral("nc: %1").arg(schema.classes().size());
    if (options.task == DatasetTask::Keypoints)
        yaml << QStringLiteral("kpt_shape: [%1, 3]").arg(keypointCount);

    yaml << QStringLiteral("names:");
    const QVector<LabelClass> &classes = schema.classes();
    for (int i = 0; i < classes.size(); ++i)
        yaml << QStringLiteral("  %1: %2").arg(i).arg(classes.at(i).name);

    const QString yamlPath = outputRoot.absoluteFilePath(QStringLiteral("data.yaml"));
    QFile yamlFile(yamlPath);
    if (!yamlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        report.fail(QStringLiteral("Could not write ") + yamlPath);
        return report;
    }
    QTextStream yamlStream(&yamlFile);
    for (const QString &line : yaml)
        yamlStream << line << '\n';
    yamlFile.close();

    report.outputs << QStringLiteral("data.yaml")
                   << QStringLiteral("labels/train (%1 files)").arg(trainCount);
    if (valCount > 0)
        report.outputs << QStringLiteral("labels/val (%1 files)").arg(valCount);
    if (options.copyImages)
        report.outputs << QStringLiteral("images/");
    else
        report.warn(QStringLiteral("Images were not copied; place them under images/ yourself "
                                   "before training."));

    report.ok = true;
    return report;
}

IoReport YoloFormat::importDataset(const ImportOptions &options, ImportedDataset *out) const
{
    IoReport report;
    if (!out) {
        report.fail(QStringLiteral("Internal error: no destination for imported data."));
        return report;
    }

    const QString yamlPath = locateDataYaml(options.path);
    if (yamlPath.isEmpty()) {
        report.fail(QStringLiteral("No data.yaml found in %1. Select the dataset root or its "
                                   "data.yaml directly.").arg(options.path));
        return report;
    }

    DataYaml yaml;
    QString yamlError;
    if (!parseDataYaml(yamlPath, &yaml, &yamlError)) {
        report.fail(yamlError);
        return report;
    }

    // The dataset root is data.yaml's own directory; a `path:` key is usually an
    // absolute path from whichever machine exported it and is often stale.
    const QDir root = QFileInfo(yamlPath).absoluteDir();

    QVector<int> classIdByIndex;
    classIdByIndex.reserve(yaml.names.size());
    QString templateId;
    if (yaml.keypointCount > 0) {
        QStringList pointNames;
        pointNames.reserve(yaml.keypointCount);
        // COCO-17 is by far the most common pose layout, so borrow its names and
        // edges when the count matches; otherwise fall back to positional names.
        const KeypointTemplate coco = KeypointTemplate::coco17();
        if (yaml.keypointCount == coco.pointCount()) {
            pointNames = coco.pointNames;
        } else {
            for (int i = 0; i < yaml.keypointCount; ++i)
                pointNames.append(QStringLiteral("point_%1").arg(i));
        }

        templateId = QStringLiteral("yolo_pose_%1").arg(yaml.keypointCount);
        out->schema.addKeypointTemplate(
            KeypointTemplate::fromNames(templateId, QStringLiteral("YOLO pose (%1)").arg(yaml.keypointCount),
                                        pointNames, yaml.keypointCount == coco.pointCount()
                                                        ? coco.edges
                                                        : QVector<QPair<int, int>>()));
    }

    for (const QString &name : yaml.names) {
        ShapeTypeFlags allowed = RectShapeFlag | PolygonShapeFlag;
        if (yaml.keypointCount > 0)
            allowed |= SkeletonShapeFlag;
        classIdByIndex.append(out->schema.ensureClass(name, allowed, templateId));
    }

    // --- collect label files -------------------------------------------------
    QStringList labelFiles;
    const QString labelsRoot = root.absoluteFilePath(QStringLiteral("labels"));
    if (QFileInfo(labelsRoot).isDir()) {
        QDirIterator it(labelsRoot, {QStringLiteral("*.txt")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
            labelFiles.append(it.next());
    } else {
        QDirIterator it(root.absolutePath(), {QStringLiteral("*.txt")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString candidate = it.next();
            if (QFileInfo(candidate).fileName().compare(QStringLiteral("classes.txt"),
                                                        Qt::CaseInsensitive) != 0) {
                labelFiles.append(candidate);
            }
        }
    }

    if (labelFiles.isEmpty()) {
        report.fail(QStringLiteral("No .txt label files found under ") + root.absolutePath());
        return report;
    }
    std::sort(labelFiles.begin(), labelFiles.end());

    QHash<QString, int> directoryVotes;
    struct PendingFrame
    {
        QString imagePath;
        FrameAnnotations annotations;
    };
    QList<PendingFrame> pending;

    for (const QString &labelPath : labelFiles) {
        const QString imagePath = findImageForLabel(labelPath, root);
        if (imagePath.isEmpty()) {
            report.warn(QStringLiteral("No image found for %1.")
                            .arg(root.relativeFilePath(labelPath)));
            ++report.skipped;
            continue;
        }

        const QSize imageSize = QImageReader(imagePath).size();
        if (!imageSize.isValid() || imageSize.isEmpty()) {
            report.warn(QStringLiteral("Could not read the size of %1.")
                            .arg(QFileInfo(imagePath).fileName()));
            ++report.skipped;
            continue;
        }

        QFile file(labelPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            report.warn(QStringLiteral("Could not read ") + labelPath);
            ++report.skipped;
            continue;
        }

        PendingFrame frame;
        frame.imagePath = imagePath;
        frame.annotations.setImageSize(imageSize);

        QTextStream stream(&file);
        int lineNumber = 0;
        while (!stream.atEnd()) {
            ++lineNumber;
            const QString line = stream.readLine().trimmed();
            if (line.isEmpty())
                continue;

            const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                                  Qt::SkipEmptyParts);
            if (tokens.size() < 5) {
                report.warn(QStringLiteral("%1:%2 has too few values.")
                                .arg(QFileInfo(labelPath).fileName()).arg(lineNumber));
                ++report.skipped;
                continue;
            }

            bool classOk = false;
            const int classIndex = tokens.first().toInt(&classOk);
            if (!classOk || classIndex < 0 || classIndex >= classIdByIndex.size()) {
                report.warn(QStringLiteral("%1:%2 references class index %3, which data.yaml "
                                           "does not define.")
                                .arg(QFileInfo(labelPath).fileName())
                                .arg(lineNumber)
                                .arg(tokens.first()));
                ++report.skipped;
                continue;
            }
            const int labelId = classIdByIndex.at(classIndex);

            QVector<double> values;
            values.reserve(tokens.size() - 1);
            bool numbersOk = true;
            for (int i = 1; i < tokens.size(); ++i) {
                bool ok = false;
                values.append(tokens.at(i).toDouble(&ok));
                numbersOk = numbersOk && ok;
            }
            if (!numbersOk) {
                report.warn(QStringLiteral("%1:%2 contains non-numeric values.")
                                .arg(QFileInfo(labelPath).fileName()).arg(lineNumber));
                ++report.skipped;
                continue;
            }

            const int count = values.size();
            const bool poseShape = yaml.keypointCount > 0
                && count == 4 + yaml.keypointCount * yaml.keypointDims;

            if (poseShape) {
                QVector<QPointF> points;
                QVector<int> flags;
                for (int i = 0; i < yaml.keypointCount; ++i) {
                    const int base = 4 + i * yaml.keypointDims;
                    const double x = values.at(base) * imageSize.width();
                    const double y = values.at(base + 1) * imageSize.height();
                    const int visibility = yaml.keypointDims >= 3
                        ? static_cast<int>(values.at(base + 2))
                        : static_cast<int>(PointVisibility::Visible);
                    points.append(QPointF(x, y));
                    flags.append(visibility);
                }
                frame.annotations.addShape(AnnoShape::makeSkeleton(points, labelId, flags));
                ++report.shapes;

            } else if (count == 4) {
                const double cx = values.at(0) * imageSize.width();
                const double cy = values.at(1) * imageSize.height();
                const double w = values.at(2) * imageSize.width();
                const double h = values.at(3) * imageSize.height();
                const QRectF rect(cx - w / 2.0, cy - h / 2.0, w, h);
                frame.annotations.addShape(
                    AnnoShape::makeRect(IoGeometry::clampRectToImage(rect, imageSize), labelId));
                ++report.shapes;

            } else if (count >= 6 && count % 2 == 0) {
                const QVector<QPointF> points = IoGeometry::denormalizePoints(values, imageSize);
                if (points.size() < 3) {
                    ++report.skipped;
                    continue;
                }
                frame.annotations.addShape(AnnoShape::makePolygon(
                    IoGeometry::clampPointsToImage(points, imageSize), labelId));
                ++report.shapes;

            } else {
                report.warn(QStringLiteral("%1:%2 has %3 values, which matches no YOLO row "
                                           "shape for this dataset.")
                                .arg(QFileInfo(labelPath).fileName()).arg(lineNumber).arg(count));
                ++report.skipped;
                continue;
            }
        }

        directoryVotes[QFileInfo(imagePath).absolutePath()] += 1;
        pending.append(frame);
    }

    if (pending.isEmpty()) {
        report.fail(QStringLiteral("No label file could be paired with an image."));
        return report;
    }

    // A project has one image folder, so pick the split that contributed most
    // and tell the user what was left behind.
    QString chosenDirectory;
    int bestVotes = 0;
    for (auto it = directoryVotes.constBegin(); it != directoryVotes.constEnd(); ++it) {
        if (it.value() > bestVotes) {
            bestVotes = it.value();
            chosenDirectory = it.key();
        }
    }
    if (directoryVotes.size() > 1) {
        report.warn(QStringLiteral("Images span %1 directories (train/val). Importing the largest "
                                   "group (%2 images) from %3.")
                        .arg(directoryVotes.size()).arg(bestVotes).arg(chosenDirectory));
    }

    out->imageDirectory = chosenDirectory;
    for (const PendingFrame &frame : pending) {
        if (QFileInfo(frame.imagePath).absolutePath() != chosenDirectory)
            continue;

        const QString baseName = QFileInfo(frame.imagePath).fileName();
        out->byFileName.insert(baseName, frame.annotations);
        out->imageFileNames.append(baseName);
    }

    report.frames = out->byFileName.size();
    if (report.frames == 0) {
        report.fail(QStringLiteral("No frames survived import."));
        return report;
    }

    report.ok = true;
    return report;
}
