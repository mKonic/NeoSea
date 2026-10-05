#include "app/goals.h"

#include "app/app.h"
#include "app/dialogs.h"
#include "app/editorview.h"
#include "app/theme.h"
#include "core/counters.h"
#include "core/i18n.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace neosea {

namespace {

// bars for each day's words, a line for the book's total, a dashed goal
class Chart : public QWidget {
public:
    Chart(const counters::Chart &c, int bookGoal, int dailyGoal, QWidget *parent) : QWidget(parent), m_c(c), m_goal(bookGoal), m_daily(dailyGoal)
    {
        setFixedSize(520, 200);
        setToolTip(t("Words written over the last 30 days"));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const double W = width(), H = height(), PAD = 6;
        const double maxC = std::max({double(*std::max_element(m_c.total.begin(), m_c.total.end())), double(m_goal), 1.0});
        const double maxD = std::max({double(*std::max_element(m_c.daily.begin(), m_c.daily.end())), double(m_daily), 1.0});
        const double bw = (W - PAD * 2) / 30;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#3d5a4f"));
        for (int i = 0; i < m_c.daily.size(); ++i) {
            const double h = std::round(m_c.daily[i] / maxD * (H * 0.45));
            p.drawRoundedRect(QRectF(PAD + i * bw, H - PAD - h, bw - 2, h), 1.5, 1.5);
        }
        auto yOf = [&](double v) { return H - PAD - v / maxC * (H - PAD * 2 - 20); };
        QPainterPath line;
        for (int i = 0; i < m_c.total.size(); ++i) {
            const QPointF pt(PAD + i * bw + bw / 2, yOf(m_c.total[i]));
            i ? line.lineTo(pt) : line.moveTo(pt);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor("#c9a86a"), 2));
        p.drawPath(line);
        if (m_goal) {
            QColor g("#c9a86a");
            g.setAlphaF(0.7);
            QPen pen(g, 1);
            pen.setDashPattern({5, 4});
            p.setPen(pen);
            p.drawLine(QPointF(PAD, yOf(m_goal)), QPointF(W - PAD, yOf(m_goal)));
        }
    }

private:
    counters::Chart m_c;
    int m_goal, m_daily;
};

// an hour of the day as the writer's language says it: 1 am / 13:00
QString hourLabel(int h)
{
    if (h == 0) return t("midnight");
    if (I18n::locale().startsWith("en")) {
        if (h == 12) return t("noon");
        return h < 12 ? t("{h} am", {{"h", QString::number(h)}}) : t("{h} pm", {{"h", QString::number(h - 12)}});
    }
    return QLocale(I18n::locale()).toString(QTime(h, 0), QLocale::ShortFormat);
}

QWidget *number(const QString &big, const QString &label, QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *col = new QVBoxLayout(w);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto *b = new QLabel(big, w);
    b->setStyleSheet(QStringLiteral("color: %1; font-size: 28px;").arg(theme().text.name()));
    auto *l = new QLabel(label, w);
    l->setStyleSheet(QStringLiteral("color: %1; font-size: 11px; letter-spacing: 1px;").arg(theme().muted.name()));
    col->addWidget(b);
    col->addWidget(l);
    return w;
}

QSpinBox *goalField(int value, int placeholder, QWidget *parent)
{
    auto *s = new QSpinBox(parent);
    s->setRange(0, 10'000'000);
    s->setSingleStep(100);
    s->setSpecialValueText(QString::number(placeholder)); // shown faint when unset, as NEO's placeholder
    s->setValue(value);
    s->setFixedWidth(110);
    return s;
}

} // namespace

void openGoals(QWidget *parent, App *app, EditorView *view)
{
    BookSession *s = app->session();
    const bool hasBook = s != nullptr && view;
    QJsonObject &lib = app->library();
    QDialog *d = makeDialog(parent, hasBook ? t("{title} — progress", {{"title", s->book().value("title").toString()}}) : t("Goals"), hasBook ? 580 : 380);
    auto *lay = static_cast<QVBoxLayout *>(d->layout());
    const int dayEndsAt = lib.value("dayEndsAt").toInt();
    int total = 0;
    if (hasBook) {
        view->scheduleCounters();
        total = s->book().value("wordCount").toInt();
        const QJsonObject today = s->book().value("dailyCounts").toObject().value(counters::writingDay(QDateTime::currentDateTime(), dayEndsAt)).toObject();
        const int wordsToday = today.isEmpty() ? 0 : std::max(0, today.value("end").toInt() - today.value("start").toInt());
        const int goal = s->book().value("wordGoal").toInt();
        auto *nums = new QHBoxLayout;
        const QLocale loc(I18n::locale());
        nums->addWidget(number(loc.toString(total), t("total words"), d));
        nums->addWidget(number(loc.toString(wordsToday), t("today"), d));
        nums->addWidget(number(goal ? QString::number(std::min(100, int(std::lround(total * 100.0 / goal)))) + "%" : QStringLiteral("—"), t("of book goal"), d));
        lay->addLayout(nums);
        lay->addWidget(new Chart(counters::lastThirtyDays(s->book(), QDateTime::currentDateTime(), dayEndsAt), goal, lib.value("dailyGoal").toInt(), d));
        auto *legend = new QHBoxLayout;
        auto soft = [&](const QString &text, const QString &color) {
            auto *l = new QLabel(text, d);
            l->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(color));
            return l;
        };
        legend->addWidget(soft(t("30 days ago"), theme().muted.name()));
        legend->addStretch();
        legend->addWidget(soft(QStringLiteral("▮ ") + t("daily words"), "#5f8a7a"));
        legend->addWidget(soft(QStringLiteral("— ") + t("total") + (goal ? QStringLiteral(" · - - ") + t("goal") : QString()), theme().accent.name()));
        legend->addStretch();
        legend->addWidget(soft(t("today"), theme().muted.name()));
        lay->addLayout(legend);
    }
    auto label = [&](const QString &text) {
        auto *l = new QLabel(text, d);
        l->setStyleSheet(QStringLiteral("color: %1;").arg(theme().text.name()));
        return l;
    };
    auto *goals = new QHBoxLayout;
    goals->addWidget(label(t("Daily goal")));
    QSpinBox *daily = goalField(lib.value("dailyGoal").toInt(), 500, d);
    goals->addWidget(daily);
    QSpinBox *bookGoal = nullptr;
    if (hasBook) {
        goals->addSpacing(18);
        goals->addWidget(label(t("Book goal")));
        bookGoal = goalField(s->book().value("wordGoal").toInt(), 80000, d);
        goals->addWidget(bookGoal);
    }
    goals->addStretch();
    lay->addSpacing(hasBook ? 12 : 4);
    lay->addLayout(goals);
    auto *ends = new QHBoxLayout;
    ends->addWidget(label(t("Day ends at")));
    auto *hours = new QComboBox(d);
    for (int h = 0; h < 24; ++h) hours->addItem(hourLabel(h), h);
    hours->setCurrentIndex(std::clamp(dayEndsAt, 0, 23));
    ends->addWidget(hours);
    ends->addStretch();
    lay->addLayout(ends);
    QSpinBox *sprintTarget = nullptr;
    QPushButton *sprintBtn = nullptr;
    if (hasBook) {
        auto *row = new QHBoxLayout;
        row->addWidget(label(t("Sprint")));
        sprintTarget = new QSpinBox(d);
        sprintTarget->setRange(50, 1'000'000);
        sprintTarget->setSingleStep(50);
        sprintTarget->setValue(view->sprint() ? view->sprint()->target : 500);
        row->addWidget(sprintTarget);
        row->addWidget(label(t("words")));
        sprintBtn = new QPushButton(view->sprint() && !view->sprint()->done ? t("End sprint") : t("Start sprint"), d);
        sprintBtn->setObjectName("gold");
        row->addWidget(sprintBtn);
        row->addStretch();
        lay->addLayout(row);
    }
    auto *foot = new QHBoxLayout;
    foot->addStretch();
    auto *done = new QPushButton(t("Done"), d);
    done->setObjectName("gold");
    done->setDefault(true);
    foot->addWidget(done);
    lay->addSpacing(8);
    lay->addLayout(foot);
    // Done, Esc and the window around all keep the edits
    auto keep = [&] {
        lib.insert("dailyGoal", daily->value());
        lib.insert("dayEndsAt", hours->currentData().toInt());
        if (hasBook) {
            s->book().insert("wordGoal", bookGoal->value());
            s->saveMeta();
        }
        app->writeLibrary();
    };
    QObject::connect(done, &QPushButton::clicked, d, &QDialog::accept);
    if (sprintBtn) QObject::connect(sprintBtn, &QPushButton::clicked, d, [&] {
        if (view->sprint() && !view->sprint()->done) view->endSprint();
        else view->startSprint(sprintTarget->value());
        d->accept();
    });
    d->exec();
    keep();
    delete d;
    if (hasBook) view->scheduleCounters();
}

} // namespace neosea
