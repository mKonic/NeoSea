#include "core/htmldom.h"

#include <QHash>
#include <QSet>

namespace neosea::html {

namespace {

const QSet<QString> &voids()
{
    static const QSet<QString> s{"br", "img", "hr", "meta", "link", "input", "wbr", "col", "area",
                                 "base", "embed", "source", "track", "param"};
    return s;
}

const QSet<QString> &blocks()
{
    static const QSet<QString> s{"p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li",
                                 "blockquote", "pre", "section", "article", "header", "footer", "table",
                                 "tr", "td", "th", "dd", "dt", "dl", "nav", "aside", "figure", "hr",
                                 "address", "main", "tbody", "thead", "tfoot"};
    return s;
}

const QHash<QString, QString> &entities()
{
    static const QHash<QString, QString> e{
        {"amp", "&"},       {"lt", "<"},         {"gt", ">"},        {"quot", "\""},     {"apos", "'"},
        {"nbsp", " "}, {"mdash", "—"},      {"ndash", "–"},     {"hellip", "…"},    {"lsquo", "‘"},
        {"rsquo", "’"},     {"ldquo", "“"},      {"rdquo", "”"},     {"sbquo", "‚"},     {"bdquo", "„"},
        {"laquo", "«"},     {"raquo", "»"},      {"copy", "©"},      {"reg", "®"},       {"trade", "™"},
        {"shy", "­"},  {"thinsp", " "}, {"ensp", " "}, {"emsp", " "}, {"zwj", "‍"},
        {"zwnj", "‌"}, {"middot", "·"},     {"bull", "•"},      {"deg", "°"},       {"times", "×"},
        {"eacute", "é"},    {"egrave", "è"},     {"agrave", "à"},    {"ccedil", "ç"},    {"uuml", "ü"},
        {"ouml", "ö"},      {"auml", "ä"},       {"szlig", "ß"},     {"ntilde", "ñ"},    {"iexcl", "¡"},
        {"iquest", "¿"},    {"sect", "§"},       {"para", "¶"},      {"dagger", "†"},    {"prime", "′"},
    };
    return e;
}

const QSet<QString> &rawText()
{
    static const QSet<QString> s{"script", "style", "title", "textarea", "xml", "head"};
    return s;
}

struct Parser {
    const QString &s;
    qsizetype i = 0;
    NodePtr root;
    QList<Node *> stack;

    explicit Parser(const QString &src) : s(src)
    {
        root = element("#root");
        stack << root.get();
    }

    Node *top() { return stack.last(); }

    void addText(const QString &raw)
    {
        if (raw.isEmpty()) return;
        const QString text = decodeEntities(raw);
        Node *t = top();
        if (!t->children.isEmpty() && t->children.last()->type == Node::Text) {
            t->children.last()->text += text;
            return;
        }
        t->appendChild(textNode(text));
    }

    bool inStack(const QString &tag) const
    {
        for (qsizetype k = stack.size() - 1; k > 0; --k)
            if (stack[k]->tag == tag) return true;
        return false;
    }

    void closeTo(const QString &tag)
    {
        while (stack.size() > 1) {
            Node *n = stack.takeLast();
            if (n->tag == tag) break;
        }
    }

    void startTag(const QString &name, QList<QPair<QString, QString>> attrs, bool selfClosing)
    {
        // a block start closes an open paragraph, as HTML does; a list item
        // closes the one before it
        if (blocks().contains(name) && inStack("p")) {
            // only when the p is the nearest block (p inside div inside p can't happen)
            for (qsizetype k = stack.size() - 1; k > 0; --k) {
                if (stack[k]->tag == "p") { closeTo("p"); break; }
                if (blocks().contains(stack[k]->tag)) break;
            }
        }
        if (name == "li" && inStack("li")) {
            for (qsizetype k = stack.size() - 1; k > 0; --k) {
                if (stack[k]->tag == "li") { closeTo("li"); break; }
                if (stack[k]->tag == "ul" || stack[k]->tag == "ol") break;
            }
        }
        NodePtr el = element(name);
        el->attrs = std::move(attrs);
        top()->appendChild(el);
        if (!selfClosing && !isVoid(name)) stack << el.get();
    }

    void endTag(const QString &name)
    {
        if (name == "br") { // </br> reads as <br>
            top()->appendChild(element("br"));
            return;
        }
        if (inStack(name)) closeTo(name);
    }

    // the tag at s[i] == '<'; returns false when it isn't a tag (a lone '<')
    bool tag()
    {
        const qsizetype start = i;
        if (s.mid(i, 4) == QLatin1String("<!--")) {
            const qsizetype end = s.indexOf(QLatin1String("-->"), i + 4);
            i = end < 0 ? s.size() : end + 3;
            return true;
        }
        if (s.mid(i, 2) == QLatin1String("<!") || s.mid(i, 2) == QLatin1String("<?")) {
            const qsizetype end = s.indexOf('>', i);
            i = end < 0 ? s.size() : end + 1;
            return true;
        }
        qsizetype j = i + 1;
        const bool closing = j < s.size() && s[j] == '/';
        if (closing) ++j;
        const qsizetype nameStart = j;
        while (j < s.size() && (s[j].isLetterOrNumber() || s[j] == ':' || s[j] == '-' || s[j] == '_')) ++j;
        if (j == nameStart) { i = start; return false; }
        const QString name = s.mid(nameStart, j - nameStart).toLower();
        QList<QPair<QString, QString>> attrs;
        bool selfClosing = false;
        // attributes
        while (j < s.size() && s[j] != '>') {
            if (s[j].isSpace()) { ++j; continue; }
            if (s[j] == '/') { selfClosing = true; ++j; continue; }
            const qsizetype an = j;
            while (j < s.size() && !s[j].isSpace() && s[j] != '=' && s[j] != '>' && s[j] != '/') ++j;
            QString aname = s.mid(an, j - an).toLower();
            QString value;
            while (j < s.size() && s[j].isSpace()) ++j;
            if (j < s.size() && s[j] == '=') {
                ++j;
                while (j < s.size() && s[j].isSpace()) ++j;
                if (j < s.size() && (s[j] == '"' || s[j] == '\'')) {
                    const QChar q = s[j++];
                    const qsizetype vs = j;
                    while (j < s.size() && s[j] != q) ++j;
                    value = decodeEntities(s.mid(vs, j - vs));
                    if (j < s.size()) ++j;
                } else {
                    const qsizetype vs = j;
                    while (j < s.size() && !s[j].isSpace() && s[j] != '>') ++j;
                    value = decodeEntities(s.mid(vs, j - vs));
                }
            }
            if (!aname.isEmpty()) {
                bool dup = false;
                for (const auto &a : attrs) dup |= a.first == aname;
                if (!dup) attrs.append({aname, value});
            } else {
                ++j;
            }
        }
        i = j < s.size() ? j + 1 : s.size();
        if (closing) {
            endTag(name);
            return true;
        }
        if (rawText().contains(name)) {
            // dropped whole: nothing in a clipboard loads or runs
            const qsizetype end = s.indexOf(QLatin1String("</") + name, i, Qt::CaseInsensitive);
            if (end < 0) { i = s.size(); return true; }
            const qsizetype gt = s.indexOf('>', end);
            i = gt < 0 ? s.size() : gt + 1;
            return true;
        }
        startTag(name, std::move(attrs), selfClosing);
        return true;
    }

    void run()
    {
        qsizetype textStart = 0;
        while (i < s.size()) {
            if (s[i] == '<') {
                const qsizetype here = i;
                addText(s.mid(textStart, here - textStart));
                if (!tag()) { // not a tag: a literal '<'
                    i = here + 1;
                    textStart = here;
                    continue;
                }
                textStart = i;
                continue;
            }
            ++i;
        }
        addText(s.mid(textStart));
    }
};

void serialize(const Node &n, QString &out)
{
    if (n.type == Node::Text) {
        out += escapeText(n.text);
        return;
    }
    out += '<' + n.tag;
    for (const auto &[k, v] : n.attrs) out += ' ' + k + "=\"" + escapeAttr(v) + '"';
    out += '>';
    if (isVoid(n.tag)) return;
    for (const auto &c : n.children) serialize(*c, out);
    out += "</" + n.tag + '>';
}

} // namespace

bool Node::isElement(const char *name) const
{
    return type == Element && (!name || tag == QLatin1String(name));
}

QString Node::attr(const QString &name) const
{
    for (const auto &[k, v] : attrs)
        if (k == name) return v;
    return {};
}

bool Node::hasAttr(const QString &name) const
{
    for (const auto &a : attrs)
        if (a.first == name) return true;
    return false;
}

void Node::setAttr(const QString &name, const QString &value)
{
    for (auto &a : attrs)
        if (a.first == name) { a.second = value; return; }
    attrs.append({name, value});
}

void Node::removeAttr(const QString &name)
{
    attrs.removeIf([&](const auto &a) { return a.first == name; });
}

QStringList Node::classes() const { return attr("class").split(' ', Qt::SkipEmptyParts); }

bool Node::hasClass(const QString &cls) const { return classes().contains(cls); }

void Node::addClass(const QString &cls)
{
    QStringList c = classes();
    if (c.contains(cls)) return;
    c << cls;
    setAttr("class", c.join(' '));
}

void Node::removeClass(const QString &cls)
{
    QStringList c = classes();
    if (!c.removeAll(cls)) return;
    if (c.isEmpty()) removeAttr("class");
    else setAttr("class", c.join(' '));
}

QString Node::style(const QString &property) const
{
    for (const QString &decl : attr("style").split(';')) {
        const qsizetype colon = decl.indexOf(':');
        if (colon < 0) continue;
        if (decl.left(colon).trimmed().toLower() == property) return decl.mid(colon + 1).trimmed().toLower();
    }
    return {};
}

QString Node::textContent() const
{
    if (type == Text) return text;
    QString out;
    for (const auto &c : children) out += c->textContent();
    return out;
}

QList<Node *> Node::descendants(const char *tagName)
{
    QList<Node *> out;
    for (const auto &c : children) {
        if (c->type != Element) continue;
        if (!tagName || c->tag == QLatin1String(tagName)) out << c.get();
        out << c->descendants(tagName);
    }
    return out;
}

void Node::appendChild(std::shared_ptr<Node> child)
{
    child->parent = this;
    children.append(std::move(child));
}

void Node::unwrap()
{
    if (!parent) return;
    auto &sib = parent->children;
    for (qsizetype k = 0; k < sib.size(); ++k) {
        if (sib[k].get() != this) continue;
        NodePtr keep = sib[k]; // alive until we're done
        sib.removeAt(k);
        for (qsizetype c = 0; c < keep->children.size(); ++c) {
            keep->children[c]->parent = parent;
            sib.insert(k + c, keep->children[c]);
        }
        keep->children.clear();
        keep->parent = nullptr;
        return;
    }
}

void Node::remove()
{
    if (!parent) return;
    Node *p = parent;
    parent = nullptr;
    p->children.removeIf([this](const NodePtr &c) { return c.get() == this; });
}

NodePtr element(const QString &tag)
{
    auto n = std::make_shared<Node>();
    n->type = Node::Element;
    n->tag = tag;
    return n;
}

NodePtr textNode(const QString &text)
{
    auto n = std::make_shared<Node>();
    n->type = Node::Text;
    n->text = text;
    return n;
}

NodePtr parse(const QString &htmlText)
{
    Parser p(htmlText);
    p.run();
    return p.root;
}

QString innerHtml(const Node &n)
{
    QString out;
    for (const auto &c : n.children) serialize(*c, out);
    return out;
}

QString outerHtml(const Node &n)
{
    QString out;
    serialize(n, out);
    return out;
}

QString escapeText(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        switch (c.unicode()) {
        case '&': out += QLatin1String("&amp;"); break;
        case '<': out += QLatin1String("&lt;"); break;
        case '>': out += QLatin1String("&gt;"); break;
        case 0xa0: out += QLatin1String("&nbsp;"); break;
        default: out += c;
        }
    }
    return out;
}

QString escapeAttr(const QString &s)
{
    QString out;
    for (QChar c : s) {
        switch (c.unicode()) {
        case '&': out += QLatin1String("&amp;"); break;
        case '"': out += QLatin1String("&quot;"); break;
        case 0xa0: out += QLatin1String("&nbsp;"); break;
        default: out += c;
        }
    }
    return out;
}

QString escHtml(const QString &s)
{
    QString out = s;
    out.replace('&', QLatin1String("&amp;")).replace('<', QLatin1String("&lt;")).replace('>', QLatin1String("&gt;"));
    return out;
}

QString decodeEntities(const QString &s)
{
    if (!s.contains('&')) return s;
    QString out;
    out.reserve(s.size());
    qsizetype i = 0;
    while (i < s.size()) {
        if (s[i] != '&') { out += s[i++]; continue; }
        const qsizetype semi = s.indexOf(';', i + 1);
        if (semi < 0 || semi - i > 12) { out += s[i++]; continue; }
        const QString name = s.mid(i + 1, semi - i - 1);
        if (name.startsWith('#')) {
            bool ok = false;
            const uint cp = (name.size() > 1 && (name[1] == 'x' || name[1] == 'X')) ? name.mid(2).toUInt(&ok, 16)
                                                                                  : name.mid(1).toUInt(&ok, 10);
            if (ok && cp > 0 && cp <= 0x10FFFF) {
                out += QString::fromUcs4(reinterpret_cast<const char32_t *>(&cp), 1);
                i = semi + 1;
                continue;
            }
        } else if (auto it = entities().constFind(name); it != entities().constEnd()) {
            out += *it;
            i = semi + 1;
            continue;
        }
        out += s[i++];
    }
    return out;
}

bool isVoid(const QString &tag) { return voids().contains(tag); }
bool isBlock(const QString &tag) { return blocks().contains(tag); }

} // namespace neosea::html
