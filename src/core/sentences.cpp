#include "core/sentences.h"

#include <unicode/brkiter.h>
#include <unicode/unistr.h>

#include <memory>

namespace neosea {

QPair<int, int> sentenceAt(const QString &text, int offset, const QString &lang)
{
    if (text.trimmed().isEmpty()) return {0, 0};
    UErrorCode err = U_ZERO_ERROR;
    // with ICU's abbreviation list, so "Mr. Smith" is one sentence
    QByteArray loc = lang.section('-', 0, 0).toLatin1();
    if (loc.isEmpty()) loc = "en";
    std::unique_ptr<icu::BreakIterator> it(icu::BreakIterator::createSentenceInstance(icu::Locale((loc + "@ss=standard").constData()), err));
    if (U_FAILURE(err)) return {0, int(text.size())};
    const icu::UnicodeString u(reinterpret_cast<const UChar *>(text.utf16()), int32_t(text.size()));
    it->setText(u);
    int start = 0, hitStart = -1, hitEnd = -1, lastStart = 0, lastEnd = int(text.size());
    for (int end = it->next(); end != icu::BreakIterator::DONE; start = end, end = it->next()) {
        lastStart = start;
        lastEnd = end;
        if (offset >= start && offset <= end) {
            hitStart = start;
            hitEnd = end;
            if (offset < end) break; // the caret at a sentence's end still belongs to it, unless the next one has it
        }
    }
    if (hitStart < 0) {
        hitStart = lastStart;
        hitEnd = lastEnd;
    }
    int e = hitEnd;
    while (e > hitStart && text[e - 1].isSpace()) e--;
    return {hitStart, std::max(e, hitStart)};
}

QList<QPair<int, int>> sentenceSpans(const QString &text, int from, const QString &lang)
{
    QList<QPair<int, int>> out;
    if (text.trimmed().isEmpty()) return out;
    UErrorCode err = U_ZERO_ERROR;
    QByteArray loc = lang.section('-', 0, 0).toLatin1();
    if (loc.isEmpty()) loc = "en";
    std::unique_ptr<icu::BreakIterator> it(icu::BreakIterator::createSentenceInstance(icu::Locale((loc + "@ss=standard").constData()), err));
    if (U_FAILURE(err)) return {{from, int(text.size())}};
    const icu::UnicodeString u(reinterpret_cast<const UChar *>(text.utf16()), int32_t(text.size()));
    it->setText(u);
    int start = 0;
    for (int end = it->next(); end != icu::BreakIterator::DONE; start = end, end = it->next()) {
        const int a = std::max(start, from);
        if (end > a && !text.mid(a, end - a).trimmed().isEmpty()) out << qMakePair(a, end);
    }
    return out;
}

} // namespace neosea
