#pragma once
// NEO's palette: a dark room, the page in it. Night is dark paper for
// midnight sessions; Paper is the white page in the dark room; Light is the
// white page in a light room, shelf included.

#include <QColor>
#include <QString>

namespace neosea {

struct Theme {
    QString name = "night";
    QColor bg, bgSoft, pane, paper, ink, accent, muted, red, chapterHead, ghost, sceneBreak, line, text;
    bool lightRoom = false;

    static Theme byName(const QString &name);
    // the application style sheet: dialogs, menus, buttons, the bottom bar
    QString styleSheet() const;
};

const Theme &theme();
void setTheme(const QString &name);

} // namespace neosea
