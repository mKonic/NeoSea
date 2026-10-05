#include "core/shelves.h"

#include "print.h"

#include <QJsonArray>

#include <gtest/gtest.h>

using namespace neosea;
using namespace neosea::shelves;

namespace {

QJsonObject library(QJsonArray ids, bool bound = false)
{
    QJsonObject s{{"id", "s1"}, {"name", "Silo"}, {"bookIds", ids}};
    if (bound) s.insert("binding", QJsonObject{{"bound", true}, {"numbering", "through"}});
    return QJsonObject{{"authors", QJsonArray{QJsonObject{{"id", "a1"}, {"name", "Hugh"}}}},
                       {"shelves", QJsonArray{s}}};
}

// cover, dedication, two titles, a part before the second, acknowledgments
KindOf kinds()
{
    return [](const QString &id) -> QString {
        if (id == "cov") return "cover";
        if (id == "ded") return "dedication";
        if (id == "p1") return "part";
        if (id == "ack") return "acknowledgments";
        return {};
    };
}

} // namespace

TEST(Shelves, TitlesStayInsideABoundBooksBody)
{
    QJsonObject lib = library({"cov", "ded", "t1", "ack"}, true);
    placeTitle(lib, "s1", "t2", kinds());
    EXPECT_EQ(bookIds(shelf(lib, "s1")), (QStringList{"cov", "ded", "t1", "t2", "ack"}));
    placeTitle(lib, "s1", "t0", kinds(), true);
    EXPECT_EQ(bookIds(shelf(lib, "s1")), (QStringList{"cov", "ded", "t0", "t1", "t2", "ack"}));
    // dragged to the very front or end, a title stops at the body's edge
    moveBook(lib, "t2", "s1", 0, kinds());
    EXPECT_EQ(bookIds(shelf(lib, "s1")), (QStringList{"cov", "ded", "t2", "t0", "t1", "ack"}));
    moveBook(lib, "t2", "s1", 99, kinds());
    EXPECT_EQ(bookIds(shelf(lib, "s1")).indexOf("t2"), 4);
    // a page keeps its place
    moveBook(lib, "ded", "s1", 4, kinds());
    EXPECT_FALSE(bookIds(shelf(lib, "s1")).contains("ded")); // taken off, not put in the body
}

TEST(Shelves, UnbindParksPagesAndBindPutsThemBack)
{
    QJsonObject lib = library({"cov", "ded", "t1", "p1", "t2", "ack"}, true);
    unbind(lib, "s1", kinds());
    EXPECT_EQ(bookIds(shelf(lib, "s1")), (QStringList{"t1", "t2"}));
    const QJsonArray parked = shelf(lib, "s1").value("binding").toObject().value("parked").toArray();
    ASSERT_EQ(parked.size(), 4);
    EXPECT_EQ(parked[2].toObject().value("before").toString(), "t2"); // the part remembers its title
    // a title added while unbound sits in the body when bound again
    placeTitle(lib, "s1", "t3", kinds());
    bind(lib, "s1", "cov");
    EXPECT_EQ(bookIds(shelf(lib, "s1")), (QStringList{"cov", "ded", "t1", "p1", "t2", "t3", "ack"}));
    EXPECT_TRUE(isBound(shelf(lib, "s1")));
    EXPECT_TRUE(shelf(lib, "s1").value("binding").toObject().value("parked").toArray().isEmpty());
}

TEST(Shelves, NewPagesGoWhereBooksPutThem)
{
    const QStringList ids{"cov", "ded", "t1", "t2", "ack"};
    EXPECT_EQ(pageIndex(ids, "copyright", {}, kinds()), 1); // after the cover, before the dedication
    EXPECT_EQ(pageIndex(ids, "epigraph", {}, kinds()), 2);
    EXPECT_EQ(pageIndex(ids, "about", {}, kinds()), 5);
    EXPECT_EQ(pageIndex(ids, "epilogue", {}, kinds()), 4); // before acknowledgments
    EXPECT_EQ(pageIndex(ids, "part", "t2", kinds()), 3);
    EXPECT_EQ(pageIndex(ids, "part", {}, kinds()), 4);
}

TEST(Shelves, PenNamesOwnTheirShelves)
{
    QJsonObject lib{{"authorName", "Hugh"}, {"shelves", QJsonArray{QJsonObject{{"id", "s1"}, {"name", "WIP"}, {"bookIds", QJsonArray{"b1"}}}}}};
    EXPECT_EQ(currentAuthor(lib, "Anonymous").value("name").toString(), "Hugh"); // seeded from the legacy field
    const QString a2 = addAuthor(lib, "Ann Pen", "Works in Progress");
    EXPECT_EQ(currentAuthorId(lib, "x"), a2);
    EXPECT_EQ(shelvesFor(lib, a2).size(), 1);
    EXPECT_EQ(shelvesFor(lib, "a1").size(), 1); // the unowned shelf is the home name's
    EXPECT_TRUE(removeAuthor(lib, a2));
    EXPECT_EQ(shelvesFor(lib, "a1").size(), 2); // its shelves come home
    EXPECT_FALSE(removeAuthor(lib, "a1"));      // the last name stays
}

TEST(Shelves, DeletingAShelfMovesItsBooks)
{
    QJsonObject lib = library({"t1", "t2"});
    EXPECT_FALSE(deleteShelf(lib, "s1", kinds())); // the only shelf
    const QString s2 = addShelf(lib, "Two", "a1");
    placeTitle(lib, s2, "t9", kinds());
    EXPECT_TRUE(deleteShelf(lib, s2, kinds()));
    EXPECT_EQ(bookIds(shelf(lib, "s1")), (QStringList{"t1", "t2", "t9"}));
}
