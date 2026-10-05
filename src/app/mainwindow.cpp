#include "app/mainwindow.h"

#include "app/app.h"
#include "app/appsettings.h"
#include "app/dialogs.h"
#include "app/chapteredit.h"
#include "app/editorview.h"
#include "app/goals.h"
#include "app/writingmodes.h"
#include "app/vimkeys.h"
#include "app/readaloud.h"
#include "app/shelfview.h"
#include "app/theme.h"
#include "core/bookmodel.h"
#include "core/covers.h"
#include "core/exporter.h"
#include "core/fonts.h"
#include "core/i18n.h"
#include "core/scriptpdf.h"
#include "core/shelves.h"
#include "neosea_version.h"

#include <QGuiApplication>
#include <QTextEdit>
#include <QClipboard>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QCloseEvent>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWindow>

namespace neosea {

MainWindow::MainWindow(App *app, QWidget *parent) : QMainWindow(parent), m_app(app)
{
    setWindowTitle("neosea");
    setMinimumSize(700, 600);
    applyTheme();
    m_views = new QStackedWidget(this);
    m_shelf = new ShelfView(app);
    m_editor = new EditorView(app);
    m_views->addWidget(m_shelf);
    m_views->addWidget(m_editor);
    setCentralWidget(m_views);
    m_toast = new Toast(this);
    connect(app, &App::toast, this, [this](const QString &m, int ms) { m_toast->show(m, ms); });
    connect(m_shelf, &ShelfView::openBook, this, &MainWindow::openBook);
    connect(m_shelf, &ShelfView::importRequested, this, &MainWindow::importBooks);
    connect(m_editor, &EditorView::backToShelf, this, &MainWindow::backToShelf);
    connect(m_editor, &EditorView::exportChapter, this, [this](const QString &chId) {
        const QVariant fmt = optionModal(this, t("Export “{title}”", {{"title", chapterHeading(chId, m_app->session()->book())}}), {},
                                         {{t("Text (.txt)"), {}, "txt"},
                                          {t("Markdown (.md)"), {}, "md"},
                                          {t("HTML (.html)"), {}, "html"},
                                          {"PDF (.pdf)", {}, "pdf"},
                                          {t("Word (.docx)"), {}, "docx"},
                                          {t("EPUB (.epub)"), {}, "epub"}});
        if (fmt.isValid()) exportBook(fmt.toString(), chId);
    });
    buildMenus();
    // the window comes back where it was left, when that place is still on a screen
    const QJsonObject w = AppSettings::read().value("window").toObject();
    QRect geo(0, 0, std::max(800, w.value("width").toInt(1200)), std::max(600, w.value("height").toInt(800)));
    if (w.contains("x")) {
        geo.moveTo(w.value("x").toInt(), w.value("y").toInt());
        bool onScreen = false;
        for (QScreen *s : QGuiApplication::screens()) onScreen |= s->availableGeometry().intersects(geo.adjusted(100, 40, -100, -40));
        if (!onScreen) geo.moveTo(0, 0);
        setGeometry(geo);
    } else {
        resize(geo.size());
    }
    // a look at library.json now and then, for the other device's shelves
    connect(&m_libraryLook, &QTimer::timeout, this, [this] {
        if (m_views->currentWidget() == m_shelf && isActiveWindow()) m_app->refreshLibrary();
    });
    m_libraryLook.start(30000);
    if (!m_app->library().value("firstRunDone").toBool()) QTimer::singleShot(0, this, &MainWindow::firstRun);
    // one zip of the whole library a day, the last fourteen kept
    QTimer::singleShot(3000, this, [this] { m_app->lib().dailyBackup(); });
}

void MainWindow::applyTheme()
{
    setTheme(m_app->library().value("pageTheme").toString("night"));
    qApp->setStyleSheet(theme().styleSheet());
    QPalette pal = qApp->palette();
    pal.setColor(QPalette::Window, theme().bg);
    pal.setColor(QPalette::Base, theme().bg);
    pal.setColor(QPalette::Text, theme().text);
    pal.setColor(QPalette::WindowText, theme().text);
    pal.setColor(QPalette::Highlight, QColor(201, 168, 106, 120));
    qApp->setPalette(pal);
}

void MainWindow::setLibraryValue(const QString &key, const QJsonValue &v)
{
    if (v.isNull() || v.isUndefined()) m_app->library().remove(key);
    else m_app->library().insert(key, v);
    m_app->writeLibrary();
}

void MainWindow::buildMenus()
{
    menuBar()->clear();
    const QJsonObject lib = m_app->library();
    auto add = [](QMenu *m, const QString &label, const QKeySequence &key, std::function<void()> fn) {
        QAction *a = m->addAction(label);
        if (!key.isEmpty()) a->setShortcut(key);
        QObject::connect(a, &QAction::triggered, a, fn);
        return a;
    };
    const bool script = m_app->session() && isScript(m_app->session()->book());

    QMenu *file = menuBar()->addMenu(t("File"));
    QMenu *exp = file->addMenu(t("Export"));
    if (script) {
        add(exp, "PDF (.pdf)", {}, [this] { exportBook("pdf"); });
        add(exp, "Fountain (.fountain)", {}, [this] { exportBook("fountain"); });
        add(exp, "Final Draft (.fdx)", {}, [this] { exportBook("fdx"); });
    } else {
        add(exp, t("Plain Text (.txt)"), {}, [this] { exportBook("txt"); });
        add(exp, "Markdown (.md)", {}, [this] { exportBook("md"); });
        add(exp, t("Web Page (.html)"), {}, [this] { exportBook("html"); });
        add(exp, "PDF (.pdf)", {}, [this] { exportBook("pdf"); });
        add(exp, "Word (.docx)", {}, [this] { exportBook("docx"); });
        add(exp, "EPUB (.epub)", {}, [this] { exportBook("epub"); });
        exp->addSeparator();
        QAction *titles = exp->addAction(t("Chapter Titles Only"));
        titles->setCheckable(true);
        titles->setChecked(lib.value("exportCustomChapterTitles").toBool());
        connect(titles, &QAction::toggled, this, [this](bool on) { setLibraryValue("exportCustomChapterTitles", on); });
    }
    file->addSeparator();
    add(file, t("Goals…"), QKeySequence("Ctrl+,"), [this] { openGoals(); });
    QMenu *style = file->addMenu(t("New Books Open To"));
    auto *styleGroup = new QActionGroup(style);
    for (const auto &[label, value] : QList<QPair<QString, QString>>{{t("Blank Page"), "pantser"}, {t("Outline First"), "plotter"}}) {
        QAction *a = style->addAction(label);
        a->setCheckable(true);
        a->setChecked((lib.value("writingStyle").toString() == "plotter") == (value == "plotter"));
        styleGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, value] { setLibraryValue("writingStyle", value); });
    }
    file->addSeparator();
    add(file, t("Import Manuscripts…"), QKeySequence("Ctrl+Shift+I"), [this] { importBooks(); });
    add(file, t("Reshelve a Book…"), {}, [this] { reshelve(); });
    add(file, t("Library Folder…"), {}, [this] { chooseLibraryFolder(); });
    file->addSeparator();
    add(file, t("Quit"), QKeySequence::Quit, [this] { close(); });

    QMenu *editMenu = menuBar()->addMenu(t("Edit"));
    // the standard items act as their keys would in whatever has the caret
    // (a chapter's own Ctrl+Z, a card's, a field's); outside text, Ctrl+Z
    // takes back the last change to the book's structure
    auto keyTo = [this](const QKeySequence &seq) {
        QWidget *w = QApplication::focusWidget();
        const QKeyCombination k = seq[0];
        if (w) {
            QKeyEvent press(QEvent::KeyPress, int(k.key()), k.keyboardModifiers());
            QApplication::sendEvent(w, &press);
            QKeyEvent release(QEvent::KeyRelease, int(k.key()), k.keyboardModifiers());
            QApplication::sendEvent(w, &release);
            if (press.isAccepted()) return;
        }
        if (seq == QKeySequence::Undo && m_views->currentWidget() == m_editor) m_editor->structuralUndo();
    };
    for (const auto &[label, std] : QList<QPair<QString, QKeySequence::StandardKey>>{{t("Undo"), QKeySequence::Undo}, {t("Redo"), QKeySequence::Redo}}) {
        QAction *a = editMenu->addAction(label);
        a->setShortcut(std);
        connect(a, &QAction::triggered, this, [keyTo, std] { keyTo(QKeySequence(std)); });
    }
    editMenu->addSeparator();
    for (const auto &[label, std] : QList<QPair<QString, QKeySequence::StandardKey>>{{t("Cut"), QKeySequence::Cut}, {t("Copy"), QKeySequence::Copy}, {t("Paste"), QKeySequence::Paste}}) {
        QAction *a = editMenu->addAction(label);
        a->setShortcut(std);
        connect(a, &QAction::triggered, this, [keyTo, std] { keyTo(QKeySequence(std)); });
    }
    add(editMenu, t("Paste and Match Style"), QKeySequence("Ctrl+Shift+V"), [] {
        QWidget *w = QApplication::focusWidget();
        if (auto *c = qobject_cast<ChapterEdit *>(w)) c->pastePlain();
        else if (auto *te = qobject_cast<QTextEdit *>(w)) te->insertPlainText(QGuiApplication::clipboard()->text());
        else if (auto *le = qobject_cast<QLineEdit *>(w)) le->insert(QGuiApplication::clipboard()->text());
    });
    QAction *selectAll = editMenu->addAction(t("Select All"));
    selectAll->setShortcut(QKeySequence::SelectAll);
    connect(selectAll, &QAction::triggered, this, [keyTo] { keyTo(QKeySequence(QKeySequence::SelectAll)); });
    editMenu->addSeparator();
    add(editMenu, t("Find & Replace").replace('&', "&&"), QKeySequence("Ctrl+F"), [this] {
        if (m_views->currentWidget() == m_editor) QMetaObject::invokeMethod(m_editor, "openSearch");
    });
    add(editMenu, t("Spellcheck Pass"), QKeySequence("Ctrl+;"), [this] {
        if (m_views->currentWidget() == m_editor) m_editor->toggleSpellcheck();
    });
    QMenu *spellMenu = editMenu->addMenu(t("Spellcheck Language"));
    auto *spellGroup = new QActionGroup(spellMenu);
    for (const spell::Language &l : spell::languages()) {
        QAction *a = spellMenu->addAction(l.label);
        a->setCheckable(true);
        a->setChecked(m_app->spellLanguage() == l.code);
        spellGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, code = l.code] { changeSpellLanguage(code); });
    }
    editMenu->addSeparator();
    add(editMenu, t("Placeholder"), QKeySequence("Ctrl+Shift+X"), [this] {
        if (m_views->currentWidget() == m_editor && m_editor->tab() == "manuscript") m_editor->insertPlaceholder();
    });

    QMenu *format = menuBar()->addMenu(t("Format"));
    if (!script) {
        QMenu *bodyFont = format->addMenu(t("Body Font"));
        auto *fontGroup = new QActionGroup(bodyFont);
        const QString cur = lib.value("fonts").toObject().value("body").toString();
        for (const QString &f : bodyFontChoices()) {
            QAction *a = bodyFont->addAction(f);
            a->setCheckable(true);
            a->setChecked(cur == f || (cur.isEmpty() && f == "Gelasio"));
            fontGroup->addAction(a);
            connect(a, &QAction::triggered, this, [this, f] {
                QJsonObject fonts = m_app->library().value("fonts").toObject();
                fonts.insert("body", f);
                setLibraryValue("fonts", fonts);
                m_editor->applyLook();
            });
        }
        QMenu *caps = format->addMenu(t("Drop Cap Style"));
        auto *capGroup = new QActionGroup(caps);
        const QString curCap = lib.value("fonts").toObject().value("dropcap").toString("literary");
        for (const auto &[label, value] : QList<QPair<QString, QString>>{{t("Literary"), "literary"}, {t("Fantasy"), "fantasy"}, {t("Sci-Fi"), "scifi"}, {t("Off"), "none"}}) {
            if (value == "none") caps->addSeparator();
            QAction *a = caps->addAction(label);
            a->setCheckable(true);
            a->setChecked(curCap == value);
            capGroup->addAction(a);
            connect(a, &QAction::triggered, this, [this, value] {
                QJsonObject fonts = m_app->library().value("fonts").toObject();
                fonts.insert("dropcap", value);
                setLibraryValue("fonts", fonts);
                m_editor->applyLook();
            });
        }
        QMenu *align = format->addMenu(t("Align Paragraph"));
        for (const auto &[label, key, value] : QList<std::tuple<QString, QString, Qt::Alignment>>{
                 {t("Left"), "Ctrl+Shift+L", Qt::AlignLeft}, {t("Center"), "Ctrl+Shift+C", Qt::AlignHCenter},
                 {t("Right"), "Ctrl+Shift+R", Qt::AlignRight}, {t("Justify"), "Ctrl+Shift+J", Qt::AlignJustify}}) {
            add(align, label, QKeySequence(key), [this, value] {
                if (ChapterEdit *e = m_editor->editorFor(m_editor->currentChapter())) {
                    QTextCursor c = e->textCursor();
                    m_editor->snapshot("align", false);
                    edit::setAlignment(c, value);
                    e->setTextCursor(c);
                }
            });
        }
    }
    format->addSeparator();
    add(format, t("Larger Text"), QKeySequence("Ctrl+="), [this] {
        if (m_editor->zoomCards(1)) return;
        setLibraryValue("editorFontSize", std::min(22, m_app->library().value("editorFontSize").toInt(17) + 1));
        m_editor->applyLook();
    });
    add(format, t("Smaller Text"), QKeySequence("Ctrl+-"), [this] {
        if (m_editor->zoomCards(-1)) return;
        setLibraryValue("editorFontSize", std::max(14, m_app->library().value("editorFontSize").toInt(17) - 1));
        m_editor->applyLook();
    });
    add(format, t("Reset Text Size"), QKeySequence("Ctrl+0"), [this] {
        if (m_editor->zoomCards(0)) return;
        setLibraryValue("editorFontSize", 17);
        setLibraryValue("pageZoom", 1);
        m_editor->applyLook();
    });
    format->addSeparator();
    QAction *typewriter = add(format, t("Typewriter Scrolling"), QKeySequence("Ctrl+Shift+T"), [this] { m_editor->modes()->toggleTypewriter(); });
    typewriter->setCheckable(true);
    connect(format, &QMenu::aboutToShow, typewriter, [this, typewriter] { typewriter->setChecked(m_editor->modes()->typewriter()); });
    if (!script) {
        format->addSeparator();
        add(format, t("Flush Paragraph") + "\tShift+Enter", {}, [this] {
            if (ChapterEdit *e = m_editor->editorFor(m_editor->currentChapter())) {
                QTextCursor c = e->textCursor();
                edit::Context ctx;
                ctx.snapshot = [this](const QString &l, bool r) { m_editor->snapshot(l, r); };
                edit::toggleParaKind(c, "flush", ctx);
                e->setTextCursor(c);
                e->resetUndo();
            }
        });
        add(format, t("Poetry Paragraph") + "\tCtrl+Shift+Enter", {}, [this] {
            if (ChapterEdit *e = m_editor->editorFor(m_editor->currentChapter())) {
                QTextCursor c = e->textCursor();
                edit::Context ctx;
                ctx.snapshot = [this](const QString &l, bool r) { m_editor->snapshot(l, r); };
                edit::toggleParaKind(c, "poetry", ctx);
                e->setTextCursor(c);
                e->resetUndo();
            }
        });
    }
    format->addSeparator();
    QAction *md = format->addAction(t("Markdown Emphasis"));
    md->setCheckable(true);
    md->setChecked(!lib.value("markdownOff").toBool());
    connect(md, &QAction::toggled, this, [this](bool on) {
        setLibraryValue("markdownOff", on ? QJsonValue() : QJsonValue(true));
        m_toast->show(on ? t("Markdown emphasis on: *italic*, **bold**") : t("Markdown emphasis off: asterisks stay asterisks"));
    });

    QMenu *view = menuBar()->addMenu(t("View"));
    add(view, t("Keyboard Shortcuts…"), QKeySequence("Ctrl+/"), [this] { showHelp(); });
    view->addSeparator();
    add(view, t("Full Screen"), QKeySequence("Ctrl+Shift+F"), [this] { isFullScreen() ? showNormal() : showFullScreen(); });
    QMenu *focusMenu = view->addMenu(t("Focus Mode"));
    add(focusMenu, t("Cycle"), QKeySequence("Ctrl+Shift+O"), [this] {
        if (m_views->currentWidget() == m_editor) m_editor->modes()->cycleFocus();
    });
    focusMenu->addSeparator();
    auto *focusGroup = new QActionGroup(focusMenu);
    for (const auto &[label, value] : QList<QPair<QString, QString>>{{t("Sentence"), "sentence"}, {t("Paragraph"), "paragraph"}, {t("Off"), "off"}}) {
        QAction *a = focusMenu->addAction(label);
        a->setCheckable(true);
        focusGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, value] { m_editor->modes()->setFocusLevel(value); });
    }
    // the ticks follow the level, however it was set
    connect(focusMenu, &QMenu::aboutToShow, this, [this, focusGroup] {
        for (QAction *a : focusGroup->actions()) a->setChecked(a->text() == (m_editor->modes()->focusLevel() == "sentence" ? t("Sentence") : m_editor->modes()->focusLevel() == "paragraph" ? t("Paragraph") : t("Off")));
    });
    add(view, t("Read Aloud"), QKeySequence("Ctrl+Shift+U"), [this] {
        if (m_views->currentWidget() == m_editor && m_app->session()) m_editor->readAloud()->toggle();
    });
    QAction *vimAction = add(view, t("Vim Keys"), QKeySequence(), [this] { m_editor->vim()->toggle(); });
    vimAction->setCheckable(true);
    connect(view, &QMenu::aboutToShow, vimAction, [this, vimAction] { vimAction->setChecked(m_editor->vim()->enabled()); });
    view->addSeparator();
    QMenu *page = view->addMenu(t("Page"));
    auto *pageGroup = new QActionGroup(page);
    for (const auto &[label, value] : QList<QPair<QString, QString>>{{t("Night"), "night"}, {t("Paper"), "paper"}, {t("Light"), "light"}}) {
        QAction *a = page->addAction(label);
        a->setCheckable(true);
        a->setChecked(lib.value("pageTheme").toString("night") == value);
        pageGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, value] {
            setLibraryValue("pageTheme", value);
            applyTheme();
            m_shelf->rebuild();
            m_editor->applyLook();
            m_editor->update();
        });
    }
    view->addSeparator();
    QMenu *lang = view->addMenu(t("Language"));
    auto *langGroup = new QActionGroup(lang);
    for (const Language &l : I18n::languages(resourcesDir() + "/locales")) {
        QAction *a = lang->addAction(l.name);
        a->setCheckable(true);
        a->setChecked(I18n::locale() == l.code);
        langGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, code = l.code] {
            AppSettings::set("uiLanguage", code);
            m_toast->show(t("The new language takes over when neosea next opens."), 6000);
        });
    }

    QMenu *help = menuBar()->addMenu(t("Help"));
    add(help, t("NEO Shortcuts"), {}, [this] { showHelp(); });
    help->addSeparator();
    add(help, t("About NEO").replace("NEO", "neosea"), {}, [this] { showAbout(); });
}

void MainWindow::openBook(const QString &bookId)
{
    if (!m_app->openBook(bookId)) {
        m_toast->show(t("Couldn’t open that book"));
        return;
    }
    const auto meta = m_app->session()->book();
    setWindowTitle(meta.value("title").toString() + " — neosea");
    m_views->setCurrentWidget(m_editor);
    m_editor->openSession();
    buildMenus();
}

void MainWindow::backToShelf()
{
    m_editor->closeSession();
    m_app->closeBook();
    setWindowTitle("neosea");
    m_views->setCurrentWidget(m_shelf);
    m_shelf->rebuild();
    buildMenus();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    m_editor->flush(true);
    if (!isFullScreen() && !isMinimized()) {
        const QRect g = normalGeometry();
        AppSettings::set("window", QJsonObject{{"x", g.x()}, {"y", g.y()}, {"width", g.width()}, {"height", g.height()}});
    }
    m_app->closeBook();
    e->accept();
}

void MainWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    if (e->type() != QEvent::ActivationChange) return;
    if (!isActiveWindow()) {
        // flush whenever focus leaves the window
        if (m_app->session()) m_editor->flush(true);
        return;
    }
    // back in view: a look at what the other device wrote
    QTimer::singleShot(300, this, [this] {
        if (m_app->session()) m_editor->refreshFromDisk();
        else m_app->refreshLibrary();
    });
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    if (e->key() == Qt::Key_Escape) {
        if (isFullScreen()) showNormal();
        else if (m_views->currentWidget() == m_editor) backToShelf();
        return;
    }
    if (ctrl && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
        isFullScreen() ? showNormal() : showFullScreen();
        return;
    }
    if (m_views->currentWidget() == m_editor && ctrl) {
        if ((e->modifiers() & Qt::AltModifier) && (e->key() == Qt::Key_Down || e->key() == Qt::Key_Up)) {
            m_editor->gotoChapter(e->key() == Qt::Key_Down ? 1 : -1);
            return;
        }
        // outside the text, Ctrl+Z belongs to the structure
        if (e->key() == Qt::Key_Z && !(e->modifiers() & Qt::ShiftModifier)) {
            m_editor->structuralUndo();
            return;
        }
        if (e->key() == Qt::Key_PageDown || e->key() == Qt::Key_PageUp) {
            m_editor->gotoChapter(e->key() == Qt::Key_PageDown ? 1 : -1);
            return;
        }
    }
    QMainWindow::keyPressEvent(e);
}

QString MainWindow::saveTarget(const QString &defaultName, const QString &format)
{
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QFileDialog::getSaveFileName(this, t("Export"), QDir(docs).filePath(defaultName + "." + format),
                                        format.toUpper() + " (*." + format + ")");
}

void MainWindow::exportBook(const QString &format, const QString &chId)
{
    BookSession *s = m_app->session();
    if (!s) {
        m_toast->show(t("Open a book first"));
        return;
    }
    m_editor->flush();
    QJsonObject &book = s->book();
    const QString title = book.value("title").toString();
    QByteArray bytes;
    QString defaultName = safeName(title);
    if (isScript(book)) {
        QList<Para> paras;
        for (const QString &c : s->order()) paras << parseChapter(s->chapterHtml(c));
        sp::TitlePage tp{isUntitled(title) ? QString() : title, book.contains("credit") ? book.value("credit").toString() : t("Written by"),
                         book.value("author").toString(), book.value("draft").toString(), m_app->library().value("scriptContact").toString()};
        const QList<sp::PrintLine> lines = sp::printLines(paras);
        QString f = format == "pdf" || format == "fdx" ? format : "fountain";
        if (f == "pdf") bytes = sp::buildScriptPdf(lines, tp);
        else if (f == "fdx") {
            QList<sp::FdxLine> fl;
            for (const auto &l : lines) fl << sp::FdxLine{l.type, l.runs};
            bytes = sp::toFdx(fl, tp).toUtf8();
        } else {
            QList<sp::Line> fl;
            for (const auto &l : lines) fl << sp::Line{l.type, sp::fountainOfRuns(l.runs)};
            bytes = sp::toFountain(fl, tp).toUtf8();
        }
        const QString path = saveTarget(defaultName, f);
        if (path.isEmpty()) return;
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size()) {
            m_toast->show(t("Couldn’t export: {error}", {{"error", out.errorString()}}), 8000);
            return;
        }
        m_toast->show(t("Exported: {file}", {{"file", QFileInfo(path).fileName()}}));
        return;
    }
    ExportData d;
    if (bookExportData(book, s->allChapters(), m_app->library().value("exportCustomChapterTitles").toBool(), m_app->writingLanguage(), d))
        s->saveMeta();
    if (!chId.isEmpty()) {
        auto one = chapterExportData(d, chId);
        if (!one) return;
        d = *one;
        defaultName += "-" + safeName(d.chapterOnly);
    }
    const QJsonObject fonts = m_app->library().value("fonts").toObject();
    HtmlOptions o;
    o.bodyFont = bodyFontFamily(fonts.value("body").toString());
    o.dropCapFont = dropCapFamily(fonts.value("dropcap").toString("literary"));
    o.customTitlesOnly = m_app->library().value("exportCustomChapterTitles").toBool();
    // the cover that travels: the writer's own image, else the abstract with its type
    CoverImage cover;
    {
        QImage art;
        if (!book.value("coverImage").toString().isEmpty()) art.load(m_app->lib().coverPath(s->id(), book.value("coverImage").toString()));
        QBuffer buf(&cover.bytes);
        buf.open(QIODevice::WriteOnly);
        covers::renderFull(book, art).save(&buf, "JPG", 90);
    }
    if (format == "txt") bytes = buildTxt(d).toUtf8();
    else if (format == "md") bytes = buildMd(d).toUtf8();
    else if (format == "docx") bytes = buildDocx(d);
    else if (format == "epub") bytes = buildEpub(d, cover);
    else if (format == "html") {
        if (chId.isEmpty()) {
            o.coverBytes = cover.bytes;
            o.coverMime = cover.mime;
        }
        bytes = buildHtml(d, o).toUtf8();
    } else if (format == "pdf") {
        if (chId.isEmpty()) {
            o.coverBytes = cover.bytes;
            o.coverMime = cover.mime;
        }
        QString err;
        bytes = buildPdf(d, o, &err);
    }
    const QString path = saveTarget(defaultName, format);
    if (path.isEmpty()) return;
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size()) {
        m_app->lib().logError("export " + format, out.errorString());
        m_toast->show(t("Couldn’t export: {error}", {{"error", out.errorString()}}), 8000);
        return;
    }
    m_toast->show(t("Exported: {file}", {{"file", QFileInfo(path).fileName()}}));
}

void MainWindow::exportShelf(const QString &shelfId, bool bound)
{
    QJsonObject shelf = shelves::shelf(m_app->library(), shelfId);
    QString title = shelf.value("name").toString();
    if (!bound) {
        if (shelves::bookIds(shelf).isEmpty()) {
            m_toast->show(t("This shelf has no books on it yet"));
            return;
        }
        auto name = askInput(this, t("Anthology title"), t("Shown on the title page, cover, and metadata"), title);
        if (!name) return;
        if (!name->isEmpty()) title = *name;
    }
    const QVariant fmt = optionModal(this, bound ? t("Export “{title}”", {{"title", title}}) : t("Export the anthology as…"), {},
                                     {{"EPUB", t("For ebook stores — the TOC lists every story."), "epub"},
                                      {"Word (.docx)", t("For editors — each story starts on a new page."), "docx"},
                                      {"PDF", t("For reading, sharing, and print."), "pdf"}});
    if (!fmt.isValid()) return;
    m_toast->show(t("Collecting the shelf…"));
    ExportData d = shelfBookData(m_app->lib(), shelf, m_app->library(), bound, title, m_app->writingLanguage());
    if (bound) {
        shelves::putShelf(m_app->library(), shelf); // the binding's uuid
        m_app->writeLibrary();
    }
    if (d.sections.isEmpty()) {
        m_toast->show(t("No words found on this shelf yet"));
        return;
    }
    QJsonObject coverMeta{{"id", d.id}, {"title", d.title}, {"author", d.author}, {"coverSeed", d.coverSeed}};
    QImage art;
    if (!d.coverImage.isEmpty()) art.load(m_app->lib().coverPath(d.id, d.coverImage));
    CoverImage cover;
    QBuffer buf(&cover.bytes);
    buf.open(QIODevice::WriteOnly);
    covers::renderFull(coverMeta, art).save(&buf, "JPG", 90);
    QByteArray bytes;
    const QString f = fmt.toString();
    if (f == "docx") bytes = buildDocx(d);
    else if (f == "epub") bytes = buildEpub(d, cover);
    else {
        HtmlOptions o;
        const QJsonObject fonts = m_app->library().value("fonts").toObject();
        o.bodyFont = bodyFontFamily(fonts.value("body").toString());
        o.dropCapFont = dropCapFamily(fonts.value("dropcap").toString("literary"));
        o.coverBytes = cover.bytes;
        o.coverMime = cover.mime;
        bytes = buildPdf(d, o);
    }
    const QString path = saveTarget(safeName(d.title), f);
    if (path.isEmpty()) return;
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size()) {
        m_toast->show(t("Couldn’t export: {error}", {{"error", out.errorString()}}), 8000);
        return;
    }
    m_toast->show(bound ? t("Exported: {file}", {{"file", QFileInfo(path).fileName()}})
                        : t("Anthology of {n} works exported: {file}", {{"n", int(shelves::bookIds(shelf).size())}, {"file", QFileInfo(path).fileName()}}),
                  6000);
}

void MainWindow::importBooks()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, t("Bring your manuscripts home"), QDir::homePath(),
                                                            t("Manuscripts") + " (*.docx *.txt *.md *.fountain *.fdx)");
    if (paths.isEmpty()) return;
    const auto mine = m_app->myShelves();
    const QString shelf = mine.isEmpty() ? QString() : mine.first().value("id").toString();
    const App::ImportOutcome r = m_app->importFiles(paths, shelf);
    for (const QString &e : r.errors) m_toast->show(t("Couldn’t import {name}: {error}", {{"name", e.section(": ", 0, 0)}, {"error", e.section(": ", 1)}}), 6000);
    const QString shelfName = shelves::shelf(m_app->library(), shelf).value("name").toString();
    if (r.books) m_toast->show(t("{n} books imported onto “{shelf}” — chapters and scene breaks detected", {{"n", r.books}, {"shelf", shelfName}}), 6000);
    else if (r.scripts) m_toast->show(t("{n} scripts imported onto “{shelf}”", {{"n", r.scripts}, {"shelf", shelfName}}), 6000);
}

void MainWindow::reshelve()
{
    QSet<QString> shelved;
    for (const auto &v : m_app->library().value("shelves").toArray()) {
        for (const QString &id : shelves::bookIds(v.toObject())) shelved.insert(id);
        for (const auto &p : v.toObject().value("binding").toObject().value("parked").toArray()) shelved.insert(p.toObject().value("id").toString());
    }
    QList<BookListing> loose;
    for (const BookListing &b : m_app->lib().listBooks())
        if (!shelved.contains(b.id) && b.kind.isEmpty()) loose << b;
    std::sort(loose.begin(), loose.end(), [](const BookListing &a, const BookListing &b) { return a.modified > b.modified; });
    if (loose.isEmpty()) {
        m_toast->show(t("Every book in your library is already on a shelf"));
        return;
    }
    QList<Choice> choices;
    for (const BookListing &b : loose) choices << Choice{b.title, b.author.isEmpty() ? QString() : t("by {author}", {{"author", b.author}}), b.id};
    const QString pick = optionModal(this, t("Books in your library that aren’t on a shelf"), {}, choices).toString();
    if (pick.isEmpty()) return;
    const auto mine = m_app->myShelves();
    shelves::placeTitle(m_app->library(), mine.first().value("id").toString(), pick, m_app->kindOf());
    m_app->writeLibrary();
    m_shelf->rebuild();
    for (const BookListing &b : loose)
        if (b.id == pick) m_toast->show(t("“{title}” is back on the shelf", {{"title", b.title}}));
}

void MainWindow::chooseLibraryFolder()
{
    const QString cur = m_app->lib().dir();
    const bool custom = cur != AppSettings::defaultLibraryDir();
    QList<Choice> choices{{t("Choose Folder…"), {}, "choose"}};
    if (custom) choices << Choice{t("Use Default Folder"), {}, "default"};
    const QString c = optionModal(this, t("Library folder"),
                                  t("Your books live in:\n{dir}\n\nChoose another folder and NEO restarts there. Existing books stay where they are — move the files yourself if you want them along.", {{"dir", cur}}),
                                  choices).toString();
    if (c.isEmpty()) return;
    if (c == "choose") {
        const QString dir = QFileDialog::getExistingDirectory(this, t("Choose a folder for your NEO library"), cur);
        if (dir.isEmpty() || dir == cur) return;
        AppSettings::set("libraryDir", dir);
    } else {
        AppSettings::set("libraryDir", QJsonValue());
    }
    m_editor->flush(true);
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
    qApp->quit();
}

void MainWindow::openGoals()
{
    neosea::openGoals(this, m_app, m_app->session() ? m_editor : nullptr);
}

void MainWindow::showHelp()
{
    const QList<QPair<QString, QString>> keys{
        {"Enter ×2", t("Section break")},           {"Enter ×3", t("New chapter")},
        {"Shift+Enter", t("Flush Paragraph")},      {"Ctrl+Shift+Enter", t("Poetry Paragraph")},
        {"Ctrl+Shift+X", t("Placeholder")},         {"Ctrl+Alt+↑/↓", t("Previous / next chapter")},
        {"Ctrl+Enter", t("Full Screen")},           {"Ctrl+F", t("Find & Replace")},
        {"Ctrl+,", t("Goals…")},                    {"Esc", t("Back to your bookshelf")},
    };
    QString text;
    for (const auto &[k, v] : keys) text += QStringLiteral("<tr><td style='color:#c9a86a;padding-right:18px'>%1</td><td>%2</td></tr>").arg(k.toHtmlEscaped(), v.toHtmlEscaped());
    QDialog d(this, Qt::Dialog | Qt::FramelessWindowHint);
    d.setStyleSheet(QStringLiteral("QDialog { background: %1; border: 1px solid #333; border-radius: 12px; }").arg(theme().bgSoft.name()));
    auto *col = new QVBoxLayout(&d);
    col->setContentsMargins(30, 26, 30, 22);
    auto *h = new QLabel(t("Keyboard Shortcuts…").remove("…"), &d);
    h->setStyleSheet(QStringLiteral("color: %1; font-size: 16px;").arg(theme().accent.name()));
    col->addWidget(h);
    auto *body = new QLabel("<table>" + text + "</table>", &d);
    col->addWidget(body);
    auto *ok = new QPushButton(t("OK"), &d);
    ok->setObjectName("gold");
    connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
    col->addWidget(ok, 0, Qt::AlignRight);
    d.exec();
}

void MainWindow::showAbout()
{
    optionModal(this, "neosea", t("Version {version}", {{"version", QString::fromLatin1(kVersionName)}}) + "\n" + t("A word processor for authors."), {});
}

void MainWindow::firstRun()
{
    QDialog d(this, Qt::Dialog | Qt::FramelessWindowHint);
    d.setModal(true);
    d.setFixedWidth(480);
    d.setStyleSheet(QStringLiteral("QDialog { background: %1; border: 1px solid #333; border-radius: 12px; }").arg(theme().bgSoft.name()));
    auto *col = new QVBoxLayout(&d);
    col->setContentsMargins(38, 34, 38, 30);
    col->setSpacing(10);
    auto *h = new QLabel(t("Welcome to NEO").replace("NEO", "neosea"), &d);
    h->setStyleSheet(QStringLiteral("color: %1; font-size: 18px;").arg(theme().accent.name()));
    col->addWidget(h);
    auto *intro = new QLabel(t("NEO knows you're writing books. A few quick questions and it will never ask anything again.").replace("NEO", "neosea"), &d);
    intro->setWordWrap(true);
    col->addWidget(intro);
    auto *nameL = new QLabel(t("Your name") + "  <span style='color:#8a8a8a;font-size:12px'>" + t("(appears as the author on every document — leave blank for “Anonymous”)") + "</span>", &d);
    nameL->setWordWrap(true);
    col->addWidget(nameL);
    auto *name = new QLineEdit(&d);
    col->addWidget(name);
    auto *penL = new QLabel(t("Pen name") + "  <span style='color:#8a8a8a;font-size:12px'>" + t("(optional — used on title pages if set)") + "</span>", &d);
    penL->setWordWrap(true);
    col->addWidget(penL);
    auto *pen = new QLineEdit(&d);
    col->addWidget(pen);
    col->addSpacing(8);
    col->addWidget(new QLabel(t("Are you a pantser or a plotter?"), &d));
    auto *row = new QHBoxLayout;
    QString style;
    for (const auto &[label, desc, value] : QList<std::tuple<QString, QString, QString>>{
             {t("Pantser"), t("I write by the seat of my pants. New books open on a blank page."), "pantser"},
             {t("Plotter"), t("I outline first. New books open in the Outline tab."), "plotter"}}) {
        auto *b = new QPushButton(&d);
        b->setObjectName("choice");
        auto *bl = new QVBoxLayout(b);
        auto *l1 = new QLabel(label, b);
        l1->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(theme().accent.name()));
        auto *l2 = new QLabel(desc, b);
        l2->setWordWrap(true);
        l2->setStyleSheet("color: #8a8a8a; font-size: 12px;");
        for (QLabel *l : {l1, l2}) l->setAttribute(Qt::WA_TransparentForMouseEvents);
        bl->addWidget(l1);
        bl->addWidget(l2);
        b->setMinimumHeight(bl->sizeHint().height() + 8);
        connect(b, &QPushButton::clicked, &d, [&d, &style, v = value] {
            style = v;
            d.accept();
        });
        row->addWidget(b);
    }
    col->addLayout(row);
    d.exec();
    QJsonObject &lib = m_app->library();
    lib.insert("authorName", name->text().trimmed());
    lib.insert("penNames", pen->text().trimmed().isEmpty() ? QJsonArray{} : QJsonArray{pen->text().trimmed()});
    lib.insert("writingStyle", style.isEmpty() ? "pantser" : style);
    QJsonObject fonts = lib.value("fonts").toObject();
    if (!fonts.contains("body")) fonts.insert("body", "Gelasio");
    if (!fonts.contains("dropcap")) fonts.insert("dropcap", "literary");
    lib.insert("fonts", fonts);
    lib.insert("firstRunDone", true);
    QString shown = name->text().trimmed();
    if (shown.isEmpty()) shown = pen->text().trimmed();
    if (shown.isEmpty()) shown = t("Anonymous");
    QJsonArray authors = lib.value("authors").toArray();
    if (!authors.isEmpty()) {
        QJsonObject a = authors.first().toObject();
        a.insert("name", shown);
        authors[0] = a;
        lib.insert("authors", authors);
    }
    m_app->writeLibrary();
    m_shelf->rebuild();
    buildMenus();
}

void MainWindow::changeSpellLanguage(const QString &code)
{
    // Edit → Spellcheck Language: the dictionary changes, the choice stays with the library
    if (spell::dictionaryBase(resourcesDir(), code).isEmpty()) {
        emit m_app->toast(t("That dictionary would not load"));
        return;
    }
    m_app->setSpellLanguage(code);
    static const QHash<QString, const char *> names{{"en-US", "US English"}, {"en-GB", "UK English"}, {"en-CA", "Canadian English"}, {"en-AU", "Australian English"},
                                                    {"fr", "French"}, {"es", "Spanish"}, {"de", "German"}, {"nl", "Dutch"}, {"pl", "Polish"},
                                                    {"pt-BR", "Brazilian Portuguese"}, {"ro", "Romanian"}, {"ru", "Russian"}};
    emit m_app->toast(t("Spellcheck: {lang}", {{"lang", names.contains(code) ? t(names.value(code)) : code}}));
}

} // namespace neosea

#include "moc_mainwindow.cpp"
