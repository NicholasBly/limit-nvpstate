#pragma once

#include <ui_limitnvpstate.h>

#include <vector>

class QComboBox;
class QSystemTrayIcon;
class QTreeWidgetItem;

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

    // Releases the limit on every GPU we touched and stops any further
    // limiting. Safe to call twice.
    void releaseLimits();

    // Show, un-minimise and focus the window (tray click, second launch).
    void bringToFront();

    // Ordered teardown: stop triggers, release limits, remove the tray icon,
    // then leave the event loop.
    void shutdown(int exitCode);

private:
    Ui::limitnvpstateClass ui;
    QSystemTrayIcon*       trayIcon = nullptr;

    // Limitable P-States of the selected GPU (P0 excluded), in driver order.
    std::vector<int> availablePStates;

    bool fail(const char* message);
    void createTrayIcon();
    void saveProcessExceptions();

    // One exception-list row: the exe name plus its "When behind" combo.
    void addProcessRow(const QString& name, int background);
    void fillBackgroundCombo(QComboBox* combo, int background) const;
    int  backgroundOf(QTreeWidgetItem* item) const;

    // After the GPU changes: rebuild every row's combo for the new GPU's
    // states, snapping values it does not offer. Saves if anything moved.
    void refreshBackgroundCombos();

    // Fills the Limit P-State combo for one GPU and publishes the selection.
    // Returns false (after showing an error) if NVAPI cannot list its states.
    bool getAvailablePStates(int gpuIndex);

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
