#include "app/theme.h"

namespace neosea {

namespace {
Theme g_theme = Theme::byName("night");
}

Theme Theme::byName(const QString &name)
{
    Theme t;
    t.name = name == "paper" || name == "light" ? name : "night";
    t.bg = QColor("#191919");
    t.bgSoft = QColor("#222222");
    t.pane = QColor("#202020");
    t.accent = QColor("#c9a86a");
    t.muted = QColor("#8a8a8a");
    t.red = QColor("#c0392b");
    t.line = QColor("#3a3a3a");
    t.text = QColor("#dddddd");
    if (t.name == "night") {
        t.paper = QColor("#232221");
        t.ink = QColor("#d6d2c6");
        t.chapterHead = QColor("#918b7d");
        t.ghost = QColor("#6f6a5e");
        t.sceneBreak = QColor("#7d7768");
    } else {
        t.paper = QColor("#fbfaf7");
        t.ink = QColor("#1c1c1c");
        t.chapterHead = QColor("#555555");
        t.ghost = QColor("#a9a294");
        t.sceneBreak = QColor("#888888");
    }
    if (t.name == "light") {
        t.lightRoom = true;
        t.bg = QColor("#efede8");
        t.bgSoft = QColor("#f7f5f0");
        t.pane = QColor("#f4f2ed");
        t.text = QColor("#2b2926");
        t.muted = QColor("#7d7a72");
        t.line = QColor("#d9d3c4");
    }
    return t;
}

QString Theme::styleSheet() const
{
    return QStringLiteral(R"(
QWidget { color: %1; }
QMainWindow, #room { background: %2; }
QToolTip { background: %3; color: %1; border: 1px solid %4; padding: 4px 6px; }
QDialog { background: %3; }
QDialog QLabel#title { color: %5; font-size: 16px; }
QDialog QLabel { color: %1; }
QLineEdit { background: %2; border: 1px solid %4; border-radius: 6px; padding: 7px 10px; color: %1; selection-background-color: %5; }
QLineEdit:focus { border-color: %5; }
QSpinBox, QComboBox { background: %2; border: 1px solid %4; border-radius: 6px; padding: 5px 8px; color: %1; selection-background-color: %5; }
QSpinBox:focus, QComboBox:focus { border-color: %5; }
QSpinBox::up-button, QSpinBox::down-button { width: 0; border: none; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView { background: #262626; color: #dddddd; border: 1px solid #3a3a3a; selection-background-color: #333333; outline: none; }
QPushButton { background: none; border: none; color: %6; padding: 7px 10px; }
QPushButton:hover { color: %1; }
QPushButton#gold { background: %5; color: #191919; border-radius: 6px; padding: 7px 18px; }
QPushButton#gold:hover { background: #d4b479; }
#choice { background: %2; border: 1px solid %4; border-radius: 8px; color: %1; }
#choice:hover, #choice:focus { border-color: %5; }
#choice[danger="true"] { border-color: #6b3a34; }
QPushButton#outline { background: none; border: 1px solid %4; border-radius: 6px; color: %6; padding: 5px 12px; font-size: 12px; }
QPushButton#outline:hover { color: %5; border-color: %5; }
QMenu { background: #262626; border: 1px solid #3a3a3a; border-radius: 8px; padding: 6px; color: #dddddd; }
QMenu::item { padding: 6px 22px 6px 12px; border-radius: 5px; }
QMenu::item:selected { background: #333333; }
QMenu::item:disabled { color: #666666; }
QMenu::separator { height: 1px; background: #3a3a3a; margin: 4px 6px; }
QMenuBar { background: %2; color: %6; }
QMenuBar::item:selected { background: %3; color: %1; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: rgba(128,128,128,0.35); border-radius: 4px; min-height: 30px; margin: 2px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }
QScrollBar:horizontal { height: 0; }
)")
        .arg(text.name(), bg.name(), bgSoft.name(), line.name(), accent.name(), muted.name());
}

const Theme &theme() { return g_theme; }

void setTheme(const QString &name) { g_theme = Theme::byName(name); }

} // namespace neosea
