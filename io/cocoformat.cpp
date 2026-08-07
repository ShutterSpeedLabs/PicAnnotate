#include "cocoformat.h"
#include "frameexporter.h"
#include "iogeometry.h"

#include "../core/project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <algorithm>

namespace {

struct SplitWriter
{
    QJsonArray images;
    QJsonArray annotations;
    int frames = 0;
};

QJsonArray rectToBbox(const QRectF &rect)
{
    return QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()};
}

QJsonArray pointsToRing(const QVector<QPointF> &points)
{
    QJsonArray ring;
    for (const QPointF &p : points) {
        ring.append(p.x());
        ring.append(p.y());
    }
    return ring;
}

// COCO wraps polygon rings in an outer array. Built by append() rather than
// QJsonArray{ring}: a braced init list holding a single QJsonArray selects the
// copy constructor, which would emit one flat array instead of a nested one.
QJsonArray ringsToSegmentation(const QJsonArray &ring)
{
    QJsonArray segmentation;
    segmentation.append(ring);
    return segmentation;
}

// Base file name for the JSON of a given task/split.
QString annotationFileName(DatasetTask task, bool validation, bool split)
{
    const QString stem = task == DatasetTask::Keypoints
        ? QStringLiteral("person_keypoints")
        : QStringLiteral("instances");

    if (!split)
        return stem + QStringLiteral(".json");
    return QStringLiteral("%1_%2.json").arg(stem, FrameExporter::splitName(validation));
}

// Directories a COCO json's images might live in, most specific first. COCO
// layouts vary (annotations/ + images/, or everything in one folder), so try the
// common shapes rather than demanding one.
QStringList candidateImageRoots(const QString &annotationPath)
{
    const QDir jsonDir = QFileInfo(annotationPath).absoluteDir();

    QStringList roots;
    roots << jsonDir.absolutePath();
    roots << jsonDir.absoluteFilePath(QStringLiteral("images"));

    QDir parent = jsonDir;
    if (parent.cdUp()) {
        roots << parent.absolutePath();
        roots << parent.absoluteFilePath(QStringLiteral("images"));

        QDir grandParent = parent;
        if (grandParent.cdUp()) {
            roots << grandParent.absolutePath();
            roots << grandParent.absoluteFilePath(QStringLiteral("images"));
        }
    }
    return roots;
}

QString resolveImagePath(const QStringList &roots, const QString &relativeName)
{
    for (const QString &root : roots) {
        const QString candidate = QDir(root).absoluteFilePath(relativeName);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return QString();
}

// True when `segmentation` is a list of flat coordinate rings (polygon form)
// rather than an RLE object.
bool isPolygonSegmentation(const QJsonValue &value)
{
    if (!value.isArray())
        return false;
    const QJsonArray outer = value.toArray();
    return !outer.isEmpty() && outer.first().isArray();
}

QVector<QPointF> ringToPoints(const QJsonArray &ring)
{
    QVector<QPointF> points;
    points.reserve(ring.size() / 2);
    for (int i = 0; i + 1 < ring.size(); i += 2)
        points.append(QPointF(ring.at(i).toDouble(), ring.at(i + 1).toDouble()));
    return points;
}

struct CocoCategory
{
    QString name;
    QStringList keypointNames;
    QVector<QPair<int, int>> skeletonEdges;   // 0-based
    int labelId = -1;
    QString templateId;
};

} // namespace

IoReport CocoFormat::exportDataset(const Project &project, const ExportOptions &options) const
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

    QDir outputRoot(options.outputDir);
    if (!outputRoot.mkpath(QStringLiteral("."))) {
        report.fail(QStringLiteral("Could not create output directory: ") + options.outputDir);
        return report;
    }

    const bool split = options.valSplit > 0.0;
    if (!outputRoot.mkpath(QStringLiteral("annotations"))) {
        report.fail(QStringLiteral("Could not create annotations directory."));
        return report;
    }

    const LabelSchema &schema = project.labelSchema();

    // Category ids are 1-based and follow the schema's class order, so the same
    // project always exports the same ids.
    QJsonArray categories;
    QHash<int, int> labelToCategory;
    const QVector<LabelClass> &classes = schema.classes();
    for (int i = 0; i < classes.size(); ++i) {
        const LabelClass &label = classes.at(i);
        const int categoryId = i + 1;
        labelToCategory.insert(label.id, categoryId);

        QJsonObject category;
        category["id"] = categoryId;
        category["name"] = label.name;
        category["supercategory"] = QString();

        if (options.task == DatasetTask::Keypoints) {
            const KeypointTemplate tmpl = schema.templateForClass(label.id);
            QJsonArray names;
            for (const QString &pointName : tmpl.pointNames)
                names.append(pointName);

            QJsonArray skeleton;
            for (const auto &edge : tmpl.oneBasedEdges())
                skeleton.append(QJsonArray{edge.first, edge.second});

            category["keypoints"] = names;
            category["skeleton"] = skeleton;
        }
        categories.append(category);
    }

    if (options.copyImages && !outputRoot.mkpath(QStringLiteral("images"))) {
        report.fail(QStringLiteral("Could not create images directory."));
        return report;
    }

    SplitWriter trainSplit;
    SplitWriter valSplit;
    int nextAnnotationId = 1;

    for (int ordinal = 0; ordinal < frameIndices.size(); ++ordinal) {
        const int frameIndex = frameIndices.at(ordinal);
        const QSize imageSize = project.frameSize(frameIndex);
        if (!imageSize.isValid() || imageSize.isEmpty()) {
            report.warn(QStringLiteral("Frame %1 skipped: image size unknown.").arg(frameIndex));
            ++report.skipped;
            continue;
        }

        const bool validation = split && FrameExporter::isValidationFrame(ordinal, options.valSplit);
        SplitWriter &target = validation ? valSplit : trainSplit;

        const QString fileName = FrameExporter::imageFileName(project, frameIndex);
        const int imageId = frameIndex + 1;

        QString imageRelativePath = fileName;
        if (options.copyImages) {
            QString subdir = QStringLiteral("images");
            if (split) {
                subdir += QLatin1Char('/') + FrameExporter::splitName(validation);
                if (!outputRoot.mkpath(subdir)) {
                    report.fail(QStringLiteral("Could not create directory: ") + subdir);
                    return report;
                }
            }

            const QString destPath = outputRoot.absoluteFilePath(subdir + QLatin1Char('/') + fileName);
            QString copyError;
            if (!FrameExporter::writeFrameImage(project, frameIndex, destPath, &copyError)) {
                report.warn(copyError);
                ++report.skipped;
                continue;
            }
            imageRelativePath = subdir + QLatin1Char('/') + fileName;
        }

        QJsonObject image;
        image["id"] = imageId;
        image["file_name"] = imageRelativePath;
        image["width"] = imageSize.width();
        image["height"] = imageSize.height();
        if (project.isVideoSource())
            image["frame_index"] = frameIndex;
        target.images.append(image);
        ++target.frames;

        // The resolved view, so interpolated track frames export as real
        // annotations rather than being silently dropped.
        for (const AnnoShape &shape : project.resolvedShapes(frameIndex)) {
            const auto categoryIt = labelToCategory.constFind(shape.labelId());
            if (categoryIt == labelToCategory.constEnd()) {
                report.warn(QStringLiteral("Shape with no label skipped on frame %1.").arg(frameIndex));
                ++report.skipped;
                continue;
            }

            QJsonObject annotation;
            annotation["id"] = nextAnnotationId;
            annotation["image_id"] = imageId;
            annotation["category_id"] = categoryIt.value();
            annotation["iscrowd"] = 0;

            bool emitted = false;

            if (options.task == DatasetTask::Detection) {
                if (shape.type() == ShapeType::Polyline || shape.type() == ShapeType::Keypoint) {
                    report.warn(QStringLiteral("%1 shapes carry no box and were skipped for detection export.")
                                    .arg(shapeTypeToString(shape.type())));
                    ++report.skipped;
                    continue;
                }

                const QRectF box = IoGeometry::clampRectToImage(shape.boundingRect(), imageSize);
                if (!IoGeometry::isExportableRect(box)) {
                    report.warn(QStringLiteral("Degenerate box skipped on frame %1.").arg(frameIndex));
                    ++report.skipped;
                    continue;
                }
                if (shape.isRotated()) {
                    report.warn(QStringLiteral("Rotated boxes were written as their axis-aligned bounds; "
                                               "COCO has no rotation field."));
                }

                annotation["bbox"] = rectToBbox(box);
                annotation["area"] = box.width() * box.height();
                emitted = true;

            } else if (options.task == DatasetTask::Segmentation) {
                QVector<QPointF> ring;
                if (shape.type() == ShapeType::Polygon) {
                    ring = IoGeometry::clampPointsToImage(shape.points(), imageSize);
                } else if (shape.type() == ShapeType::Rect) {
                    // A box is a perfectly good 4-point mask, and exporting it
                    // keeps mixed projects usable for segmentation training.
                    ring = IoGeometry::clampPointsToImage(shape.rotatedRectCorners(), imageSize);
                } else {
                    report.warn(QStringLiteral("%1 shapes were skipped for segmentation export.")
                                    .arg(shapeTypeToString(shape.type())));
                    ++report.skipped;
                    continue;
                }

                if (ring.size() < 3) {
                    report.warn(QStringLiteral("Polygon with fewer than 3 points skipped on frame %1.")
                                    .arg(frameIndex));
                    ++report.skipped;
                    continue;
                }

                const QRectF bounds = IoGeometry::boundsOfPoints(ring);
                annotation["segmentation"] = ringsToSegmentation(pointsToRing(ring));
                annotation["area"] = IoGeometry::polygonArea(ring);
                annotation["bbox"] = rectToBbox(bounds);
                emitted = true;

            } else if (options.task == DatasetTask::Keypoints) {
                if (shape.type() != ShapeType::Skeleton) {
                    report.warn(QStringLiteral("%1 shapes were skipped for keypoint export.")
                                    .arg(shapeTypeToString(shape.type())));
                    ++report.skipped;
                    continue;
                }

                const KeypointTemplate tmpl = schema.templateForClass(shape.labelId());
                const int expected = tmpl.pointCount();
                const QVector<QPointF> points = shape.points();
                if (points.size() != expected) {
                    report.warn(QStringLiteral("Skeleton on frame %1 has %2 points but its template "
                                               "defines %3; missing points written as unlabeled.")
                                    .arg(frameIndex).arg(points.size()).arg(expected));
                }

                QJsonArray keypoints;
                int labeled = 0;
                for (int i = 0; i < expected; ++i) {
                    const PointVisibility visibility =
                        i < points.size() ? shape.visibilityAt(i) : PointVisibility::NotLabeled;

                    if (i >= points.size() || visibility == PointVisibility::NotLabeled) {
                        keypoints.append(0);
                        keypoints.append(0);
                        keypoints.append(0);
                        continue;
                    }

                    const QPointF p = points.at(i);
                    keypoints.append(qBound(0.0, p.x(), static_cast<double>(imageSize.width())));
                    keypoints.append(qBound(0.0, p.y(), static_cast<double>(imageSize.height())));
                    keypoints.append(static_cast<int>(visibility));
                    ++labeled;
                }

                if (labeled == 0) {
                    report.warn(QStringLiteral("Skeleton with no labeled points skipped on frame %1.")
                                    .arg(frameIndex));
                    ++report.skipped;
                    continue;
                }

                const QRectF box = IoGeometry::clampRectToImage(shape.boundingRect(), imageSize);
                annotation["keypoints"] = keypoints;
                annotation["num_keypoints"] = labeled;
                annotation["bbox"] = rectToBbox(box);
                annotation["area"] = box.width() * box.height();
                emitted = true;
            }

            if (!emitted)
                continue;

            if (shape.trackId() >= 0) {
                // Not part of the COCO spec, but every video-capable consumer
                // looks for one of these two spellings.
                annotation["track_id"] = shape.trackId();
                annotation["attributes"] = QJsonObject{{"track_id", shape.trackId()}};
            }
            if (!shape.attributes().isEmpty()) {
                QJsonObject attributes = annotation["attributes"].toObject();
                const QVariantMap extra = shape.attributes();
                for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
                    attributes.insert(it.key(), QJsonValue::fromVariant(it.value()));
                annotation["attributes"] = attributes;
            }

            target.annotations.append(annotation);
            ++nextAnnotationId;
            ++report.shapes;
        }
    }

    QJsonObject info;
    info["description"] = QStringLiteral("Exported by PicAnnotate");
    info["task"] = datasetTaskToString(options.task);
    info["version"] = QStringLiteral("1.0");

    const auto writeSplit = [&](const SplitWriter &writer, bool validation) -> bool {
        if (writer.images.isEmpty())
            return true;

        QJsonObject root;
        root["info"] = info;
        root["licenses"] = QJsonArray();
        root["images"] = writer.images;
        root["annotations"] = writer.annotations;
        root["categories"] = categories;

        const QString relativePath =
            QStringLiteral("annotations/") + annotationFileName(options.task, validation, split);
        const QString path = outputRoot.absoluteFilePath(relativePath);

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            report.fail(QStringLiteral("Could not write ") + path);
            return false;
        }
        file.write(QJsonDocument(root).toJson());

        report.outputs.append(relativePath);
        report.frames += writer.frames;
        return true;
    };

    if (!writeSplit(trainSplit, false))
        return report;
    if (split && !writeSplit(valSplit, true))
        return report;

    if (!options.copyImages)
        report.warn(QStringLiteral("Images were not copied; file_name values are bare names "
                                   "relative to wherever you place the images."));

    report.ok = true;
    return report;
}

IoReport CocoFormat::importDataset(const ImportOptions &options, ImportedDataset *out) const
{
    IoReport report;
    if (!out) {
        report.fail(QStringLiteral("Internal error: no destination for imported data."));
        return report;
    }

    QFile file(options.path);
    if (!file.open(QIODevice::ReadOnly)) {
        report.fail(QStringLiteral("Could not read ") + options.path);
        return report;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull() || !doc.isObject()) {
        report.fail(QStringLiteral("Not valid COCO JSON: ") + parseError.errorString());
        return report;
    }

    const QJsonObject root = doc.object();
    if (!root.contains("images") || !root.contains("annotations")) {
        report.fail(QStringLiteral("File has no \"images\"/\"annotations\" arrays — "
                                   "this does not look like a COCO dataset."));
        return report;
    }

    // --- categories -> schema classes (+ keypoint templates) ------------------
    QHash<int, CocoCategory> categories;
    const QJsonArray categoriesArray = root["categories"].toArray();
    for (const QJsonValue &value : categoriesArray) {
        const QJsonObject obj = value.toObject();

        CocoCategory category;
        category.name = obj["name"].toString();
        if (category.name.isEmpty())
            category.name = QStringLiteral("category_%1").arg(obj["id"].toInt());

        const QJsonArray keypointsArray = obj["keypoints"].toArray();
        for (const QJsonValue &kp : keypointsArray)
            category.keypointNames.append(kp.toString());

        const QJsonArray skeletonArray = obj["skeleton"].toArray();
        for (const QJsonValue &edgeValue : skeletonArray) {
            const QJsonArray pair = edgeValue.toArray();
            if (pair.size() == 2) {
                // COCO skeleton indices are 1-based.
                category.skeletonEdges.append({pair.at(0).toInt() - 1, pair.at(1).toInt() - 1});
            }
        }

        ShapeTypeFlags allowed = RectShapeFlag | PolygonShapeFlag;
        if (!category.keypointNames.isEmpty()) {
            allowed |= SkeletonShapeFlag;
            category.templateId = QStringLiteral("coco_%1").arg(category.name);
            out->schema.addKeypointTemplate(KeypointTemplate::fromNames(
                category.templateId, category.name, category.keypointNames, category.skeletonEdges));
        }

        category.labelId = out->schema.ensureClass(category.name, allowed, category.templateId);
        categories.insert(obj["id"].toInt(), category);
    }

    // --- images --------------------------------------------------------------
    struct CocoImage
    {
        QString relativeName;
        QSize size;
    };
    QHash<int, CocoImage> images;
    QList<int> imageOrder;

    const QJsonArray imagesArray = root["images"].toArray();
    for (const QJsonValue &value : imagesArray) {
        const QJsonObject obj = value.toObject();
        const int imageId = obj["id"].toInt(-1);
        const QString name = obj["file_name"].toString();
        if (imageId < 0 || name.isEmpty()) {
            report.warn(QStringLiteral("Image entry without id/file_name skipped."));
            continue;
        }

        CocoImage image;
        image.relativeName = name;
        image.size = QSize(obj["width"].toInt(0), obj["height"].toInt(0));
        images.insert(imageId, image);
        imageOrder.append(imageId);
    }

    if (images.isEmpty()) {
        report.fail(QStringLiteral("Dataset lists no usable images."));
        return report;
    }

    // --- locate the images on disk ------------------------------------------
    const QStringList roots = candidateImageRoots(options.path);
    QHash<QString, int> directoryVotes;
    QHash<int, QString> resolvedPaths;
    for (int imageId : imageOrder) {
        const QString absolute = resolveImagePath(roots, images.value(imageId).relativeName);
        if (absolute.isEmpty())
            continue;
        resolvedPaths.insert(imageId, absolute);
        directoryVotes[QFileInfo(absolute).absolutePath()] += 1;
    }

    QString chosenDirectory;
    int bestVotes = 0;
    for (auto it = directoryVotes.constBegin(); it != directoryVotes.constEnd(); ++it) {
        if (it.value() > bestVotes) {
            bestVotes = it.value();
            chosenDirectory = it.key();
        }
    }

    if (chosenDirectory.isEmpty()) {
        report.warn(QStringLiteral("Could not find the dataset's image files; annotations will be "
                                   "matched by file name against the source you already have open."));
    } else if (directoryVotes.size() > 1) {
        report.warn(QStringLiteral("Images span %1 directories (e.g. train/val). Importing the "
                                   "largest group (%2 images) from %3.")
                        .arg(directoryVotes.size()).arg(bestVotes).arg(chosenDirectory));
    }
    out->imageDirectory = chosenDirectory;

    // --- annotations ---------------------------------------------------------
    QSet<QString> acceptedNames;
    const QJsonArray annotationsArray = root["annotations"].toArray();
    for (const QJsonValue &value : annotationsArray) {
        const QJsonObject obj = value.toObject();
        const int imageId = obj["image_id"].toInt(-1);

        const auto imageIt = images.constFind(imageId);
        if (imageIt == images.constEnd()) {
            report.warn(QStringLiteral("Annotation referencing unknown image_id %1 skipped.").arg(imageId));
            ++report.skipped;
            continue;
        }

        // Only take annotations belonging to the directory group we settled on.
        if (!chosenDirectory.isEmpty()) {
            const auto resolved = resolvedPaths.constFind(imageId);
            if (resolved == resolvedPaths.constEnd()
                || QFileInfo(resolved.value()).absolutePath() != chosenDirectory) {
                ++report.skipped;
                continue;
            }
        }

        const CocoCategory category = categories.value(obj["category_id"].toInt(-1));
        if (category.labelId < 0) {
            report.warn(QStringLiteral("Annotation with unknown category_id skipped."));
            ++report.skipped;
            continue;
        }

        const QString baseName = QFileInfo(imageIt.value().relativeName).fileName();
        QSize imageSize = imageIt.value().size;
        if (!imageSize.isValid() || imageSize.isEmpty())
            imageSize = QSize();

        AnnoShape shape;
        bool built = false;

        const QJsonArray keypoints = obj["keypoints"].toArray();
        if (!keypoints.isEmpty() && !category.keypointNames.isEmpty()) {
            QVector<QPointF> points;
            QVector<int> flags;
            for (int i = 0; i + 2 < keypoints.size(); i += 3) {
                points.append(QPointF(keypoints.at(i).toDouble(), keypoints.at(i + 1).toDouble()));
                flags.append(keypoints.at(i + 2).toInt());
            }
            if (!points.isEmpty()) {
                shape = AnnoShape::makeSkeleton(points, category.labelId, flags);
                built = true;
            }
        }

        if (!built && obj.contains("segmentation")) {
            const QJsonValue segmentation = obj["segmentation"];
            if (isPolygonSegmentation(segmentation)) {
                const QJsonArray rings = segmentation.toArray();
                if (rings.size() > 1) {
                    report.warn(QStringLiteral("Multi-part/holed segmentation flattened to its "
                                               "first ring (polygon-only storage)."));
                }
                const QVector<QPointF> points = ringToPoints(rings.first().toArray());
                if (points.size() >= 3) {
                    shape = AnnoShape::makePolygon(points, category.labelId);
                    built = true;
                }
            } else if (segmentation.isObject()) {
                report.warn(QStringLiteral("RLE (iscrowd) segmentation is not supported and was "
                                           "skipped; only polygon segmentation is imported."));
                ++report.skipped;
                continue;
            }
        }

        if (!built) {
            const QJsonArray bbox = obj["bbox"].toArray();
            if (bbox.size() == 4) {
                const QRectF rect(bbox.at(0).toDouble(), bbox.at(1).toDouble(),
                                  bbox.at(2).toDouble(), bbox.at(3).toDouble());
                if (IoGeometry::isExportableRect(rect)) {
                    shape = AnnoShape::makeRect(rect, category.labelId);
                    built = true;
                }
            }
        }

        if (!built) {
            report.warn(QStringLiteral("Annotation with no usable geometry skipped."));
            ++report.skipped;
            continue;
        }

        const QJsonObject attributes = obj["attributes"].toObject();
        int trackId = obj["track_id"].toInt(-1);
        if (trackId < 0)
            trackId = attributes["track_id"].toInt(-1);
        if (trackId >= 0)
            shape.setTrackId(trackId);

        QVariantMap extra = attributes.toVariantMap();
        extra.remove(QStringLiteral("track_id"));
        if (!extra.isEmpty())
            shape.setAttributes(extra);

        FrameAnnotations &frame = out->byFileName[baseName];
        if (imageSize.isValid() && !imageSize.isEmpty())
            frame.setImageSize(imageSize);
        frame.addShape(shape);

        if (!acceptedNames.contains(baseName)) {
            acceptedNames.insert(baseName);
            out->imageFileNames.append(baseName);
        }
        ++report.shapes;
    }

    // Frames listed by the dataset but carrying no annotations are still part of
    // it (negative samples), so keep them in the frame order.
    for (int imageId : imageOrder) {
        const QString baseName = QFileInfo(images.value(imageId).relativeName).fileName();
        if (acceptedNames.contains(baseName))
            continue;
        if (!chosenDirectory.isEmpty()) {
            const auto resolved = resolvedPaths.constFind(imageId);
            if (resolved == resolvedPaths.constEnd()
                || QFileInfo(resolved.value()).absolutePath() != chosenDirectory) {
                continue;
            }
        }
        acceptedNames.insert(baseName);
        out->imageFileNames.append(baseName);
    }

    report.frames = out->byFileName.size();
    if (report.shapes == 0) {
        report.fail(QStringLiteral("No annotations could be imported. Check that the file's "
                                   "categories and image entries are populated."));
        return report;
    }

    report.ok = true;
    return report;
}
