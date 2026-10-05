#include "core/vim.h"

namespace neosea::vim {

int charClass(QChar c)
{
    if (c.isNull() || c.isSpace()) return 0;
    if (c.isLetterOrNumber() || c.isMark() || c == '_' || c == '\'' || c == QChar(0x2019)) return 1;
    return 2;
}

int wordSteps(QChar key, const QString &before, const QString &after)
{
    if (key == 'b') {
        QString chars = before;
        std::reverse(chars.begin(), chars.end());
        if (chars.isEmpty()) return -1;
        int n = 0;
        while (n < chars.size() && charClass(chars[n]) == 0) n++;
        const int cls = n < chars.size() ? charClass(chars[n]) : 0;
        while (n < chars.size() && cls && charClass(chars[n]) == cls) n++;
        return n;
    }
    const QString &chars = after;
    auto at = [&](int i) { return i < chars.size() ? chars[i] : QChar(); };
    int n = 0;
    if (key == 'w') {
        const int start = charClass(at(0));
        while (n < chars.size() && start && charClass(chars[n]) == start) n++;
        while (n < chars.size() && charClass(chars[n]) == 0) n++;
        return n >= chars.size() ? -1 : n;
    }
    // e: to the last letter of this word, or of the next
    n = 1;
    while (n < chars.size() && charClass(chars[n]) == 0) n++;
    const int cls = charClass(at(n));
    while (n + 1 < chars.size() && charClass(chars[n + 1]) == cls) n++;
    return n >= chars.size() ? -1 : n;
}

} // namespace neosea::vim
