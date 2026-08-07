#ifndef FRAMEEXPORTER_H
#define FRAMEEXPORTER_H

#include <QList>
#include <QString>

class Project;
struct ExportOptions;

// Frame-to-file plumbing shared by the dataset writers: which frames to emit,
// what to call their image files, and how to get those images onto disk.
namespace FrameExporter {

// Image file name a frame should have inside an exported dataset. Folder sources
// keep their original name; video frames get a zero-padded PNG name.
QString imageFileName(const Project &project, int index);

// Copies (folder source) or encodes (video source) the frame image to destPath.
bool writeFrameImage(const Project &project, int index, const QString &destPath, QString *error);

// Frames the export should cover, in ascending order.
QList<int> framesToExport(const Project &project, const ExportOptions &options);

// Deterministic train/val assignment by position in the export list, so
// re-exporting the same project produces the same split.
bool isValidationFrame(int ordinal, double valSplit);

QString splitName(bool validation);

} // namespace FrameExporter

#endif // FRAMEEXPORTER_H
