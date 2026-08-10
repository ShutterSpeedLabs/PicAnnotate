#ifndef AUTOANNOTATOR_H
#define AUTOANNOTATOR_H

#include "inferencetypes.h"
#include "modelspec.h"
#include "yolodetector.h"

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <opencv2/core.hpp>

class PredictionStore;
class Project;
class QThread;

// cv::Mat is refcounted, so handing one to another thread by value is a cheap
// shallow copy. Safe here because each frame's Mat is freshly decoded and never
// touched again by the sender.
Q_DECLARE_METATYPE(cv::Mat)
Q_DECLARE_METATYPE(Detection)
Q_DECLARE_METATYPE(QVector<Detection>)
Q_DECLARE_METATYPE(YoloDetector::Options)
Q_DECLARE_METATYPE(ModelSpec)

// Lives on the inference thread and owns the only YoloDetector instance.
class DetectionWorker : public QObject
{
    Q_OBJECT
public slots:
    void configure(const ModelSpec &spec, const QString &modelPath, const QStringList &classNames);
    void detect(int frameIndex, const cv::Mat &image, YoloDetector::Options options);
    void release();

signals:
    void configured(bool ok, const QString &error);
    void detected(int frameIndex, const QVector<Detection> &detections, const QString &error);

private:
    YoloDetector m_detector;
};

// Runs the selected detection or segmentation model over one frame or many,
// writing the results into the PredictionStore.
//
// Frames are decoded on the *main* thread and handed to the worker one at a
// time. The obvious alternative — let the worker pull frames itself — races on
// VideoFrameSource, which keeps a single cv::VideoCapture and a seek position
// that the UI is also using. Decoding is cheap next to inference, so serialising
// it costs almost nothing and removes the whole class of bug.
class AutoAnnotator : public QObject
{
    Q_OBJECT
public:
    AutoAnnotator(Project *project, PredictionStore *store, QObject *parent = nullptr);
    ~AutoAnnotator() override;

    bool isRunning() const { return m_running; }

    YoloDetector::Options &detectorOptions() { return m_options; }
    const YoloDetector::Options &detectorOptions() const { return m_options; }

    // Detection or Segmentation. Picks which of the two selected models runs.
    ModelTask task() const { return m_task; }
    void setTask(ModelTask task);

    // Replaces any predictions the frames already had. Both return false, with
    // a reason, when no usable model is selected.
    bool runOnFrame(int frameIndex, QString *error);
    bool runOnFrames(const QList<int> &frames, QString *error);

    void cancel();

signals:
    void started(int totalFrames);
    void progress(int done, int total, int detectionsSoFar);

    // `error` is empty on a clean finish, and set when the run stopped early.
    void finished(int framesProcessed, int totalDetections, const QString &error);

private slots:
    void onConfigured(bool ok, const QString &error);
    void onDetected(int frameIndex, const QVector<Detection> &detections, const QString &error);

private:
    void ensureWorker();
    void pumpNextFrame();
    void stop(const QString &error);

    // Resolves the model selected for the current task and the class names its
    // outputs index into. False, with a reason, when it is unusable.
    bool resolveModel(ModelSpec *spec, QString *path, QStringList *classNames,
                      QString *error) const;

    Project *m_project;
    PredictionStore *m_store;

    QThread *m_thread = nullptr;
    DetectionWorker *m_worker = nullptr;

    ModelTask m_task = ModelTask::Detection;
    YoloDetector::Options m_options;
    QString m_activeModelId;

    QList<int> m_queue;
    int m_cursor = 0;
    int m_detectionCount = 0;
    bool m_running = false;
    bool m_cancelled = false;

    // Set while waiting for configure() to come back, so the first frame is not
    // sent to a worker with no model loaded.
    bool m_awaitingConfigure = false;
};

#endif // AUTOANNOTATOR_H
