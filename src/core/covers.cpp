#include "core/covers.h"

#include "core/fonts.h"
#include "core/i18n.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRegularExpression>
#include <QSet>

#include <cmath>

namespace neosea::covers {

namespace {

constexpr double W = 208, H = 300;           // the abstract's own canvas
constexpr double TILE_W = 104, TILE_H = 150; // the cover's type is set in tile px
constexpr double PAD = 8;
constexpr double AVAIL_W = TILE_W - PAD * 2;

double between(Rng &r, double a, double b) { return a + r() * (b - a); }
template <typename T> const T &pick(Rng &r, const QList<T> &arr) { return arr[int(std::floor(r() * arr.size()))]; }

QColor hsl(double h, double s, double l)
{
    int hh = int(std::fmod(std::fmod(h, 360) + 360, 360));
    const double ss = std::round(s), ll = std::round(l);
    return QColor::fromHslF(hh / 360.0, std::clamp(ss / 100, 0.0, 1.0), std::clamp(ll / 100, 0.0, 1.0));
}

struct Palette {
    bool dark;
    QColor ground, ground2, mid, accent, pale;
};

Palette palette(Rng &r)
{
    const int base = int(std::floor(r() * 360));
    const QString scheme = pick<QString>(r, {"analogous", "split", "mono", "duotone"});
    const double sat = between(r, 28, 62);
    const bool dark = r() < 0.7;
    const double L0 = dark ? 10 : 78, L1 = dark ? 22 : 92;
    double h0 = base, h1, h2;
    if (scheme == "analogous") { h1 = base + 30; h2 = base - 25; }
    else if (scheme == "split") { h1 = base + 150; h2 = base + 210; }
    else if (scheme == "mono") { h1 = base + 8; h2 = base - 8; }
    else { h1 = base + 180; h2 = base + 180; }
    Palette p;
    p.dark = dark;
    p.ground = hsl(h0, sat * 0.8, between(r, L0, L1));
    p.ground2 = hsl(h1, sat * 0.7, dark ? between(r, 6, 18) : between(r, 70, 86));
    p.mid = hsl(h1, sat, dark ? between(r, 30, 48) : between(r, 45, 62));
    p.accent = hsl(h2, std::min(90.0, sat + 30), dark ? between(r, 52, 68) : between(r, 38, 55));
    p.pale = hsl(h0, sat * 0.5, dark ? between(r, 60, 80) : between(r, 20, 34));
    return p;
}

QColor clear(const QColor &c)
{
    QColor x = c;
    x.setAlpha(0);
    return x;
}

void ground(QPainter &g, Rng &r, const Palette &p)
{
    const double angle = between(r, 0, M_PI * 2);
    QLinearGradient lg(W / 2 - std::cos(angle) * W, H / 2 - std::sin(angle) * H, W / 2 + std::cos(angle) * W,
                       H / 2 + std::sin(angle) * H);
    lg.setColorAt(0, p.ground);
    lg.setColorAt(1, p.ground2);
    g.fillRect(QRectF(0, 0, W, H), lg);
}

using Style = void (*)(QPainter &, Rng &, const Palette &);

void orbs(QPainter &g, Rng &r, const Palette &p)
{
    ground(g, r, p);
    const int n = 2 + int(std::floor(r() * 3));
    for (int i = 0; i < n; ++i) {
        const double x = between(r, -20, W + 20), y = between(r, -20, H + 20);
        const double rad = between(r, 50, 150);
        QColor c = i == 0 ? p.accent : (r() < 0.5 ? p.mid : p.pale);
        QRadialGradient rg(x, y, rad);
        rg.setColorAt(0, c);
        rg.setColorAt(1, clear(c));
        g.setOpacity(between(r, 0.45, 0.85));
        g.fillRect(QRectF(0, 0, W, H), rg);
    }
    g.setOpacity(1);
}

void horizon(QPainter &g, Rng &r, const Palette &p)
{
    ground(g, r, p);
    const double y = between(r, H * 0.45, H * 0.75);
    QLinearGradient lg(0, y - 60, 0, y);
    lg.setColorAt(0, clear(p.accent));
    lg.setColorAt(1, p.accent);
    g.setOpacity(0.55);
    g.fillRect(QRectF(0, y - 60, W, 60), lg);
    g.setOpacity(1);
    g.fillRect(QRectF(0, y, W, H - y), p.ground2);
    if (r() < 0.6) {
        g.setPen(Qt::NoPen);
        g.setBrush(p.accent);
        const double cx = between(r, 40, W - 40);
        const double cy = y - between(r, 10, 50);
        const double rad = between(r, 8, 22);
        g.drawEllipse(QPointF(cx, cy), rad, rad);
    }
    g.setOpacity(0.5);
    g.fillRect(QRectF(0, y - 1, W, 1.5), p.pale);
    g.setOpacity(1);
}

void ring(QPainter &g, Rng &r, const Palette &p)
{
    ground(g, r, p);
    const double x = between(r, W * 0.3, W * 0.7), y = between(r, H * 0.25, H * 0.7);
    const double rad = between(r, 40, 85);
    if (r() < 0.5) {
        QPen pen(p.accent, between(r, 2, 9));
        g.setPen(pen);
        g.setBrush(Qt::NoBrush);
        g.drawEllipse(QPointF(x, y), rad, rad);
    } else {
        g.setOpacity(0.9);
        g.setPen(Qt::NoPen);
        g.setBrush(p.accent);
        g.drawEllipse(QPointF(x, y), rad, rad);
        g.setOpacity(1);
    }
    g.setOpacity(0.5);
    g.setPen(QPen(p.pale, 1));
    const double a = between(r, -0.5, 0.5);
    g.drawLine(QPointF(-10, y + std::tan(a) * (x + 10)), QPointF(W + 10, y - std::tan(a) * (W - x + 10)));
    g.setOpacity(1);
}

void bands(QPainter &g, Rng &r, const Palette &p)
{
    ground(g, r, p);
    const bool vertical = r() < 0.5;
    const QList<QColor> cols{p.mid, p.accent, p.ground2, p.pale};
    double pos = between(r, -20, 30);
    const double limit = vertical ? W : H;
    while (pos < limit + 20) {
        const double w = between(r, 10, 70);
        const QColor c = pick(r, cols);
        g.setOpacity(between(r, 0.5, 1));
        if (vertical) g.fillRect(QRectF(pos, 0, w, H), c);
        else g.fillRect(QRectF(0, pos, W, w), c);
        pos += w + between(r, 0, 40);
    }
    g.setOpacity(1);
}

void shards(QPainter &g, Rng &r, const Palette &p)
{
    ground(g, r, p);
    const int n = 3 + int(std::floor(r() * 3));
    g.setPen(Qt::NoPen);
    for (int i = 0; i < n; ++i) {
        g.setBrush(pick<QColor>(r, {p.mid, p.accent, p.pale, p.ground2}));
        g.setOpacity(between(r, 0.35, 0.8));
        QPolygonF tri;
        for (int k = 0; k < 3; ++k) {
            const double px = between(r, -30, W + 30);
            const double py = between(r, -30, H + 30);
            tri << QPointF(px, py);
        }
        g.drawPolygon(tri);
    }
    g.setOpacity(1);
}

void waves(QPainter &g, Rng &r, const Palette &p)
{
    ground(g, r, p);
    const int rows = 6 + int(std::floor(r() * 10));
    const double amp = between(r, 6, 26);
    const double freq = between(r, 0.02, 0.06);
    const double phase = between(r, 0, 6);
    const double lw = between(r, 1, 2.5);
    g.setBrush(Qt::NoBrush);
    for (int i = 0; i < rows; ++i) {
        const double y0 = (H / (rows + 1)) * (i + 1);
        g.setPen(QPen(i % 3 == 0 ? p.accent : p.pale, lw));
        g.setOpacity(between(r, 0.35, 0.9));
        QPainterPath path;
        for (double x = -2; x <= W + 2; x += 3) {
            const double y = y0 + std::sin(x * freq + phase + i * 0.6) * amp * std::sin(double(i) / rows * M_PI);
            if (x == -2) path.moveTo(x, y);
            else path.lineTo(x, y);
        }
        g.drawPath(path);
    }
    g.setOpacity(1);
}

void solitary(QPainter &g, Rng &r, const Palette &p)
{
    ground(g, r, p);
    g.setPen(Qt::NoPen);
    g.setBrush(p.accent);
    const double x = between(r, W * 0.25, W * 0.75), y = between(r, H * 0.3, H * 0.65);
    const double s = between(r, 10, 26);
    const QString kind = pick<QString>(r, {"square", "dot", "tri", "bar"});
    if (kind == "square") g.drawRect(QRectF(x - s / 2, y - s / 2, s, s));
    else if (kind == "dot") g.drawEllipse(QPointF(x, y), s / 2, s / 2);
    else if (kind == "tri") g.drawPolygon(QPolygonF{QPointF(x, y - s / 2), QPointF(x + s / 2, y + s / 2), QPointF(x - s / 2, y + s / 2)});
    else g.drawRect(QRectF(x - s * 1.5, y - 2, s * 3, 4));
}

const QList<QPair<QString, Style>> &styles()
{
    static const QList<QPair<QString, Style>> s{{"orbs", orbs},     {"horizon", horizon}, {"ring", ring},
                                                {"bands", bands},   {"shards", shards},   {"waves", waves},
                                                {"solitary", solitary}};
    return s;
}

QString paintInto(QImage &img, const QString &seed)
{
    Rng r(hash(seed));
    const Palette p = palette(r);
    const auto &st = styles()[int(std::floor(r() * styles().size()))];
    {
        QPainter g(&img);
        g.setRenderHint(QPainter::Antialiasing);
        g.scale(img.width() / W, img.height() / H);
        st.second(g, r, p);
    }
    const double amount = between(r, 6, 22) * std::min(1.0, W / img.width() * 2);
    // grain: a little noise in every pixel, as a printed cover has
    for (int y = 0; y < img.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int n = int(std::lround((r() - 0.5) * amount));
            const QRgb c = line[x];
            line[x] = qRgb(std::clamp(qRed(c) + n, 0, 255), std::clamp(qGreen(c) + n, 0, 255), std::clamp(qBlue(c) + n, 0, 255));
        }
    }
    return st.first;
}

// ---------------------------------------------------------------------------
// type

struct Template {
    QString id;
    QStringList families;
    int weight;
    bool caps;
    QString anchor;
    double lead, max;
    int maxLines;
    QString connectors;
    bool frame = false, band = false, rule = false;
};

QString registered(const QString &prefix)
{
    registerBundledFonts();
    const QStringList all = QFontDatabase::families();
    if (all.contains(prefix)) return prefix;
    for (const QString &f : all)
        if (f.startsWith(prefix)) return f;
    return prefix;
}

const QList<Template> &templates()
{
    static const QList<Template> t{
        {"stack", {registered("Anton"), registered("Russo One")}, 400, true, "top", 0.9, 46, 4, "small"},
        {"bebas", {registered("Bebas Neue"), registered("Oswald")}, 400, true, "center", 0.88, 48, 4, "small"},
        {"black", {registered("Playfair Display")}, 900, false, "center", 0.98, 40, 3, "italic"},
        {"fat", {registered("Abril Fatface"), registered("Playfair Display")}, 400, false, "bottom", 0.98, 40, 3, "italic"},
        {"cinzel", {registered("Cinzel"), registered("Forum")}, 900, true, "center", 1.05, 30, 4, "small", true},
        {"band", {registered("Josefin Sans"), registered("Jost")}, 700, true, "center", 1.0, 30, 3, "inline", false, true},
        {"oswald", {registered("Oswald"), registered("Anton")}, 700, true, "top", 0.95, 40, 4, "small", false, false, true},
    };
    return t;
}

const Template &templateById(const QString &id)
{
    for (const Template &t : templates())
        if (t.id == id) return t;
    return templates().first();
}

QString connKey(const QString &w)
{
    QString out;
    for (QChar c : w.normalized(QString::NormalizationForm_D))
        if (c.isLetter()) out += c.toLower();
    return out;
}

const QSet<QString> &connectors()
{
    static const QSet<QString> s = [] {
        const QStringList raw{
            "the", "of", "a", "an", "and", "in", "on", "to", "for", "at", "by", "from", "or", "with", "is", "are", "my", "your", "our", "his", "her", "its",
            "ο", "η", "το", "οι", "τα", "του", "της", "των", "ενας", "ενα", "μια", "και", "κι", "σε", "στο", "στη", "στην", "στον", "στους", "στις",
            "με", "για", "απο", "προς", "ως", "που",
            "le", "la", "les", "un", "une", "des", "du", "de", "et", "en", "au", "aux", "sur", "pour", "par", "dans", "ou", "avec", "mon", "ma", "mes", "ton", "ta", "tes", "son", "sa", "ses",
            "el", "los", "las", "del", "y", "una", "con", "por", "sin", "mi", "tu", "su",
            "o", "os", "as", "do", "da", "dos", "das", "e", "em", "um", "uma", "no", "na",
            "der", "die", "und", "von", "im", "ein", "eine", "dem", "den", "mit", "zum", "zur",
            "il", "lo", "i", "gli", "di", "della", "dei", "nel", "nella", "per",
            "het", "een", "van", "op", "met",
            "w", "z", "ze", "we",
            "si", "din", "cu", "pe", "pentru", "al", "ale", "sau", "spre", "sub", "intre", "fara",
            "и", "а", "но", "или", "в", "во", "на", "о", "об", "с", "со", "к", "по", "из", "за", "от", "до", "для", "без", "под", "над", "про", "у", "мой", "моя", "моё", "мои", "его", "её", "их"};
        QSet<QString> out;
        for (const QString &w : raw) out.insert(connKey(w));
        return out;
    }();
    return s;
}

QFont fontFor(const Template &t, bool italic, double px)
{
    QFont f;
    f.setFamilies(italic ? QStringList{registered("Playfair Display")} : t.families);
    f.setWeight(QFont::Weight(italic ? 900 : t.weight));
    f.setItalic(italic);
    f.setPixelSize(std::max(1, int(std::lround(px))));
    return f;
}

double widthAt100(const QString &text, const Template &t, bool italic)
{
    return QFontMetricsF(fontFor(t, italic, 100)).horizontalAdvance(text);
}

struct Laid {
    QList<Line> lines;
    double height;
};

Laid layoutTitle(const QString &title, const Template &t)
{
    QList<Line> lines = breakLines(title, t.id);
    const double availH = t.anchor == "center" ? TILE_H * 0.62 : TILE_H * 0.66;
    double big = 0;
    for (Line &ln : lines) {
        const QString txt = t.caps ? ln.text.toUpper() : ln.text;
        const bool italic = ln.small && t.connectors == "italic";
        double w = widthAt100(txt, t, italic);
        if (w <= 0) w = 50;
        ln.text = txt;
        ln.italic = italic;
        ln.size = std::min(t.max, (AVAIL_W / w) * 100);
        if (!ln.small) big = std::max(big, ln.size);
    }
    for (Line &ln : lines)
        if (ln.small) ln.size = std::min(ln.size, std::max(8.0, big * 0.34));
    double h = 0;
    for (const Line &ln : lines) h += ln.size * t.lead;
    if (h > availH) {
        const double k = availH / h;
        for (Line &ln : lines) ln.size = std::max(7.0, ln.size * k);
        h = availH;
    }
    return {lines, h};
}

std::pair<QRectF, QRectF> regions(const Template &t, double blockH)
{
    const double bh = blockH / TILE_H;
    const double auH = 14 / TILE_H;
    double top;
    if (t.anchor == "top") top = 10 / TILE_H;
    else if (t.anchor == "bottom") top = 1 - auH - 0.06 - bh;
    else top = 0.5 - bh / 2 - 0.02;
    const QRectF title(QPointF(PAD / TILE_W, std::max(0.0, top)), QPointF(1 - PAD / TILE_W, std::min(1.0, top + bh)));
    const double authorTop = t.anchor == "top" ? 1 - auH - 0.05 : std::min(1 - auH, top + bh + 0.03);
    const QRectF author(QPointF(PAD / TILE_W, authorTop), QPointF(1 - PAD / TILE_W, std::min(1.0, authorTop + auH)));
    return {title, author};
}

Ink inkFor(const QImage &art, const QRectF &region)
{
    const QImage img = art.size() == QSize(int(W), int(H)) ? art : art.scaled(int(W), int(H), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const int x0 = int(std::floor(region.left() * W)), y0 = int(std::floor(region.top() * H));
    const int x1 = std::max(x0 + 1, int(std::ceil(region.right() * W)));
    const int y1 = std::max(y0 + 1, int(std::ceil(region.bottom() * H)));
    double sum = 0, sumSq = 0;
    int n = 0, k = 0;
    for (int y = y0; y < std::min(y1, img.height()); ++y)
        for (int x = x0; x < std::min(x1, img.width()); ++x, ++k) {
            if (k % 4) continue; // every 4th pixel is plenty
            const QRgb c = img.pixel(x, y);
            const double l = (0.2126 * qRed(c) + 0.7152 * qGreen(c) + 0.0722 * qBlue(c)) / 255;
            sum += l;
            sumSq += l * l;
            n++;
        }
    if (!n) return {};
    const double mean = sum / n;
    const double sd = std::sqrt(std::max(0.0, sumSq / n - mean * mean));
    return {mean < 0.56, std::abs(mean - 0.5) < 0.22 || sd > 0.2};
}

QString seedOf(const QJsonObject &meta)
{
    const QString s = meta.value("coverSeed").toString();
    return s.isEmpty() ? meta.value("id").toString() : s;
}

void drawSpaced(QPainter &p, const QFont &font, double spacing, const QString &text, QPointF at, bool left, double cx)
{
    QFont f = font;
    f.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
    p.setFont(f);
    const QFontMetricsF fm(f);
    const double w = fm.horizontalAdvance(text);
    const double x = left ? at.x() : cx - w / 2;
    p.drawText(QPointF(x, at.y() + fm.ascent()), text);
}

} // namespace

quint32 hash(const QString &s)
{
    quint32 h = 0x811c9dc5u;
    for (QChar c : s) {
        h ^= c.unicode();
        h *= 0x01000193u;
    }
    return h;
}

double Rng::operator()()
{
    a += 0x6d2b79f5u;
    quint32 t = a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return double(t ^ (t >> 14)) / 4294967296.0;
}

QImage paintAbstract(const QString &seed, QSize size)
{
    static QHash<QString, QImage> cache;
    const QString key = seed + '@' + QString::number(size.width()) + 'x' + QString::number(size.height());
    if (auto it = cache.constFind(key); it != cache.constEnd()) return *it;
    QImage img(size, QImage::Format_RGB32);
    img.fill(Qt::black);
    paintInto(img, seed);
    if (cache.size() > 400) cache.clear();
    cache.insert(key, img);
    return img;
}

QString styleOf(const QString &seed)
{
    QImage img(4, 6, QImage::Format_RGB32);
    return paintInto(img, seed);
}

QList<Line> breakLines(const QString &title, const QString &templateId)
{
    const Template &t = templateById(templateId);
    const QStringList words = title.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (words.isEmpty()) return {Line{neosea::t("Untitled")}};
    auto isConn = [](const QString &w) { return connectors().contains(connKey(w)); };
    QList<Line> lines;
    if (words.size() <= t.maxLines) {
        for (const QString &w : words) lines << Line{w, isConn(w) && t.connectors != "inline" && words.size() > 1};
    } else {
        // greedy balance: connectors glue to the following word
        QStringList groups;
        for (qsizetype i = 0; i < words.size(); ++i) {
            if (isConn(words[i]) && i < words.size() - 1 && t.connectors != "inline") {
                groups << words[i] + ' ' + words[i + 1];
                ++i;
            } else {
                groups << words[i];
            }
        }
        const qsizetype total = groups.join(' ').size();
        const qsizetype target = (total + t.maxLines - 1) / t.maxLines;
        QString cur;
        for (const QString &g : groups) {
            if (!cur.isEmpty() && (cur + ' ' + g).size() > target && lines.size() < t.maxLines - 1) {
                lines << Line{cur};
                cur = g;
            } else {
                cur = cur.isEmpty() ? g : cur + ' ' + g;
            }
        }
        if (!cur.isEmpty()) lines << Line{cur};
    }
    // a lone leading connector on a two-word title ("The Road") stays big
    if (lines.size() == 2 && lines[0].small) lines[0].small = false;
    return lines;
}

Plan plan(const QJsonObject &meta, const QImage &art)
{
    const QString seed = seedOf(meta);
    Rng r(hash("type:" + seed));
    const Template &t = templates()[int(std::floor(r() * templates().size()))];
    const QImage canvas = art.isNull() ? paintAbstract(seed) : art;
    QString title = meta.value("title").toString();
    if (title.isEmpty()) title = "Untitled";
    const Laid laid = layoutTitle(title, t);
    const auto [titleR, authorR] = regions(t, laid.height);
    Plan p;
    p.templateId = t.id;
    p.lines = laid.lines;
    p.ink = inkFor(canvas, titleR);
    p.authorInk = inkFor(canvas, authorR);
    p.longAuthor = meta.value("author").toString().size() > 16;
    return p;
}

void paint(QPainter &p, const QRectF &rect, const QJsonObject &meta, const QImage &image)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QString seed = seedOf(meta);
    QImage tileArt;
    if (!image.isNull()) {
        // cover-fit, as CSS does
        const double k = std::max(rect.width() / image.width(), rect.height() / image.height());
        const QSizeF s(image.width() * k, image.height() * k);
        p.setClipRect(rect);
        p.drawImage(QRectF(rect.center() - QPointF(s.width() / 2, s.height() / 2), s), image);
        p.setClipping(false);
        tileArt = image.scaled(int(W), int(H), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
                      .copy(0, 0, int(W), int(H));
    } else {
        const QSize px = (rect.size() * p.device()->devicePixelRatioF()).toSize();
        const QImage art = paintAbstract(seed, px.width() > W ? px : QSize(int(W), int(H)));
        p.drawImage(rect, art);
    }
    const Plan planned = plan(meta, tileArt);
    const Template &t = templateById(planned.templateId);
    p.translate(rect.topLeft());
    const double s = rect.width() / TILE_W;
    p.scale(s, s);

    const bool light = planned.ink.light;
    const QColor ink = light ? QColor("#ffffff") : QColor("#141414");
    const QColor auInk = planned.authorInk.light ? QColor("#ffffff") : QColor("#141414");
    auto lineH = [&](const Line &ln) { return ln.size * (ln.small ? 1.4 : t.lead); };
    double blockH = 0;
    for (const Line &ln : planned.lines) blockH += lineH(ln);
    const double AU = 7.5, AU_GAP = 7;
    const bool left = t.anchor == "top" && !t.band;
    double top;
    if (t.anchor == "top") top = 10 + (t.rule ? 11 : 0);
    else if (t.anchor == "bottom") top = TILE_H - 10 - AU - AU_GAP - blockH;
    else top = (TILE_H - (t.band ? blockH + AU + AU_GAP + 6 : blockH)) / 2 - (t.band ? 0 : 2);

    // veils and plates under the type
    if (t.band) {
        p.fillRect(QRectF(0, top - 9, TILE_W, blockH + 6 + AU_GAP + AU + 17), light ? QColor(10, 10, 10, 199) : QColor(255, 255, 255, 219));
    } else if (planned.ink.scrim) {
        const QColor base = light ? QColor(0, 0, 0) : QColor(255, 255, 255);
        auto rgba = [&](double a) {
            QColor c = base;
            c.setAlphaF(a);
            return c;
        };
        if (t.anchor == "top") {
            QLinearGradient g(0, TILE_H * 0.7, 0, 0);
            g.setColorAt(0, rgba(0));
            g.setColorAt(1, rgba(0.6));
            p.fillRect(QRectF(0, 0, TILE_W, TILE_H * 0.7), g);
        } else if (t.anchor == "bottom") {
            QLinearGradient g(0, TILE_H * 0.3, 0, TILE_H);
            g.setColorAt(0, rgba(0));
            g.setColorAt(1, rgba(0.65));
            p.fillRect(QRectF(0, TILE_H * 0.3, TILE_W, TILE_H * 0.7), g);
        } else {
            QRadialGradient g(TILE_W / 2, TILE_H / 2, TILE_H * 0.5);
            g.setColorAt(0, rgba(0.5));
            g.setColorAt(0.72, rgba(0));
            g.setColorAt(1, rgba(0));
            p.fillRect(QRectF(0, 0, TILE_W, TILE_H), g);
        }
    }
    if (t.frame) {
        p.setOpacity(0.6);
        p.setPen(QPen(ink, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(5, 5, TILE_W - 10, TILE_H - 10));
        p.setOpacity(1);
    }
    if (t.rule) p.fillRect(QRectF(PAD, 10, 26, 4), ink);

    const double cx = TILE_W / 2;
    double y = top;
    for (const Line &ln : planned.lines) {
        const QFont f = fontFor(t, ln.italic, ln.size);
        const double spacing = ln.small ? (t.id == "bebas" ? 3 : t.id == "cinzel" ? 2.5 : 2)
                                        : (t.id == "band" ? 0.8 : t.id == "cinzel" ? 0.5 : t.id == "stack" ? 0.3 : t.id == "bebas" ? 0.5 : 0);
        const QPointF at(PAD, y + (lineH(ln) - ln.size) / 2);
        if (!t.band) {
            // a soft shadow under the type
            p.setPen(light ? QColor(0, 0, 0, 90) : QColor(255, 255, 255, 70));
            drawSpaced(p, f, spacing, ln.text, at + QPointF(0, 1), left, cx);
        }
        p.setPen(ink);
        drawSpaced(p, f, spacing, ln.text, at, left, cx);
        y += lineH(ln);
    }
    if (t.band) {
        p.setOpacity(0.7);
        p.fillRect(QRectF(TILE_W / 2 - 8, y + 6, 16, 1.5), ink);
        p.setOpacity(1);
        y += 8;
    }

    const QString author = meta.value("author").toString().toUpper();
    if (!author.isEmpty()) {
        const bool longAu = author.size() > 16;
        double auSize = longAu ? 6.5 : AU;
        Template josefin = templateById("band");
        auto auFont = [&](double px) {
            QFont f;
            f.setFamilies(josefin.families);
            f.setWeight(QFont::DemiBold);
            f.setPixelSize(std::max(1, int(std::lround(px * s))));
            return f;
        };
        // measure in output pixels, draw in tile coordinates
        p.save();
        p.scale(1 / s, 1 / s);
        QFont af = auFont(auSize);
        af.setLetterSpacing(QFont::AbsoluteSpacing, (longAu ? 0.8 : 1.6) * s);
        double aw = QFontMetricsF(af).horizontalAdvance(author) / s;
        if (aw > AVAIL_W) {
            auSize = std::max(4.5, auSize * AVAIL_W / aw);
            af = auFont(auSize);
            af.setLetterSpacing(QFont::AbsoluteSpacing, (longAu ? 0.8 : 1.6) * s);
            aw = QFontMetricsF(af).horizontalAdvance(author) / s;
        }
        const double ay = t.anchor == "top" ? TILE_H - 9 - AU : y + AU_GAP;
        if (planned.authorInk.scrim && !t.band) {
            const double px = left ? PAD : TILE_W / 2 - aw / 2;
            p.fillRect(QRectF((px - 6) * s, (ay - 3) * s, (aw + 12) * s, (AU + 6) * s),
                       planned.authorInk.light ? QColor(0, 0, 0, 128) : QColor(255, 255, 255, 153));
        }
        p.setFont(af);
        p.setPen(t.band ? ink : auInk);
        p.setOpacity(0.9);
        const double x = left ? PAD : TILE_W / 2 - aw / 2;
        p.drawText(QPointF(x * s, ay * s + QFontMetricsF(af).ascent()), author);
        p.setOpacity(1);
        p.restore();
    }
    p.restore();
}

QImage renderFull(const QJsonObject &meta, const QImage &image, QSize size)
{
    QImage out(size, QImage::Format_RGB32);
    out.fill(Qt::black);
    QPainter p(&out);
    paint(p, QRectF(QPointF(0, 0), QSizeF(size)), meta, image);
    return out;
}

} // namespace neosea::covers
