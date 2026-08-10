#ifndef YOLODETECTOR_H
#define YOLODETECTOR_H

#include "imageblob.h"
#include "inferencetypes.h"
#include "modelspec.h"

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>

// YOLOv8 / YOLO11 detection and instance segmentation through OpenCV's DNN
// module.
//
// Both generations share one output layout, which is why a single decoder
// handles them:
//
//   detect   output0  [1, 4 + nc, A]              cx cy w h, then class scores
//   segment  output0  [1, 4 + nc + 32, A]         ...plus 32 mask coefficients
//            output1  [1, 32, H/4, W/4]           mask prototypes
//
// Note the transposition relative to YOLOv5: attributes are the *rows* and
// anchors the columns, so the decode reads down columns.
//
// Not thread-safe — cv::dnn::Net::forward mutates the net. AutoAnnotator gives
// its worker thread its own instance.
class YoloDetector
{
public:
    struct Options
    {
        float confidenceThreshold = 0.25f;
        float nmsThreshold = 0.45f;
        int maxDetections = 300;

        // Douglas-Peucker tolerance as a fraction of contour perimeter, for
        // turning a mask into a polygon. Larger means fewer points.
        double polygonEpsilon = 0.002;

        // Only keep these model class indices. Empty keeps everything.
        QSet<int> classFilter;
    };

    bool load(const ModelSpec &spec, const QString &modelPath, const QStringList &classNames,
              QString *error);
    void unload();
    bool isLoaded() const { return m_loaded; }

    QString modelPath() const { return m_modelPath; }
    QString modelId() const { return m_modelId; }
    bool producesMasks() const { return m_segmentation; }

    // Number of classes the loaded graph actually predicts, which is not always
    // what the catalogue's label set says — a fine-tuned model reuses the
    // architecture with a different class count.
    int classCount() const { return m_classCount; }
    QStringList classNames() const { return m_classNames; }

    // Runs on a BGR image and returns detections in that image's pixel
    // coordinates. Returns an empty vector and sets `error` on failure; an empty
    // vector with no error simply means nothing was found.
    QVector<Detection> detect(const cv::Mat &bgrImage, const Options &options,
                              QString *error = nullptr);

private:
    // Runs one forward pass on a blank image at load time to learn the real
    // output layout. The alternative is guessing the class count from the
    // catalogue, which silently mis-decodes any fine-tuned model whose class
    // count differs from the label set it was registered against.
    bool probeOutputLayout(QString *error);

    // Splits the raw forward() outputs into the detection head and the optional
    // mask prototypes, identified by rank rather than by position: OpenCV does
    // not promise the order of unconnected output layers.
    bool splitOutputs(const std::vector<cv::Mat> &outputs, cv::Mat *head, cv::Mat *proto,
                      QString *error) const;

    // Reinterprets the head as an (attributes x anchors) matrix, transposing if
    // the export put anchors first.
    cv::Mat headAsAttributeMajor(const cv::Mat &head) const;

    // Mask for one detection, decoded from its coefficients and the prototypes
    // and cropped to its box. Empty when the mask has no foreground.
    QVector<QPointF> decodeMask(const cv::Mat &proto, const float *coefficients,
                                int coefficientCount, const QRectF &boxInNetwork,
                                const ImageBlob::LetterboxTransform &transform,
                                double polygonEpsilon) const;

    cv::dnn::Net m_net;
    bool m_loaded = false;
    bool m_segmentation = false;

    QString m_modelPath;
    QString m_modelId;
    QStringList m_classNames;
    int m_classCount = 0;
    cv::Size m_inputSize{640, 640};

    // Scale from network pixels to prototype pixels, normally 1/4. Read from the
    // prototype tensor rather than assumed, since it is a property of the export.
    double m_protoScale = 0.25;

    static constexpr int kMaskCoefficients = 32;
};

#endif // YOLODETECTOR_H
