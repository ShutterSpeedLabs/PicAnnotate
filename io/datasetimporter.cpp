#include "datasetimporter.h"

#include "../core/project.h"

#include <QFileInfo>
#include <QHash>

namespace {

// Maps the dataset's label ids onto the project's, creating classes by name so
// re-importing into an existing project does not duplicate the label list.
QHash<int, int> mergeSchema(Project &project, const ImportedDataset &dataset)
{
    LabelSchema &target = project.labelSchema();

    // Templates first: ensureClass() only adopts a template id that resolves.
    for (const KeypointTemplate &tmpl : dataset.schema.keypointTemplates())
        target.addKeypointTemplate(tmpl);

    QHash<int, int> labelIdMap;
    for (const LabelClass &imported : dataset.schema.classes()) {
        const int mappedId = target.ensureClass(imported.name, imported.allowedTypes,
                                                imported.keypointTemplateId);
        labelIdMap.insert(imported.id, mappedId);
    }
    return labelIdMap;
}

// Frame index for each of the source's file names, so lookups are O(1) and
// case-insensitive on Windows.
QHash<QString, int> buildFrameIndexByName(const Project &project)
{
    QHash<QString, int> index;
    const QStringList names = project.fileNames();
    for (int i = 0; i < names.size(); ++i)
        index.insert(names.at(i).toLower(), i);
    return index;
}

int countMatches(const QHash<QString, int> &frameIndex, const ImportedDataset &dataset)
{
    int matches = 0;
    for (auto it = dataset.byFileName.constBegin(); it != dataset.byFileName.constEnd(); ++it) {
        if (frameIndex.contains(it.key().toLower()))
            ++matches;
    }
    return matches;
}

} // namespace

namespace DatasetImporter {

IoReport apply(Project &project, const ImportedDataset &dataset, bool replaceExisting,
               bool groupTracks)
{
    IoReport report;

    if (dataset.isEmpty()) {
        report.fail(QStringLiteral("The dataset contained no annotations to apply."));
        return report;
    }

    // Prefer the source that is already open — the user may have opened the exact
    // folder the dataset describes, and reopening would discard their navigation.
    QHash<QString, int> frameIndex;
    if (project.hasSource())
        frameIndex = buildFrameIndexByName(project);

    const int matchesAgainstCurrent = frameIndex.isEmpty() ? 0 : countMatches(frameIndex, dataset);

    if (matchesAgainstCurrent == 0) {
        if (dataset.imageDirectory.isEmpty()) {
            report.fail(QStringLiteral("The dataset's images could not be located, and none of its "
                                       "file names match the source you have open. Open the folder "
                                       "holding these images first, then import again."));
            return report;
        }

        QString openError;
        if (!project.openImageList(dataset.imageDirectory, dataset.imageFileNames, &openError)) {
            report.fail(openError);
            return report;
        }
        frameIndex = buildFrameIndexByName(project);
    } else if (matchesAgainstCurrent < dataset.byFileName.size()) {
        report.warn(QStringLiteral("%1 of %2 annotated images matched the open source; the rest "
                                   "were ignored.")
                        .arg(matchesAgainstCurrent).arg(dataset.byFileName.size()));
    }

    const QHash<int, int> labelIdMap = mergeSchema(project, dataset);

    // Resolve everything before touching the project: tracks need frame indices,
    // which only exist once file names have been matched, and a track id has to be
    // allocated after the frame shapes are in place.
    QMap<int, FrameAnnotations> resolvedFrames;              // frame -> plain shapes
    QMap<int, QMap<int, AnnoShape>> pendingTracks;           // dataset track id -> frame -> shape
    int touchedFrames = 0;

    for (auto it = dataset.byFileName.constBegin(); it != dataset.byFileName.constEnd(); ++it) {
        const auto found = frameIndex.constFind(it.key().toLower());
        if (found == frameIndex.constEnd()) {
            ++report.skipped;
            continue;
        }

        const int targetIndex = found.value();
        FrameAnnotations &target = resolvedFrames[targetIndex];
        if (it.value().hasImageSize())
            target.setImageSize(it.value().imageSize());
        ++touchedFrames;

        for (AnnoShape shape : it.value().shapes()) {
            const auto mapped = labelIdMap.constFind(shape.labelId());
            if (mapped == labelIdMap.constEnd()) {
                ++report.skipped;
                continue;
            }
            shape.setLabelId(mapped.value());

            if (groupTracks && shape.trackId() >= 0) {
                QMap<int, AnnoShape> &frames = pendingTracks[shape.trackId()];
                if (!frames.contains(targetIndex)) {
                    frames.insert(targetIndex, shape);
                    ++report.shapes;
                    continue;
                }
                report.warn(QStringLiteral("Two annotations share track id %1 on one frame; "
                                           "the extra was kept as a plain shape.")
                                .arg(shape.trackId()));
            }

            // A shape that stays per-frame must not keep a track id, or it would
            // collide with the ids handed out to real tracks.
            shape.setTrackId(-1);
            target.addShape(shape);
            ++report.shapes;
        }
    }

    if (touchedFrames == 0) {
        report.fail(QStringLiteral("No imported frame could be matched to a source frame."));
        return report;
    }

    if (replaceExisting)
        project.clearAllAnnotations();

    for (auto it = resolvedFrames.constBegin(); it != resolvedFrames.constEnd(); ++it) {
        FrameAnnotations merged = replaceExisting ? it.value()
                                                  : project.annotationsAt(it.key());
        if (!replaceExisting) {
            if (it.value().hasImageSize())
                merged.setImageSize(it.value().imageSize());
            for (const AnnoShape &shape : it.value().shapes())
                merged.addShape(shape);
        }

        if (merged.shapes().isEmpty() && !merged.hasImageSize())
            continue;

        if (!merged.shapes().isEmpty())
            merged.setStatus(ReviewStatus::InProgress);
        project.setAnnotations(it.key(), merged);
        if (!merged.shapes().isEmpty())
            ++report.frames;
    }

    // Fresh ids, allocated after the frame shapes are stored so nextTrackId()
    // accounts for anything they still carry.
    int nextTrackId = project.nextTrackId();
    for (auto it = pendingTracks.constBegin(); it != pendingTracks.constEnd(); ++it) {
        const QMap<int, AnnoShape> &frames = it.value();
        if (frames.isEmpty())
            continue;

        const AnnoShape &first = frames.first();
        AnnoTrack track(nextTrackId, first.labelId(), first.type());

        bool mixedTypes = false;
        bool mixedLabels = false;
        for (auto frameIt = frames.constBegin(); frameIt != frames.constEnd(); ++frameIt) {
            if (frameIt.value().type() != first.type()) {
                // A track carries one geometry type; anything else is left as a
                // plain shape on its own frame rather than silently reshaped.
                mixedTypes = true;
                FrameAnnotations extra = project.annotationsAt(frameIt.key());
                AnnoShape loose = frameIt.value();
                loose.setTrackId(-1);
                extra.addShape(loose);
                project.setAnnotations(frameIt.key(), extra);
                continue;
            }
            if (frameIt.value().labelId() != first.labelId())
                mixedLabels = true;
            track.setKeyframe(frameIt.key(), frameIt.value());
        }

        if (mixedTypes) {
            report.warn(QStringLiteral("Track %1 mixed geometry types; only its %2 shapes "
                                       "became keyframes.")
                            .arg(it.key())
                            .arg(shapeTypeToString(first.type())));
        }
        if (mixedLabels) {
            report.warn(QStringLiteral("Track %1 had more than one label; the whole track "
                                       "took the first one.").arg(it.key()));
        }

        if (track.isEmpty())
            continue;

        project.tracks().insertTrack(track);
        ++nextTrackId;
        ++report.tracks;

        // Frames covered only by a track still count as imported frames.
        for (int frame : track.keyframeFrames()) {
            if (!resolvedFrames.contains(frame) || resolvedFrames.value(frame).shapes().isEmpty())
                ++report.frames;
        }
    }

    if (report.shapes == 0) {
        report.fail(QStringLiteral("Nothing could be applied to the open source."));
        return report;
    }

    report.ok = true;
    return report;
}

} // namespace DatasetImporter
