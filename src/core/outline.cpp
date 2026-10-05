#include "core/outline.h"

#include "core/bookmodel.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>

namespace neosea::outline {

namespace {

bool isBrk(const QList<Para> &p, int i) { return i >= 0 && i < p.size() && p[i].hasClass("scene-break"); }

Para breakPara(const QString &secId)
{
    Para b;
    b.setAttr("class", "scene-break");
    if (!secId.isEmpty()) b.setAttr("data-sec-brk", secId);
    Run r;
    r.text = "***";
    b.runs << r;
    return b;
}

Para ghostPara(const Note &sec)
{
    Para p;
    p.setAttr("class", "ghost");
    p.setAttr("data-sec-id", sec.id);
    Run r;
    r.text = sec.text;
    p.runs << r;
    return p;
}

int indexOfSec(const QList<Para> &paras, const QString &id)
{
    for (int i = 0; i < paras.size(); ++i)
        if (paras[i].attr("data-sec-id") == id) return i;
    return -1;
}

bool hasContent(const QList<Para> &paras)
{
    for (const Para &p : paras)
        if (!p.text().trimmed().isEmpty()) return true;
    return false;
}

bool blankBody(const QList<Para> &paras)
{
    for (const Para &p : paras)
        if (!p.text().trimmed().isEmpty() || p.hasClass("scene-break") || p.hasClass("ghost") || p.hasMark()) return false;
    return true;
}

QStringList marksIn(const QList<Para> &paras)
{
    QStringList out;
    for (const Para &p : paras)
        for (const Run &r : p.runs)
            if (r.mark) out << r.sid;
    return out;
}

// lifts a section out of its chapter: its *** and its lines
std::pair<std::optional<Para>, QList<Para>> lift(QList<Para> &paras, const Segment &seg)
{
    std::optional<Para> brk;
    if (seg.brk >= 0) brk = paras[seg.brk];
    QList<Para> ps;
    for (int i : seg.ps) ps << paras[i];
    QList<int> gone = seg.ps;
    if (seg.brk >= 0) gone << seg.brk;
    std::sort(gone.begin(), gone.end(), std::greater<int>());
    for (int i : gone) paras.removeAt(i);
    // the chapter's first section left: the next one's *** would open the chapter
    if (seg.brk < 0 && !paras.isEmpty() && paras.first().hasClass("scene-break")) paras.removeFirst();
    if (paras.isEmpty()) paras << Para{};
    return {brk, ps};
}

// sets lines down in a chapter, before a section (or at the end)
void setDown(QList<Para> &paras, const QList<Para> &ps, std::optional<Para> brk, const Segment *target)
{
    if (!target && blankBody(paras)) {
        paras = ps;
        if (paras.isEmpty()) paras << Para{};
        return;
    }
    const Para b = brk ? *brk : breakPara({});
    if (target && target->brk >= 0) {
        int at = target->brk;
        paras.insert(at++, b);
        for (const Para &p : ps) paras.insert(at++, p);
        return;
    }
    if (target && !target->ps.isEmpty()) {
        // ahead of the chapter's first section
        int at = target->ps.first();
        for (const Para &p : ps) paras.insert(at++, p);
        paras.insert(at, b);
        return;
    }
    if (paras.isEmpty() || !paras.last().hasClass("scene-break")) paras << b;
    paras << ps;
}

} // namespace

QList<Note> notesOf(const QJsonArray &a)
{
    QList<Note> out;
    for (const auto &v : a) out << Note{v.toObject().value("id").toString(), v.toObject().value("text").toString()};
    return out;
}

QJsonArray toJson(const QList<Note> &notes)
{
    QJsonArray a;
    for (const Note &n : notes) a.append(QJsonObject{{"id", n.id}, {"text", n.text}});
    return a;
}

QString newSectionId()
{
    QString rnd;
    for (int i = 0; i < 3; ++i) rnd += QString::number(QRandomGenerator::global()->bounded(36), 36);
    return "sec-" + QString::number(QDateTime::currentMSecsSinceEpoch(), 36) + rnd;
}

QString letter(int i) { return QString(QChar('A' + (i % 26))); }

QList<Segment> segments(const QList<Para> &paras, const QList<Note> &notes)
{
    QList<Segment> segs;
    Segment cur;
    for (int i = 0; i < paras.size(); ++i) {
        if (paras[i].hasClass("scene-break")) {
            segs << cur;
            cur = Segment{};
            cur.brk = i;
        } else {
            cur.ps << i;
        }
    }
    segs << cur;
    QSet<QString> ids, claimed;
    for (const Note &n : notes) ids.insert(n.id);
    static const QRegularExpression ws("\\s+");
    for (Segment &seg : segs) {
        for (int i : seg.ps) {
            const QString id = paras[i].attr("data-sec-id");
            if (!id.isEmpty() && ids.contains(id) && !claimed.contains(id)) {
                seg.id = id;
                claimed.insert(id);
                break;
            }
        }
        QStringList prose;
        for (int i : seg.ps) {
            if (paras[i].hasClass("ghost")) continue;
            prose << paras[i].text();
            if (seg.first.isEmpty()) seg.first = paras[i].text().replace(ws, " ").trimmed();
        }
        seg.words = countWords(prose.join('\n'));
        for (int i : seg.ps) seg.flag |= paras[i].hasMark();
    }
    return segs;
}

QList<Note> ordered(const QList<Para> &paras, const QList<Note> &notes)
{
    QList<Note> placed, rest;
    QStringList ids;
    for (const Segment &s : segments(paras, notes))
        if (!s.id.isEmpty()) ids << s.id;
    for (const QString &id : ids)
        for (const Note &n : notes)
            if (n.id == id) placed << n;
    for (const Note &n : notes)
        if (!ids.contains(n.id)) rest << n;
    return placed + rest;
}

int segmentStart(const QList<Para> &paras, int i)
{
    for (int q = i; q >= 0; --q) {
        if (paras[q].hasClass("scene-break")) return q;
        if (q == 0) return 0;
    }
    return i;
}

void placeGhost(QList<Para> &paras, const Note &sec, int before)
{
    if (before >= 0 && before < paras.size()) {
        if (paras[before].hasClass("scene-break")) {
            paras.insert(before, breakPara(sec.id));
            paras.insert(before + 1, ghostPara(sec));
        } else {
            // the first section of the chapter: the ghost goes ahead of it
            paras.insert(before, ghostPara(sec));
            paras.insert(before + 1, breakPara(sec.id));
        }
        return;
    }
    if (hasContent(paras) && !(!paras.isEmpty() && paras.last().hasClass("scene-break"))) paras << breakPara(sec.id);
    // an empty chapter's one empty line gives way to the ghost
    if (paras.size() == 1 && paras.first().isBlank() && !paras.first().hasMark() && paras.first().classes().isEmpty()) paras.clear();
    paras << ghostPara(sec);
}

void removeGhost(QList<Para> &paras, int index)
{
    const bool prevBrk = isBrk(paras, index - 1), nextBrk = isBrk(paras, index + 1);
    const bool last = index + 1 >= paras.size();
    if (prevBrk && (last || nextBrk)) {
        paras.removeAt(index);
        paras.removeAt(index - 1);
    } else if (index == 0 && nextBrk) {
        paras.removeAt(1);
        paras.removeAt(0);
    } else {
        paras.removeAt(index);
    }
    if (paras.isEmpty()) paras << Para{};
}

void syncGhosts(QList<Para> &paras, const QList<Note> &notes)
{
    QHash<QString, Note> byId;
    for (const Note &n : notes) byId.insert(n.id, n);
    // 1. a ghost whose note is gone (or emptied) leaves; the rest take their note's words
    for (int i = 0; i < paras.size();) {
        if (!paras[i].hasClass("ghost") || !paras[i].hasAttr("data-sec-id")) { ++i; continue; }
        const QString id = paras[i].attr("data-sec-id");
        if (byId.contains(id) && !byId.value(id).text.isEmpty()) {
            if (paras[i].text() != byId.value(id).text) {
                Run r;
                r.text = byId.value(id).text;
                paras[i].runs = {r};
            }
            ++i;
            continue;
        }
        removeGhost(paras, i);
        i = 0; // the page moved under us: look again from the top
    }
    // 2. a note with no place on the page yet gets one
    for (int i = 0; i < notes.size(); ++i) {
        const Note &sec = notes[i];
        if (sec.text.isEmpty() || indexOfSec(paras, sec.id) >= 0) continue;
        int before = -1;
        for (int j = i + 1; j < notes.size() && before < 0; ++j) {
            const int el = indexOfSec(paras, notes[j].id);
            if (el >= 0) before = segmentStart(paras, el);
        }
        placeGhost(paras, sec, before);
    }
}

// ---------------------------------------------------------------------------

QList<Para> Board::paras(const QString &chId) const
{
    QList<Para> p = parseChapter(m_s.chapterHtml(chId));
    if (p.isEmpty()) p << Para{};
    return p;
}

void Board::setParas(const QString &chId, const QList<Para> &p)
{
    m_s.setChapterHtml(chId, serializeChapter(p));
    m_s.persistChapter(chId);
}

QList<Note> Board::notes(const QString &chId) const
{
    return notesOf(m_s.book().value("sectionNotes").toObject().value(chId).toArray());
}

void Board::setNotes(const QString &chId, const QList<Note> &n)
{
    QJsonObject all = m_s.book().value("sectionNotes").toObject();
    if (n.isEmpty()) all.remove(chId);
    else all.insert(chId, toJson(n));
    m_s.book().insert("sectionNotes", all);
}

QList<Segment> Board::segments(const QString &chId) const { return outline::segments(paras(chId), notes(chId)); }

void Board::setChapterNote(const QString &chId, const QString &text)
{
    QJsonObject n = m_s.book().value("chapterNotes").toObject();
    if (n.value(chId).toString() == text) return;
    n.insert(chId, text);
    m_s.book().insert("chapterNotes", n);
    m_s.saveMeta();
}

QString Board::addSection(const QString &chId, int after, const QString &text)
{
    if (text.isEmpty()) return {};
    Note sec{newSectionId(), text};
    QList<Para> p = paras(chId);
    QList<Note> n = notes(chId);
    const QList<Segment> segs = outline::segments(p, n);
    const int nextIdx = after + 1;
    int before = -1;
    if (nextIdx >= 0 && nextIdx < segs.size()) before = segs[nextIdx].brk >= 0 ? segs[nextIdx].brk : segs[nextIdx].ps.value(0, -1);
    n << sec;
    placeGhost(p, sec, before);
    setNotes(chId, ordered(p, n));
    setParas(chId, p);
    m_s.saveMeta();
    return sec.id;
}

void Board::setSectionNote(const QString &chId, const QString &secId, const QString &text)
{
    QList<Note> n = notes(chId);
    bool changed = false;
    for (Note &x : n)
        if (x.id == secId && x.text != text) {
            x.text = text;
            changed = true;
        }
    if (!changed) return;
    setNotes(chId, n);
    QList<Para> p = paras(chId);
    syncGhosts(p, n);
    setParas(chId, p);
    m_s.saveMeta();
}

QString Board::noteUnwrittenSection(const QString &chId, int segIdx, const QString &text)
{
    if (text.isEmpty()) return {};
    QList<Para> p = paras(chId);
    QList<Note> n = notes(chId);
    const QList<Segment> segs = outline::segments(p, n);
    if (segIdx < 0 || segIdx >= segs.size()) return {};
    int anchor = -1;
    for (int i : segs[segIdx].ps)
        if (!p[i].hasClass("ghost")) { anchor = i; break; }
    if (anchor < 0) return {};
    Note sec{newSectionId(), text};
    p[anchor].setAttr("data-sec-id", sec.id);
    n << sec;
    setNotes(chId, ordered(p, n));
    setParas(chId, p);
    m_s.saveMeta();
    return sec.id;
}

void Board::deleteSectionNote(const QString &chId, const QString &secId)
{
    snap("card removed");
    QList<Note> n = notes(chId);
    n.removeIf([&](const Note &x) { return x.id == secId; });
    setNotes(chId, n);
    QList<Para> p = paras(chId);
    syncGhosts(p, n);
    setParas(chId, p);
    m_s.saveMeta();
}

void Board::repointStickies(const QList<Para> &moved, const QString &toCh)
{
    const QStringList sids = marksIn(moved);
    if (sids.isEmpty()) return;
    QJsonArray st = m_s.stickies();
    bool changed = false;
    for (int i = 0; i < st.size(); ++i) {
        QJsonObject o = st[i].toObject();
        if (sids.contains(o.value("id").toString()) && o.value("chapterId").toString() != toCh) {
            o.insert("chapterId", toCh);
            st[i] = o;
            changed = true;
        }
    }
    if (!changed) return;
    m_s.stickies() = st;
    m_s.saveStickies();
}

void Board::moveSection(const QString &fromCh, int segIdx, const QString &toCh, int before)
{
    QList<Para> from = paras(fromCh);
    QList<Note> fromNotes = notes(fromCh);
    const QList<Segment> fromSegs = outline::segments(from, fromNotes);
    if (segIdx < 0 || segIdx >= fromSegs.size()) return;
    const bool same = fromCh == toCh;
    if (same && (before == segIdx || before == segIdx + 1 || (before < 0 && segIdx == fromSegs.size() - 1))) return;
    snap("card moved");
    const Segment seg = fromSegs[segIdx];
    auto [brk, ps] = lift(from, seg);
    QList<Para> to = same ? from : paras(toCh);
    QList<Note> toNotes = same ? fromNotes : notes(toCh);
    if (same && before > segIdx) before--;
    const QList<Segment> toSegs = outline::segments(to, toNotes);
    const Segment *target = (before >= 0 && before < toSegs.size()) ? &toSegs[before] : nullptr;
    setDown(to, ps, brk, target);
    if (!seg.id.isEmpty() && !same) {
        Note moving;
        for (const Note &x : fromNotes)
            if (x.id == seg.id) moving = x;
        fromNotes.removeIf([&](const Note &x) { return x.id == seg.id; });
        if (!moving.id.isEmpty()) toNotes << moving;
    }
    if (same) {
        setNotes(fromCh, ordered(to, toNotes));
        setParas(fromCh, to);
    } else {
        setNotes(fromCh, ordered(from, fromNotes));
        setNotes(toCh, ordered(to, toNotes));
        setParas(fromCh, from);
        setParas(toCh, to);
        repointStickies(ps, toCh);
    }
    m_s.saveMeta();
}

void Board::moveVirtualNote(const QString &fromCh, const QString &secId, const QString &toCh)
{
    if (fromCh == toCh) return;
    snap("card moved");
    QList<Note> from = notes(fromCh), to = notes(toCh);
    for (const Note &x : from)
        if (x.id == secId) to << x;
    from.removeIf([&](const Note &x) { return x.id == secId; });
    setNotes(fromCh, from);
    setNotes(toCh, to);
    m_s.saveMeta();
}

void Board::moveChapter(const QString &chId, int index)
{
    QStringList order = m_s.order();
    const int from = int(order.indexOf(chId));
    if (from < 0 || index == from || index == from + 1) return;
    snap("chapter reorder");
    order.removeAt(from);
    order.insert(std::clamp(index > from ? index - 1 : index, 0, int(order.size())), chId);
    m_s.setOrder(order);
    m_s.saveMeta();
}

QString Board::sectionToChapter(const QString &chId, int segIdx)
{
    QList<Para> p = paras(chId);
    QList<Note> n = notes(chId);
    const QList<Segment> segs = outline::segments(p, n);
    if (segIdx < 0 || segIdx >= segs.size()) return {};
    snap("card to chapter");
    const Segment seg = segs[segIdx];
    auto [brk, ps] = lift(p, seg);
    QString noteText;
    if (!seg.id.isEmpty()) {
        for (const Note &x : n)
            if (x.id == seg.id) noteText = x.text;
        n.removeIf([&](const Note &x) { return x.id == seg.id; });
    }
    QList<Para> prose;
    for (Para q : ps) {
        if (q.hasClass("ghost")) continue;
        q.removeAttr("data-sec-id");
        prose << q;
    }
    setNotes(chId, n);
    setParas(chId, p);
    const QString newId = m_s.createChapterAt(m_s.order().indexOf(chId) + 1, prose.isEmpty() ? QStringLiteral("<p><br></p>") : serializeChapter(prose));
    if (!noteText.isEmpty()) {
        QJsonObject cn = m_s.book().value("chapterNotes").toObject();
        cn.insert(newId, noteText);
        m_s.book().insert("chapterNotes", cn);
    }
    repointStickies(prose, newId);
    m_s.saveMeta();
    return newId;
}

bool Board::sectionToLoose(const QString &chId, int segIdx)
{
    QList<Note> n = notes(chId);
    const QList<Segment> segs = segments(chId);
    if (segIdx < 0 || segIdx >= segs.size()) return false;
    const Segment seg = segs[segIdx];
    if (seg.words) return false;
    QString text;
    bool found = false;
    for (const Note &x : n)
        if (x.id == seg.id) { text = x.text; found = true; }
    if (!found) return false;
    snap("card to loose");
    n.removeIf([&](const Note &x) { return x.id == seg.id; });
    setNotes(chId, n);
    QJsonArray loose = m_s.book().value("looseCards").toArray();
    loose.append(QJsonObject{{"id", "lc-" + QString::number(QDateTime::currentMSecsSinceEpoch(), 36)}, {"text", text}});
    m_s.book().insert("looseCards", loose);
    QList<Para> p = paras(chId);
    syncGhosts(p, n);
    setParas(chId, p);
    m_s.saveMeta();
    return true;
}

void Board::looseToSection(const QString &looseId, const QString &toCh, int before)
{
    QJsonArray loose = m_s.book().value("looseCards").toArray();
    QString text;
    bool found = false;
    for (int i = 0; i < loose.size(); ++i)
        if (loose[i].toObject().value("id").toString() == looseId) {
            text = loose[i].toObject().value("text").toString();
            loose.removeAt(i);
            found = true;
            break;
        }
    if (!found) return;
    snap("loose card placed");
    m_s.book().insert("looseCards", loose);
    Note sec{newSectionId(), text};
    QList<Para> p = paras(toCh);
    QList<Note> n = notes(toCh);
    n << sec;
    if (!text.isEmpty()) {
        const QList<Segment> segs = outline::segments(p, n);
        int at = -1;
        if (before >= 0 && before < segs.size()) at = segs[before].brk >= 0 ? segs[before].brk : segs[before].ps.value(0, -1);
        placeGhost(p, sec, at);
    }
    setNotes(toCh, ordered(p, n));
    setParas(toCh, p);
    m_s.saveMeta();
}

std::optional<QString> Board::joinChapter(const QString &chId, const QString &intoCh)
{
    const QJsonObject &b = m_s.book();
    if (intoCh.isEmpty() || chId == intoCh || !isStory(chId, b) || !isStory(intoCh, b)) return std::nullopt;
    snap("chapter joined");
    QString text = b.value("chapterNotes").toObject().value(chId).toString().trimmed();
    if (text.isEmpty()) text = b.value("chapterTitles").toObject().value(chId).toString().trimmed();
    Note sec{newSectionId(), text};
    QList<Para> from = paras(chId);
    QList<Para> into = paras(intoCh);
    QList<Note> notesInto = notes(intoCh);
    bool hasLines = false;
    for (const Para &p : from) hasLines |= !p.text().trimmed().isEmpty() || p.hasMark();
    QList<Para> moving = hasLines ? from : QList<Para>{};
    const bool intoBlank = blankBody(into);
    if (hasLines) {
        if (intoBlank) into.clear();
        else if (into.isEmpty() || !into.last().hasClass("scene-break")) into << breakPara(sec.id);
        // a chapter that opened with a *** keeps just the one
        if (!moving.isEmpty() && moving.first().hasClass("scene-break") && !into.isEmpty() && into.last().hasClass("scene-break"))
            moving.removeFirst();
        const int start = int(into.size());
        into << moving;
        // the chapter's opening carries the new section's note; an opening
        // that's only outline gets the note as a ghost of its own
        if (start < into.size() && !into[start].hasClass("scene-break") && !into[start].hasClass("ghost"))
            into[start].setAttr("data-sec-id", sec.id);
        else if (!sec.text.isEmpty() && start < into.size())
            placeGhost(into, sec, start);
        repointStickies(moving, intoCh);
    }
    notesInto << sec;
    notesInto << notes(chId);
    setNotes(chId, {});
    if (!hasLines && !sec.text.isEmpty()) placeGhost(into, sec, -1);
    setNotes(intoCh, ordered(into, notesInto));
    // the words are safe in the other chapter before this one's file goes
    setParas(intoCh, into);
    if (m_s.book().contains("chapterTitles")) {
        QJsonObject titles = m_s.book().value("chapterTitles").toObject();
        titles.remove(chId);
        m_s.book().insert("chapterTitles", titles);
    }
    m_s.deleteChapter(chId);
    return sec.id;
}

QString Board::sectionNoteToChapter(const QString &chId, const QString &secId)
{
    QList<Note> n = notes(chId);
    int from = -1;
    for (int i = 0; i < n.size(); ++i)
        if (n[i].id == secId) from = i;
    if (from < 0) return {};
    snap("outline section to chapter");
    const Note sec = n[from];
    QList<Note> after = n.mid(from + 1);
    n = n.mid(0, from);
    // the section and the ones after it leave together, so the book's order
    // holds; sections already written over keep their prose here, so those stay
    const QList<Para> p = paras(chId);
    bool anyWritten = false;
    for (const Note &x : after) {
        const int i = indexOfSec(p, x.id);
        anyWritten |= i >= 0 && !p[i].hasClass("ghost");
    }
    const bool carry = !after.isEmpty() && !anyWritten;
    if (!carry) n << after;
    setNotes(chId, n);
    QList<Para> here = p;
    syncGhosts(here, n);
    setParas(chId, here);
    const QString newId = m_s.createChapterAt(m_s.order().indexOf(chId) + 1);
    QJsonObject cn = m_s.book().value("chapterNotes").toObject();
    cn.insert(newId, sec.text);
    m_s.book().insert("chapterNotes", cn);
    if (carry) {
        setNotes(newId, after);
        QList<Para> np = paras(newId);
        syncGhosts(np, after);
        setParas(newId, np);
    }
    m_s.saveMeta();
    return newId;
}

QJsonArray Board::loose() const { return m_s.book().value("looseCards").toArray(); }

QString Board::addLoose(const QString &text)
{
    QJsonArray l = loose();
    const QString id = "lc-" + QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
    l.append(QJsonObject{{"id", id}, {"text", text}});
    m_s.book().insert("looseCards", l);
    m_s.saveMeta();
    return id;
}

void Board::setLoose(const QString &id, const QString &text)
{
    QJsonArray l = loose();
    for (int i = 0; i < l.size(); ++i) {
        QJsonObject o = l[i].toObject();
        if (o.value("id").toString() != id || o.value("text").toString() == text) continue;
        o.insert("text", text);
        l[i] = o;
        m_s.book().insert("looseCards", l);
        m_s.saveMeta();
    }
}

void Board::removeLoose(const QString &id)
{
    QJsonArray l = loose();
    for (int i = 0; i < l.size(); ++i)
        if (l[i].toObject().value("id").toString() == id) {
            snap("loose card removed");
            l.removeAt(i);
            m_s.book().insert("looseCards", l);
            m_s.saveMeta();
            return;
        }
}

int Board::firstSectionIndex(const QString &chId) const
{
    const QList<Segment> segs = segments(chId);
    const int i = (!segs.isEmpty() && segs.first().id.isEmpty()) ? 1 : 0;
    return i < segs.size() ? i : -1;
}

} // namespace neosea::outline
