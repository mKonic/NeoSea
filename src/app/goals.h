#pragma once
// Goals (File → Goals…, or a click on the counter of today's words): the
// book's progress and the last 30 days, the daily and book goals, when the
// writing day ends, and sprints.

class QWidget;

namespace neosea {

class EditorView;
class App;

void openGoals(QWidget *parent, App *app, EditorView *view);

} // namespace neosea
