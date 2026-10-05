#include "neosea_version.h"

#include <QApplication>
#include <QLabel>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("neosea");
    app.setApplicationVersion(neosea::kVersionName);
    QLabel l("neosea");
    l.show();
    return app.exec();
}
