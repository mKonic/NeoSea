#pragma once
// The two dialog voices NEO speaks in: a question with a line to type in, and
// a list of choices, each with a sentence saying what it does. Esc or a click
// outside is Cancel. And the toast: a line at the foot of the window.

#include <QString>
#include <QVariant>
#include <QWidget>

#include <optional>

class QLabel;
class QTimer;

namespace neosea {

struct Choice {
    QString label;
    QString desc;
    QVariant value;
    bool danger = false;
};

// null on cancel
std::optional<QString> askInput(QWidget *parent, const QString &title, const QString &placeholder,
                                const QString &value = {});
// the chosen value, invalid on cancel
QVariant optionModal(QWidget *parent, const QString &title, const QString &message, const QList<Choice> &choices);
// a yes-or-no question; true for yes
bool confirm(QWidget *parent, const QString &title, const QString &message, const QString &yes, bool danger = false);

class Toast : public QWidget {
    Q_OBJECT
public:
    explicit Toast(QWidget *parent);
    void show(const QString &message, int ms = 4000);

protected:
    void paintEvent(QPaintEvent *) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    void place();
    QString m_text;
    QTimer *m_timer;
};

} // namespace neosea
