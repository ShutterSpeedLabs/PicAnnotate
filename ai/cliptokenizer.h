#ifndef CLIPTOKENIZER_H
#define CLIPTOKENIZER_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

// CLIP's byte-pair-encoding tokenizer.
//
// Implemented here rather than shelling out to Python because the text encoder
// is useless without it, and requiring a Python install to classify a region
// would defeat the point of a native tool. It reads the same vocab.json and
// merges.txt that ship with the Hugging Face model, so the tokenisation matches
// the weights exactly — an approximation would embed class names into slightly
// the wrong place in CLIP's space and silently degrade every score.
class ClipTokenizer
{
public:
    ClipTokenizer();

    // Reads vocab.json and merges.txt. Both live next to the model files; see
    // docs/AI_SETUP.md.
    bool load(const QString &vocabJsonPath, const QString &mergesTxtPath, QString *error);
    bool isLoaded() const { return !m_vocab.isEmpty() && !m_ranks.isEmpty(); }
    void unload();

    // Token ids for one string, wrapped in start/end markers and padded to
    // `contextLength`. Text longer than the context is truncated with the end
    // marker preserved, because the text encoder pools at that position.
    QVector<qint64> encode(const QString &text, int contextLength = 77) const;

    // 1 for real tokens, 0 for padding — the attention mask the exported text
    // tower expects alongside the ids.
    QVector<qint64> attentionMask(const QVector<qint64> &tokenIds) const;

    int vocabularySize() const { return m_vocab.size(); }

    static constexpr int kContextLength = 77;

private:
    // Splits text into pre-BPE words the way CLIP does: lowercase, whitespace
    // collapsed, contractions kept whole, punctuation separated.
    QStringList pretokenize(const QString &text) const;

    // Byte-pair merges for one pre-token, returning its subword pieces.
    QStringList bpe(const QString &token) const;

    QHash<QString, int> m_vocab;          // subword -> id
    QHash<QString, int> m_ranks;          // "a b" -> merge priority (lower first)
    mutable QHash<QString, QStringList> m_bpeCache;

    // GPT-2's byte-to-printable-character map, so arbitrary UTF-8 survives a
    // vocabulary that only contains printable characters.
    QVector<QChar> m_byteEncoder;

    int m_startToken = -1;
    int m_endToken = -1;
};

#endif // CLIPTOKENIZER_H
