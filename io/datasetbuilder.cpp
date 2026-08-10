#include "datasetbuilder.h"

#include "../core/imagefoldersource.h"
#include "../trackers/cvimageconvert.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// A small grayscale thumbnail is all the similarity check needs, and comparing
// 64x64 instead of full frames is what keeps the filter cheap enough to run on
// every decoded frame.
constexpr int kSimilaritySize = 64;

cv::Mat similarityThumbnail(const cv::Mat &bgr)
{
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

    cv::Mat small;
    cv::resize(gray, small, cv::Size(kSimilaritySize, kSimilaritySize), 0, 0, cv::INTER_AREA);
    return small;
}

// Mean absolute difference between two thumbnails, normalised to 0-1.
double meanDifference(const cv::Mat &a, const cv::Mat &b)
{
    if (a.empty() || b.empty() || a.size() != b.size())
        return 1.0;

    cv::Mat difference;
    cv::absdiff(a, b, difference);
    return cv::mean(difference)[0] / 255.0;
}

// Variance of the Laplacian: the standard cheap sharpness measure. A blurred
// frame has little high-frequency content, so the second derivative is flat.
double sharpness(const cv::Mat &bgr)
{
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);

    // Work at a bounded size so the threshold means the same thing for a 4K
    // frame and a 640px one.
    if (gray.cols > 640) {
        cv::Mat small;
        const double scale = 640.0 / gray.cols;
        cv::resize(gray, small, cv::Size(), scale, scale, cv::INTER_AREA);
        gray = small;
    }

    cv::Mat laplacian;
    cv::Laplacian(gray, laplacian, CV_64F);

    cv::Scalar mean;
    cv::Scalar stdDev;
    cv::meanStdDev(laplacian, mean, stdDev);
    return stdDev[0] * stdDev[0];
}

// One in N, derived from the sampling mode. For TargetFps the source's own rate
// decides the stride, so 30fps footage at 2fps target keeps every 15th frame.
int strideFor(const SamplingOptions &sampling, double sourceFps)
{
    switch (sampling.mode) {
    case SamplingOptions::Mode::All:
        return 1;
    case SamplingOptions::Mode::EveryNth:
        return std::max(1, sampling.everyNth);
    case SamplingOptions::Mode::TargetFps:
        if (sourceFps <= 0.0 || sampling.targetFps <= 0.0)
            return 1;
        return std::max(1, static_cast<int>(std::round(sourceFps / sampling.targetFps)));
    }
    return 1;
}

QString sanitisedStem(const QString &path)
{
    QString stem = QFileInfo(path).completeBaseName();
    // The stem becomes part of every output file name, so anything that would
    // need quoting in a path or a YAML file goes.
    stem.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    if (stem.isEmpty())
        stem = QStringLiteral("source");
    return stem;
}

} // namespace

QString IngestSource::displayName() const
{
    const QFileInfo info(path);
    return kind == Kind::Video ? info.fileName() : info.fileName() + QStringLiteral("/");
}

QString BuildReport::toText() const
{
    QStringList lines;

    if (!ok) {
        lines << (cancelled ? QStringLiteral("Cancelled.")
                            : QStringLiteral("Failed: %1").arg(error));
    } else {
        lines << QStringLiteral("Wrote %1 image(s) from %2 source(s) to:")
                     .arg(framesWritten)
                     .arg(sourcesProcessed);
        lines << QStringLiteral("  %1").arg(imageDirectory);
    }

    lines << QString();
    lines << QStringLiteral("Frames examined: %1").arg(framesExamined);
    if (skippedBySampling > 0)
        lines << QStringLiteral("Skipped by sampling: %1").arg(skippedBySampling);
    if (skippedAsBlurred > 0)
        lines << QStringLiteral("Skipped as blurred: %1").arg(skippedAsBlurred);
    if (skippedAsSimilar > 0)
        lines << QStringLiteral("Skipped as near-duplicates: %1").arg(skippedAsSimilar);

    if (!warnings.isEmpty()) {
        lines << QString();
        lines << QStringLiteral("Warnings:");
        for (const QString &warning : warnings)
            lines << QStringLiteral("  %1").arg(warning);
    }

    return lines.join(QLatin1Char('\n'));
}

DatasetBuilder::DatasetBuilder(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<BuildOptions>("BuildOptions");
    qRegisterMetaType<BuildReport>("BuildReport");
}

void DatasetBuilder::cancel()
{
    m_cancelled = true;
}

int DatasetBuilder::estimateFrameCount(const IngestSource &source, const SamplingOptions &sampling)
{
    int available = 0;
    double fps = 0.0;

    if (source.kind == IngestSource::Kind::Video) {
        cv::VideoCapture capture(source.path.toStdString());
        if (!capture.isOpened())
            return 0;
        available = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_COUNT));
        fps = capture.get(cv::CAP_PROP_FPS);
    } else {
        QDir directory(source.path);
        available = directory.entryList(ImageFolderSource::supportedNameFilters(),
                                        QDir::Files, QDir::Name)
                        .size();
    }

    if (available <= 0)
        return 0;

    const int stride = strideFor(sampling, fps);
    int estimate = (available + stride - 1) / stride;
    if (sampling.maxFramesPerSource > 0)
        estimate = std::min(estimate, sampling.maxFramesPerSource);

    // The blur and duplicate filters can only reduce this, and by how much is
    // not knowable without decoding, so the estimate is an upper bound.
    return estimate;
}

void DatasetBuilder::run(const BuildOptions &options)
{
    m_cancelled = false;

    BuildReport report;

    if (options.sources.isEmpty()) {
        report.error = tr("No sources were added.");
        emit finished(report);
        return;
    }
    if (options.outputDirectory.trimmed().isEmpty()) {
        report.error = tr("No output folder was chosen.");
        emit finished(report);
        return;
    }

    const QString imageDirectory = QDir(options.outputDirectory).filePath(QStringLiteral("images"));
    if (!QDir().mkpath(imageDirectory)) {
        report.error = tr("Could not create %1.").arg(imageDirectory);
        emit finished(report);
        return;
    }
    report.imageDirectory = imageDirectory;

    const SamplingOptions &sampling = options.sampling;
    const QString extension = sampling.imageFormat.isEmpty() ? QStringLiteral("jpg")
                                                             : sampling.imageFormat.toLower();

    std::vector<int> encodeParameters;
    if (extension == QLatin1String("jpg") || extension == QLatin1String("jpeg")) {
        encodeParameters = {cv::IMWRITE_JPEG_QUALITY, qBound(1, sampling.jpegQuality, 100)};
    } else if (extension == QLatin1String("png")) {
        encodeParameters = {cv::IMWRITE_PNG_COMPRESSION, 3};
    }

    for (int sourceIndex = 0; sourceIndex < options.sources.size(); ++sourceIndex) {
        if (m_cancelled)
            break;

        const IngestSource &source = options.sources.at(sourceIndex);
        const QString stem = sanitisedStem(source.path);

        // Reset per source: a near-duplicate check that carried across sources
        // would drop the first frame of a new video for resembling the last
        // frame of the previous one.
        cv::Mat lastKeptThumbnail;
        int writtenFromSource = 0;

        const auto emitProgress = [&] {
            emit progress(sourceIndex, options.sources.size(), source.displayName(),
                          report.framesWritten, report.framesExamined);
        };
        emitProgress();

        // ---- gather the frames this source offers ---------------------------
        cv::VideoCapture capture;
        QStringList folderFiles;
        QDir folderDir;
        int available = 0;
        double fps = 0.0;

        if (source.kind == IngestSource::Kind::Video) {
            capture.open(source.path.toStdString());
            if (!capture.isOpened()) {
                report.warnings << tr("Could not open video %1.").arg(source.path);
                continue;
            }
            available = static_cast<int>(capture.get(cv::CAP_PROP_FRAME_COUNT));
            fps = capture.get(cv::CAP_PROP_FPS);
            if (available <= 0) {
                report.warnings << tr("%1 reports no frame count; reading until it ends.")
                                       .arg(source.displayName());
                available = std::numeric_limits<int>::max();
            }
        } else {
            folderDir = QDir(source.path);
            folderFiles = folderDir.entryList(ImageFolderSource::supportedNameFilters(),
                                              QDir::Files, QDir::Name);
            if (folderFiles.isEmpty()) {
                report.warnings << tr("No images found in %1.").arg(source.path);
                continue;
            }
            available = folderFiles.size();
        }

        const int stride = strideFor(sampling, fps);

        // ---- walk it --------------------------------------------------------
        for (int index = 0; index < available; ++index) {
            if (m_cancelled)
                break;
            if (sampling.maxFramesPerSource > 0
                && writtenFromSource >= sampling.maxFramesPerSource) {
                break;
            }

            cv::Mat frame;

            if (source.kind == IngestSource::Kind::Video) {
                // Read sequentially and discard, rather than seeking. Seeking in
                // a compressed stream is both slower and, for many codecs,
                // inaccurate — it lands on the nearest keyframe.
                if (!capture.read(frame))
                    break;
                ++report.framesExamined;
                if (index % stride != 0) {
                    ++report.skippedBySampling;
                    continue;
                }
            } else {
                ++report.framesExamined;
                if (index % stride != 0) {
                    ++report.skippedBySampling;
                    continue;
                }
                const QString filePath = folderDir.filePath(folderFiles.at(index));
                frame = cv::imread(filePath.toStdString(), cv::IMREAD_COLOR);
                if (frame.empty()) {
                    // A QImage-readable format OpenCV cannot decode (some TIFFs,
                    // some WebP builds) still gets in this way.
                    const QImage viaQt(filePath);
                    if (viaQt.isNull()) {
                        report.warnings << tr("Could not read %1.").arg(folderFiles.at(index));
                        continue;
                    }
                    frame = qImageToBgrMat(viaQt);
                }
            }

            if (frame.empty())
                continue;

            if (sampling.skipBlurred && sharpness(frame) < sampling.blurThreshold) {
                ++report.skippedAsBlurred;
                continue;
            }

            if (sampling.skipSimilar) {
                const cv::Mat thumbnail = similarityThumbnail(frame);
                if (!lastKeptThumbnail.empty()
                    && meanDifference(thumbnail, lastKeptThumbnail)
                           < sampling.differenceThreshold) {
                    ++report.skippedAsSimilar;
                    continue;
                }
                lastKeptThumbnail = thumbnail;
            }

            if (sampling.resizeTo.isValid() && !sampling.resizeTo.isEmpty()) {
                // Fit inside the target rather than stretching to it: changing
                // the aspect ratio would make every annotation drawn later
                // describe a distorted object.
                const double scale = std::min(
                    static_cast<double>(sampling.resizeTo.width()) / frame.cols,
                    static_cast<double>(sampling.resizeTo.height()) / frame.rows);
                if (scale < 1.0) {
                    cv::Mat resized;
                    cv::resize(frame, resized, cv::Size(), scale, scale, cv::INTER_AREA);
                    frame = resized;
                }
            }

            // Prefixing with the source stem keeps frames from two videos apart
            // in one flat folder, and the index makes the order recoverable.
            const QString fileName = QStringLiteral("%1_%2.%3")
                                         .arg(stem)
                                         .arg(index, 6, 10, QLatin1Char('0'))
                                         .arg(extension);
            const QString outputPath = QDir(imageDirectory).filePath(fileName);

            if (!cv::imwrite(outputPath.toStdString(), frame, encodeParameters)) {
                report.warnings << tr("Could not write %1.").arg(fileName);
                continue;
            }

            report.fileNames << fileName;
            ++report.framesWritten;
            ++writtenFromSource;

            if (report.framesWritten % 10 == 0)
                emitProgress();
        }

        ++report.sourcesProcessed;
        emitProgress();
    }

    report.cancelled = m_cancelled;
    report.ok = !m_cancelled && report.framesWritten > 0;

    if (!report.ok && report.error.isEmpty()) {
        report.error = m_cancelled ? tr("Cancelled.")
                                   : tr("No frames were written. Check the sampling settings — "
                                        "the blur and duplicate filters may have rejected "
                                        "everything.");
    }

    emit finished(report);
}
