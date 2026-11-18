#include <about.h>
#include <version.h>

about::about(QWidget *parent) : QDialog(parent) {
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    ui.setupUi(this);
    ui.label_2->setText(QStringLiteral("Version %1 64-Bit").arg(QString::fromLatin1(APP_VERSION_STRING)));
}
