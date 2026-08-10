#include "samsegmenter.h"

#include "modelmanager.h"

#include <QObject>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace {

// Picks the graph input whose name contains any of `keywords`. Exports disagree
// on names ("image_embeddings" vs "embeddings", "orig_im_size" vs
// "original_size"), so matching loosely is more robust than a fixed table.
QString matchInput(const QStringList &names, const QStringList &keywords)
{
    for (const QString &name : names) {
        const QString lower = name.toLower();
        for (const QString &keyword : keywords) {
            if (lower.contains(keyword))
                return name;
        }
    }
    return QString();
}

} // namespace

bool SamSegmenter::load(QString *error, QString *warning)
{
    ModelManager &manager = ModelManager::instance();

    const ModelSpec *encoderSpec = manager.selectedModel(ModelTask::SamEncoder);
    const ModelSpec *decoderSpec = manager.selectedModel(ModelTask::SamDecoder);

    if (!encoderSpec || !decoderSpec) {
        if (error) {
            *error = QObject::tr("SAM needs both an encoder and a decoder. Open the Model "
                                 "Manager to select them.");
        }
        return false;
    }

    QString reason;
    if (!manager.isUsable(encoderSpec->id, &reason)) {
        if (error)
            *error = reason;
        return false;
    }
    if (!manager.isUsable(decoderSpec->id, &reason)) {
        if (error)
            *error = reason;
        return false;
    }

    // Shapes line up across SAM variants, so a mismatched pair loads and runs
    // and quietly returns garbage. Warn loudly rather than let that happen
    // silently, but do not refuse — someone may have a legitimate custom pair.
    if (warning && !encoderSpec->companionId.isEmpty()
        && encoderSpec->companionId != decoderSpec->id) {
        *warning = QObject::tr(
                       "\"%1\" expects to be paired with \"%2\", but \"%3\" is selected. "
                       "Mismatched SAM exports produce nonsense masks rather than an error.")
                       .arg(encoderSpec->displayName, encoderSpec->companionId,
                            decoderSpec->displayName);
    }

    const ExecutionProvider provider = manager.preferredProvider();

    if (!m_encoder.load(manager.localPath(encoderSpec->id), provider, error))
        return false;
    if (!m_decoder.load(manager.localPath(decoderSpec->id), provider, error)) {
        m_encoder.unload();
        return false;
    }

    if (!resolveDecoderInputs(error)) {
        unload();
        return false;
    }

    m_encoderId = encoderSpec->id;
    m_decoderId = decoderSpec->id;
    clearImage();
    return true;
}

bool SamSegmenter::resolveDecoderInputs(QString *error)
{
    const QStringList names = m_decoder.inputNames();

    m_decoderInputs = DecoderInputs();
    m_decoderInputs.embeddings = matchInput(names, {QStringLiteral("embed")});
    m_decoderInputs.pointCoords = matchInput(names, {QStringLiteral("point_coord"),
                                                     QStringLiteral("point_coords"),
                                                     QStringLiteral("coord")});
    m_decoderInputs.pointLabels = matchInput(names, {QStringLiteral("point_label"),
                                                     QStringLiteral("label")});
    m_decoderInputs.maskInput = matchInput(names, {QStringLiteral("mask_input")});
    m_decoderInputs.hasMaskInput = matchInput(names, {QStringLiteral("has_mask")});
    m_decoderInputs.originalSize = matchInput(names, {QStringLiteral("orig_im_size"),
                                                      QStringLiteral("orig_size"),
                                                      QStringLiteral("original_size")});

    if (!m_decoderInputs.isUsable()) {
        if (error) {
            *error = QObject::tr("The selected SAM decoder does not look like a SAM mask "
                                 "decoder. Its inputs are: %1")
                         .arg(names.join(QStringLiteral(", ")));
        }
        return false;
    }
    return true;
}

void SamSegmenter::unload()
{
    m_encoder.unload();
    m_decoder.unload();
    m_decoderInputs = DecoderInputs();
    m_encoderId.clear();
    m_decoderId.clear();
    clearImage();
}

bool SamSegmenter::isReady() const
{
    return m_encoder.isLoaded() && m_decoder.isLoaded();
}

void SamSegmenter::clearImage()
{
    m_embedding.clear();
    m_hasEmbedding = false;
    m_sourceSize = QSize();
}

bool SamSegmenter::setImage(const cv::Mat &bgrImage, QString *error)
{
    clearImage();

    if (!isReady()) {
        if (error)
            *error = QObject::tr("SAM models are not loaded.");
        return false;
    }
    if (bgrImage.empty()) {
        if (error)
            *error = QObject::tr("The frame could not be decoded.");
        return false;
    }

    // SAM scales the long side to 1024 and pads bottom/right — not centred, the
    // way YOLO does. Getting this wrong shifts every prompt.
    cv::Mat prepared;
    m_transform = ImageBlob::letterbox(bgrImage, &prepared,
                                       cv::Size(kEncoderSize, kEncoderSize),
                                       cv::Scalar(0, 0, 0), /*centred=*/false);
    m_sourceSize = QSize(bgrImage.cols, bgrImage.rows);

    // Normalisation is in 0-255 RGB, so the divisor is 1 rather than 255.
    const Tensor input = ImageBlob::toNchwFloat(prepared, /*swapRedBlue=*/true,
                                                /*scaleDivisor=*/1.0,
                                                cv::Scalar(kMeanR, kMeanG, kMeanB),
                                                cv::Scalar(kStdR, kStdG, kStdB));
    if (!input.isValid()) {
        if (error)
            *error = QObject::tr("Could not build the encoder input tensor.");
        return false;
    }

    const QStringList encoderInputs = m_encoder.inputNames();
    if (encoderInputs.isEmpty()) {
        if (error)
            *error = QObject::tr("The SAM encoder declares no inputs.");
        return false;
    }

    QVector<Tensor> outputs;
    if (!m_encoder.run({{encoderInputs.first(), input}}, {}, &outputs, error))
        return false;

    if (outputs.isEmpty() || !outputs.first().isValid()) {
        if (error)
            *error = QObject::tr("The SAM encoder produced no embedding.");
        return false;
    }

    m_embedding = outputs.first();
    m_hasEmbedding = true;
    return true;
}

void SamSegmenter::buildPointTensors(const Prompt &prompt, std::vector<float> *coords,
                                     std::vector<float> *labels) const
{
    const auto append = [&](const QPointF &imagePoint, float label) {
        const QPointF mapped = m_transform.toNetwork(imagePoint);
        coords->push_back(static_cast<float>(mapped.x()));
        coords->push_back(static_cast<float>(mapped.y()));
        labels->push_back(label);
    };

    for (const QPointF &point : prompt.positivePoints)
        append(point, 1.0f);
    for (const QPointF &point : prompt.negativePoints)
        append(point, 0.0f);

    if (prompt.box.isValid()) {
        // Labels 2 and 3 are how SAM encodes a box: its two opposite corners.
        append(prompt.box.topLeft(), 2.0f);
        append(prompt.box.bottomRight(), 3.0f);
    } else {
        // Without a box SAM requires one padding point, label -1. Omitting it
        // does not error; it shifts the model off its training distribution and
        // the masks quietly get worse.
        coords->push_back(0.0f);
        coords->push_back(0.0f);
        labels->push_back(-1.0f);
    }
}

std::optional<SamSegmenter::Result> SamSegmenter::segment(const Prompt &prompt,
                                                          double polygonEpsilon, QString *error)
{
    if (!isReady()) {
        if (error)
            *error = QObject::tr("SAM models are not loaded.");
        return std::nullopt;
    }
    if (!m_hasEmbedding) {
        if (error)
            *error = QObject::tr("No frame has been encoded yet.");
        return std::nullopt;
    }
    if (prompt.isEmpty())
        return std::nullopt;

    std::vector<float> coords;
    std::vector<float> labels;
    buildPointTensors(prompt, &coords, &labels);

    const qint64 pointCount = static_cast<qint64>(labels.size());

    QVector<NamedTensor> inputs;
    inputs.append({m_decoderInputs.embeddings, m_embedding});
    inputs.append({m_decoderInputs.pointCoords,
                   Tensor::fromFloats({1, pointCount, 2}, std::move(coords))});
    inputs.append({m_decoderInputs.pointLabels,
                   Tensor::fromFloats({1, pointCount}, std::move(labels))});

    // No previous mask is being refined, so an all-zero mask with has_mask 0.
    // Both still have to be supplied — the graph declares them as inputs.
    if (!m_decoderInputs.maskInput.isEmpty()) {
        inputs.append({m_decoderInputs.maskInput,
                       Tensor::fromFloats({1, 1, 256, 256}, std::vector<float>(256 * 256, 0.0f))});
    }
    if (!m_decoderInputs.hasMaskInput.isEmpty()) {
        inputs.append({m_decoderInputs.hasMaskInput,
                       Tensor::fromFloats({1}, std::vector<float>{0.0f})});
    }
    if (!m_decoderInputs.originalSize.isEmpty()) {
        // SAM takes this as (height, width), not (width, height).
        inputs.append({m_decoderInputs.originalSize,
                       Tensor::fromFloats({2},
                                          std::vector<float>{
                                              static_cast<float>(m_sourceSize.height()),
                                              static_cast<float>(m_sourceSize.width())})});
    }

    QVector<Tensor> outputs;
    if (!m_decoder.run(inputs, {}, &outputs, error))
        return std::nullopt;

    if (outputs.isEmpty()) {
        if (error)
            *error = QObject::tr("The SAM decoder produced no output.");
        return std::nullopt;
    }

    // Masks are the 4-D output; the IoU predictions are the small 2-D one.
    // Identifying by rank survives exports that order them differently.
    Tensor masks;
    Tensor scores;
    for (const Tensor &output : outputs) {
        if (output.rank() == 4 && masks.isEmpty())
            masks = output;
        else if (output.rank() <= 2 && scores.isEmpty())
            scores = output;
    }

    if (masks.isEmpty()) {
        if (error)
            *error = QObject::tr("The SAM decoder produced no mask tensor.");
        return std::nullopt;
    }

    return maskToResult(masks, scores, polygonEpsilon);
}

std::optional<SamSegmenter::Result> SamSegmenter::maskToResult(const Tensor &masks,
                                                               const Tensor &scores,
                                                               double polygonEpsilon) const
{
    const int maskCount = static_cast<int>(masks.dim(1));
    const int maskHeight = static_cast<int>(masks.dim(2));
    const int maskWidth = static_cast<int>(masks.dim(3));
    if (maskCount <= 0 || maskHeight <= 0 || maskWidth <= 0)
        return std::nullopt;

    // Multi-mask exports return three candidates at increasing granularity;
    // take the one SAM itself rates highest rather than the first.
    int best = 0;
    float bestScore = 0.0f;
    if (!scores.f32.empty()) {
        for (int i = 0; i < maskCount && i < static_cast<int>(scores.f32.size()); ++i) {
            if (scores.f32[static_cast<size_t>(i)] > bestScore) {
                bestScore = scores.f32[static_cast<size_t>(i)];
                best = i;
            }
        }
    }

    const size_t pixels = static_cast<size_t>(maskHeight) * maskWidth;
    const size_t offset = static_cast<size_t>(best) * pixels;
    if (offset + pixels > masks.f32.size())
        return std::nullopt;

    const cv::Mat logits(maskHeight, maskWidth, CV_32F,
                         const_cast<float *>(masks.f32.data() + offset));

    // SAM's mask logits are thresholded at zero, not at a sigmoid of 0.5 — the
    // two are equivalent, but the logits are what comes out of the graph.
    cv::Mat binary;
    cv::threshold(logits, binary, 0.0, 255.0, cv::THRESH_BINARY);
    binary.convertTo(binary, CV_8U);

    cv::Mat atSourceScale;
    if (maskWidth == m_sourceSize.width() && maskHeight == m_sourceSize.height()) {
        // The export took orig_im_size, so the mask is already in source space.
        atSourceScale = binary;
    } else {
        // The export returns a 256x256 mask covering the *padded* 1024 square.
        // Crop away the padding before rescaling, or the mask ends up squashed
        // by exactly the letterbox ratio.
        const double toMask = static_cast<double>(maskWidth) / kEncoderSize;
        const int validWidth = std::max(
            1, static_cast<int>(std::round(m_sourceSize.width() * m_transform.scale * toMask)));
        const int validHeight = std::max(
            1, static_cast<int>(std::round(m_sourceSize.height() * m_transform.scale * toMask)));

        const cv::Rect valid = cv::Rect(0, 0, validWidth, validHeight)
                               & cv::Rect(0, 0, maskWidth, maskHeight);
        if (valid.width <= 0 || valid.height <= 0)
            return std::nullopt;

        cv::resize(binary(valid), atSourceScale,
                   cv::Size(m_sourceSize.width(), m_sourceSize.height()), 0, 0,
                   cv::INTER_NEAREST);
    }

    Result result;
    result.polygon = ImageBlob::largestContourPolygon(atSourceScale, polygonEpsilon);
    if (!result.isValid())
        return std::nullopt;

    result.box = ImageBlob::maskBounds(atSourceScale);
    result.score = bestScore > 0.0f ? bestScore : 1.0f;
    return result;
}
