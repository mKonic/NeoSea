#include <QGuiApplication>

#include <gtest/gtest.h>

// QTextDocument and fonts need a QGuiApplication; offscreen keeps the tests
// off any display.
int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
