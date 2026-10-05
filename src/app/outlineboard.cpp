#include "app/outlineboard.h"

#include "app/app.h"
#include "app/dialogs.h"
#include "app/editorview.h"
#include "app/theme.h"
#include "core/bookmodel.h"
#include "core/fonts.h"
#include "core/i18n.h"
#include "core/outline.h"

#include <QApplication>
#include <QDrag>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBlock>
#include <QVBoxLayout>

namespace neosea {

namespace {

const char *kCardMime = "application/x-neosea-card";
const QList<double> kZooms{0.55, 0.7, 0.85, 1, 1.15, 1.3, 1.5};

QString quoted(const QString &s) { return QStringLiteral("“") + (s.size() > 220 ? s.left(220).trimmed() + "…" : s) + QStringLiteral("”"); }

// the story entry before this one (pages and parts aren't where sections go)
QString storyBefore(const QJsonObject &book, const QString &chId)
{
    const QStringList order = chapterOrder(book);
    for (int i = int(order.indexOf(chId)) - 1; i >= 0; --i)
        if (isStory(order[i], book)) return order[i];
    return {};
}

int storyEnd(const QJsonObject &book)
{
    const QStringList order = chapterOrder(book);
    int at = int(order.size());
    while (at > 0 && kBackKinds.contains(chapterKind(order[at - 1], book))) at--;
    return at;
}

QString partTitle(BookSession *s, const QString &chId)
{
    const QList<Para> p = parseChapter(s->chapterHtml(chId));
    for (const Para &x : p)
        if (!x.hasClass("scene-break") && !x.text().trimmed().isEmpty() && !typing::isAttribution(x.text().trimmed())) return x.text().trimmed();
    return {};
}

struct Cell {
    enum Kind { Chapter, Section, Part, Add, Fresh } kind = Section;
    QString ch, sec, letter, mark, note, excerpt, partLabel;
    int seg = -1, words = 0, after = -1;
    bool markWord = false, flag = false, virtualNote = false, written = false, first = false, last = false;
    QRectF rect;
    QRectF card() const;
};

} // namespace

// ---------------------------------------------------------------------------
// the cards

class CardView : public QWidget {
public:
    CardView(OutlineBoard *board) : QWidget(board), m_board(board)
    {
        setAcceptDrops(true);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

    double zoom() const { return std::clamp(app()->library().value("cardZoom").toDouble(1), 0.55, 1.5); }

    void build();
    void layoutCells(int width);
    QSize sizeHint() const override { return QSize(600, m_height); }
    void closeEditor(bool save = true);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
    }
    void resizeEvent(QResizeEvent *) override { layoutCells(width()); }
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->mimeData()->hasFormat(kCardMime)) e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *) override
    {
        m_target = {};
        update();
    }
    void dropEvent(QDropEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    App *app() const { return m_board->view()->app(); }
    BookSession *session() const { return app()->session(); }
    int cellAt(QPointF p) const;
    QRectF plusRect(const Cell &c) const;
    void paintCard(QPainter &p, const Cell &c, bool lifted);
    void openCard(int index);
    void newCardAfter(int index);
    void newChapterCard(int at);
    void cardMenu(int index, QPoint global);
    void goToCard(const Cell &c);
    void saveCard(const Cell &c, const QString &text);
    struct Target {
        int cell = -1;
        QString side; // before, after, into
    };
    Target dragTarget(QPointF p, const QJsonObject &src) const;
    void drop(const QJsonObject &src, const Target &t);

    OutlineBoard *m_board;
    QList<Cell> m_cells;
    int m_height = 400;
    int m_hover = -1;
    int m_pressed = -1;
    QPointF m_pressPos;
    Target m_target;
    // writing on a card
    QPlainTextEdit *m_edit = nullptr;
    QWidget *m_tools = nullptr;
    int m_open = -1;
};

namespace {
double g_cz = 1; // the zoom the cells were laid out at
}

QRectF Cell::card() const
{
    const double cz = g_cz;
    double l = 4 * cz, r = 4 * cz;
    if (first) l = 5 + 8 * cz;
    if (last) r = 5 + 8 * cz;
    return rect.adjusted(l, 8 * cz, -r, -8 * cz);
}

void CardView::build()
{
    closeEditor(false);
    m_cells.clear();
    BookSession *s = session();
    if (!s) return;
    const QJsonObject &b = s->book();
    outline::Board board(*s);
    const QString solo = soloStory(b);
    const QJsonObject chapterNotes = b.value("chapterNotes").toObject();
    QSet<QString> flagged;
    for (const QString &chId : s->order()) {
        const QString kind = chapterKind(chId, b);
        if (kind == "part") {
            Cell c;
            c.kind = Cell::Part;
            c.ch = chId;
            const QString title = partTitle(s, chId);
            c.partLabel = chapterName(chId, b) + (title.isEmpty() ? QString() : QStringLiteral(" · ") + title);
            m_cells << c;
            continue;
        }
        if (!kStoryKinds.contains(kind)) continue;
        const QList<outline::Segment> segs = board.segments(chId);
        const QList<outline::Note> notes = board.notes(chId);
        const bool openingPlain = !segs.isEmpty() && segs.first().id.isEmpty();
        Cell ch;
        ch.kind = Cell::Chapter;
        ch.ch = chId;
        if (chId == solo) {
            ch.mark = t("The story");
            ch.markWord = true;
        } else if (kind == "chapter") {
            ch.mark = QString::number(chapterNumber(chId, b));
        } else {
            ch.mark = chapterName(chId, b);
            ch.markWord = true;
        }
        for (const auto &seg : segs) ch.words += seg.words;
        for (const auto &seg : segs) ch.flag |= seg.flag;
        ch.note = chapterNotes.value(chId).toString();
        ch.excerpt = openingPlain && !segs.first().first.isEmpty() ? quoted(segs.first().first) : QString();
        ch.first = true;
        m_cells << ch;
        int letter = 0;
        QSet<QString> onPage;
        for (int i = 0; i < segs.size(); ++i) {
            if (!segs[i].id.isEmpty()) onPage.insert(segs[i].id);
            if (i == 0 && openingPlain) continue;
            Cell c;
            c.kind = Cell::Section;
            c.ch = chId;
            c.seg = i;
            c.sec = segs[i].id;
            c.letter = outline::letter(letter++);
            c.flag = segs[i].flag;
            c.words = segs[i].words;
            c.written = segs[i].words > 0;
            c.excerpt = segs[i].first.isEmpty() ? QString() : quoted(segs[i].first);
            for (const auto &n : notes)
                if (n.id == c.sec) c.note = n.text;
            m_cells << c;
        }
        // notes the page doesn't hold yet
        for (const auto &n : notes) {
            if (onPage.contains(n.id)) continue;
            Cell c;
            c.kind = Cell::Section;
            c.ch = chId;
            c.sec = n.id;
            c.note = n.text;
            c.virtualNote = true;
            c.letter = outline::letter(letter++);
            m_cells << c;
        }
        m_cells.last().last = true;
    }
    Cell add;
    add.kind = Cell::Add;
    m_cells << add;
    layoutCells(width());
    update();
}

void CardView::layoutCells(int width)
{
    const double cz = zoom();
    g_cz = cz;
    const double minW = 176 * cz;
    const int cols = std::max(1, int(width / minW));
    const double colW = double(width) / cols;
    const double cellH = 112 * cz + 16 * cz;
    const double gap = 14 * cz;
    int col = 0;
    double y = 0;
    for (Cell &c : m_cells) {
        if (c.kind == Cell::Part) {
            if (col) {
                col = 0;
                y += cellH + gap;
            }
            c.rect = QRectF(0, y, width, 34);
            y += 34 + gap / 2;
            continue;
        }
        c.rect = QRectF(col * colW, y, colW, cellH);
        if (++col >= cols) {
            col = 0;
            y += cellH + gap;
        }
    }
    m_height = int(y + (col ? cellH : 0) + 40);
    setMinimumHeight(m_height);
    updateGeometry();
}

QRectF CardView::plusRect(const Cell &c) const
{
    return QRectF(c.rect.right() - 11, c.rect.center().y() - 11, 22, 22);
}

int CardView::cellAt(QPointF p) const
{
    for (int i = 0; i < m_cells.size(); ++i)
        if (m_cells[i].rect.contains(p)) return i;
    return -1;
}

void CardView::paintCard(QPainter &p, const Cell &c, bool lifted)
{
    const double cz = g_cz;
    const QRectF r = c.card();
    const bool night = theme().name == "night";
    QPainterPath shape;
    shape.addRoundedRect(r, 3, 3);
    p.setOpacity(lifted ? 0.3 : 1);
    p.fillPath(shape.translated(0, 4), QColor(0, 0, 0, 70));
    const QColor bg = c.kind == Cell::Chapter ? (night ? QColor("#ebe7dd") : QColor("#fbfaf7")) : (night ? QColor("#e2ddd1") : QColor("#f4f1e8"));
    p.fillPath(shape, bg);
    if (m_target.cell >= 0 && m_target.side == "into" && &m_cells[m_target.cell] == &c) {
        p.setPen(QPen(theme().accent, 3));
        p.drawPath(shape);
    }
    const QRectF in = r.adjusted(11 * cz, 7 * cz, -11 * cz, -7 * cz);
    QFont base = font();
    base.setPixelSize(std::max(8, int(13 * cz)));
    const QJsonObject fonts = app()->library().value("fonts").toObject();
    QFont body(bodyFontFamily(fonts.value("body").toString()));
    body.setPixelSize(std::max(8, int(13 * cz)));
    double y = in.top();
    const QColor mark("#7a6538"), soft("#6f6a5e");
    if (c.kind == Cell::Chapter) {
        QFont mf = c.markWord ? base : QFont(dropCapFamily(fonts.value("dropcap").toString("literary")).isEmpty() ? body.family() : dropCapFamily(fonts.value("dropcap").toString("literary")));
        if (c.markWord) {
            mf.setPixelSize(std::max(8, int(10.5 * cz)));
            mf.setCapitalization(QFont::AllUppercase);
            mf.setLetterSpacing(QFont::AbsoluteSpacing, 2);
        } else {
            mf.setPixelSize(std::max(10, int(34 * cz)));
        }
        p.setFont(mf);
        p.setPen(mark);
        const QFontMetricsF fm(mf);
        const double headH = c.markWord ? fm.height() + 6 * cz : fm.ascent() + 2;
        p.drawText(QRectF(in.left(), y, in.width(), headH), Qt::AlignLeft | Qt::AlignBottom, c.mark);
        QFont wf = base;
        wf.setPixelSize(std::max(7, int(10.5 * cz)));
        p.setFont(wf);
        p.setPen(soft);
        double right = in.right();
        if (c.flag) {
            p.setPen(Qt::NoPen);
            p.setBrush(theme().red);
            p.drawEllipse(QPointF(right - 4, y + headH - 8), 3.5, 3.5);
            right -= 12;
            p.setPen(soft);
        }
        if (c.words) p.drawText(QRectF(in.left(), y, right - in.left(), headH), Qt::AlignRight | Qt::AlignBottom, t("{n} words", {{"n", c.words}}));
        y += headH + 2;
        p.setPen(QPen(QColor("#e3b4ae"), 1));
        p.drawLine(QPointF(in.left(), y), QPointF(in.right(), y));
        y += 4 * cz;
    } else {
        QFont lf = base;
        lf.setPixelSize(std::max(7, int(10.5 * cz)));
        lf.setWeight(QFont::DemiBold);
        p.setFont(lf);
        p.setPen(mark);
        const double headH = QFontMetricsF(lf).height();
        p.drawText(QRectF(in.left(), y, in.width(), headH), Qt::AlignLeft | Qt::AlignVCenter, c.kind == Cell::Fresh ? QStringLiteral("+") : c.letter);
        if (c.flag) {
            p.setPen(Qt::NoPen);
            p.setBrush(theme().red);
            p.drawEllipse(QPointF(in.right() - 4, y + headH / 2), 3.5, 3.5);
        }
        y += headH + 3;
    }
    // the note, or the section's first line in quotes, or the prompt
    QString text = c.note;
    QColor color("#1c1c1c");
    bool italic = false;
    if (text.isEmpty() && !c.excerpt.isEmpty()) {
        text = c.excerpt;
        color = QColor("#4a463e");
    } else if (text.isEmpty()) {
        text = c.kind == Cell::Chapter ? t("What happens in this chapter…") : t("What happens in this section…");
        color = QColor("#9a9486");
        italic = true;
    } else if (c.kind == Cell::Section && !c.written) {
        color = QColor("#807a6c");
        italic = true;
    }
    QFont tf = body;
    tf.setItalic(italic);
    p.setFont(tf);
    p.setPen(color);
    const double footH = c.kind == Cell::Section ? QFontMetricsF(base).height() : 0;
    const QRectF textRect(in.left(), y, in.width(), in.bottom() - y - footH);
    if (g_cz >= 0.8 || c.kind != Cell::Chapter) {
        const QFontMetricsF fm(tf);
        const int lines = std::max(1, int(textRect.height() / fm.lineSpacing()));
        QString shown;
        QStringList words = text.split(' ');
        QString line;
        int n = 0;
        for (int i = 0; i < words.size() && n < lines; ++i) {
            const QString next = line.isEmpty() ? words[i] : line + ' ' + words[i];
            if (fm.horizontalAdvance(next) > textRect.width() && !line.isEmpty()) {
                n++;
                if (n == lines) {
                    shown += fm.elidedText(line + ' ' + words.mid(i).join(' '), Qt::ElideRight, textRect.width());
                    line.clear();
                    break;
                }
                shown += line + '\n';
                line = words[i];
            } else {
                line = next;
            }
        }
        if (!line.isEmpty()) shown += line;
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignTop, shown);
    }
    if (c.kind == Cell::Section && g_cz >= 0.8) {
        QFont ff = base;
        ff.setPixelSize(std::max(7, int(10.5 * cz)));
        p.setFont(ff);
        p.setPen(soft);
        p.drawText(QRectF(in.left(), in.bottom() - footH, in.width(), footH), Qt::AlignLeft | Qt::AlignBottom,
                   c.words ? t("{n} words", {{"n", c.words}}) : t("not written yet"));
    }
    p.setOpacity(1);
}

void CardView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double cz = g_cz;
    QColor mat = theme().accent;
    mat.setAlphaF(0.1);
    for (int i = 0; i < m_cells.size(); ++i) {
        const Cell &c = m_cells[i];
        if (c.kind == Cell::Part) {
            QFont f = font();
            f.setPixelSize(11);
            f.setCapitalization(QFont::AllUppercase);
            f.setLetterSpacing(QFont::AbsoluteSpacing, 2.5);
            p.setFont(f);
            p.setPen(theme().accent);
            const QRectF tr = c.rect.adjusted(6, 10, 0, 0);
            const double w = QFontMetricsF(f).horizontalAdvance(c.partLabel.toUpper());
            p.drawText(tr, Qt::AlignLeft | Qt::AlignTop, c.partLabel);
            QColor rule = theme().accent;
            rule.setAlphaF(0.3);
            p.setPen(rule);
            p.drawLine(QPointF(tr.left() + w + 12, tr.top() + 8), QPointF(c.rect.right(), tr.top() + 8));
            continue;
        }
        if (c.kind == Cell::Add) {
            const QRectF r = c.rect.adjusted(5, 8 * cz, -5, -8 * cz);
            QPainterPath path;
            path.addRoundedRect(r, 6, 6);
            QColor dash = m_hover == i ? theme().accent : theme().muted;
            p.setPen(QPen(dash, 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            QFont f = font();
            f.setPixelSize(std::max(9, int(13 * cz)));
            p.setFont(f);
            p.drawText(r, Qt::AlignCenter, t("+ Chapter"));
            continue;
        }
        // the chapter's mat, rounded where it starts and ends
        QRectF m = c.rect;
        if (c.first) m.setLeft(m.left() + 5);
        if (c.last) m.setRight(m.right() - 5);
        QPainterPath mp;
        const double rad = 10;
        if (c.first && c.last) mp.addRoundedRect(m, rad, rad);
        else if (c.first) {
            mp.addRoundedRect(m, rad, rad);
            QPainterPath sq;
            sq.addRect(m.adjusted(m.width() / 2, 0, 0, 0));
            mp = mp.united(sq);
        } else if (c.last) {
            mp.addRoundedRect(m, rad, rad);
            QPainterPath sq;
            sq.addRect(m.adjusted(0, 0, -m.width() / 2, 0));
            mp = mp.united(sq);
        } else {
            mp.addRect(m);
        }
        p.fillPath(mp, mat);
        paintCard(p, c, false);
        if (m_hover == i && m_open < 0 && c.kind != Cell::Fresh) {
            const QRectF pr = plusRect(c);
            p.setPen(QPen(theme().accent, 1));
            p.setBrush(theme().bg);
            p.drawEllipse(pr);
            QFont f = font();
            f.setPixelSize(15);
            p.setFont(f);
            p.drawText(pr, Qt::AlignCenter, "+");
        }
    }
    // where a dragged card would land
    if (m_target.cell >= 0 && m_target.side != "into") {
        const QRectF r = m_cells[m_target.cell].card();
        const double x = m_target.side == "before" ? r.left() - 6 : r.right() + 3;
        p.fillRect(QRectF(x, r.top() + 6, 3, std::max(10.0, r.height() - 12)), theme().accent);
    }
    if (m_target.cell >= 0 && m_target.side == "into") {
        const Cell &c = m_cells[m_target.cell];
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        p.setPen(theme().accent);
        p.drawText(c.card().bottomLeft() + QPointF(8, 16), t("goes to the end of {chapter}", {{"chapter", chapterName(c.ch, session()->book())}}));
    }
}

void CardView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    m_pressPos = e->position();
    m_pressed = cellAt(e->position());
}

void CardView::mouseMoveEvent(QMouseEvent *e)
{
    const int h = cellAt(e->position());
    if (h != m_hover) {
        m_hover = h;
        update();
    }
    if (m_pressed < 0 || !(e->buttons() & Qt::LeftButton)) return;
    if ((e->position() - m_pressPos).manhattanLength() < 6) return;
    const Cell c = m_cells.value(m_pressed);
    m_pressed = -1;
    if (c.kind != Cell::Chapter && c.kind != Cell::Section) return;
    closeEditor();
    QJsonObject src{{"kind", c.kind == Cell::Chapter ? "chapter" : "section"}, {"ch", c.ch}, {"seg", c.seg}, {"sec", c.sec},
                    {"virtual", c.virtualNote}, {"written", c.written}};
    auto *drag = new QDrag(this);
    auto *mime = new QMimeData;
    mime->setData(kCardMime, QJsonDocument(src).toJson(QJsonDocument::Compact));
    drag->setMimeData(mime);
    const QRectF cr = c.card();
    QPixmap pm(cr.size().toSize() * devicePixelRatioF());
    pm.setDevicePixelRatio(devicePixelRatioF());
    pm.fill(Qt::transparent);
    {
        QPainter pp(&pm);
        pp.setRenderHint(QPainter::Antialiasing);
        pp.translate(-cr.topLeft());
        paintCard(pp, c, false);
    }
    drag->setPixmap(pm);
    drag->setHotSpot((m_pressPos - cr.topLeft()).toPoint());
    // the loose cards take a card that has no writing yet
    if (c.kind == Cell::Section && !c.written && !c.sec.isEmpty()) m_board->view()->openSidePane();
    drag->exec(Qt::MoveAction);
    m_target = {};
    update();
}

void CardView::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    const int i = cellAt(e->position());
    const int pressed = m_pressed;
    m_pressed = -1;
    if (i < 0 || i != pressed) return;
    const Cell &c = m_cells[i];
    if (c.kind == Cell::Add) {
        if (BookSession *s = session()) newChapterCard(storyEnd(s->book()));
        return;
    }
    if (c.kind == Cell::Part) return;
    if (plusRect(c).contains(e->position()) && c.kind != Cell::Fresh) {
        newCardAfter(i);
        return;
    }
    openCard(i);
}

void CardView::contextMenuEvent(QContextMenuEvent *e)
{
    const int i = cellAt(e->pos());
    if (i < 0) return;
    cardMenu(i, e->globalPos());
}


void CardView::openCard(int index)
{
    closeEditor();
    if (index < 0 || index >= m_cells.size()) return;
    m_open = index;
    const Cell &c = m_cells[index];
    const QRectF r = c.card();
    const bool left = r.left() + r.width() * 2 > width() - 4;
    QRectF er(left ? r.right() - r.width() * 2 : r.left(), r.top(), r.width() * 2 - 8, std::max(180 * g_cz, r.height()));
    // on the room, not the board: the open card is taller than its cell
    er.translate(pos());
    m_edit = new QPlainTextEdit(parentWidget());
    m_edit->setFrameShape(QFrame::NoFrame);
    m_edit->setPlainText(c.note);
    const bool hasExcerpt = !c.excerpt.isEmpty() && c.kind != Cell::Fresh;
    m_edit->setPlaceholderText(hasExcerpt ? t("Write a note…") : c.kind == Cell::Chapter ? t("What happens in this chapter…") : t("What happens in this section…"));
    m_edit->setStyleSheet(QStringLiteral("QPlainTextEdit { background: #f4f1e8; color: #1c1c1c; border-radius: 3px; padding: %1px; }").arg(int(10 * g_cz)));
    QFont f(bodyFontFamily(app()->library().value("fonts").toObject().value("body").toString()));
    f.setPixelSize(int(15 * g_cz));
    m_edit->setFont(f);
    m_tools = new QWidget(parentWidget());
    m_tools->setStyleSheet("QWidget { background: #f4f1e8; } QPushButton { border: 1px solid #cfc8b8; border-radius: 12px; padding: 3px 10px; color: #5b5648; font-size: 11px; } "
                           "QPushButton:hover { border-color: #c9a86a; color: #1c1c1c; } QLabel { color: #6f6a5e; font-size: 11px; }");
    auto *rows = new QVBoxLayout(m_tools);
    rows->setContentsMargins(8, 4, 8, 6);
    rows->setSpacing(4);
    auto *row = new QHBoxLayout;
    rows->addLayout(row);
    if (c.kind != Cell::Fresh && !c.virtualNote) {
        auto *go = new QPushButton(t("Go to the page"), m_tools);
        connect(go, &QPushButton::clicked, this, [this, c] {
            closeEditor();
            goToCard(c);
        });
        row->addWidget(go);
    }
    auto *more = new QPushButton(t("New card"), m_tools);
    connect(more, &QPushButton::clicked, this, [this] { if (m_open >= 0) newCardAfter(m_open); });
    row->addWidget(more);
    auto *tip = new QLabel(t("Enter: done · Tab: next card · {key}: new card", {{"key", "Alt+Enter"}}), m_tools);
    tip->setWordWrap(true);
    row->addStretch();
    rows->addWidget(tip);
    const int toolsH = 54;
    QRectF textRect = er.adjusted(0, 0, 0, -toolsH);
    m_edit->setGeometry(textRect.toRect());
    m_tools->setGeometry(QRectF(er.left(), textRect.bottom(), er.width(), toolsH).toRect());
    m_edit->show();
    m_tools->show();
    m_edit->raise();
    m_tools->raise();
    m_edit->installEventFilter(this);
    m_edit->setFocus();
    m_edit->moveCursor(QTextCursor::End);
    update();
}

void CardView::saveCard(const Cell &c, const QString &val)
{
    BookSession *s = session();
    if (!s) return;
    if (val == c.note && c.kind != Cell::Fresh) return;
    m_board->view()->syncAll();
    outline::Board b(*s);
    if (c.kind == Cell::Chapter) b.setChapterNote(c.ch, val);
    else if (c.kind == Cell::Fresh) b.addSection(c.ch, c.after, val);
    else if (!c.sec.isEmpty()) b.setSectionNote(c.ch, c.sec, val);
    else b.noteUnwrittenSection(c.ch, c.seg, val);
    m_board->changed();
}

void CardView::closeEditor(bool save)
{
    if (!m_edit) return;
    // saving redraws the board: let go of the editor first
    QPlainTextEdit *e = m_edit;
    QWidget *tools = m_tools;
    const QString text = e->toPlainText().simplified();
    const Cell card = m_cells.value(m_open);
    const bool valid = m_open >= 0 && m_open < m_cells.size();
    m_edit = nullptr;
    m_tools = nullptr;
    m_open = -1;
    e->removeEventFilter(this);
    e->hide();
    tools->hide();
    e->deleteLater();
    tools->deleteLater();
    if (save && valid) saveCard(card, text);
    update();
}

bool CardView::eventFilter(QObject *o, QEvent *e)
{
    if (o != m_edit) return false;
    if (e->type() == QEvent::FocusOut) {
        // the window losing focus isn't the writer leaving the card
        QTimer::singleShot(0, this, [this] {
            if (!m_edit || !isActiveWindow()) return;
            if (m_edit->hasFocus() || (m_tools && m_tools->isAncestorOf(QApplication::focusWidget()))) return;
            closeEditor();
            m_board->render();
        });
        return false;
    }
    if (e->type() != QEvent::KeyPress) return false;
    auto *k = static_cast<QKeyEvent *>(e);
    const bool alt = k->modifiers() & Qt::AltModifier;
    if (k->key() == Qt::Key_Escape) {
        closeEditor();
        m_board->render();
        return true;
    }
    if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && !alt && !(k->modifiers() & Qt::ShiftModifier)) {
        closeEditor();
        m_board->render();
        return true;
    }
    if ((k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) && alt) {
        const int at = m_open;
        const Cell c = m_cells.value(at);
        if (m_edit->toPlainText().trimmed().isEmpty() && c.kind == Cell::Fresh) {
            // an empty new card and ⌥Enter again: a new chapter instead
            closeEditor(false);
            newChapterCard(int(session()->order().indexOf(c.ch)) + 1);
            return true;
        }
        newCardAfter(at);
        return true;
    }
    if (k->key() == Qt::Key_Tab || k->key() == Qt::Key_Backtab) {
        const int at = m_open;
        const bool back = k->key() == Qt::Key_Backtab || (k->modifiers() & Qt::ShiftModifier);
        // the next card's identity, so it can be found after the board redraws
        int nextIdx = -1;
        for (int i = back ? at - 1 : at + 1; i >= 0 && i < m_cells.size(); i += back ? -1 : 1)
            if (m_cells[i].kind == Cell::Chapter || m_cells[i].kind == Cell::Section) { nextIdx = i; break; }
        const QString nch = nextIdx >= 0 ? m_cells[nextIdx].ch : QString();
        const QString nsec = nextIdx >= 0 ? m_cells[nextIdx].sec : QString();
        const int kind = nextIdx >= 0 ? int(m_cells[nextIdx].kind) : -1;
        const int nseg = nextIdx >= 0 ? m_cells[nextIdx].seg : -1;
        closeEditor();
        m_board->render();
        if (nextIdx < 0) return true;
        for (int i = 0; i < m_cells.size(); ++i) {
            const Cell &c = m_cells[i];
            if (c.ch != nch || int(c.kind) != kind) continue;
            if (c.kind == Cell::Chapter || (!nsec.isEmpty() && c.sec == nsec) || (nsec.isEmpty() && c.seg == nseg)) {
                openCard(i);
                break;
            }
        }
        return true;
    }
    if (k->key() == Qt::Key_Backspace && m_edit->toPlainText().isEmpty()) {
        const Cell c = m_cells.value(m_open);
        if (c.kind == Cell::Fresh) {
            closeEditor(false);
            m_board->render();
            return true;
        }
        if (c.kind == Cell::Section && !c.sec.isEmpty() && !c.written) {
            closeEditor(false);
            m_board->view()->syncAll();
            outline::Board(*session()).deleteSectionNote(c.ch, c.sec);
            m_board->changed();
            return true;
        }
    }
    return false;
}

void CardView::newCardAfter(int index)
{
    closeEditor();
    // the board may have redrawn: find the card again by its identity
    if (index < 0 || index >= m_cells.size()) return;
    const Cell src = m_cells[index];
    BookSession *s = session();
    if (!s) return;
    outline::Board b(*s);
    const QList<outline::Segment> segs = b.segments(src.ch);
    int after;
    if (src.kind == Cell::Chapter) after = (!segs.isEmpty() && segs.first().id.isEmpty()) ? 0 : -1;
    else if (src.virtualNote || src.seg < 0) after = int(segs.size()) - 1;
    else after = src.seg;
    Cell fresh;
    fresh.kind = Cell::Fresh;
    fresh.ch = src.ch;
    fresh.after = after;
    fresh.letter = "+";
    // it joins the chapter's mat
    int at = index + 1;
    fresh.last = m_cells[index].last;
    m_cells[index].last = false;
    m_cells.insert(at, fresh);
    layoutCells(width());
    update();
    openCard(at);
}

void CardView::newChapterCard(int at)
{
    closeEditor();
    BookSession *s = session();
    if (!s) return;
    m_board->view()->syncAll();
    s->snapshot("card new chapter");
    const QString id = s->createChapterAt(at);
    m_board->changed();
    for (int i = 0; i < m_cells.size(); ++i)
        if (m_cells[i].kind == Cell::Chapter && m_cells[i].ch == id) {
            openCard(i);
            break;
        }
}

void CardView::goToCard(const Cell &c)
{
    EditorView *v = m_board->view();
    v->showTab("manuscript");
    if (c.kind == Cell::Chapter || c.seg < 0) {
        v->focusChapter(c.ch);
        return;
    }
    ChapterEdit *e = v->editorFor(c.ch);
    if (!e) return;
    const QList<outline::Segment> segs = outline::Board(*session()).segments(c.ch);
    if (c.seg >= segs.size() || segs[c.seg].ps.isEmpty()) {
        v->focusChapter(c.ch);
        return;
    }
    const int block = segs[c.seg].ps.first();
    QTextBlock b = e->document()->findBlockByNumber(block);
    QTextCursor cur(b);
    // a ghost is selected, ready to be written over
    if (b.blockFormat().property(QTextFormat::UserProperty + 1).toString().split(' ').contains("ghost")) cur.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    e->setFocus();
    e->setTextCursor(cur);
    e->ensureCursorVisible();
}

void CardView::cardMenu(int index, QPoint global)
{
    const Cell c = m_cells.value(index);
    BookSession *s = session();
    if (!s) return;
    if (c.kind == Cell::Chapter || c.kind == Cell::Part) {
        m_board->view()->chapterMenu(c.ch, global);
        m_board->render();
        return;
    }
    if (c.kind != Cell::Section) return;
    QMenu m(this);
    QAction *go = m.addAction(t("Go to the page"));
    go->setEnabled(!c.virtualNote);
    QAction *chapter = m.addAction(t("Make it a chapter"));
    chapter->setEnabled(!c.virtualNote);
    QAction *loose = m.addAction(t("Move to loose cards"));
    loose->setEnabled(!c.written && !c.sec.isEmpty());
    m.addSeparator();
    QAction *del = m.addAction(t("Delete the note"));
    del->setEnabled(!c.sec.isEmpty());
    QAction *picked = m.exec(global);
    if (!picked) return;
    m_board->view()->syncAll();
    outline::Board b(*s);
    if (picked == go) goToCard(c);
    else if (picked == chapter) {
        b.sectionToChapter(c.ch, c.seg);
        m_board->changed();
    } else if (picked == loose) {
        if (!b.sectionToLoose(c.ch, c.seg)) emit app()->toast(t("That section has writing in it. Drag it to another chapter instead."));
        m_board->changed();
    } else if (picked == del) {
        b.deleteSectionNote(c.ch, c.sec);
        if (c.written) emit app()->toast(t("The note is gone; the writing stays on the page"));
        m_board->changed();
    }
}

CardView::Target CardView::dragTarget(QPointF pos, const QJsonObject &src) const
{
    Target out;
    double best = 1e18;
    for (int i = 0; i < m_cells.size(); ++i) {
        const Cell &c = m_cells[i];
        if (c.kind != Cell::Chapter && c.kind != Cell::Section) continue;
        const QRectF r = c.card();
        const double dy = pos.y() < r.top() ? r.top() - pos.y() : pos.y() > r.bottom() ? pos.y() - r.bottom() : 0;
        const double dx = pos.x() < r.left() ? r.left() - pos.x() : pos.x() > r.right() ? pos.x() - r.right() : 0;
        const double d = dy * 3 + dx;
        if (d < best) {
            best = d;
            out.cell = i;
        }
    }
    if (out.cell < 0) return out;
    const Cell &c = m_cells[out.cell];
    const QRectF r = c.card();
    const QString kind = src.value("kind").toString();
    // over the middle of a chapter card: into that chapter
    const bool middle = pos.x() > r.left() + r.width() * 0.25 && pos.x() < r.right() - r.width() * 0.25 && pos.y() >= r.top() && pos.y() <= r.bottom();
    if (middle && c.kind == Cell::Chapter && (kind == "section" || kind == "loose" || (kind == "chapter" && c.ch != src.value("ch").toString()))) {
        out.side = "into";
        return out;
    }
    out.side = pos.x() < r.center().x() ? "before" : "after";
    // a chapter goes in only between chapters
    if (kind == "chapter" && (c.kind != Cell::Chapter || out.side == "after")) {
        int last = out.cell;
        for (int i = 0; i < m_cells.size(); ++i)
            if (m_cells[i].ch == c.ch && (m_cells[i].kind == Cell::Chapter || m_cells[i].kind == Cell::Section)) last = i;
        out.cell = last;
        out.side = "after";
    }
    return out;
}

void CardView::dragMoveEvent(QDragMoveEvent *e)
{
    if (!e->mimeData()->hasFormat(kCardMime)) return;
    const QJsonObject src = QJsonDocument::fromJson(e->mimeData()->data(kCardMime)).object();
    m_target = dragTarget(e->position(), src);
    update();
    e->acceptProposedAction();
    // near the top or bottom: the board scrolls
    if (auto *sa = qobject_cast<QScrollArea *>(parentWidget()->parentWidget())) {
        const QPoint vp = mapTo(sa->viewport(), e->position().toPoint());
        if (vp.y() < 50) sa->verticalScrollBar()->setValue(sa->verticalScrollBar()->value() - 14);
        else if (vp.y() > sa->viewport()->height() - 50) sa->verticalScrollBar()->setValue(sa->verticalScrollBar()->value() + 14);
    }
}

void CardView::dropEvent(QDropEvent *e)
{
    const QJsonObject src = QJsonDocument::fromJson(e->mimeData()->data(kCardMime)).object();
    const Target target = dragTarget(e->position(), src);
    m_target = {};
    e->acceptProposedAction();
    QTimer::singleShot(0, this, [this, src, target] { drop(src, target); });
}

void CardView::drop(const QJsonObject &src, const Target &target)
{
    BookSession *s = session();
    if (!s || target.cell < 0) return;
    m_board->view()->syncAll();
    outline::Board b(*s);
    const Cell tc = m_cells[target.cell];
    const QString kind = src.value("kind").toString();
    const QString ch = src.value("ch").toString();
    if (target.side == "into") {
        if (kind == "chapter") {
            if (!b.joinChapter(ch, tc.ch)) emit app()->toast(t("Only chapters can become sections"));
        } else if (kind == "loose") {
            b.looseToSection(src.value("loose").toString(), tc.ch, -1);
        } else if (src.value("virtual").toBool()) {
            b.moveVirtualNote(ch, src.value("sec").toString(), tc.ch);
        } else {
            b.moveSection(ch, src.value("seg").toInt(), tc.ch, -1);
        }
        m_board->changed();
        return;
    }
    if (kind == "chapter") {
        const int at = int(s->order().indexOf(tc.ch)) + (target.side == "after" ? 1 : 0);
        b.moveChapter(ch, at);
        m_board->changed();
        return;
    }
    // a section (or a loose card): which chapter, and before which section
    QString toCh;
    int before = -1;
    if (tc.kind == Cell::Chapter) {
        if (target.side == "after") {
            toCh = tc.ch;
            before = b.firstSectionIndex(tc.ch);
        } else {
            const QString prev = storyBefore(s->book(), tc.ch);
            if (!prev.isEmpty()) toCh = prev;
            else {
                toCh = tc.ch;
                before = b.firstSectionIndex(tc.ch);
            }
        }
    } else {
        toCh = tc.ch;
        const int idx = tc.seg;
        if (target.side == "before") before = idx >= 0 ? idx : -1;
        else {
            const int n = int(b.segments(tc.ch).size());
            before = idx >= 0 && idx + 1 < n ? idx + 1 : -1;
        }
    }
    if (kind == "loose") b.looseToSection(src.value("loose").toString(), toCh, before);
    else if (src.value("virtual").toBool()) b.moveVirtualNote(ch, src.value("sec").toString(), toCh);
    else b.moveSection(ch, src.value("seg").toInt(), toCh, before);
    m_board->changed();
}

// ---------------------------------------------------------------------------

OutlineBoard::OutlineBoard(EditorView *view) : QWidget(view), m_view(view)
{
    setFocusPolicy(Qt::ClickFocus);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    m_scroll = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *room = new QWidget;
    room->setObjectName("room");
    auto *col = new QVBoxLayout(room);
    col->setContentsMargins(36, 26, 36, 90);
    col->setSpacing(18);
    m_switch = new QWidget(room);
    auto *sw = new QHBoxLayout(m_switch);
    sw->setContentsMargins(0, 0, 0, 0);
    sw->addStretch();
    m_listBtn = new QPushButton(t("List"), m_switch);
    m_cardsBtn = new QPushButton(t("Cards"), m_switch);
    for (QPushButton *b : {m_listBtn, m_cardsBtn}) {
        b->setCheckable(true);
        b->setStyleSheet(QStringLiteral("QPushButton { color: %1; border: none; border-bottom: 2px solid transparent; padding: 4px 10px; font-size: 12px; } "
                                        "QPushButton:checked { color: %2; border-bottom-color: %3; }")
                             .arg(theme().muted.name(), theme().text.name(), theme().accent.name()));
        sw->addWidget(b);
    }
    sw->addStretch();
    connect(m_listBtn, &QPushButton::clicked, this, [this] { setMode("list"); });
    connect(m_cardsBtn, &QPushButton::clicked, this, [this] { setMode("cards"); });
    col->addWidget(m_switch);
    m_cards = new CardView(this);
    col->addWidget(m_cards);
    m_list = new QWidget(room);
    m_lines = new QVBoxLayout(m_list);
    m_lines->setContentsMargins(120, 0, 120, 0);
    m_lines->setSpacing(4);
    col->addWidget(m_list);
    m_hint = new QLabel(room);
    m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet("color: #7d7a72; font-size: 12.5px; font-style: italic;");
    col->addWidget(m_hint);
    col->addStretch();
    m_scroll->setWidget(room);
    lay->addWidget(m_scroll);
    m_cards->setParent(room);
}

bool OutlineBoard::showingCards() const
{
    App *app = m_view->app();
    return app->session() && (isScript(app->session()->book()) || app->library().value("outlineView").toString("cards") == "cards");
}

void OutlineBoard::setMode(const QString &mode)
{
    m_cards->closeEditor();
    m_view->app()->library().insert("outlineView", mode);
    m_view->app()->writeLibrary();
    render();
}

void OutlineBoard::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
}

void OutlineBoard::keyPressEvent(QKeyEvent *e)
{
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    if (ctrl && e->key() == Qt::Key_Z) {
        m_view->structuralUndo();
        render();
        return;
    }
    QWidget::keyPressEvent(e);
}

void OutlineBoard::render()
{
    BookSession *s = m_view->app()->session();
    if (!s) return;
    // an outline needs somewhere to start
    bool anyStory = false;
    for (const QString &c : s->order()) anyStory |= isStory(c, s->book());
    if (!anyStory) {
        s->createChapterAt(storyEnd(s->book()));
        m_view->rebuildChapters();
    }
    const bool cards = showingCards();
    m_listBtn->setChecked(!cards);
    m_cardsBtn->setChecked(cards);
    m_switch->setVisible(!isScript(s->book()));
    m_cards->setVisible(cards);
    m_list->setVisible(!cards);
    if (cards) {
        m_cards->build();
        m_hint->setText(t("Click a card to write on it · drag it to move it, writing and all · right-click for more · + adds a card"));
    } else {
        renderList();
        m_hint->setText(t("Enter — new chapter · Tab — make it a section, or a new section below one · ⇧Tab — make it a chapter again · Backspace on an empty line removes it"));
    }
}

void OutlineBoard::changed()
{
    m_view->rebuildChapters();
    m_view->scheduleCounters();
    render();
    m_view->refreshSidePane(); // a loose card may have found its place
}

void OutlineBoard::stepZoom(int dir)
{
    QJsonObject &lib = m_view->app()->library();
    const double now = std::clamp(lib.value("cardZoom").toDouble(1), 0.55, 1.5);
    double next = now;
    if (dir == 0) next = 1;
    else if (dir > 0) {
        for (double z : kZooms)
            if (z > now + 0.001) { next = z; break; }
    } else {
        for (auto it = kZooms.rbegin(); it != kZooms.rend(); ++it)
            if (*it < now - 0.001) { next = *it; break; }
    }
    if (next == now) return;
    lib.insert("cardZoom", next);
    m_view->app()->writeLibrary();
    render();
}

// ---------------------------------------------------------------------------
// the List

namespace {

class LineEdit : public QLineEdit {
public:
    std::function<bool(QKeyEvent *)> keys;
    using QLineEdit::QLineEdit;

protected:
    bool event(QEvent *e) override
    {
        // Tab belongs to the outline here, not to the focus chain
        if (e->type() == QEvent::KeyPress && keys && keys(static_cast<QKeyEvent *>(e))) return true;
        return QLineEdit::event(e);
    }
};

} // namespace

void OutlineBoard::renderList()
{
    while (QLayoutItem *item = m_lines->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    BookSession *s = m_view->app()->session();
    if (!s) return;
    const QJsonObject &b = s->book();
    const QJsonObject chapterNotes = b.value("chapterNotes").toObject();
    auto lineStyle = QStringLiteral("QLineEdit { background: transparent; color: %1; border: none; border-bottom: 1px dashed transparent; font-size: 15px; } "
                                    "QLineEdit:focus { border-bottom-color: #4a4742; }")
                         .arg(theme().text.name());
    auto focusLine = [this](const QString &key) {
        QTimer::singleShot(0, this, [this, key] {
            for (QLineEdit *e : m_list->findChildren<QLineEdit *>())
                if (e->property("key").toString() == key) {
                    e->setFocus();
                    e->end(false);
                }
        });
    };
    for (const QString &chId : s->order()) {
        const QString kind = chapterKind(chId, b);
        if (kind == "part") {
            auto *row = new QLabel(chapterMark(chId, b) + "    " + (chapterName(chId, b) + (partTitle(s, chId).isEmpty() ? QString() : ": " + partTitle(s, chId))).toUpper(), m_list);
            row->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; letter-spacing: 3px; margin-top: 22px;").arg(theme().chapterHead.name()));
            m_lines->addWidget(row);
            continue;
        }
        if (!kStoryKinds.contains(kind)) continue;
        auto addLine = [&](bool section, const QString &label, const QString &text, const QString &secId, int index) {
            auto *row = new QWidget(m_list);
            auto *h = new QHBoxLayout(row);
            h->setContentsMargins(section ? 40 : 0, section ? 2 : 14, 0, 2);
            auto *num = new QLabel(label, row);
            num->setMinimumWidth(24);
            num->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            num->setStyleSheet(QStringLiteral("color: %1; %2").arg(section ? "#8d8778" : theme().chapterHead.name(), section ? "" : "font-weight: bold;"));
            auto *edit = new LineEdit(text, row);
            edit->setFrame(false);
            edit->setStyleSheet(lineStyle);
            edit->setPlaceholderText(section ? t("What happens in this section…") : t("What happens in this chapter…"));
            edit->setProperty("key", section ? secId : chId);
            h->addWidget(num);
            h->addSpacing(12);
            h->addWidget(edit, 1);
            m_lines->addWidget(row);
            auto save = [this, chId, secId, section, edit] {
                BookSession *s = m_view->app()->session();
                if (!s) return;
                outline::Board bd(*s);
                if (section) bd.setSectionNote(chId, secId, edit->text().trimmed());
                else bd.setChapterNote(chId, edit->text().trimmed());
            };
            connect(edit, &QLineEdit::editingFinished, this, [this, save, section] {
                save();
                if (section) m_view->rebuildChapters();
            });
            edit->keys = [this, s, chId, secId, section, index, edit, save, focusLine](QKeyEvent *k) {
                outline::Board bd(*s);
                const bool shift = k->modifiers() & Qt::ShiftModifier;
                if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
                    save();
                    // Enter: always a new chapter; at the very start of a line with words, before it
                    const bool above = !section && edit->cursorPosition() == 0 && !edit->text().trimmed().isEmpty();
                    m_view->syncAll();
                    s->snapshot("outline new chapter");
                    const QString newId = s->createChapterAt(s->order().indexOf(chId) + (above ? 0 : 1));
                    changed();
                    focusLine(newId);
                    return true;
                }
                if (k->key() == Qt::Key_Up || k->key() == Qt::Key_Down) {
                    QList<QLineEdit *> ordered;
                    for (int i = 0; i < m_lines->count(); ++i)
                        if (QWidget *w = m_lines->itemAt(i)->widget())
                            if (auto *e = w->findChild<QLineEdit *>()) ordered << e;
                    const int at = int(ordered.indexOf(edit)) + (k->key() == Qt::Key_Down ? 1 : -1);
                    if (at >= 0 && at < ordered.size()) {
                        ordered[at]->setFocus();
                        ordered[at]->end(false);
                    }
                    return true;
                }
                if (k->key() == Qt::Key_Tab && !shift) {
                    save();
                    m_view->syncAll();
                    if (section) {
                        // a new section below this one
                        s->snapshot("outline new section");
                        QList<outline::Note> n = bd.notes(chId);
                        const outline::Note sec{outline::newSectionId(), QString()};
                        n.insert(index + 1, sec);
                        bd.setNotes(chId, n);
                        s->saveMeta();
                        changed();
                        focusLine(sec.id);
                        return true;
                    }
                    const QString prev = storyBefore(s->book(), chId);
                    if (prev.isEmpty()) {
                        emit m_view->app()->toast(t("The first line has to be a chapter"));
                        return true;
                    }
                    auto sec = bd.joinChapter(chId, prev);
                    changed();
                    focusLine(sec ? *sec : prev);
                    return true;
                }
                if (k->key() == Qt::Key_Backtab || (k->key() == Qt::Key_Tab && shift)) {
                    if (!section) return true;
                    save();
                    m_view->syncAll();
                    const QString newId = bd.sectionNoteToChapter(chId, secId);
                    changed();
                    focusLine(newId);
                    return true;
                }
                if (k->key() == Qt::Key_Backspace && edit->text().trimmed().isEmpty()) {
                    m_view->syncAll();
                    if (section) {
                        const QList<outline::Note> n = bd.notes(chId);
                        const QString focus = index > 0 ? n.value(index - 1).id : chId;
                        bd.deleteSectionNote(chId, secId);
                        changed();
                        focusLine(focus);
                        return true;
                    }
                    int stories = 0;
                    for (const QString &c : s->order()) stories += isStory(c, s->book());
                    if (stories > 1 && countWords(chapterPlainText(s->chapterHtml(chId))) == 0) {
                        s->snapshot("outline chapter removed");
                        QString prev = storyBefore(s->book(), chId);
                        s->deleteChapter(chId);
                        if (prev.isEmpty())
                            for (const QString &c : s->order())
                                if (isStory(c, s->book())) { prev = c; break; }
                        changed();
                        focusLine(prev);
                        return true;
                    }
                    return true;
                }
                if ((k->modifiers() & Qt::ControlModifier) && k->key() == Qt::Key_Z && !edit->isUndoAvailable()) {
                    m_view->structuralUndo();
                    render();
                    return true;
                }
                return false;
            };
        };
        addLine(false, chapterMark(chId, b), chapterNotes.value(chId).toString(), {}, 0);
        const QList<outline::Note> notes = outline::Board(*s).notes(chId);
        for (int j = 0; j < notes.size(); ++j) addLine(true, outline::letter(j), notes[j].text, notes[j].id, j);
    }
}

// ---------------------------------------------------------------------------
// loose cards: ideas with no home yet, in the right-hand pane

namespace {

class LooseList : public QWidget {
public:
    LooseList(EditorView *view, QWidget *parent) : QWidget(parent), m_view(view)
    {
        setAcceptDrops(true);
        auto *col = new QVBoxLayout(this);
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(12);
        BookSession *s = view->app()->session();
        if (!s) return;
        const QJsonArray loose = s->book().value("looseCards").toArray();
        for (const auto &v : loose) {
            const QJsonObject o = v.toObject();
            auto *card = new QPlainTextEdit(o.value("text").toString(), this);
            card->setPlaceholderText(t("Write a note…"));
            card->setStyleSheet("QPlainTextEdit { background: #f4f1e8; color: #1c1c1c; border-radius: 3px; padding: 6px; }");
            card->setFixedHeight(84);
            card->setProperty("loose", o.value("id").toString());
            card->installEventFilter(this);
            card->viewport()->installEventFilter(this);
            const QString id = o.value("id").toString();
            connect(card, &QPlainTextEdit::textChanged, this, [this, card, id] {
                if (BookSession *s = m_view->app()->session()) outline::Board(*s).setLoose(id, card->toPlainText().simplified());
            });
            col->addWidget(card);
            if (view->property("focusLoose").toString() == id) {
                view->setProperty("focusLoose", QVariant());
                QTimer::singleShot(0, card, [card] { card->setFocus(); });
            }
        }
        auto *add = new QPushButton(t("+ Loose card"), this);
        add->setStyleSheet(QStringLiteral("QPushButton { height: 40px; border: 1px dashed #4a4a4a; border-radius: 4px; color: %1; font-size: 12px; } "
                                          "QPushButton:hover { color: %2; border-color: %2; }")
                               .arg(theme().muted.name(), theme().accent.name()));
        connect(add, &QPushButton::clicked, this, [this] {
            if (BookSession *s = m_view->app()->session()) {
                // the new card takes the caret once the pane is drawn again
                m_view->setProperty("focusLoose", outline::Board(*s).addLoose(QString()));
                m_view->refreshSidePane();
            }
        });
        col->addWidget(add);
        auto *tip = new QLabel(t("Ideas without a home yet. Drag one onto the board when it finds its place."), this);
        tip->setWordWrap(true);
        tip->setStyleSheet(QStringLiteral("color: %1; font-size: 12px; font-style: italic;").arg(theme().muted.name()));
        col->addWidget(tip);
        col->addStretch();
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        auto *card = qobject_cast<QPlainTextEdit *>(o);
        if (!card) card = qobject_cast<QPlainTextEdit *>(o->parent());
        if (!card) return false;
        if (e->type() == QEvent::MouseButtonPress) m_press = static_cast<QMouseEvent *>(e)->position().toPoint();
        if (e->type() == QEvent::MouseMove && (static_cast<QMouseEvent *>(e)->buttons() & Qt::LeftButton)
            && (static_cast<QMouseEvent *>(e)->position().toPoint() - m_press).manhattanLength() > 12 && !card->textCursor().hasSelection()) {
            auto *drag = new QDrag(card);
            auto *mime = new QMimeData;
            mime->setData(kCardMime, QJsonDocument(QJsonObject{{"kind", "loose"}, {"loose", card->property("loose").toString()}}).toJson(QJsonDocument::Compact));
            drag->setMimeData(mime);
            drag->setPixmap(card->grab());
            drag->exec(Qt::MoveAction);
            return true;
        }
        if (e->type() == QEvent::KeyPress) {
            auto *k = static_cast<QKeyEvent *>(e);
            if (k->key() == Qt::Key_Backspace && card->toPlainText().isEmpty()) {
                if (BookSession *s = m_view->app()->session()) outline::Board(*s).removeLoose(card->property("loose").toString());
                m_view->refreshSidePane();
                return true;
            }
        }
        return false;
    }
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        const QJsonObject src = QJsonDocument::fromJson(e->mimeData()->data(kCardMime)).object();
        if (src.value("kind").toString() == "section" && !src.value("written").toBool() && !src.value("sec").toString().isEmpty()) e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override
    {
        const QJsonObject src = QJsonDocument::fromJson(e->mimeData()->data(kCardMime)).object();
        e->acceptProposedAction();
        QTimer::singleShot(0, m_view, [v = m_view, src] {
            BookSession *s = v->app()->session();
            if (!s) return;
            v->syncAll();
            outline::Board b(*s);
            if (src.value("virtual").toBool()) {
                // a note not on the page: straight to the loose cards
                QList<outline::Note> n = b.notes(src.value("ch").toString());
                QString text;
                for (const auto &x : n)
                    if (x.id == src.value("sec").toString()) text = x.text;
                n.removeIf([&](const outline::Note &x) { return x.id == src.value("sec").toString(); });
                b.setNotes(src.value("ch").toString(), n);
                b.addLoose(text);
            } else {
                b.sectionToLoose(src.value("ch").toString(), src.value("seg").toInt());
            }
            v->rebuildChapters();
            v->refreshOutline();
            v->refreshSidePane();
        });
    }

private:
    EditorView *m_view;
    QPoint m_press;
};

} // namespace

QWidget *OutlineBoard::looseCards(EditorView *view, QWidget *parent) { return new LooseList(view, parent); }

} // namespace neosea

#include "moc_outlineboard.cpp"
