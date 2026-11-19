// Test-only remote control, compiled identically into every variant.
//
// Reads one command per line from %HOOK_CMD% (polled every 50 ms on the GUI
// thread, so it keeps working inside modal loops), drives real widgets
// through their public Qt API (so the app's own slots run), and appends
// "ack <n> <result>" lines to %HOOK_ACK%.
//
//   combo <objectName> <index>     QComboBox::setCurrentIndex
//   click <objectName>             QAbstractButton::click
//   action <objectName>            QAction::trigger (menus)
//   select <objectName> <text>     select the row whose first column is <text>
//   rowcombo <text> <index>        "When behind" combo of that row (tree only)
//   filedialog <path>              arm: the next "Add" file picker returns <path>
//   close                          QWidget::close on the main window
//   trayexit                       tray menu "Exit"
//   dump                           one line describing the UI state
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QDialog>
#include <QSystemTrayIcon>
#include <QTextStream>
#include <QTimer>
#include <QTreeWidget>

namespace {

qint64  cmdOffset = 0;
int     cmdCount  = 0;
QString armedFile;

QWidget* mainWindow() {
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (qobject_cast<QMainWindow*>(w)) return w;
    }
    return nullptr;
}

template <typename T> T* find(const QString& name) {
    QWidget* w = mainWindow();
    return w ? w->findChild<T*>(name) : nullptr;
}

void ack(const QString& text) {
    QFile f(qEnvironmentVariable("HOOK_ACK"));
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream(&f) << "ack " << cmdCount << " " << text << "\n";
    }
}

QString dump() {
    QStringList parts;
    QWidget* w = mainWindow();
    parts << QString("visible=%1").arg(w && w->isVisible());
    parts << QString("version=%1").arg(QCoreApplication::applicationVersion());
    for (const char* name : { "selectedGPU", "selectedPState", "unlimitTrigger" }) {
        if (QComboBox* c = find<QComboBox>(name)) parts << QString("%1=%2").arg(name, c->currentText());
    }
    if (QAction* a = find<QAction>("actionAddToStartup")) parts << QString("startup=%1").arg(a->isChecked());
    QStringList rows;
    if (QListWidget* l = find<QListWidget>("processExceptionsList")) {
        for (int i = 0; i < l->count(); i++) rows << l->item(i)->text();
    } else if (QTreeWidget* t = find<QTreeWidget>("processExceptionsList")) {
        for (int i = 0; i < t->topLevelItemCount(); i++) {
            QTreeWidgetItem* it = t->topLevelItem(i);
            QComboBox* c = qobject_cast<QComboBox*>(t->itemWidget(it, 1));
            rows << it->text(0) + ":" + (c ? c->currentText() : "?");
        }
    }
    parts << "list=[" + rows.join(",") + "]";
    int boxes = 0;
    QString firstText;
    int firstFormat = -1;   // Qt::PlainText 0, RichText 1, AutoText 2
    for (QWidget* tl : QApplication::topLevelWidgets()) {
        QMessageBox* mb = qobject_cast<QMessageBox*>(tl);
        if (mb && mb->isVisible()) {
            if (!boxes) { firstText = mb->text(); firstFormat = mb->textFormat(); }
            boxes++;
        }
    }
    QString progress;
    for (QWidget* tl : QApplication::topLevelWidgets()) {
        QProgressDialog* pd = qobject_cast<QProgressDialog*>(tl);
        if (pd && pd->isVisible()) progress = pd->labelText();
    }
    parts << QString("progress={%1}").arg(progress.simplified());
    parts << QString("msgboxes=%1").arg(boxes);
    parts << QString("msgformat=%1").arg(firstFormat);
    parts << QString("msgtext={%1}").arg(firstText.simplified());
    return parts.join(" ");
}

void selectRow(const QString& name, const QString& text) {
    if (QListWidget* l = find<QListWidget>(name)) {
        l->clearSelection();
        for (QListWidgetItem* it : l->findItems(text, Qt::MatchFixedString)) it->setSelected(true);
    } else if (QTreeWidget* t = find<QTreeWidget>(name)) {
        t->clearSelection();
        for (QTreeWidgetItem* it : t->findItems(text, Qt::MatchFixedString, 0)) it->setSelected(true);
    }
}

void run(const QString& line) {
    const QStringList a = line.split(' ', Qt::SkipEmptyParts);
    if (a.isEmpty()) return;
    cmdCount++;
    const QString& c = a[0];

    // Like a user: a greyed-out control can't be changed.
    if (c == "combo" && a.size() == 3) {
        QComboBox* cb = find<QComboBox>(a[1]);
        if (cb && !cb->isEnabled()) { ack("disabled"); return; }
        ack(cb ? "ok " + cb->itemText(a[2].toInt()) : "missing");   // ack first: the slot may open a box
        if (cb) cb->setCurrentIndex(a[2].toInt());
    } else if (c == "click" && a.size() == 2) {
        QAbstractButton* b = find<QAbstractButton>(a[1]);
        if (b && !b->isEnabled()) { ack("disabled"); return; }
        ack(b ? "ok" : "missing");          // ack first: click may open a modal loop
        if (b) b->click();
    } else if (c == "action" && a.size() == 2) {
        QAction* act = find<QAction>(a[1]);
        ack(act ? "ok" : "missing");
        if (act) act->trigger();
    } else if (c == "select" && a.size() == 3) {
        selectRow(a[1], a[2]);
        ack("ok");
    } else if (c == "rowcombo" && a.size() == 3) {
        QTreeWidget* t = find<QTreeWidget>("processExceptionsList");
        QComboBox* cb = nullptr;
        if (t) for (QTreeWidgetItem* it : t->findItems(a[1], Qt::MatchFixedString, 0)) cb = qobject_cast<QComboBox*>(t->itemWidget(it, 1));
        if (cb) cb->setCurrentIndex(a[2].toInt());
        ack(cb ? "ok " + cb->currentText() : "missing");
    } else if (c == "filedialog" && a.size() == 2) {
        armedFile = a[1];
        ack("ok");
    } else if (c == "close") {
        ack("ok");
        if (QWidget* w = mainWindow()) w->close();
    } else if (c == "trayexit") {
        QSystemTrayIcon* tray = mainWindow() ? mainWindow()->findChild<QSystemTrayIcon*>() : nullptr;
        if (!tray) {
            // the original creates its tray icon without storing it; it is still a child
            for (QObject* o : mainWindow()->children()) if ((tray = qobject_cast<QSystemTrayIcon*>(o))) break;
        }
        QAction* exitAction = nullptr;
        if (tray && tray->contextMenu()) for (QAction* x : tray->contextMenu()->actions()) if (x->text() == "Exit") exitAction = x;
        ack(exitAction ? "ok" : "missing");
        if (exitAction) exitAction->trigger();
    } else if (c == "msgclick" && a.size() >= 2) {
        // Click the button with this text in the visible message box.
        const QString text = QStringList(a.mid(1)).join(' ');
        QAbstractButton* target = nullptr;
        // Any visible dialog: message boxes and the progress dialog's Cancel.
        for (QWidget* tl : QApplication::topLevelWidgets()) {
            QDialog* dlg = qobject_cast<QDialog*>(tl);
            if (dlg && dlg->isVisible())
                for (QAbstractButton* b : dlg->findChildren<QAbstractButton*>()) if (b->text() == text) target = b;
        }
        ack(target ? "ok" : "missing");
        if (target) target->click();
    } else if (c == "dump") {
        ack(dump());
    } else {
        ack("unknown " + line);
    }
}

void poll() {
    QFile f(qEnvironmentVariable("HOOK_CMD"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    if (f.size() <= cmdOffset) return;
    f.seek(cmdOffset);
    const QByteArray chunk = f.readAll();
    const int lastNl = chunk.lastIndexOf('\n');
    if (lastNl < 0) return;
    cmdOffset += lastNl + 1;
    // Run each command from the event loop, not inside this timer: Qt does not
    // re-enter a timer whose handler is still running, so a message box opened
    // by a command would otherwise freeze the remote control until closed.
    for (const QByteArray& l : chunk.left(lastNl).split('\n')) {
        const QString line = QString::fromUtf8(l).trimmed();
        QMetaObject::invokeMethod(qApp, [line]() { run(line); }, Qt::QueuedConnection);
    }
}

void install() {
    if (qEnvironmentVariableIsEmpty("HOOK_CMD")) return;
    // Start after whatever is already in the command file: an instance the
    // updater restarted must not replay its predecessor's commands. Acks keep
    // counting from there, so the runner's numbering still lines up.
    {
        QFile f(qEnvironmentVariable("HOOK_CMD"));
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray existing = f.readAll();
            const int lastNl = existing.lastIndexOf('\n');
            cmdOffset = lastNl + 1;
            cmdCount  = existing.left(lastNl + 1).count('\n');
        }
    }
    QTimer* t = new QTimer(qApp);
    QObject::connect(t, &QTimer::timeout, poll);
    t->start(50);
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(install)

// The file picker froze under Wine, so every variant's "Add" button gets this
// stand-in instead (linked with --allow-multiple-definition; ours comes first).
// It returns whatever "filedialog <path>" armed, once, like a user picking it.
QStringList QFileDialog::getOpenFileNames(QWidget*, const QString&, const QString&, const QString&, QString*, Options) {
    QStringList picked;
    if (!armedFile.isEmpty()) picked << armedFile;
    armedFile.clear();
    return picked;
}
