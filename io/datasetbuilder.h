#ifndef DATASETBUILDER_H
#define DATASETBUILDER_H

#include <QMetaType>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

// Turns raw media — videos and image folders — into a flat folder of sampled
// frames ready to annotate.
//
// This is the step before annotation, and the one that decides how good the
// dataset can be. Taking every frame of a video produces thousands of
// near-identical images that inflate the labelling effort without adding any
// information, so sampling and near-duplicate rejection are the point of this
// class rather than optional extras.

// One thing to ingest.
struct IngestSource
{
    enum class Kind { Video, ImageFolder };

    Kind kind = Kind::Video;
    QString path;

    QString displayName() const;
};

struct SamplingOptions
{
    enum class Mode {
        All,        // every frame
        EveryNth,   // one in N
        TargetFps   // resample a video to a given rate
    };

    Mode mode = Mode::EveryNth;
    int everyNth = 10;
    double targetFps = 2.0;

    // Stop after this many frames from any one source. 0 means no limit, which
    // is easy to regret on a two-hour video.
    int maxFramesPerSource = 0;

    // Reject frames whose Laplacian variance is below the threshold. Motion
    // blur is the main reason a sampled video frame is unusable, and a blurred
    // frame teaches a detector nothing.
    bool skipBlurred = false;
    double blurThreshold = 60.0;

    // Reject a frame too similar to the last one kept. Compared against the
    // last *kept* frame rather than the previous frame, so a slow pan does not
    // sneak through one small step at a time.
    bool skipSimilar = false;
    double differenceThreshold = 0.04;   // mean absolute difference, 0-1

    // Downscale on the way out. An invalid size keeps the original.
    QSize resizeTo;

    QString imageFormat = QStringLiteral("jpg");
    int jpegQuality = 92;
};

struct BuildOptions
{
    QString outputDirectory;
    QVector<IngestSource> sources;
    SamplingOptions sampling;
};

struct BuildReport
{
    bool ok = false;
    bool cancelled = false;
    QString error;
    QStringList warnings;

    int sourcesProcessed = 0;
    int framesExamined = 0;
    int framesWritten = 0;
    int skippedBySampling = 0;
    int skippedAsBlurred = 0;
    int skippedAsSimilar = 0;

    QString imageDirectory;
    QStringList fileNames;   // written names, in order

    QString toText() const;
};

Q_DECLARE_METATYPE(BuildOptions)
Q_DECLARE_METATYPE(BuildReport)

class DatasetBuilder : public QObject
{
    Q_OBJECT
public:
    explicit DatasetBuilder(QObject *parent = nullptr);

    // Frames a source will be asked for, for the wizard's estimate. Cheap: it
    // reads metadata rather than decoding.
    static int estimateFrameCount(const IngestSource &source, const SamplingOptions &sampling);

public slots:
    // Blocking. Meant to be invoked on a worker thread; progress and completion
    // come back as signals.
    void run(const BuildOptions &options);
    void cancel();

signals:
    void progress(int sourceIndex, int sourceCount, const QString &sourceName, int framesWritten,
                  int framesExamined);
    void finished(const BuildReport &report);

private:
    bool m_cancelled = false;
};

#endif // DATASETBUILDER_H
