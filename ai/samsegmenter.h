#ifndef SAMSEGMENTER_H
#define SAMSEGMENTER_H

#include "imageblob.h"
#include "inferencetypes.h"
#include "onnxsession.h"

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

#include <opencv2/core.hpp>

#include <optional>

// Segment Anything, split the way the model is meant to be used interactively.
//
// The encoder is expensive (a second for MobileSAM, several for ViT-B on CPU)
// and depends only on the image. The decoder is cheap and depends on the
// prompt. So the encoder runs once per frame and its embedding is held here,
// and every click is a decoder-only pass. Collapsing the two would make each
// click cost a full encode, which is the difference between a usable tool and
// an unusable one.
class SamSegmenter
{
public:
    // Clicks and an optional box, all in source-image pixel coordinates.
    struct Prompt
    {
        QVector<QPointF> positivePoints;   // "include this"
        QVector<QPointF> negativePoints;   // "exclude this"
        QRectF box;                        // invalid when there is no box prompt

        bool isEmpty() const
        {
            return positivePoints.isEmpty() && negativePoints.isEmpty() && !box.isValid();
        }
    };

    struct Result
    {
        QVector<QPointF> polygon;   // source-image coordinates
        QRectF box;                 // bounds of the mask
        float score = 0.0f;         // SAM's own IoU prediction

        bool isValid() const { return polygon.size() >= 3; }
    };

    // Resolves the selected encoder/decoder pair through ModelManager and loads
    // both. Warns rather than fails when the two are not declared companions:
    // mismatched exports produce plausible-looking nonsense, not an error.
    bool load(QString *error, QString *warning = nullptr);
    void unload();
    bool isReady() const;

    QString encoderId() const { return m_encoderId; }
    QString decoderId() const { return m_decoderId; }

    // Runs the encoder and keeps the embedding. Everything after this is fast
    // until the image changes.
    bool setImage(const cv::Mat &bgrImage, QString *error);
    bool hasImage() const { return m_hasEmbedding; }
    void clearImage();

    // Decoder-only pass against the cached embedding.
    std::optional<Result> segment(const Prompt &prompt, double polygonEpsilon,
                                  QString *error);

private:
    // Names vary between exports (samexporter, the official script, community
    // re-exports), so each is matched against the graph's declared inputs rather
    // than hard-coded. An export that omits one is handled, not rejected.
    struct DecoderInputs
    {
        QString embeddings;
        QString pointCoords;
        QString pointLabels;
        QString maskInput;
        QString hasMaskInput;
        QString originalSize;   // empty when the export does not take one

        bool isUsable() const
        {
            return !embeddings.isEmpty() && !pointCoords.isEmpty() && !pointLabels.isEmpty();
        }
    };

    bool resolveDecoderInputs(QString *error);

    // Builds the (N, 2) coordinate and (N,) label tensors SAM expects. Labels
    // follow SAM's convention: 1 foreground, 0 background, 2/3 the box corners,
    // -1 a padding point.
    void buildPointTensors(const Prompt &prompt, std::vector<float> *coords,
                           std::vector<float> *labels) const;

    // Turns a decoder mask tensor into a polygon in source coordinates. Handles
    // both export shapes: masks already at the original size, and masks at the
    // model's own 256x256 resolution that still need un-padding and rescaling.
    std::optional<Result> maskToResult(const Tensor &masks, const Tensor &scores,
                                       double polygonEpsilon) const;

    OnnxSession m_encoder;
    OnnxSession m_decoder;
    DecoderInputs m_decoderInputs;

    QString m_encoderId;
    QString m_decoderId;

    // Cached embedding for the current image, plus the transform used to make
    // it — prompts have to be mapped into the same space.
    Tensor m_embedding;
    bool m_hasEmbedding = false;
    ImageBlob::LetterboxTransform m_transform;
    QSize m_sourceSize;

    // SAM's fixed input resolution and its ImageNet normalisation statistics,
    // in 0-255 RGB.
    static constexpr int kEncoderSize = 1024;
    static constexpr double kMeanR = 123.675;
    static constexpr double kMeanG = 116.28;
    static constexpr double kMeanB = 103.53;
    static constexpr double kStdR = 58.395;
    static constexpr double kStdG = 57.12;
    static constexpr double kStdB = 57.375;
};

#endif // SAMSEGMENTER_H
