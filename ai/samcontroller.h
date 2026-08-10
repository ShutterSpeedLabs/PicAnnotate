#ifndef SAMCONTROLLER_H
#define SAMCONTROLLER_H

#include "samsegmenter.h"

#include <QMetaType>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <opencv2/core.hpp>

#include <optional>

class Project;
class QThread;

Q_DECLARE_METATYPE(SamSegmenter::Prompt)

// Lives on the SAM thread and owns the only segmenter.
class SamWorker : public QObject
{
    Q_OBJECT
public slots:
    void configure();
    void encode(int frameIndex, const cv::Mat &image);
    void decode(quint64 requestId, const SamSegmenter::Prompt &prompt, double polygonEpsilon);
    void release();

signals:
    void configured(bool ok, const QString &error, const QString &warning);
    void encoded(int frameIndex, bool ok, const QString &error);
    void decoded(quint64 requestId, bool ok, const QVector<QPointF> &polygon, const QRectF &box,
                 float score, const QString &error);

private:
    SamSegmenter m_segmenter;
};

// Drives interactive SAM for the UI.
//
// Two timescales have to be reconciled: encoding a frame takes a second or
// more, while a click should feel instant. So encoding happens once per frame
// off the UI thread, and clicks turn into decode requests that are *coalesced* —
// if the user clicks again while a decode is running, the older request is
// dropped rather than queued, so the preview always chases the newest prompt
// instead of replaying a backlog.
class SamController : public QObject
{
    Q_OBJECT
public:
    explicit SamController(Project *project, QObject *parent = nullptr);
    ~SamController() override;

    // Turning this on loads the models and encodes the current frame.
    bool isActive() const { return m_active; }
    void setActive(bool active);

    bool isBusy() const { return m_encoding || m_decodeInFlight; }
    bool isEncoding() const { return m_encoding; }

    // True once the current frame's embedding is ready and prompts will answer.
    bool acceptsPrompts() const;

    // Replaces the prompt and asks for a new mask. Cheap to call on every click.
    void setPrompt(const SamSegmenter::Prompt &prompt);
    void clearPrompt();
    const SamSegmenter::Prompt &prompt() const { return m_prompt; }

    // The last mask produced, for the commit path.
    const QVector<QPointF> &previewPolygon() const { return m_previewPolygon; }
    float previewScore() const { return m_previewScore; }
    bool hasPreview() const { return m_previewPolygon.size() >= 3; }

    // Re-encodes when the playhead moves. A no-op while inactive.
    void onCurrentFrameChanged();

    // Douglas-Peucker tolerance for the mask outline, as a fraction of its
    // perimeter. Exposed because the right value is a matter of taste: low
    // keeps organic edges, high gives a polygon that is pleasant to hand-edit.
    double polygonEpsilon() const { return m_polygonEpsilon; }
    void setPolygonEpsilon(double epsilon);

signals:
    void statusChanged(const QString &text);
    void busyChanged(bool busy);
    void activeChanged(bool active);

    void previewChanged(const QVector<QPointF> &polygon, float score);
    void previewCleared();

    // Something went wrong that the user needs to see, rather than a status line.
    void failed(const QString &message);

private slots:
    void onConfigured(bool ok, const QString &error, const QString &warning);
    void onEncoded(int frameIndex, bool ok, const QString &error);
    void onDecoded(quint64 requestId, bool ok, const QVector<QPointF> &polygon, const QRectF &box,
                   float score, const QString &error);

private:
    void ensureWorker();
    void requestEncode();
    void dispatchPrompt();
    void setBusy();

    Project *m_project;
    QThread *m_thread = nullptr;
    SamWorker *m_worker = nullptr;

    bool m_active = false;
    bool m_configured = false;
    bool m_configuring = false;
    bool m_encoding = false;
    bool m_decodeInFlight = false;

    // Frame whose embedding the worker currently holds, and the one it is
    // encoding. -1 for none.
    int m_encodedFrame = -1;
    int m_encodingFrame = -1;

    SamSegmenter::Prompt m_prompt;

    // Set when a prompt arrives while a decode is running. Only the newest is
    // kept — intermediate ones are stale the moment the next click lands.
    std::optional<SamSegmenter::Prompt> m_queuedPrompt;

    quint64 m_nextRequestId = 1;
    quint64 m_latestRequestId = 0;

    QVector<QPointF> m_previewPolygon;
    float m_previewScore = 0.0f;

    double m_polygonEpsilon = 0.004;
    bool m_lastBusy = false;
};

#endif // SAMCONTROLLER_H
