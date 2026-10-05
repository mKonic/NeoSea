#include "app/dialogs.h"

#include "app/theme.h"
#include "core/i18n.h"

#include <QDialog>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace neosea {

QDialog *makeDialog(QWidget *parent, const QString &title, int width)
{
    auto *d = new QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint);
    d->setAttribute(Qt::WA_DeleteOnClose, false);
    d->setModal(true);
    d->setFixedWidth(width);
    d->setStyleSheet(QStringLiteral("QDialog { background: %1; border: 1px solid #333; border-radius: 12px; }")
                         .arg(theme().bgSoft.name()));
    auto *lay = new QVBoxLayout(d);
    lay->setContentsMargins(30, 26, 30, 22);
    lay->setSpacing(10);
    auto *h = new QLabel(title, d);
    h->setObjectName("title");
    h->setWordWrap(true);
    h->setStyleSheet(QStringLiteral("color: %1; font-size: 16px;").arg(theme().accent.name()));
    lay->addWidget(h);
    return d;
}

std::optional<QString> askInput(QWidget *parent, const QString &title, const QString &placeholder, const QString &value)
{
    QDialog *d = makeDialog(parent, title, 380);
    auto *edit = new QLineEdit(d);
    edit->setPlaceholderText(placeholder);
    edit->setText(value);
    edit->selectAll();
    d->layout()->addWidget(edit);
    auto *row = new QHBoxLayout;
    row->addStretch();
    auto *cancel = new QPushButton(t("Cancel"), d);
    auto *ok = new QPushButton(t("OK"), d);
    ok->setObjectName("gold");
    ok->setDefault(true);
    row->addWidget(cancel);
    row->addWidget(ok);
    static_cast<QVBoxLayout *>(d->layout())->addLayout(row);
    QObject::connect(cancel, &QPushButton::clicked, d, &QDialog::reject);
    QObject::connect(ok, &QPushButton::clicked, d, &QDialog::accept);
    QObject::connect(edit, &QLineEdit::returnPressed, d, &QDialog::accept);
    edit->setFocus();
    const int r = d->exec();
    const QString text = edit->text().trimmed();
    delete d;
    if (r != QDialog::Accepted) return std::nullopt;
    return text;
}

QVariant optionModal(QWidget *parent, const QString &title, const QString &message, const QList<Choice> &choices)
{
    QDialog *d = makeDialog(parent, title, 420);
    if (!message.isEmpty()) {
        auto *m = new QLabel(message, d);
        m->setWordWrap(true);
        d->layout()->addWidget(m);
    }
    QVariant picked;
    for (const Choice &c : choices) {
        auto *b = new QPushButton(d);
        b->setObjectName("choice");
        b->setProperty("danger", c.danger);
        auto *bl = new QVBoxLayout(b);
        bl->setContentsMargins(14, 10, 14, 10);
        bl->setSpacing(4);
        auto *l = new QLabel(c.label, b);
        l->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(c.danger ? "#d97b6c" : theme().accent.name()));
        l->setAttribute(Qt::WA_TransparentForMouseEvents);
        bl->addWidget(l);
        if (!c.desc.isEmpty()) {
            auto *s = new QLabel(c.desc, b);
            s->setWordWrap(true);
            s->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(theme().muted.name()));
            s->setAttribute(Qt::WA_TransparentForMouseEvents);
            bl->addWidget(s);
        }
        b->setMinimumHeight(bl->sizeHint().height());
        QObject::connect(b, &QPushButton::clicked, d, [d, &picked, v = c.value] {
            picked = v;
            d->accept();
        });
        d->layout()->addWidget(b);
    }
    auto *row = new QHBoxLayout;
    row->addStretch();
    auto *cancel = new QPushButton(t("Cancel"), d);
    row->addWidget(cancel);
    static_cast<QVBoxLayout *>(d->layout())->addLayout(row);
    QObject::connect(cancel, &QPushButton::clicked, d, &QDialog::reject);
    d->exec();
    delete d;
    return picked;
}

bool confirm(QWidget *parent, const QString &title, const QString &message, const QString &yes, bool danger)
{
    return optionModal(parent, title, message, {Choice{yes, {}, true, danger}}).toBool();
}

// ---------------------------------------------------------------------------

Toast::Toast(QWidget *parent) : QWidget(parent), m_timer(new QTimer(this))
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &QWidget::hide);
    parent->installEventFilter(this);
    hide();
}

void Toast::show(const QString &message, int ms)
{
    m_text = message;
    place();
    QWidget::show();
    raise();
    update();
    m_timer->start(ms);
}

void Toast::place()
{
    QWidget *p = parentWidget();
    QFont f = font();
    f.setPixelSize(13);
    const QFontMetrics fm(f);
    const int maxW = std::min(p->width() - 40, 640);
    const QRect text = fm.boundingRect(QRect(0, 0, maxW - 36, 400), Qt::TextWordWrap, m_text);
    const int w = text.width() + 36, h = text.height() + 20;
    setGeometry((p->width() - w) / 2, p->height() - h - 56, w, h);
}

bool Toast::eventFilter(QObject *o, QEvent *e)
{
    if (o == parentWidget() && e->type() == QEvent::Resize && isVisible()) place();
    return false;
}

void Toast::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    p.fillPath(path, QColor(38, 38, 38, 240));
    p.setPen(QColor("#3a3a3a"));
    p.drawPath(path);
    QFont f = font();
    f.setPixelSize(13);
    p.setFont(f);
    p.setPen(QColor("#dddddd"));
    p.drawText(rect().adjusted(18, 10, -18, -10), Qt::AlignCenter | Qt::TextWordWrap, m_text);
}

} // namespace neosea

#include "moc_dialogs.cpp"
