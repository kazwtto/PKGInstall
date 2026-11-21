#pragma once

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QMainWindow>
#include <atomic>
#include <filesystem>
#include <sstream>
#include <vector>

#include "src/appsettings.h"
#include "src/pkg.h"
#include "src/psf.h"
#include "src/shadps4config.h"

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class UpdateChecker;

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void pkgButtonClicked();
    void folderButtonClicked();
    void dlcButtonClicked();
    void languageChanged(int index);
    void appearanceChanged(Appearance appearance);

private:
    std::optional<std::filesystem::path> FindGameByID(const std::filesystem::path &dir,
                                                      const std::string &game_id,
                                                      int max_depth);
    void InstallDragDropPkg(std::filesystem::path file, bool isZar = false);
    void LoadSettings();
    void SaveSettings();
    void LoadFoldersFromShadps4File();
    void DetectShadps4Folders();
    void ApplyShadps4Config(const Shadps4Config& config);

    void SetupTitleBar();
    void SetupAppearanceMenu();
    void SetupLanguageCombo();
    void SetupInstallOverlay();
    void ShowInstallOverlay(const QString& title, int maxProgress);
    void UpdateInstallProgress(int current, int maxProgress, const QString& statusText = QString());
    void HideInstallOverlay();
    void UpdateGameInfoCard(const std::filesystem::path& file);

    void ApplyAppearance(Appearance appearance);
    void ApplyIcons();
    void RetranslateCustom();

    Ui::MainWindow* ui;

    UiPreferences preferences;
    QMenu* appearanceMenu = nullptr;
    QAction* appearanceHeaderAction = nullptr;
    QAction* appearanceAutoAction = nullptr;
    QAction* appearanceLightAction = nullptr;
    QAction* appearanceDarkAction = nullptr;
    QAction* separateUpdateAction = nullptr;
    QAction* askPatchModeAction = nullptr;
    QAction* autoCheckUpdatesAction = nullptr;
    QAction* checkUpdatesAction = nullptr;
    UpdateChecker* updateChecker = nullptr;

    QWidget* installOverlay = nullptr;
    QLabel* overlayIconLabel = nullptr;
    QLabel* overlayTitleLabel = nullptr;
    QLabel* overlayStatusLabel = nullptr;
    QProgressBar* overlayProgressBar = nullptr;
    QLabel* overlayEtaLabel = nullptr;
    QPushButton* overlayCancelButton = nullptr;
    QElapsedTimer installTimer;
    std::atomic<bool> installCanceled{false};
    QFutureWatcher<bool>* activeZarWatcher = nullptr;
    QFutureWatcher<void>* activeExtractWatcher = nullptr;

    bool useSeparateUpdate = true;
    bool askPatchInstallMode = true;
    bool autoCheckUpdates = true;
    std::filesystem::path outputPath = "";
    std::filesystem::path dlcPath = "";
    std::filesystem::path pkgPath = "";
    std::filesystem::path settingsFile;
    PKG pkg;
    PSF psf;

    std::vector<std::string> SplitString(const std::string& str, char delimiter) {
        std::istringstream iss(str);
        std::vector<std::string> output;
        std::string token;
        while (std::getline(iss, token, delimiter)) {
            output.push_back(std::move(token));
        }
        return output;
    }
};
