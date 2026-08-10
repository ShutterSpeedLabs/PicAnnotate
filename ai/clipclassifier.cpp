#include "clipclassifier.h"

#include "imageblob.h"
#include "modelmanager.h"

#include <QDir>
#include <QFileInfo>
#include <QObject>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// Dot product of two vectors that are already unit length, i.e. their cosine
// similarity. Both towers L2-normalise inside the graph (see docs/AI_SETUP.md),
// and normalising again here is done defensively in case an export does not.
float dot(const QVector<float> &a, const QVector<float> &b)
{
    const int n = std::min(a.size(), b.size());
    float sum = 0.0f;
    for (int i = 0; i < n; ++i)
        sum += a.at(i) * b.at(i);
    return sum;
}

void normalise(QVector<float> *vector)
{
    double sumSquares = 0.0;
    for (float value : *vector)
        sumSquares += static_cast<double>(value) * value;

    const double length = std::sqrt(sumSquares);
    if (length <= 1e-8)
        return;

    for (float &value : *vector)
        value = static_cast<float>(value / length);
}

} // namespace

QStringList ClipClassifier::promptTemplates()
{
    // A small ensemble from OpenAI's published CLIP prompt set, biased towards
    // the ones that suit cropped objects rather than whole scenes.
    return {QStringLiteral("a photo of a %1."),
            QStringLiteral("a close-up photo of a %1."),
            QStringLiteral("a cropped photo of a %1."),
            QStringLiteral("a photo of one %1.")};
}

void ClipClassifier::setContextMargin(double margin)
{
    m_contextMargin = qBound(0.0, margin, 1.0);
}

bool ClipClassifier::isReady() const
{
    return m_imageEncoder.isLoaded() && m_textEncoder.isLoaded() && m_tokenizer.isLoaded();
}

void ClipClassifier::unload()
{
    m_imageEncoder.unload();
    m_textEncoder.unload();
    m_tokenizer.unload();
    m_candidates.clear();
    m_textEmbeddings.clear();
}

bool ClipClassifier::load(QString *error)
{
    ModelManager &manager = ModelManager::instance();

    const ModelSpec *imageSpec = manager.selectedModel(ModelTask::ClipImage);
    const ModelSpec *textSpec = manager.selectedModel(ModelTask::ClipText);

    if (!imageSpec || !textSpec) {
        if (error) {
            *error = QObject::tr("Zero-shot classification needs both CLIP towers. Open the "
                                 "Model Manager to select them.");
        }
        return false;
    }

    QString reason;
    if (!manager.isUsable(imageSpec->id, &reason) || !manager.isUsable(textSpec->id, &reason)) {
        if (error)
            *error = reason;
        return false;
    }

    const ExecutionProvider provider = manager.preferredProvider();
    const QString textPath = manager.localPath(textSpec->id);

    if (!m_imageEncoder.load(manager.localPath(imageSpec->id), provider, error))
        return false;
    if (!m_textEncoder.load(textPath, provider, error)) {
        m_imageEncoder.unload();
        return false;
    }
    if (!loadTokenizer(textPath, error)) {
        m_imageEncoder.unload();
        m_textEncoder.unload();
        return false;
    }

    m_candidates.clear();
    m_textEmbeddings.clear();
    return true;
}

bool ClipClassifier::loadTokenizer(const QString &textModelPath, QString *error)
{
    // Look beside the model first, then in the standard model folders — the two
    // small text files usually arrive with the .onnx but may have been dropped
    // in the shared models directory instead.
    QStringList directories;
    directories << QFileInfo(textModelPath).absolutePath();
    directories << ModelManager::instance().searchPaths();

    for (const QString &directory : directories) {
        const QString vocab = QDir(directory).filePath(QStringLiteral("vocab.json"));
        const QString merges = QDir(directory).filePath(QStringLiteral("merges.txt"));
        if (QFileInfo::exists(vocab) && QFileInfo::exists(merges))
            return m_tokenizer.load(vocab, merges, error);
    }

    if (error) {
        *error = QObject::tr("CLIP's tokenizer files were not found. Put vocab.json and "
                             "merges.txt in one of:\n  %1\n\nBoth are small text files from "
                             "the model's Hugging Face page; see docs/AI_SETUP.md.")
                     .arg(directories.join(QStringLiteral("\n  ")));
    }
    return false;
}

std::optional<QVector<float>> ClipClassifier::embedClassName(const QString &name, QString *error)
{
    const QStringList inputNames = m_textEncoder.inputNames();
    if (inputNames.isEmpty()) {
        if (error)
            *error = QObject::tr("The CLIP text encoder declares no inputs.");
        return std::nullopt;
    }

    // Name the ids and mask inputs by position only as a last resort: an export
    // that reorders them would otherwise feed the mask in as token ids.
    QString idsInput = inputNames.first();
    QString maskInput;
    for (const QString &input : inputNames) {
        const QString lower = input.toLower();
        if (lower.contains(QLatin1String("input_ids")) || lower.contains(QLatin1String("token")))
            idsInput = input;
        else if (lower.contains(QLatin1String("mask")))
            maskInput = input;
    }

    QVector<float> accumulated;

    for (const QString &templateText : promptTemplates()) {
        const QString prompt = templateText.arg(name);
        const QVector<qint64> ids = m_tokenizer.encode(prompt, ClipTokenizer::kContextLength);
        const QVector<qint64> mask = m_tokenizer.attentionMask(ids);

        QVector<NamedTensor> inputs;
        inputs.append({idsInput,
                       Tensor::fromInt64s({1, ClipTokenizer::kContextLength},
                                          std::vector<qint64>(ids.begin(), ids.end()))});
        if (!maskInput.isEmpty()) {
            inputs.append({maskInput,
                           Tensor::fromInt64s({1, ClipTokenizer::kContextLength},
                                              std::vector<qint64>(mask.begin(), mask.end()))});
        }

        QVector<Tensor> outputs;
        if (!m_textEncoder.run(inputs, {}, &outputs, error))
            return std::nullopt;
        if (outputs.isEmpty() || outputs.first().f32.empty()) {
            if (error)
                *error = QObject::tr("The CLIP text encoder produced no embedding.");
            return std::nullopt;
        }

        QVector<float> embedding(outputs.first().f32.begin(), outputs.first().f32.end());
        normalise(&embedding);

        if (accumulated.isEmpty()) {
            accumulated = embedding;
        } else {
            const int n = std::min(accumulated.size(), embedding.size());
            for (int i = 0; i < n; ++i)
                accumulated[i] += embedding.at(i);
        }
    }

    if (accumulated.isEmpty())
        return std::nullopt;

    // Averaging unit vectors then renormalising is the standard prompt-ensemble
    // recipe; the sum's direction is what carries the meaning.
    normalise(&accumulated);
    return accumulated;
}

bool ClipClassifier::setCandidates(const QVector<Candidate> &candidates, QString *error)
{
    if (!isReady()) {
        if (error)
            *error = QObject::tr("CLIP models are not loaded.");
        return false;
    }

    m_candidates.clear();
    m_textEmbeddings.clear();

    for (const Candidate &candidate : candidates) {
        if (candidate.name.trimmed().isEmpty())
            continue;

        const std::optional<QVector<float>> embedding = embedClassName(candidate.name, error);
        if (!embedding) {
            m_candidates.clear();
            m_textEmbeddings.clear();
            return false;
        }

        m_candidates.append(candidate);
        m_textEmbeddings.append(*embedding);
    }

    if (m_candidates.isEmpty()) {
        if (error) {
            *error = QObject::tr("There are no classes to score against. Add some in the "
                                 "Labels panel first.");
        }
        return false;
    }
    return true;
}

std::optional<QVector<float>> ClipClassifier::embedImage(const cv::Mat &bgrCrop, QString *error)
{
    if (bgrCrop.empty()) {
        if (error)
            *error = QObject::tr("The region is empty.");
        return std::nullopt;
    }

    cv::Mat resized;
    // CLIP resizes with bicubic; matching the training transform matters more
    // here than the marginal speed of a cheaper filter.
    cv::resize(bgrCrop, resized, cv::Size(kImageSize, kImageSize), 0, 0, cv::INTER_CUBIC);

    const Tensor input = ImageBlob::toNchwFloat(resized, /*swapRedBlue=*/true,
                                                /*scaleDivisor=*/255.0,
                                                cv::Scalar(kMeanR, kMeanG, kMeanB),
                                                cv::Scalar(kStdR, kStdG, kStdB));
    if (!input.isValid()) {
        if (error)
            *error = QObject::tr("Could not build the CLIP image tensor.");
        return std::nullopt;
    }

    const QStringList inputNames = m_imageEncoder.inputNames();
    if (inputNames.isEmpty()) {
        if (error)
            *error = QObject::tr("The CLIP image encoder declares no inputs.");
        return std::nullopt;
    }

    QVector<Tensor> outputs;
    if (!m_imageEncoder.run({{inputNames.first(), input}}, {}, &outputs, error))
        return std::nullopt;
    if (outputs.isEmpty() || outputs.first().f32.empty()) {
        if (error)
            *error = QObject::tr("The CLIP image encoder produced no embedding.");
        return std::nullopt;
    }

    QVector<float> embedding(outputs.first().f32.begin(), outputs.first().f32.end());
    normalise(&embedding);
    return embedding;
}

QVector<ClipClassifier::Score> ClipClassifier::classifyCrop(const cv::Mat &bgrCrop, QString *error)
{
    QVector<Score> scores;

    if (!isReady()) {
        if (error)
            *error = QObject::tr("CLIP models are not loaded.");
        return scores;
    }
    if (m_candidates.isEmpty()) {
        if (error)
            *error = QObject::tr("No classes have been embedded yet.");
        return scores;
    }

    const std::optional<QVector<float>> embedding = embedImage(bgrCrop, error);
    if (!embedding)
        return scores;

    scores.reserve(m_candidates.size());
    for (int i = 0; i < m_candidates.size(); ++i) {
        Score score;
        score.labelId = m_candidates.at(i).labelId;
        score.className = m_candidates.at(i).name;
        score.similarity = dot(*embedding, m_textEmbeddings.at(i));
        scores.append(score);
    }

    // Softmax with CLIP's learned temperature (logit_scale = 100). Raw cosine
    // similarities all sit in a narrow band around 0.2, which reads as "the
    // model is unsure about everything"; the temperature is what turns them
    // into the confident distribution CLIP was trained to produce.
    constexpr double kLogitScale = 100.0;
    double maxLogit = -std::numeric_limits<double>::infinity();
    for (const Score &score : scores)
        maxLogit = std::max(maxLogit, kLogitScale * score.similarity);

    double sum = 0.0;
    QVector<double> exponentials;
    exponentials.reserve(scores.size());
    for (const Score &score : scores) {
        const double value = std::exp(kLogitScale * score.similarity - maxLogit);
        exponentials.append(value);
        sum += value;
    }
    for (int i = 0; i < scores.size(); ++i)
        scores[i].probability = static_cast<float>(sum > 0.0 ? exponentials.at(i) / sum : 0.0);

    std::sort(scores.begin(), scores.end(),
              [](const Score &a, const Score &b) { return a.similarity > b.similarity; });

    return scores;
}

QVector<ClipClassifier::Score> ClipClassifier::classifyRegion(const cv::Mat &bgrImage,
                                                              const QRectF &box, QString *error)
{
    if (bgrImage.empty() || !box.isValid()) {
        if (error)
            *error = QObject::tr("The region is empty.");
        return {};
    }

    // Square the box around its centre before adding context. Feeding CLIP a
    // stretched crop of a tall object distorts it away from anything the model
    // has seen; squaring keeps the object whole and its proportions intact.
    const QPointF centre = box.center();
    const double side = std::max(box.width(), box.height()) * (1.0 + m_contextMargin);
    const double half = side / 2.0;

    const int left = std::max(0, static_cast<int>(std::floor(centre.x() - half)));
    const int top = std::max(0, static_cast<int>(std::floor(centre.y() - half)));
    const int right = std::min(bgrImage.cols, static_cast<int>(std::ceil(centre.x() + half)));
    const int bottom = std::min(bgrImage.rows, static_cast<int>(std::ceil(centre.y() + half)));

    if (right - left < 2 || bottom - top < 2) {
        if (error)
            *error = QObject::tr("The region is too small to classify.");
        return {};
    }

    return classifyCrop(bgrImage(cv::Rect(left, top, right - left, bottom - top)), error);
}
