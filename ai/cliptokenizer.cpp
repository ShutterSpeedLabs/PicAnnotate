#include "cliptokenizer.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QObject>
#include <QRegularExpression>
#include <QTextStream>

#include <limits>

namespace {

// CLIP's pre-tokenizer. Contractions stay whole, letters group into words,
// digits are individual, and runs of punctuation group together. The two
// special markers are matched first so they are never split.
const char *kPatternText =
    R"(<\|startoftext\|>|<\|endoftext\|>|'s|'t|'re|'ve|'m|'ll|'d|[\p{L}]+|[\p{N}]|[^\s\p{L}\p{N}]+)";

const QRegularExpression &pretokenPattern()
{
    static const QRegularExpression pattern(
        QString::fromLatin1(kPatternText),
        QRegularExpression::UseUnicodePropertiesOption | QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

const QRegularExpression &whitespaceRun()
{
    static const QRegularExpression pattern(QStringLiteral("\\s+"));
    return pattern;
}

// GPT-2's reversible byte encoder: maps all 256 byte values onto printable
// Unicode code points, so a vocabulary of printable strings can represent any
// byte sequence.
QVector<QChar> buildByteEncoder()
{
    QVector<int> printable;
    for (int b = '!'; b <= '~'; ++b)
        printable.append(b);
    for (int b = 0xa1; b <= 0xac; ++b)
        printable.append(b);
    for (int b = 0xae; b <= 0xff; ++b)
        printable.append(b);

    QVector<int> mapped = printable;
    int next = 0;
    for (int b = 0; b < 256; ++b) {
        if (printable.contains(b))
            continue;
        // Bytes with no printable form are pushed above the BMP-safe range,
        // starting at U+0100.
        printable.append(b);
        mapped.append(256 + next);
        ++next;
    }

    QVector<QChar> encoder(256);
    for (int i = 0; i < printable.size(); ++i)
        encoder[printable.at(i)] = QChar(static_cast<char16_t>(mapped.at(i)));
    return encoder;
}

// Adjacent character pairs of a word, as "a b" keys matching merges.txt.
QStringList pairsOf(const QStringList &word)
{
    QStringList pairs;
    for (int i = 0; i + 1 < word.size(); ++i)
        pairs.append(word.at(i) + QLatin1Char(' ') + word.at(i + 1));
    return pairs;
}

} // namespace

ClipTokenizer::ClipTokenizer()
    : m_byteEncoder(buildByteEncoder())
{
}

void ClipTokenizer::unload()
{
    m_vocab.clear();
    m_ranks.clear();
    m_bpeCache.clear();
    m_startToken = -1;
    m_endToken = -1;
}

bool ClipTokenizer::load(const QString &vocabJsonPath, const QString &mergesTxtPath,
                         QString *error)
{
    unload();

    QFile vocabFile(vocabJsonPath);
    if (!vocabFile.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QObject::tr("Could not open %1: %2")
                         .arg(vocabJsonPath, vocabFile.errorString());
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(vocabFile.readAll(), &parseError);
    if (!doc.isObject()) {
        if (error) {
            *error = QObject::tr("%1 is not a valid vocabulary file: %2")
                         .arg(vocabJsonPath, parseError.errorString());
        }
        return false;
    }

    const QJsonObject vocabObject = doc.object();
    m_vocab.reserve(vocabObject.size());
    for (auto it = vocabObject.constBegin(); it != vocabObject.constEnd(); ++it)
        m_vocab.insert(it.key(), it.value().toInt());

    QFile mergesFile(mergesTxtPath);
    if (!mergesFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = QObject::tr("Could not open %1: %2")
                         .arg(mergesTxtPath, mergesFile.errorString());
        }
        unload();
        return false;
    }

    QTextStream stream(&mergesFile);
    int rank = 0;
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        // The first line is a "#version" header, and blank lines are padding.
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.count(QLatin1Char(' ')) != 1)
            continue;
        m_ranks.insert(line, rank++);
    }

    if (m_ranks.isEmpty()) {
        if (error)
            *error = QObject::tr("%1 contained no merge rules.").arg(mergesTxtPath);
        unload();
        return false;
    }

    m_startToken = m_vocab.value(QStringLiteral("<|startoftext|>"), -1);
    m_endToken = m_vocab.value(QStringLiteral("<|endoftext|>"), -1);
    if (m_startToken < 0 || m_endToken < 0) {
        if (error) {
            *error = QObject::tr("%1 has no <|startoftext|>/<|endoftext|> markers, so it is not "
                                 "a CLIP vocabulary.")
                         .arg(vocabJsonPath);
        }
        unload();
        return false;
    }

    return true;
}

QStringList ClipTokenizer::pretokenize(const QString &text) const
{
    QString cleaned = text;
    cleaned.replace(whitespaceRun(), QStringLiteral(" "));
    cleaned = cleaned.trimmed().toLower();

    QStringList tokens;
    QRegularExpressionMatchIterator it = pretokenPattern().globalMatch(cleaned);
    while (it.hasNext())
        tokens.append(it.next().captured(0));
    return tokens;
}

QStringList ClipTokenizer::bpe(const QString &token) const
{
    const auto cached = m_bpeCache.constFind(token);
    if (cached != m_bpeCache.constEnd())
        return cached.value();

    if (token.isEmpty())
        return {};

    // Every word ends with an explicit end-of-word marker on its last
    // character, which is how BPE distinguishes "in" inside a word from "in"
    // as a whole word.
    QStringList word;
    word.reserve(token.size());
    for (int i = 0; i + 1 < token.size(); ++i)
        word.append(QString(token.at(i)));
    word.append(QString(token.at(token.size() - 1)) + QStringLiteral("</w>"));

    if (word.size() == 1) {
        m_bpeCache.insert(token, word);
        return word;
    }

    while (true) {
        const QStringList pairs = pairsOf(word);
        if (pairs.isEmpty())
            break;

        // Merge the highest-priority (lowest-rank) pair present.
        QString bestPair;
        int bestRank = std::numeric_limits<int>::max();
        for (const QString &pair : pairs) {
            const auto rank = m_ranks.constFind(pair);
            if (rank != m_ranks.constEnd() && rank.value() < bestRank) {
                bestRank = rank.value();
                bestPair = pair;
            }
        }
        if (bestPair.isEmpty())
            break;

        const int space = bestPair.indexOf(QLatin1Char(' '));
        const QString first = bestPair.left(space);
        const QString second = bestPair.mid(space + 1);

        QStringList merged;
        merged.reserve(word.size());
        for (int i = 0; i < word.size();) {
            if (i + 1 < word.size() && word.at(i) == first && word.at(i + 1) == second) {
                merged.append(first + second);
                i += 2;
            } else {
                merged.append(word.at(i));
                ++i;
            }
        }

        word = merged;
        if (word.size() == 1)
            break;
    }

    m_bpeCache.insert(token, word);
    return word;
}

QVector<qint64> ClipTokenizer::encode(const QString &text, int contextLength) const
{
    QVector<qint64> ids(contextLength, 0);
    if (!isLoaded() || contextLength < 2)
        return ids;

    QVector<qint64> body;
    for (const QString &pretoken : pretokenize(text)) {
        // Byte-encode before BPE so non-ASCII class names still tokenise.
        const QByteArray utf8 = pretoken.toUtf8();
        QString encoded;
        encoded.reserve(utf8.size());
        for (char raw : utf8)
            encoded.append(m_byteEncoder.at(static_cast<quint8>(raw)));

        for (const QString &piece : bpe(encoded)) {
            const auto id = m_vocab.constFind(piece);
            if (id != m_vocab.constEnd())
                body.append(id.value());
        }
    }

    // The text tower pools at the end marker's position, so truncation has to
    // keep it: dropping it would make the embedding come from a random token.
    const int room = contextLength - 2;
    if (body.size() > room)
        body.resize(room);

    ids[0] = m_startToken;
    for (int i = 0; i < body.size(); ++i)
        ids[i + 1] = body.at(i);
    ids[body.size() + 1] = m_endToken;

    return ids;
}

QVector<qint64> ClipTokenizer::attentionMask(const QVector<qint64> &tokenIds) const
{
    QVector<qint64> mask(tokenIds.size(), 0);

    // Everything up to and including the end marker is real; the rest is
    // padding. Scanning for the marker rather than for non-zero ids is what
    // makes this correct when a token legitimately has id 0.
    for (int i = 0; i < tokenIds.size(); ++i) {
        mask[i] = 1;
        if (tokenIds.at(i) == m_endToken)
            break;
    }
    return mask;
}
