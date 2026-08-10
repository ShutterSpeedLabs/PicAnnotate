#ifndef CLIPCLASSIFIER_H
#define CLIPCLASSIFIER_H

#include "cliptokenizer.h"
#include "onnxsession.h"

#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include <opencv2/core.hpp>

#include <optional>

// Zero-shot region classification with CLIP.
//
// This is the piece that works on *your* classes. A detector can only name the
// eighty things COCO knows about; CLIP embeds an image crop and a piece of text
// into the same space, so "cracked insulator" can be scored against a region
// without anyone having trained a model on cracked insulators.
//
// Text embeddings are computed once per label schema and cached, because they
// depend only on the class names. Only the image tower runs per region.
class ClipClassifier
{
public:
    struct Score
    {
        int labelId = -1;        // project LabelSchema id
        QString className;
        float similarity = 0.0f; // cosine similarity, roughly 0.15-0.35 in practice
        float probability = 0.0f;// softmax over the candidates, easier to read
    };

    // One class to score against: the project's class id and the name to embed.
    struct Candidate
    {
        int labelId = -1;
        QString name;
    };

    bool load(QString *error);
    void unload();
    bool isReady() const;

    // Embeds the class names. Costs one text-tower pass per name per prompt
    // template, so it is done on a schema change rather than per region.
    bool setCandidates(const QVector<Candidate> &candidates, QString *error);
    const QVector<Candidate> &candidates() const { return m_candidates; }
    bool hasCandidates() const { return !m_candidates.isEmpty(); }

    // Scores a region of `bgrImage`, highest similarity first. The box is
    // squared and given a little context before cropping — CLIP was trained on
    // whole photographs, and a tightly cropped sliver scores poorly against
    // every class equally.
    QVector<Score> classifyRegion(const cv::Mat &bgrImage, const QRectF &box, QString *error);

    // Scores a crop that has already been cut out.
    QVector<Score> classifyCrop(const cv::Mat &bgrCrop, QString *error);

    // Prompt templates the class names are substituted into, then averaged.
    // Ensembling several phrasings is markedly more accurate than one, and it
    // costs nothing at classify time because the average is precomputed.
    static QStringList promptTemplates();

    // How much context to include around a box, as a fraction of its size.
    double contextMargin() const { return m_contextMargin; }
    void setContextMargin(double margin);

private:
    // Locates vocab.json and merges.txt beside the text model.
    bool loadTokenizer(const QString &textModelPath, QString *error);

    // L2-normalised embedding for one crop.
    std::optional<QVector<float>> embedImage(const cv::Mat &bgrCrop, QString *error);

    // Averaged, L2-normalised embedding for one class name across the templates.
    std::optional<QVector<float>> embedClassName(const QString &name, QString *error);

    OnnxSession m_imageEncoder;
    OnnxSession m_textEncoder;
    ClipTokenizer m_tokenizer;

    QVector<Candidate> m_candidates;

    // One row per candidate, each L2-normalised, so scoring is a dot product.
    QVector<QVector<float>> m_textEmbeddings;

    double m_contextMargin = 0.15;

    static constexpr int kImageSize = 224;

    // CLIP's normalisation statistics, in 0-1 RGB.
    static constexpr double kMeanR = 0.48145466;
    static constexpr double kMeanG = 0.4578275;
    static constexpr double kMeanB = 0.40821073;
    static constexpr double kStdR = 0.26862954;
    static constexpr double kStdG = 0.26130258;
    static constexpr double kStdB = 0.27577711;
};

#endif // CLIPCLASSIFIER_H
