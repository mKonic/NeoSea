#include "core/chapter.h"

#include "core/htmldom.h"

#include <QRegularExpression>

namespace neosea {

using html::Node;
using html::NodePtr;

namespace {

constexpr QChar kLineSep(0x2028);

const QStringList kScreenOnly{"data-attr", "data-speech", "data-first", "data-walk", "data-pg", "data-fill",
                              "data-contd", "data-ghost", "data-ghost-empty", "data-sp-paste"};

struct Fmt {
    bool b = false, i = false, u = false, s = false;
};

Fmt styleOf(const Node &el, Fmt f)
{
    const QString tag = el.tag;
    const QString fw = el.style("font-weight");
    const QString fs = el.style("font-style");
    QString deco = el.style("text-decoration");
    if (deco.isEmpty()) deco = el.style("text-decoration-line");
    if (tag == "i" || tag == "em" || fs == "italic") f.i = true;
    if (tag == "b" || tag == "strong" || fw == "bold" || fw == "bolder" || fw.toInt() >= 600) f.b = true;
    // Google Docs wraps a whole clipboard in <b style="font-weight:normal">
    if ((tag == "b" || tag == "strong") && (fw == "normal" || fw == "400")) f.b = false;
    if (fs == "normal" && (tag == "i" || tag == "em")) f.i = false;
    if (tag == "u" || tag == "ins" || deco.contains("underline")) f.u = true;
    if (tag == "s" || tag == "strike" || tag == "del" || deco.contains("line-through")) f.s = true;
    return f;
}

void pushText(QList<Run> &runs, const QString &text, const Fmt &f)
{
    if (text.isEmpty()) return;
    Run r;
    r.text = text;
    r.b = f.b;
    r.i = f.i;
    r.u = f.u;
    r.s = f.s;
    runs << r;
}

void collectRuns(const Node &n, Fmt f, QList<Run> &runs)
{
    for (const auto &c : n.children) {
        if (c->type == Node::Text) {
            pushText(runs, c->text, f);
            continue;
        }
        if (c->hasClass("ph-mark")) {
            Run m;
            m.mark = true;
            m.sid = c->attr("data-sid");
            m.text = QStringLiteral("⚑");
            runs << m;
            continue;
        }
        if (c->hasClass("darling-anchor")) continue; // legacy invisible markers
        if (c->tag == "br") {
            pushText(runs, QString(kLineSep), f);
            continue;
        }
        if (c->tag == "img" || c->tag == "hr") continue;
        collectRuns(*c, styleOf(*c, f), runs);
    }
}

Para paraFrom(const Node &p)
{
    Para out;
    if (p.tag == "p") {
        for (const auto &a : p.attrs)
            if (!kScreenOnly.contains(a.first)) out.attrs << a;
    }
    collectRuns(p, Fmt{}, out.runs);
    // a trailing <br> is how an editable paragraph stays open; it isn't text
    while (!out.runs.isEmpty() && !out.runs.last().mark && out.runs.last().text.endsWith(kLineSep)) {
        out.runs.last().text.chop(1);
        if (out.runs.last().text.isEmpty()) out.runs.removeLast();
    }
    out.normalize();
    return out;
}

} // namespace

// ---------------------------------------------------------------------------

QString Para::attr(const QString &name) const
{
    for (const auto &[k, v] : attrs)
        if (k == name) return v;
    return {};
}

bool Para::hasAttr(const QString &name) const
{
    for (const auto &a : attrs)
        if (a.first == name) return true;
    return false;
}

void Para::setAttr(const QString &name, const QString &value)
{
    for (auto &a : attrs)
        if (a.first == name) { a.second = value; return; }
    attrs.append({name, value});
}

void Para::removeAttr(const QString &name)
{
    attrs.removeIf([&](const auto &a) { return a.first == name; });
}

QStringList Para::classes() const { return attr("class").split(' ', Qt::SkipEmptyParts); }

bool Para::hasClass(const QString &cls) const { return classes().contains(cls); }

void Para::addClass(const QString &cls)
{
    QStringList c = classes();
    if (c.contains(cls)) return;
    c << cls;
    setAttr("class", c.join(' '));
}

void Para::removeClass(const QString &cls)
{
    QStringList c = classes();
    if (!c.removeAll(cls)) return;
    if (c.isEmpty()) removeAttr("class");
    else setAttr("class", c.join(' '));
}

QString Para::align() const
{
    for (const QString &decl : attr("style").split(';')) {
        const qsizetype colon = decl.indexOf(':');
        if (colon >= 0 && decl.left(colon).trimmed().toLower() == "text-align")
            return decl.mid(colon + 1).trimmed().toLower();
    }
    return {};
}

void Para::setAlign(const QString &a)
{
    QStringList keep;
    for (const QString &decl : attr("style").split(';', Qt::SkipEmptyParts)) {
        const qsizetype colon = decl.indexOf(':');
        if (colon >= 0 && decl.left(colon).trimmed().toLower() == "text-align") continue;
        if (!decl.trimmed().isEmpty()) keep << decl.trimmed();
    }
    if (!a.isEmpty() && a != "left") keep << "text-align: " + a;
    if (keep.isEmpty()) removeAttr("style");
    else setAttr("style", keep.join("; ") + ';');
}

QString Para::text() const
{
    QString out;
    for (const Run &r : runs)
        if (!r.mark) out += r.text;
    return out;
}

bool Para::hasMark() const
{
    for (const Run &r : runs)
        if (r.mark) return true;
    return false;
}

void Para::normalize()
{
    QList<Run> out;
    for (const Run &r : runs) {
        if (!r.mark && r.text.isEmpty()) continue;
        if (!out.isEmpty() && out.last().sameStyle(r)) out.last().text += r.text;
        else out << r;
    }
    runs = out;
}

QString runHtml(const Run &r)
{
    if (r.mark)
        return QStringLiteral("<span class=\"ph-mark\" data-sid=\"%1\" contenteditable=\"false\">⚑</span>")
            .arg(html::escapeAttr(r.sid));
    QString t;
    const QStringList lines = r.text.split(kLineSep);
    for (qsizetype k = 0; k < lines.size(); ++k) {
        if (k) t += QLatin1String("<br>");
        t += html::escapeText(lines[k]);
    }
    if (r.s) t = "<strike>" + t + "</strike>";
    if (r.u) t = "<u>" + t + "</u>";
    if (r.i) t = "<i>" + t + "</i>";
    if (r.b) t = "<b>" + t + "</b>";
    return t;
}

QString paraInnerHtml(const Para &p)
{
    QString inner;
    for (const Run &r : p.runs) inner += runHtml(r);
    // an empty paragraph keeps a <br>, or it would have no height to type in
    if (inner.isEmpty()) inner = QStringLiteral("<br>");
    // the same when it ends in a line break: the last line needs a <br> too
    else if (!p.runs.isEmpty() && !p.runs.last().mark && p.runs.last().text.endsWith(kLineSep))
        inner += QStringLiteral("<br>");
    return inner;
}

QList<Para> parseChapter(const QString &htmlText)
{
    QList<Para> out;
    NodePtr root = html::parse(htmlText);
    NodePtr loose; // loose inline content collects into one paragraph
    auto flushLoose = [&] {
        if (!loose) return;
        Para p = paraFrom(*loose);
        if (!p.runs.isEmpty()) out << p;
        loose.reset();
    };
    std::function<void(Node &)> walkBlocks = [&](Node &parent) {
        for (const auto &c : parent.children) {
            if (c->type == Node::Element && (c->tag == "p" || c->tag == "li" || (c->tag.size() == 2 && c->tag[0] == 'h' && c->tag[1].isDigit()) || c->tag == "pre")) {
                flushLoose();
                out << paraFrom(*c);
            } else if (c->type == Node::Element && html::isBlock(c->tag)) {
                flushLoose();
                // a div with only inline content is a paragraph (what an editor
                // writes when it forgets the <p>); one with blocks is a wrapper
                bool hasBlock = false;
                for (const auto &g : c->children) hasBlock |= g->type == Node::Element && html::isBlock(g->tag);
                if (hasBlock) walkBlocks(*c);
                else out << paraFrom(*c);
            } else {
                // a <br> or whitespace between blocks is nothing
                const bool blank = (c->type == Node::Text && c->text.trimmed().isEmpty()) || c->tag == "br";
                if (!loose && blank) continue;
                if (!loose) loose = html::element("p");
                // copy, not move: walkBlocks iterates parent's list
                auto clone = std::make_shared<Node>(*c);
                loose->appendChild(clone);
            }
        }
        flushLoose();
    };
    walkBlocks(*root);
    return out;
}

QString serializeChapter(const QList<Para> &paras)
{
    QString out;
    for (const Para &p : paras) {
        out += QLatin1String("<p");
        for (const auto &[k, v] : p.attrs) out += ' ' + k + "=\"" + html::escapeAttr(v) + '"';
        out += '>' + paraInnerHtml(p) + QLatin1String("</p>");
    }
    return out;
}

QString chapterPlainText(const QString &htmlText)
{
    QString out;
    for (const Para &p : parseChapter(htmlText)) {
        if (p.hasClass("ghost")) continue;
        QString t = p.text();
        t.replace(kLineSep, '\n');
        out += t + '\n';
    }
    return out;
}

QString escHtml(const QString &s) { return html::escHtml(s); }

QList<Para> cleanPaste(const QString &htmlText)
{
    NodePtr root = html::parse(htmlText);
    for (Node *n : root->descendants())
        if (n->tag == "img" || n->tag == "table" || n->tag == "meta" || n->tag == "link") n->remove();
    // walk the tree into runs, a paragraph break at every block edge and <br>
    QList<QList<Run>> paras{{}};
    std::function<void(const Node &, Fmt)> walk = [&](const Node &n, Fmt f) {
        for (const auto &c : n.children) {
            if (c->type == Node::Text) {
                Run r;
                r.text = c->text;
                r.b = f.b;
                r.i = f.i;
                r.u = f.u;
                r.s = f.s;
                paras.last() << r;
                continue;
            }
            if (c->hasClass("ph-mark")) {
                Run m;
                m.mark = true;
                m.sid = c->attr("data-sid");
                m.text = QStringLiteral("⚑");
                paras.last() << m;
                continue;
            }
            if (c->tag == "br") { paras << QList<Run>{}; continue; }
            const bool block = html::isBlock(c->tag);
            if (block) paras << QList<Run>{};
            walk(*c, styleOf(*c, f));
            if (block) paras << QList<Run>{};
        }
    };
    walk(*root, Fmt{});
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    QList<Para> out;
    for (QList<Run> runs : paras) {
        bool filled = false;
        for (Run &r : runs) {
            if (r.mark) continue;
            r.text.replace(QChar(0xa0), ' ');
            r.text.replace(ws, QStringLiteral(" "));
            filled |= !r.text.trimmed().isEmpty();
        }
        if (!filled) continue;
        // each paragraph is trimmed
        for (Run &r : runs) {
            if (r.mark) continue;
            while (r.text.startsWith(' ')) r.text.remove(0, 1);
            if (!r.text.isEmpty()) break;
        }
        for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
            if (it->mark) continue;
            while (it->text.endsWith(' ')) it->text.chop(1);
            if (!it->text.isEmpty()) break;
        }
        Para p;
        p.runs = runs;
        p.normalize();
        out << p;
    }
    return out;
}

} // namespace neosea
