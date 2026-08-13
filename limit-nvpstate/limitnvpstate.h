#pragma once

#include <ui_limitnvpstate.h>

class QSystemTrayIcon;

class limitnvpstate : public QMainWindow {
    Q_OBJECT

public:
    explicit limitnvpstate(QWidget* parent = nullptr);
    ~limitnvpstate() override;

    // Everything that can fail now lives here rather than in the constructor,
    // so main() can report and return cleanly. The original called exit(1) from
    // inside the constructor on eight different paths, which skips destructors,
    // leaves the tray icon registered and (once past the initial limit) leaves
    // the GPU clamped.
    bool init();

    // Releases the limit on every GPU we may have touched. Safe to call twice.
    void releaseLimits();

    // Ordered teardown: stop triggers, release limits, remove the tray icon,
    // then leave the event loop.
    void shutdown(int exitCode);

private:
    Ui::limitnvpstateClass ui;
    QSystemTrayIcon*       trayIcon = nullptr;

    bool fail(const char* message);
    void createTrayIcon();
    void startTrigger(int trigger);
    void saveProcessExceptions();
    bool getAvailablePStates();

private slots:
    void unlimitTriggerChanged(int index);
    void selectedGPUChanged(int index);
    void selectedPStateChanged(int index);
    void addProcess();
    void removeProcess();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* e) override;
};
