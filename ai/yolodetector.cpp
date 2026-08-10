#include "yolodetector.h"

#include <QFileInfo>
#include <QObject>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace {

// Anchor counts run to tens of thousands while the attribute count is at most a
// few hundred, so the shorter axis of the head is the attribute axis. Used only
// when the expected attribute count does not identify the layout outright.
bool looksLikeAttributeMajor(int dim1, int dim2)
{
    return dim1 <= dim2;
}

} // namespace

bool YoloDetector::load(const ModelSpec &spec, const QString &modelPath,
                        const QStringList &classNames, QString *error)
{
    if (m_loaded && m_modelPath == modelPath)
        return true;

    unload();

    if (!QFileInfo::exists(modelPath)) {
        if (error)
            *error = QObject::tr("Model file not found: %1").arg(modelPath);
        return false;
    }

    try {
        m_net = cv::dnn::readNetFromONNX(modelPath.toStdString());
    } catch (const cv::Exception &e) {
        if (error) {
            *error = QObject::tr("OpenCV could not read %1:\n%2\n\n"
                                 "Ultralytics exports need opset 12 — re-export with "
                                 "'yolo export ... opset=12 simplify=True'.")
                         .arg(QFileInfo(modelPath).fileName(), QString::fromUtf8(e.what()));
        }
        return false;
    }

    if (m_net.empty()) {
        if (error)
            *error = QObject::tr("%1 loaded as an empty network.").arg(QFileInfo(modelPath).fileName());
        return false;
    }

    m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);

    m_modelPath = modelPath;
    m_modelId = spec.id;
    m_classNames = classNames;
    m_segmentation = spec.task == ModelTask::Segmentation;
    m_inputSize = spec.inputSize.isValid() ? cv::Size(spec.inputSize.width(), spec.inputSize.height())
                                           : cv::Size(640, 640);

    if (!probeOutputLayout(error)) {
        unload();
        return false;
    }

    m_loaded = true;
    return true;
}

void YoloDetector::unload()
{
    m_net = cv::dnn::Net();
    m_loaded = false;
    m_segmentation = false;
    m_modelPath.clear();
    m_modelId.clear();
    m_classNames.clear();
    m_classCount = 0;
    m_protoScale = 0.25;
}

bool YoloDetector::probeOutputLayout(QString *error)
{
    const cv::Mat blank(m_inputSize, CV_8UC3, cv::Scalar(114, 114, 114));

    std::vector<cv::Mat> outputs;
    try {
        cv::Mat blob;
        cv::dnn::blobFromImage(blank, blob, 1.0 / 255.0, m_inputSize, cv::Scalar(), true, false);
        m_net.setInput(blob);
        m_net.forward(outputs, m_net.getUnconnectedOutLayersNames());
    } catch (const cv::Exception &e) {
        if (error) {
            *error = QObject::tr("A test run of %1 failed:\n%2")
                         .arg(QFileInfo(m_modelPath).fileName(), QString::fromUtf8(e.what()));
        }
        return false;
    }

    cv::Mat head;
    cv::Mat proto;
    if (!splitOutputs(outputs, &head, &proto, error))
        return false;

    const int dim1 = head.size[1];
    const int dim2 = head.size[2];

    // Prefer the axis whose length matches what the catalogue's label set
    // implies; fall back to "the short axis is attributes" for a model whose
    // class count we do not know in advance.
    const int expected = m_classNames.isEmpty()
                             ? -1
                             : 4 + m_classNames.size() + (m_segmentation ? kMaskCoefficients : 0);

    int attributes = 0;
    if (expected > 0 && dim1 == expected)
        attributes = dim1;
    else if (expected > 0 && dim2 == expected)
        attributes = dim2;
    else
        attributes = looksLikeAttributeMajor(dim1, dim2) ? dim1 : dim2;

    m_classCount = attributes - 4 - (m_segmentation ? kMaskCoefficients : 0);
    if (m_classCount <= 0) {
        if (error) {
            *error = QObject::tr("%1 produced a %2x%3 head, which implies %4 classes. "
                                 "Either this is not a YOLOv8/YOLO11 %5 model, or it was "
                                 "exported with a layout this reader does not handle.")
                         .arg(QFileInfo(m_modelPath).fileName())
                         .arg(dim1)
                         .arg(dim2)
                         .arg(m_classCount)
                         .arg(m_segmentation ? QObject::tr("segmentation")
                                             : QObject::tr("detection"));
        }
        return false;
    }

    if (m_segmentation) {
        if (proto.empty() || proto.dims != 4) {
            if (error) {
                *error = QObject::tr("%1 is registered as a segmentation model but emitted no "
                                     "mask prototypes. Re-export it from a *-seg checkpoint.")
                             .arg(QFileInfo(m_modelPath).fileName());
            }
            return false;
        }
        m_protoScale = static_cast<double>(proto.size[3]) / m_inputSize.width;
    }

    // A class list shorter than the graph's class count would index out of
    // bounds later; pad it so every detection still gets a usable name.
    while (m_classNames.size() < m_classCount)
        m_classNames.append(QObject::tr("class %1").arg(m_classNames.size()));
    if (m_classNames.size() > m_classCount)
        m_classNames = m_classNames.mid(0, m_classCount);

    return true;
}

bool YoloDetector::splitOutputs(const std::vector<cv::Mat> &outputs, cv::Mat *head, cv::Mat *proto,
                                QString *error) const
{
    for (const cv::Mat &output : outputs) {
        if (output.dims == 3 && head->empty())
            *head = output;
        else if (output.dims == 4 && proto->empty())
            *proto = output;
    }

    if (head->empty()) {
        if (error) {
            *error = QObject::tr("%1 produced no 3-D detection head (got %2 output(s)). "
                                 "This does not look like a YOLOv8/YOLO11 export.")
                         .arg(QFileInfo(m_modelPath).fileName())
                         .arg(outputs.size());
        }
        return false;
    }
    return true;
}

cv::Mat YoloDetector::headAsAttributeMajor(const cv::Mat &head) const
{
    const int dim1 = head.size[1];
    const int dim2 = head.size[2];
    const int attributes = 4 + m_classCount + (m_segmentation ? kMaskCoefficients : 0);

    // Drop the batch dimension: a 3-D [1, a, b] Mat is contiguous, so this is a
    // reinterpretation rather than a copy.
    const cv::Mat flat(dim1, dim2, CV_32F, const_cast<float *>(head.ptr<float>()));

    if (dim1 == attributes)
        return flat;
    if (dim2 == attributes)
        return flat.t();

    return looksLikeAttributeMajor(dim1, dim2) ? flat : cv::Mat(flat.t());
}

QVector<Detection> YoloDetector::detect(const cv::Mat &bgrImage, const Options &options,
                                        QString *error)
{
    QVector<Detection> results;

    if (!m_loaded) {
        if (error)
            *error = QObject::tr("No detection model is loaded.");
        return results;
    }
    if (bgrImage.empty()) {
        if (error)
            *error = QObject::tr("The frame could not be decoded.");
        return results;
    }

    cv::Mat letterboxed;
    const ImageBlob::LetterboxTransform transform =
        ImageBlob::letterbox(bgrImage, &letterboxed, m_inputSize);

    std::vector<cv::Mat> outputs;
    try {
        cv::Mat blob;
        cv::dnn::blobFromImage(letterboxed, blob, 1.0 / 255.0, m_inputSize, cv::Scalar(), true,
                               false);
        m_net.setInput(blob);
        m_net.forward(outputs, m_net.getUnconnectedOutLayersNames());
    } catch (const cv::Exception &e) {
        if (error)
            *error = QObject::tr("Detection failed: %1").arg(QString::fromUtf8(e.what()));
        return results;
    }

    cv::Mat head;
    cv::Mat proto;
    if (!splitOutputs(outputs, &head, &proto, error))
        return results;

    const cv::Mat attributes = headAsAttributeMajor(head);
    const int anchorCount = attributes.cols;
    const int coefficientOffset = 4 + m_classCount;

    std::vector<cv::Rect2d> boxes;
    std::vector<float> scores;
    std::vector<int> classIds;
    std::vector<int> anchors;
    boxes.reserve(256);
    scores.reserve(256);
    classIds.reserve(256);
    anchors.reserve(256);

    for (int a = 0; a < anchorCount; ++a) {
        // Best class for this anchor. YOLOv8 heads are already sigmoid-activated
        // and carry no separate objectness term, so the class score is the score.
        int bestClass = -1;
        float bestScore = options.confidenceThreshold;
        for (int c = 0; c < m_classCount; ++c) {
            const float score = attributes.at<float>(4 + c, a);
            if (score > bestScore) {
                bestScore = score;
                bestClass = c;
            }
        }
        if (bestClass < 0)
            continue;
        if (!options.classFilter.isEmpty() && !options.classFilter.contains(bestClass))
            continue;

        const float cx = attributes.at<float>(0, a);
        const float cy = attributes.at<float>(1, a);
        const float w = attributes.at<float>(2, a);
        const float h = attributes.at<float>(3, a);
        if (w <= 0.0f || h <= 0.0f)
            continue;

        boxes.emplace_back(cx - w / 2.0, cy - h / 2.0, w, h);
        scores.push_back(bestScore);
        classIds.push_back(bestClass);
        anchors.push_back(a);
    }

    if (boxes.empty())
        return results;

    // Batched NMS suppresses within each class rather than across all of them,
    // so a person standing in front of a car does not delete the car.
    std::vector<int> keep;
    std::vector<cv::Rect> intBoxes;
    intBoxes.reserve(boxes.size());
    for (const cv::Rect2d &box : boxes)
        intBoxes.emplace_back(box);

    cv::dnn::NMSBoxesBatched(intBoxes, scores, classIds, options.confidenceThreshold,
                             options.nmsThreshold, keep);

    const QRectF sourceBounds(0, 0, bgrImage.cols, bgrImage.rows);

    for (int index : keep) {
        if (results.size() >= options.maxDetections)
            break;

        const cv::Rect2d &networkBox = boxes[static_cast<size_t>(index)];
        const QRectF boxInNetwork(networkBox.x, networkBox.y, networkBox.width,
                                  networkBox.height);

        QRectF boxInSource = transform.toSource(boxInNetwork).intersected(sourceBounds);
        if (boxInSource.width() < 1.0 || boxInSource.height() < 1.0)
            continue;

        Detection detection;
        detection.box = boxInSource;
        detection.classIndex = classIds[static_cast<size_t>(index)];
        detection.className = m_classNames.value(detection.classIndex);
        detection.score = scores[static_cast<size_t>(index)];

        if (m_segmentation && !proto.empty()) {
            const int anchor = anchors[static_cast<size_t>(index)];
            std::vector<float> coefficients(kMaskCoefficients);
            for (int k = 0; k < kMaskCoefficients; ++k)
                coefficients[static_cast<size_t>(k)] = attributes.at<float>(coefficientOffset + k, anchor);

            detection.polygon = decodeMask(proto, coefficients.data(), kMaskCoefficients,
                                           boxInNetwork, transform, options.polygonEpsilon);
        }

        results.append(detection);
    }

    return results;
}

QVector<QPointF> YoloDetector::decodeMask(const cv::Mat &proto, const float *coefficients,
                                          int coefficientCount, const QRectF &boxInNetwork,
                                          const ImageBlob::LetterboxTransform &transform,
                                          double polygonEpsilon) const
{
    if (proto.dims != 4 || proto.size[1] != coefficientCount)
        return {};

    const int protoHeight = proto.size[2];
    const int protoWidth = proto.size[3];
    const int protoPixels = protoHeight * protoWidth;

    // A linear combination of the prototypes, weighted by this instance's
    // coefficients: (1 x 32) * (32 x H*W) -> (1 x H*W).
    const cv::Mat protoMat(coefficientCount, protoPixels, CV_32F,
                           const_cast<float *>(proto.ptr<float>()));
    const cv::Mat coefficientMat(1, coefficientCount, CV_32F, const_cast<float *>(coefficients));

    // Materialise the product before reshaping: cv::MatExpr is a lazy expression
    // and has no reshape().
    const cv::Mat combined = coefficientMat * protoMat;
    const cv::Mat logits = combined.reshape(1, protoHeight);

    // Crop before the sigmoid and the upscale: the mask outside the box is
    // discarded anyway, and a 160x160 exp() per instance adds up over a batch run.
    const double scaleX = static_cast<double>(protoWidth) / transform.networkSize.width();
    const double scaleY = static_cast<double>(protoHeight) / transform.networkSize.height();

    cv::Rect protoRoi(static_cast<int>(std::floor(boxInNetwork.left() * scaleX)),
                      static_cast<int>(std::floor(boxInNetwork.top() * scaleY)),
                      static_cast<int>(std::ceil(boxInNetwork.width() * scaleX)),
                      static_cast<int>(std::ceil(boxInNetwork.height() * scaleY)));
    protoRoi &= cv::Rect(0, 0, protoWidth, protoHeight);
    if (protoRoi.width <= 0 || protoRoi.height <= 0)
        return {};

    cv::Mat cropped = logits(protoRoi).clone();

    cv::Mat negated;
    cv::exp(-cropped, negated);
    const cv::Mat probability = 1.0 / (1.0 + negated);

    // Upscale straight to the box's size in source pixels, so the contour comes
    // out in image coordinates with no further mapping.
    const QRectF sourceBoxF = transform.toSource(boxInNetwork);
    const int left = std::max(0, static_cast<int>(std::floor(sourceBoxF.left())));
    const int top = std::max(0, static_cast<int>(std::floor(sourceBoxF.top())));
    const int right = std::min(transform.sourceSize.width(),
                               static_cast<int>(std::ceil(sourceBoxF.right())));
    const int bottom = std::min(transform.sourceSize.height(),
                                static_cast<int>(std::ceil(sourceBoxF.bottom())));
    const int width = right - left;
    const int height = bottom - top;
    if (width <= 0 || height <= 0)
        return {};

    cv::Mat upscaled;
    cv::resize(probability, upscaled, cv::Size(width, height), 0, 0, cv::INTER_LINEAR);

    cv::Mat binary;
    cv::threshold(upscaled, binary, 0.5, 255.0, cv::THRESH_BINARY);
    binary.convertTo(binary, CV_8U);

    QVector<QPointF> polygon = ImageBlob::largestContourPolygon(binary, polygonEpsilon);
    for (QPointF &point : polygon)
        point += QPointF(left, top);

    return polygon;
}
