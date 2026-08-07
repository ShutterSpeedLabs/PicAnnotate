#include "frameexporter.h"
#include "datasetformat.h"

#include "../core/project.h"

#include <QFile>
#include <QFileInfo>
#include <QImage>

namespace FrameExporter {

QString imageFileName(const Project &project, int index)
{
    const QString sourceFile = project.framePath(index);
    if (!sourceFile.isEmpty())
        return QFileInfo(sourceFile).fileName();

    // Video frames have no file of their own, so mint a stable, sortable name.
    return QStringLiteral("frame_%1.png").arg(index, 6, 10, QChar('0'));
}

bool writeFrameImage(const Project &project, int index, const QString &destPath, QString *error)
{
    const QString sourceFile = project.framePath(index);

    if (!sourceFile.isEmpty()) {
        if (QFileInfo(sourceFile).canonicalFilePath() == QFileInfo(destPath).canonicalFilePath())
            return true;
        if (QFile::exists(destPath))
            QFile::remove(destPath);
        if (QFile::copy(sourceFile, destPath))
            return true;
        if (error)
            *error = QStringLiteral("Could not copy %1").arg(sourceFile);
        return false;
    }

    const QImage frame = project.frameAt(index);
    if (frame.isNull()) {
        if (error)
            *error = QStringLiteral("Frame %1 could not be decoded").arg(index);
        return false;
    }

    if (!frame.save(destPath)) {
        if (error)
            *error = QStringLiteral("Could not write %1").arg(destPath);
        return false;
    }
    return true;
}

QList<int> framesToExport(const Project &project, const ExportOptions &options)
{
    if (options.includeEmptyFrames) {
        QList<int> all;
        const int count = project.frameCount();
        all.reserve(count);
        for (int i = 0; i < count; ++i)
            all.append(i);
        return all;
    }

    QList<int> annotated = project.annotatedFrameIndices();
    std::sort(annotated.begin(), annotated.end());
    return annotated;
}

bool isValidationFrame(int ordinal, double valSplit)
{
    if (valSplit <= 0.0)
        return false;

    const int stride = qMax(2, qRound(1.0 / qBound(0.01, valSplit, 0.9)));
    return (ordinal % stride) == (stride - 1);
}

QString splitName(bool validation)
{
    return validation ? QStringLiteral("val") : QStringLiteral("train");
}

} // namespace FrameExporter
