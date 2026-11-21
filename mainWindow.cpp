#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QEvent>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QGraphicsBlurEffect>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QProgressBar>
#include <QProgressDialog>
#include <QSignalBlocker>
#include <QStyleHints>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVersionNumber>
#include <QWindow>
#include <QtConcurrent/QtConcurrentMap>
#include <QtConcurrent/QtConcurrentRun>
#include <cmath>
#include <memory>
#include <toml.hpp>

#include "./ui_mainWindow.h"
#include "mainWindow.h"
#include "src/appsettings.h"
#include "src/i18n.h"
#include "src/icons.h"
#include "src/loader.h"
#include "src/path_utils.h"
#include "src/pathpicker.h"
#include "src/theme.h"
#include "src/update_checker.h"

namespace toml {
template <typename TC, typename K>
std::filesystem::path find_fs_path_or(const basic_value<TC>& v, const K& ky,
                                      std::filesystem::path opt) {
    try {
        auto str = find<std::string>(v, ky);
        if (str.empty()) {
            return opt;
        }
        return Common::Path::fromUtf8(str);
    } catch (...) {
        return opt;
    }
}

} // namespace toml

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);

    // The window paints its own title bar, so the native frame is dropped and the window is
    // kept at a fixed 4:3 size.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAcceptDrops(true);
    setWindowTitle(QStringLiteral("PKGInstall"));
    setFixedSize(640, 480);

    preferences = AppSettings::loadUiPreferences();
    settingsFile = AppSettings::filePath();
    updateChecker = new UpdateChecker(this, this);

    SetupTitleBar();
    SetupAppearanceMenu();
    SetupLanguageCombo();
    SetupInstallOverlay();
    ui->gameInfoCard->hide();

    LoadSettings();
    ApplyAppearance(preferences.appearance);

    if (autoCheckUpdates) {
        QTimer::singleShot(800, this, [this]() {
            if (autoCheckUpdates && updateChecker) {
                updateChecker->checkForUpdates(false);
            }
        });
    }

    connect(ui->pkgPicker, &QPushButton::clicked, this, &MainWindow::pkgButtonClicked);
    connect(ui->outputPicker, &QPushButton::clicked, this, &MainWindow::folderButtonClicked);
    connect(ui->dlcPicker, &QPushButton::clicked, this, &MainWindow::dlcButtonClicked);

    connect(ui->extractButton, &QPushButton::clicked, this,
            [this]() { InstallDragDropPkg(pkgPath, false); });

    auto* extractMenu = new QMenu(this);
    auto* zarAction = extractMenu->addAction(tr("Extract as ZAR"));
    connect(zarAction, &QAction::triggered, this,
            [this]() { InstallDragDropPkg(pkgPath, true); });

    connect(ui->extractArrowButton, &QPushButton::clicked, this, [this, extractMenu]() {
        const int menuWidth = extractMenu->sizeHint().width();
        const QPoint pos = ui->extractArrowButton->mapToGlobal(
            QPoint(ui->extractArrowButton->width() - menuWidth,
                   ui->extractArrowButton->height() + 4));
        extractMenu->popup(pos);
    });

    connect(ui->saveSettingsButton, &QPushButton::clicked, this, [this]() { SaveSettings(); });

    connect(ui->loadConfigButton, &QPushButton::clicked, this,
            [this]() { LoadFoldersFromShadps4File(); });

    connect(ui->detectConfigButton, &QPushButton::clicked, this,
            [this]() { DetectShadps4Folders(); });

    connect(ui->setOutputButton, &QPushButton::clicked, this, [this]() {
        if (!ui->folderComboBox->currentText().isEmpty()) {
            ui->outputPicker->setPath(ui->folderComboBox->currentText());
            outputPath = Common::Path::fromQString(ui->folderComboBox->currentText());
        } else {
            QMessageBox::information(
                this, tr("Error"),
                tr("Folder list is empty, load a shadPS4 config file to get the list."));
        }
    });

    // Keep following the system color scheme while "Automatic" is selected.
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) {
                if (preferences.appearance == Appearance::Auto) {
                    ApplyAppearance(Appearance::Auto);
                }
            });
}

void MainWindow::SetupTitleBar() {
    ui->titleBar->installEventFilter(this);
    ui->appTitle->installEventFilter(this);
    ui->appIcon->installEventFilter(this);

    const QPixmap logo(QStringLiteral(":/logo.png"));
    if (!logo.isNull()) {
        // Scale to the exact device pixels of the screen, so the mark stays sharp on
        // displays that are not at 100% scaling.
        const qreal ratio = Icons::screenRatio();
        const int size = static_cast<int>(std::ceil(22 * ratio));
        QPixmap scaled = logo.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(ratio);
        ui->appIcon->setPixmap(scaled);

        // A multi size icon lets Windows and the window manager pick the exact size
        // instead of rescaling a single bitmap.
        QIcon windowIcon;
        for (int iconSize : {16, 24, 32, 48, 64, 128, 256}) {
            windowIcon.addPixmap(logo.scaled(iconSize, iconSize, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation));
        }
        setWindowIcon(windowIcon);
    }

    auto* shadow = new QGraphicsDropShadowEffect(ui->windowRoot);
    shadow->setBlurRadius(20);
    shadow->setOffset(0, 5);
    ui->windowRoot->setGraphicsEffect(shadow);

    connect(ui->minimizeButton, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(ui->closeWindowButton, &QToolButton::clicked, this, &QWidget::close);
}

void MainWindow::SetupAppearanceMenu() {
    appearanceMenu = new QMenu(this);

    appearanceHeaderAction = appearanceMenu->addAction(QString());
    appearanceHeaderAction->setEnabled(false);
    appearanceMenu->addSeparator();

    appearanceAutoAction = appearanceMenu->addAction(QString());
    appearanceLightAction = appearanceMenu->addAction(QString());
    appearanceDarkAction = appearanceMenu->addAction(QString());

    auto* group = new QActionGroup(this);
    for (QAction* action : {appearanceAutoAction, appearanceLightAction, appearanceDarkAction}) {
        action->setCheckable(true);
        group->addAction(action);
    }

    connect(appearanceAutoAction, &QAction::triggered, this,
            [this]() { appearanceChanged(Appearance::Auto); });
    connect(appearanceLightAction, &QAction::triggered, this,
            [this]() { appearanceChanged(Appearance::Light); });
    connect(appearanceDarkAction, &QAction::triggered, this,
            [this]() { appearanceChanged(Appearance::Dark); });

    appearanceMenu->addSeparator();
    separateUpdateAction = appearanceMenu->addAction(QString());
    separateUpdateAction->setCheckable(true);
    separateUpdateAction->setChecked(useSeparateUpdate);
    connect(separateUpdateAction, &QAction::toggled, this, [this](bool checked) {
        useSeparateUpdate = checked;
        SaveSettings();
    });

    askPatchModeAction = appearanceMenu->addAction(QString());
    askPatchModeAction->setCheckable(true);
    askPatchModeAction->setChecked(askPatchInstallMode);
    connect(askPatchModeAction, &QAction::toggled, this, [this](bool checked) {
        askPatchInstallMode = checked;
        SaveSettings();
    });

    appearanceMenu->addSeparator();
    autoCheckUpdatesAction = appearanceMenu->addAction(QString());
    autoCheckUpdatesAction->setCheckable(true);
    autoCheckUpdatesAction->setChecked(autoCheckUpdates);
    connect(autoCheckUpdatesAction, &QAction::toggled, this, [this](bool checked) {
        autoCheckUpdates = checked;
        SaveSettings();
    });

    checkUpdatesAction = appearanceMenu->addAction(QString());
    connect(checkUpdatesAction, &QAction::triggered, this, [this]() {
        if (updateChecker) {
            updateChecker->checkForUpdates(true);
        }
    });

    connect(ui->preferencesButton, &QToolButton::clicked, this, [this]() {
        const int menuWidth = appearanceMenu->sizeHint().width();
        const QPoint pos = ui->preferencesButton->mapToGlobal(
            QPoint(ui->preferencesButton->width() - menuWidth + 8,
                   ui->preferencesButton->height() + 8));
        appearanceMenu->popup(pos);
    });

    RetranslateCustom();
}

void MainWindow::SetupLanguageCombo() {
    // Follow the system language by default, translations can also be picked manually.
    ui->languageCombo->addItem(QString(), QLatin1String(I18n::SystemLanguageCode));

    for (const I18n::LanguageOption& language : I18n::languages()) {
        ui->languageCombo->addItem(language.name, language.code);
    }

    for (int i = 0; i < ui->languageCombo->count(); ++i) {
        if (ui->languageCombo->itemData(i).toString() == preferences.language) {
            ui->languageCombo->setCurrentIndex(i);
            break;
        }
    }

    connect(ui->languageCombo, &QComboBox::currentIndexChanged, this, &MainWindow::languageChanged);
}

void MainWindow::ApplyAppearance(Appearance appearance) {
    Theme::apply(appearance);

    if (auto* shadow = qobject_cast<QGraphicsDropShadowEffect*>(ui->windowRoot->graphicsEffect())) {
        shadow->setColor(Theme::palette().shadow);
    }

    ApplyIcons();
    RetranslateCustom();
}

void MainWindow::ApplyIcons() {
    const Theme::Palette& palette = Theme::palette();

    ui->filesIcon->setPixmap(Icons::pixmap(Icons::Glyph::Folder, palette.iconAccent, 17));
    ui->installIcon->setPixmap(Icons::pixmap(Icons::Glyph::Database, palette.iconAccent, 17));

    for (QLabel* chip : {ui->pkgChip, ui->outChip, ui->dlcChip}) {
        chip->setPixmap(Icons::pixmap(Icons::Glyph::Folder, palette.iconAccent, 13));
    }

    for (PathPicker* picker : {ui->pkgPicker, ui->outputPicker, ui->dlcPicker}) {
        picker->setGlyph(Icons::pixmap(Icons::Glyph::Folder, palette.muted, 13));
    }

    ui->globeIcon->setPixmap(Icons::pixmap(Icons::Glyph::Globe, palette.muted, 14));

    struct ButtonIcon {
        QAbstractButton* button;
        Icons::Glyph glyph;
        int size;
    };

    const std::vector<ButtonIcon> buttons = {
        {ui->preferencesButton, Icons::Glyph::Gear, 16},
        {ui->minimizeButton, Icons::Glyph::Minimize, 14},
        {ui->closeWindowButton, Icons::Glyph::Close, 14},
        {ui->setOutputButton, Icons::Glyph::Folder, 13},
        {ui->loadConfigButton, Icons::Glyph::List, 13},
        {ui->detectConfigButton, Icons::Glyph::Search, 13},
        {ui->saveSettingsButton, Icons::Glyph::Gear, 13},
        {ui->extractButton, Icons::Glyph::Package, 16},
    };

    for (const ButtonIcon& entry : buttons) {
        const QColor color = entry.button == ui->extractButton ? palette.onAccent : palette.text;
        entry.button->setIcon(Icons::icon(entry.glyph, color, entry.size));
        entry.button->setIconSize(QSize(entry.size, entry.size));
    }

    ui->extractArrowButton->setIcon(Icons::icon(Icons::Glyph::ChevronDown, palette.onAccent, 11));
    ui->extractArrowButton->setIconSize(QSize(11, 11));
}

void MainWindow::RetranslateCustom() {
    if (ui->languageCombo->count() > 0) {
        ui->languageCombo->setItemText(0, tr("System (automatic)"));
    }

    ui->folderComboBox->setPlaceholderText(tr("Select a shadPS4 install folder..."));
    ui->pkgPicker->setPlaceholderText(tr("Select a PKG file..."));
    ui->outputPicker->setPlaceholderText(tr("Select an output folder..."));
    ui->dlcPicker->setPlaceholderText(tr("Select a DLC folder..."));

    if (appearanceMenu == nullptr) {
        return;
    }

    appearanceHeaderAction->setText(tr("Appearance"));
    appearanceAutoAction->setText(tr("Automatic (system)"));
    appearanceLightAction->setText(tr("Light"));
    appearanceDarkAction->setText(tr("Dark"));

    if (separateUpdateAction != nullptr) {
        separateUpdateAction->setText(tr("Use separate update folder"));
    }

    if (askPatchModeAction != nullptr) {
        askPatchModeAction->setText(tr("Always ask how to apply patches"));
    }

    if (autoCheckUpdatesAction != nullptr) {
        autoCheckUpdatesAction->setText(tr("Automatically check for updates"));
    }

    if (checkUpdatesAction != nullptr) {
        checkUpdatesAction->setText(tr("Check for updates..."));
    }

    const Appearance current = Theme::currentAppearance();
    appearanceAutoAction->setChecked(current == Appearance::Auto);
    appearanceLightAction->setChecked(current == Appearance::Light);
    appearanceDarkAction->setChecked(current == Appearance::Dark);

    if (!pkgPath.empty()) {
        UpdateGameInfoCard(pkgPath);
    }
}

void MainWindow::languageChanged(int index) {
    if (index < 0) {
        return;
    }

    const QString code = ui->languageCombo->itemData(index).toString();
    if (code.isEmpty() || code == preferences.language) {
        return;
    }

    preferences.language = code;
    I18n::apply(I18n::resolveCode(code));
    AppSettings::saveUiPreferences(preferences);
}

void MainWindow::appearanceChanged(Appearance appearance) {
    if (preferences.appearance == appearance) {
        ApplyAppearance(appearance);
        return;
    }

    preferences.appearance = appearance;
    ApplyAppearance(appearance);
    AppSettings::saveUiPreferences(preferences);
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
        RetranslateCustom();
    }

    QMainWindow::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (activeZarWatcher != nullptr || activeExtractWatcher != nullptr) {
        installCanceled = true;
        if (overlayStatusLabel != nullptr) {
            overlayStatusLabel->setText(tr("Canceling..."));
        }
        if (overlayCancelButton != nullptr) {
            overlayCancelButton->setEnabled(false);
        }
        if (activeZarWatcher != nullptr) {
            activeZarWatcher->waitForFinished();
        }
        if (activeExtractWatcher != nullptr) {
            activeExtractWatcher->waitForFinished();
        }
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (activeZarWatcher != nullptr || activeExtractWatcher != nullptr) {
        event->ignore();
        return;
    }
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (urls.size() == 1 && urls.first().isLocalFile() &&
            urls.first().toLocalFile().endsWith(QStringLiteral(".pkg"), Qt::CaseInsensitive)) {
            event->acceptProposedAction();
            return;
        }
    }
    event->ignore();
}

void MainWindow::dropEvent(QDropEvent* event) {
    if (activeZarWatcher != nullptr || activeExtractWatcher != nullptr) {
        event->ignore();
        return;
    }
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (urls.size() == 1 && urls.first().isLocalFile()) {
            const QString localFile = urls.first().toLocalFile();
            if (!localFile.isEmpty()) {
                pkgPath = Common::Path::fromQString(localFile);
                ui->pkgPicker->setPath(localFile);
                UpdateGameInfoCard(pkgPath);
                event->acceptProposedAction();
                return;
            }
        }
    }
    event->ignore();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == ui->titleBar || watched == ui->appTitle || watched == ui->appIcon) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && windowHandle() != nullptr) {
                windowHandle()->startSystemMove();
                return true;
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::LoadSettings() {
    std::error_code ec;
    if (!std::filesystem::exists(settingsFile.parent_path(), ec)) {
        std::filesystem::create_directories(settingsFile.parent_path(), ec);
    }

    auto syncMenuToggles = [this]() {
        if (separateUpdateAction) {
            const QSignalBlocker b(separateUpdateAction);
            separateUpdateAction->setChecked(useSeparateUpdate);
        }
        if (askPatchModeAction) {
            const QSignalBlocker b(askPatchModeAction);
            askPatchModeAction->setChecked(askPatchInstallMode);
        }
        if (autoCheckUpdatesAction) {
            const QSignalBlocker b(autoCheckUpdatesAction);
            autoCheckUpdatesAction->setChecked(autoCheckUpdates);
        }
    };

    if (!std::filesystem::exists(settingsFile, ec)) {
        syncMenuToggles();
        return;
    }

    toml::value data;
    try {
        std::ifstream ifs;
        ifs.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        ifs.open(settingsFile, std::ios_base::binary);
        data = toml::parse(ifs, Common::Path::toUtf8(settingsFile.filename()));
    } catch (std::exception& ex) {
        QMessageBox::critical(this, tr("Filesystem error"), QString::fromUtf8(ex.what()));
        return;
    }

    useSeparateUpdate = toml::find_or<bool>(data, "Settings", "UseSeparateUpdateFolder", true);
    askPatchInstallMode = toml::find_or<bool>(data, "Settings", "AskPatchInstallMode", true);
    autoCheckUpdates = toml::find_or<bool>(data, "Settings", "AutoCheckUpdates", true);
    syncMenuToggles();

    if (data.contains("Paths")) {
        const toml::value& launcher = data.at("Paths");
        outputPath = toml::find_fs_path_or(launcher, "outputPath", {});
        dlcPath = toml::find_fs_path_or(launcher, "dlcPath", {});
    }

    ui->outputPicker->setPath(Common::Path::toQString(outputPath));
    ui->dlcPicker->setPath(Common::Path::toQString(dlcPath));

    std::vector<std::string> install_dirs;
    if (data.contains("ShadPS4InstallFolders")) {
        const toml::value& installFolders = data.at("ShadPS4InstallFolders");
        toml::array arr = toml::find_or<toml::array>(installFolders, "Folders", {});
        for (const auto& folder : arr) {
            if (folder.is_string()) {
                install_dirs.push_back(folder.as_string());
            }
        }
    }

    for (const std::string& dirUtf8 : install_dirs) {
        ui->folderComboBox->addItem(
            Common::Path::toQString(Common::Path::fromUtf8(dirUtf8)));
    }
}

void MainWindow::SaveSettings() {
    std::error_code ec;
    if (!std::filesystem::exists(settingsFile.parent_path(), ec)) {
        std::filesystem::create_directories(settingsFile.parent_path(), ec);
    }

    toml::value data;
    try {
        std::ifstream ifs;
        ifs.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        ifs.open(settingsFile, std::ios_base::binary);
        data = toml::parse(ifs, Common::Path::toUtf8(settingsFile.filename()));
    } catch (std::exception& ex) {
        // if it doesn't exist, the save function will create the file
        if (std::filesystem::exists(settingsFile, ec)) {
            QMessageBox::critical(this, tr("Filesystem error"), QString::fromUtf8(ex.what()));
            return;
        }
    }

    std::vector<std::string> game_dirs;
    for (int i = 0; i < ui->folderComboBox->count(); ++i) {
        const auto itemPath = Common::Path::fromQString(ui->folderComboBox->itemText(i));
        game_dirs.push_back(Common::Path::toUtf8(itemPath));
    }

    data["ShadPS4InstallFolders"]["Folders"] = game_dirs;
    data["Paths"]["outputPath"] = Common::Path::toUtf8(outputPath);
    data["Paths"]["dlcPath"] = Common::Path::toUtf8(dlcPath);
    data["Settings"]["UseSeparateUpdateFolder"] = useSeparateUpdate;
    data["Settings"]["AskPatchInstallMode"] = askPatchInstallMode;
    data["Settings"]["AutoCheckUpdates"] = autoCheckUpdates;

    // Keep the language/appearance selection that was written by the preferences helper.
    data["UI"]["language"] = preferences.language.toStdString();
    data["UI"]["appearance"] = AppSettings::appearanceToString(preferences.appearance).toStdString();

    std::ofstream file(settingsFile, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        QMessageBox::critical(this, tr("Filesystem error"),
                              tr("Could not open settings file for writing."));
        return;
    }
    file << data;
    file.flush();
    if (file.fail()) {
        QMessageBox::critical(this, tr("Filesystem error"),
                              tr("Failed to write settings file."));
    }
}

void MainWindow::LoadFoldersFromShadps4File() {
    const QString shadConfig =
        QFileDialog::getOpenFileName(this, tr("Load shadPS4 config file"), QDir::currentPath(),
                                     tr("shadPS4 config file (config.json config.toml)"));

    if (shadConfig.isEmpty()) {
        return;
    }

    const std::filesystem::path shadConfigFile = Common::Path::fromQString(shadConfig);
    const std::optional<Shadps4Config> config = Shadps4ConfigFinder::Read(shadConfigFile);
    if (!config.has_value() || config->installDirs.empty()) {
        QMessageBox::information(this, tr("PKGInstall"),
                                 tr("No game install folders found in this file."));
        return;
    }

    ApplyShadps4Config(*config);
}

void MainWindow::DetectShadps4Folders() {
    const std::optional<std::filesystem::path> file = Shadps4ConfigFinder::Detect();
    const std::optional<Shadps4Config> config =
        file.has_value() ? Shadps4ConfigFinder::Read(*file) : std::nullopt;

    if (!config.has_value() || config->installDirs.empty()) {
        ui->detectConfigButton->setToolTip(
            tr("Search the standard folders for the emulator configuration"));
        QMessageBox::information(
            this, tr("PKGInstall"),
            tr("No shadPS4 configuration file with install folders was found in the standard "
               "folders. Select one with the config file button."));
        return;
    }

    ApplyShadps4Config(*config);

    const QString configPath = Common::Path::toQString(*file);
    ui->detectConfigButton->setToolTip(tr("Install folders loaded from %1").arg(configPath));
}

void MainWindow::ApplyShadps4Config(const Shadps4Config& config) {
    ui->folderComboBox->clear();
    for (const std::filesystem::path& folder : config.installDirs) {
        ui->folderComboBox->addItem(Common::Path::toQString(folder));
    }

    if (config.addonDir.has_value() && !config.addonDir->empty()) {
        dlcPath = *config.addonDir;
        ui->dlcPicker->setPath(Common::Path::toQString(dlcPath));
    }
}

void MainWindow::folderButtonClicked() {
    const QString startDir =
        outputPath.empty() ? QDir::homePath() : Common::Path::toQString(outputPath);
    const QString folder =
        QFileDialog::getExistingDirectory(this, tr("Set Output folder"), startDir);
    if (!folder.isEmpty()) {
        ui->outputPicker->setPath(folder);
        outputPath = Common::Path::fromQString(folder);
    }
}

void MainWindow::dlcButtonClicked() {
    const QString startDir =
        dlcPath.empty() ? QDir::homePath() : Common::Path::toQString(dlcPath);
    const QString folder =
        QFileDialog::getExistingDirectory(this, tr("Set DLC folder"), startDir);
    if (!folder.isEmpty()) {
        ui->dlcPicker->setPath(folder);
        dlcPath = Common::Path::fromQString(folder);
    }
}

void MainWindow::pkgButtonClicked() {
    const QString startDir =
        pkgPath.empty() ? QDir::homePath() : Common::Path::toQString(pkgPath.parent_path());
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Select PKG file"), startDir, tr("PKG files (*.pkg)"));

    if (!file.isEmpty()) {
        pkgPath = Common::Path::fromQString(file);
        ui->pkgPicker->setPath(file);
        UpdateGameInfoCard(pkgPath);
    }
}

void MainWindow::SetupInstallOverlay() {
    installOverlay = new QWidget(ui->centralwidget);
    installOverlay->setObjectName(QStringLiteral("installOverlay"));
    installOverlay->setAttribute(Qt::WA_StyledBackground, true);
    installOverlay->hide();

    auto* overlayLayout = new QVBoxLayout(installOverlay);
    overlayLayout->setContentsMargins(24, 24, 24, 24);
    overlayLayout->setAlignment(Qt::AlignCenter);

    auto* card = new QFrame(installOverlay);
    card->setObjectName(QStringLiteral("overlayCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);

    auto* cardShadow = new QGraphicsDropShadowEffect(card);
    cardShadow->setColor(QColor(0, 0, 0, 180));
    cardShadow->setBlurRadius(36);
    cardShadow->setOffset(0, 8);
    card->setGraphicsEffect(cardShadow);

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(32, 28, 32, 28);
    cardLayout->setSpacing(14);
    cardLayout->setAlignment(Qt::AlignCenter);

    overlayIconLabel = new QLabel(card);
    overlayIconLabel->setObjectName(QStringLiteral("overlayIconLabel"));
    overlayIconLabel->setAlignment(Qt::AlignCenter);
    overlayIconLabel->setMinimumSize(64, 64);
    overlayIconLabel->setMaximumSize(64, 64);
    cardLayout->addWidget(overlayIconLabel, 0, Qt::AlignCenter);

    overlayTitleLabel = new QLabel(card);
    overlayTitleLabel->setObjectName(QStringLiteral("overlayTitle"));
    overlayTitleLabel->setAlignment(Qt::AlignCenter);
    overlayTitleLabel->setWordWrap(true);
    cardLayout->addWidget(overlayTitleLabel);

    overlayStatusLabel = new QLabel(card);
    overlayStatusLabel->setObjectName(QStringLiteral("overlayStatus"));
    overlayStatusLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(overlayStatusLabel);

    overlayProgressBar = new QProgressBar(card);
    overlayProgressBar->setObjectName(QStringLiteral("overlayProgress"));
    overlayProgressBar->setTextVisible(true);
    cardLayout->addWidget(overlayProgressBar);

    overlayEtaLabel = new QLabel(card);
    overlayEtaLabel->setObjectName(QStringLiteral("overlayEta"));
    overlayEtaLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(overlayEtaLabel);

    overlayCancelButton = new QPushButton(tr("Cancel"), card);
    overlayCancelButton->setObjectName(QStringLiteral("overlayCancelButton"));
    overlayCancelButton->setCursor(Qt::PointingHandCursor);
    cardLayout->addWidget(overlayCancelButton, 0, Qt::AlignCenter);

    connect(overlayCancelButton, &QPushButton::clicked, this, [this]() {
        installCanceled = true;
        overlayStatusLabel->setText(tr("Canceling..."));
        overlayCancelButton->setEnabled(false);
    });

    overlayLayout->addWidget(card, 0, Qt::AlignCenter);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (installOverlay) {
        installOverlay->setGeometry(ui->centralwidget->rect());
    }
}

void MainWindow::ShowInstallOverlay(const QString& title, int maxProgress) {
    installCanceled = false;
    installTimer.restart();

    if (!pkg.icon0.empty()) {
        QPixmap pixmap;
        pixmap.loadFromData(reinterpret_cast<const uchar*>(pkg.icon0.data()),
                            static_cast<uint>(pkg.icon0.size()));
        if (!pixmap.isNull()) {
            overlayIconLabel->setPixmap(pixmap.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            overlayIconLabel->setPixmap(Icons::pixmap(Icons::Glyph::Package, Theme::palette().accent, 48));
        }
    } else {
        overlayIconLabel->setPixmap(Icons::pixmap(Icons::Glyph::Package, Theme::palette().accent, 48));
    }

    overlayTitleLabel->setText(title);
    overlayStatusLabel->setText(tr("Preparing installation..."));
    overlayProgressBar->setRange(0, maxProgress > 0 ? maxProgress : 100);
    overlayProgressBar->setValue(0);
    overlayEtaLabel->setText(tr("Calculating estimated time..."));
    overlayCancelButton->setEnabled(true);

    auto* blur = new QGraphicsBlurEffect(ui->windowRoot);
    blur->setBlurRadius(20);
    ui->windowRoot->setGraphicsEffect(blur);

    ui->extractButton->setEnabled(false);
    ui->extractArrowButton->setEnabled(false);

    installOverlay->setGeometry(ui->centralwidget->rect());
    installOverlay->raise();
    installOverlay->show();
}

void MainWindow::UpdateInstallProgress(int current, int maxProgress, const QString& statusText) {
    if (maxProgress > 0) {
        overlayProgressBar->setRange(0, maxProgress);
    }
    overlayProgressBar->setValue(current);

    if (!statusText.isEmpty()) {
        overlayStatusLabel->setText(statusText);
    } else if (maxProgress > 0) {
        overlayStatusLabel->setText(tr("Extracting files: %1 of %2").arg(current).arg(maxProgress));
    }

    qint64 elapsedMs = installTimer.elapsed();
    if (current > 0 && maxProgress > current && elapsedMs > 500) {
        qint64 remainingMs = (elapsedMs * (maxProgress - current)) / current;
        int totalSec = static_cast<int>(remainingMs / 1000);
        int min = totalSec / 60;
        int sec = totalSec % 60;
        overlayEtaLabel->setText(tr("Estimated time remaining: %1:%2")
                                     .arg(min, 2, 10, QChar('0'))
                                     .arg(sec, 2, 10, QChar('0')));
    } else if (current >= maxProgress) {
        overlayEtaLabel->setText(tr("Finalizing..."));
    }
}

void MainWindow::HideInstallOverlay() {
    if (installOverlay) {
        installOverlay->hide();
    }
    ui->extractButton->setEnabled(true);
    ui->extractArrowButton->setEnabled(true);

    auto* shadow = new QGraphicsDropShadowEffect(ui->windowRoot);
    shadow->setColor(Theme::palette().shadow);
    shadow->setBlurRadius(20);
    shadow->setOffset(0, 5);
    ui->windowRoot->setGraphicsEffect(shadow);
}

void MainWindow::UpdateGameInfoCard(const std::filesystem::path& file) {
    if (file.empty() || !std::filesystem::exists(file)) {
        ui->gameInfoCard->hide();
        return;
    }

    if (Loader::DetectFileType(file) != Loader::FileTypes::Pkg) {
        ui->gameInfoCard->hide();
        return;
    }

    PKG infoPkg;
    std::string failreason;
    if (!infoPkg.Open(file, failreason)) {
        ui->gameInfoCard->hide();
        return;
    }

    PSF infoPsf;
    if (!infoPsf.Open(infoPkg.sfo)) {
        ui->gameInfoCard->hide();
        return;
    }

    if (!infoPkg.icon0.empty()) {
        QPixmap iconPixmap;
        if (iconPixmap.loadFromData(reinterpret_cast<const uchar*>(infoPkg.icon0.data()),
                                    static_cast<uint>(infoPkg.icon0.size()))) {
            ui->gameIconLabel->setPixmap(iconPixmap.scaled(54, 54, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            ui->gameIconLabel->setPixmap(Icons::pixmap(Icons::Glyph::Package, Theme::palette().accent, 32));
        }
    } else {
        ui->gameIconLabel->setPixmap(Icons::pixmap(Icons::Glyph::Package, Theme::palette().accent, 32));
    }

    QString titleText;
    if (auto titleVal = infoPsf.GetString("TITLE"); titleVal.has_value()) {
        titleText = QString::fromUtf8(std::string{*titleVal}.c_str());
    } else {
        titleText = QString::fromStdString(std::string{infoPkg.GetTitleID()});
    }
    ui->gameTitleLabel->setText(titleText);

    QString titleIdText = QString::fromStdString(std::string{infoPkg.GetTitleID()});
    ui->gameTitleIdLabel->setText(titleIdText);

    QString appVerText = tr("v1.00");
    if (auto appVerVal = infoPsf.GetString("APP_VER"); appVerVal.has_value()) {
        appVerText = QString("v%1").arg(QString::fromUtf8(std::string{*appVerVal}.c_str()));
    }
    ui->gameVersionLabel->setText(appVerText);

    QString pkgFlags = QString::fromStdString(infoPkg.GetPkgFlags());
    QString categoryText = tr("Game");
    if (pkgFlags.contains(QStringLiteral("PATCH"))) {
        categoryText = tr("Patch / Update");
    } else if (pkgFlags.contains(QStringLiteral("DLC")) || pkgFlags.contains(QStringLiteral("ADDON"))) {
        categoryText = tr("DLC / Add-on");
    } else if (auto catVal = infoPsf.GetString("CATEGORY"); catVal.has_value()) {
        std::string cat = std::string{*catVal};
        if (cat == "gp") {
            categoryText = tr("Patch / Update");
        } else if (cat == "ac") {
            categoryText = tr("DLC / Add-on");
        } else if (cat == "gd") {
            categoryText = tr("Game (Digital)");
        }
    }
    ui->gameCategoryLabel->setText(categoryText);

    std::error_code ec;
    auto bytes = std::filesystem::file_size(file, ec);
    if (!ec) {
        double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
        if (gb >= 1.0) {
            ui->gameSizeLabel->setText(QString("%1 GB").arg(gb, 0, 'f', 2));
        } else {
            double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
            ui->gameSizeLabel->setText(QString("%1 MB").arg(mb, 0, 'f', 1));
        }
    } else {
        ui->gameSizeLabel->setText(QString());
    }

    ui->gameInfoCard->show();
}

void MainWindow::InstallDragDropPkg(std::filesystem::path file, bool isZar) {
    if (activeZarWatcher != nullptr || activeExtractWatcher != nullptr) {
        return;
    }

    if (file.empty() || !std::filesystem::exists(file) || !std::filesystem::exists(outputPath)) {
        QMessageBox::information(this, tr("Error"),
                                 tr("Existing PKG file and output folder must be set"));
        return;
    }

    if (Loader::DetectFileType(file) == Loader::FileTypes::Pkg) {
        std::string failreason;
        pkg = PKG();
        if (!pkg.Open(file, failreason)) {
            QMessageBox::critical(this, tr("PKG ERROR"), QString::fromStdString(failreason));
            return;
        }
        if (!psf.Open(pkg.sfo)) {
            QMessageBox::critical(this,
                                  tr("PKG ERROR"),
                                  tr("Could not read SFO. Check log for details"));
            return;
        }
        const std::string category =
            psf.GetString("CATEGORY").has_value() ? std::string{*psf.GetString("CATEGORY")} : std::string{};

        std::filesystem::path game_install_dir = outputPath;
        QString pkgType = QString::fromStdString(pkg.GetPkgFlags());

        if (pkgType.contains("PATCH") && askPatchInstallMode) {
            QMessageBox patchBox(this);
            patchBox.setWindowTitle(tr("Patch Installation"));
            patchBox.setText(tr("How would you like to install this update?"));
            patchBox.setInformativeText(tr("You can apply the update directly into the game folder or extract it into a separate update folder."));
            QPushButton* applyDirectButton = patchBox.addButton(tr("Apply directly to game"), QMessageBox::ActionRole);
            QPushButton* separateButton = patchBox.addButton(tr("Separate update folder"), QMessageBox::ActionRole);
            QPushButton* cancelButton = patchBox.addButton(QMessageBox::Cancel);
            patchBox.setDefaultButton(useSeparateUpdate ? separateButton : applyDirectButton);

            auto* dontAskCheck = new QCheckBox(tr("Do not ask this again"), &patchBox);
            patchBox.setCheckBox(dontAskCheck);

            patchBox.exec();
            if (patchBox.clickedButton() == cancelButton) {
                return;
            }

            if (patchBox.clickedButton() == applyDirectButton) {
                useSeparateUpdate = false;
            } else if (patchBox.clickedButton() == separateButton) {
                useSeparateUpdate = true;
            }

            if (dontAskCheck->isChecked()) {
                askPatchInstallMode = false;
                if (askPatchModeAction) {
                    const QSignalBlocker b(askPatchModeAction);
                    askPatchModeAction->setChecked(false);
                }
            }

            if (separateUpdateAction) {
                const QSignalBlocker b(separateUpdateAction);
                separateUpdateAction->setChecked(useSeparateUpdate);
            }
            SaveSettings();
        }

        bool use_game_update = pkgType.contains("PATCH") && useSeparateUpdate;

        QString itemTitle;
        if (auto titleVal = psf.GetString("TITLE"); titleVal.has_value()) {
            itemTitle = QString(tr("Installing: %1 (%2)"))
                            .arg(QString::fromUtf8(std::string{*titleVal}.c_str()))
                            .arg(QString::fromStdString(std::string{pkg.GetTitleID()}));
        } else {
            itemTitle = QString(tr("Installing: %1"))
                            .arg(QString::fromStdString(std::string{pkg.GetTitleID()}));
        }

        if (isZar) {
            std::filesystem::path zar_file_path;
            if (pkgType.contains("PATCH")) {
                zar_file_path = outputPath / (std::string{pkg.GetTitleID()} + (useSeparateUpdate ? "-patch.zar" : ".zar"));
            } else {
                zar_file_path = outputPath / (std::string{pkg.GetTitleID()} + ".zar");
            }

            if (std::filesystem::exists(zar_file_path)) {
                QMessageBox msgBox(this);
                msgBox.setWindowTitle(tr("PKG Installation"));
                msgBox.setText(QString(tr("ZAR file already exists:\n%1\n\nWould you like to overwrite?"))
                                   .arg(Common::Path::toQString(zar_file_path)));
                msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                msgBox.setDefaultButton(QMessageBox::No);
                if (msgBox.exec() != QMessageBox::Yes) {
                    return;
                }
            }

            ShowInstallOverlay(itemTitle, 100);

            auto errReason = std::make_shared<std::string>();
            auto* watcher = new QFutureWatcher<bool>(this);
            activeZarWatcher = watcher;

            connect(watcher, &QFutureWatcher<bool>::finished, this, [=, this]() {
                const bool success = watcher->result();
                activeZarWatcher = nullptr;
                HideInstallOverlay();
                watcher->deleteLater();

                if (installCanceled) {
                    QMessageBox::information(this, tr("Canceled"), tr("Installation was canceled."));
                    return;
                }

                if (!success) {
                    QMessageBox::critical(this, tr("PKG ERROR"),
                                          QString::fromStdString(*errReason));
                    return;
                }

                QIcon windowIcon;
                if (!pkg.icon0.empty()) {
                    QPixmap px;
                    px.loadFromData(reinterpret_cast<const uchar*>(pkg.icon0.data()),
                                    static_cast<uint>(pkg.icon0.size()));
                    if (!px.isNull()) {
                        windowIcon = QIcon(px);
                    }
                }

                QMessageBox extractMsgBox(this);
                extractMsgBox.setWindowTitle(tr("Installation Finished"));
                if (!windowIcon.isNull()) {
                    extractMsgBox.setWindowIcon(windowIcon);
                }
                extractMsgBox.setText(QString(tr("Game successfully extracted as ZAR at\n%1"))
                                          .arg(Common::Path::toQString(zar_file_path)));
                extractMsgBox.addButton(QMessageBox::Ok);
                extractMsgBox.exec();
            });

            auto future = QtConcurrent::run([this, file, zar_file_path, errReason]() {
                return pkg.ExtractToZar(
                    file, zar_file_path, *errReason,
                    [this](int current, int total) {
                        QMetaObject::invokeMethod(this, [this, current, total]() {
                            UpdateInstallProgress(current, total);
                        }, Qt::QueuedConnection);
                    },
                    [this]() -> bool {
                        return installCanceled.load();
                    });
            });
            watcher->setFuture(future);
            return;
        }

        // Default paths for normal extraction
        auto game_folder_path = game_install_dir / pkg.GetTitleID();
        const int max_depth = 5;
        if (auto found_game =
                FindGameByID(game_install_dir, std::string{pkg.GetTitleID()}, max_depth);
            found_game.has_value()) {
            game_folder_path = found_game.value().parent_path();
        }
        auto game_update_path =
            use_game_update
                ? game_folder_path.parent_path() / (std::string{pkg.GetTitleID()} + "-patch")
                : game_folder_path;

        const QString gameDirPath = Common::Path::toQString(game_folder_path);
        QDir game_dir(gameDirPath);
        if (game_dir.exists()) {
            QMessageBox msgBox(this);
            msgBox.setWindowTitle(tr("PKG Installation"));

            std::string content_id;
            if (auto value = psf.GetString("CONTENT_ID"); value.has_value()) {
                content_id = std::string{*value};
            } else {
                QMessageBox::critical(this, tr("PKG ERROR"), tr("PSF file there is no CONTENT_ID"));
                return;
            }
            const auto contentParts = SplitString(content_id, '-');
            if (contentParts.size() < 3) {
                QMessageBox::critical(this, tr("PKG ERROR"),
                                      tr("Invalid CONTENT_ID format in PSF file."));
                return;
            }
            const std::string& entitlement_label = contentParts[2];

            if (pkgType.contains("PATCH")) {
                QString pkg_app_version;
                if (auto app_ver = psf.GetString("APP_VER"); app_ver.has_value()) {
                    pkg_app_version = QString::fromStdString(std::string{*app_ver});
                } else {
                    QMessageBox::critical(this, tr("PKG ERROR"), tr("PSF file there is no APP_VER"));
                    return;
                }
                std::filesystem::path sce_folder_path =
                    std::filesystem::exists(game_update_path / "sce_sys" / "param.sfo")
                        ? game_update_path / "sce_sys" / "param.sfo"
                        : game_folder_path / "sce_sys" / "param.sfo";

                PSF installedPsf;
                if (!installedPsf.Open(sce_folder_path)) {
                    QMessageBox::critical(this, tr("PKG ERROR"),
                                          tr("Could not read installed game SFO."));
                    return;
                }
                QString game_app_version;
                if (auto app_ver = installedPsf.GetString("APP_VER"); app_ver.has_value()) {
                    game_app_version = QString::fromStdString(std::string{*app_ver});
                } else {
                    QMessageBox::critical(this, tr("PKG ERROR"), tr("PSF file there is no APP_VER"));
                    return;
                }

                const QVersionNumber appVer = QVersionNumber::fromString(game_app_version.trimmed());
                const QVersionNumber pkgVer = QVersionNumber::fromString(pkg_app_version.trimmed());
                const int cmp = (!appVer.isNull() && !pkgVer.isNull())
                                    ? QVersionNumber::compare(pkgVer, appVer)
                                    : QString::compare(pkg_app_version, game_app_version);

                if (cmp == 0) {
                    msgBox.setText(QString(tr("Patch detected!") + "\n"
                                           + tr("PKG and Game versions match: ") + pkg_app_version
                                           + "\n" + tr("Would you like to overwrite?")));
                    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                    msgBox.setDefaultButton(QMessageBox::No);
                } else if (cmp < 0) {
                    msgBox.setText(QString(
                        tr("Patch detected!") + "\n"
                        + tr("PKG Version %1 is older than existing version: ").arg(pkg_app_version)
                        + game_app_version + "\n" + tr("Would you like to overwrite?")));
                    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                    msgBox.setDefaultButton(QMessageBox::No);
                } else {
                    msgBox.setText(QString(
                        tr("Patch detected!") + "\n" + tr("Game exists: ") + game_app_version + "\n"
                        + tr("Would you like to apply Patch: ") + pkg_app_version + " ?"));
                    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                    msgBox.setDefaultButton(QMessageBox::No);
                }
                int result = msgBox.exec();
                if (result != QMessageBox::Yes) {
                    return;
                }
            } else if (category == "ac") {
                if (dlcPath.empty()) {
                    QMessageBox::information(this, tr("DLC Install"),
                                             tr("Please select a DLC folder first."));
                    return;
                }
                const auto addon_extract_path = dlcPath;
                const QString addonDirPath = Common::Path::toQString(addon_extract_path);
                QDir addon_dir(addonDirPath);

                if (!addon_dir.exists()) {
                    QMessageBox addonMsgBox(this);
                    addonMsgBox.setWindowTitle(tr("DLC Install"));
                    addonMsgBox.setText(QString(tr("Would you like to install DLC: %1?"))
                                            .arg(QString::fromStdString(entitlement_label)));

                    addonMsgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                    addonMsgBox.setDefaultButton(QMessageBox::No);
                    int result = addonMsgBox.exec();
                    if (result == QMessageBox::Yes) {
                        game_update_path = addon_extract_path;
                    } else {
                        return;
                    }
                } else {
                    msgBox.setText(QString(tr("DLC already installed:") + "\n" + addonDirPath
                                           + "\n\n" + tr("Would you like to overwrite?")));
                    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                    msgBox.setDefaultButton(QMessageBox::No);
                    int result = msgBox.exec();
                    if (result == QMessageBox::Yes) {
                        game_update_path = addon_extract_path;
                    } else {
                        return;
                    }
                }
            } else {
                msgBox.setText(QString(tr("Game already installed") + "\n" + gameDirPath + "\n"
                                       + tr("Would you like to overwrite?")));
                msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
                msgBox.setDefaultButton(QMessageBox::No);
                int result = msgBox.exec();
                if (result != QMessageBox::Yes) {
                    return;
                }
            }
        } else {
            if (pkgType.contains("PATCH") || category == "ac") {
                QMessageBox::information(
                    this,
                    tr("PKG Installation"),
                    tr("PKG is a patch or DLC, please install base game first!"));
                return;
            }
        }

        if (!pkg.Extract(file, game_update_path, failreason)) {
            QMessageBox::critical(this, tr("PKG ERROR"), QString::fromStdString(failreason));
        } else {
            int nfiles = pkg.GetNumberOfFiles();

            auto showSuccessDialog = [this, game_folder_path]() {
                const QString path = Common::Path::toQString(game_folder_path.parent_path());
                QIcon windowIcon;
                if (!pkg.icon0.empty()) {
                    QPixmap px;
                    px.loadFromData(reinterpret_cast<const uchar*>(pkg.icon0.data()),
                                    static_cast<uint>(pkg.icon0.size()));
                    if (!px.isNull()) {
                        windowIcon = QIcon(px);
                    }
                } else {
                    windowIcon = QIcon(Common::Path::toQString(game_folder_path / "sce_sys/icon0.png"));
                }

                QMessageBox extractMsgBox(this);
                extractMsgBox.setWindowTitle(tr("Installation Finished"));
                if (!windowIcon.isNull()) {
                    extractMsgBox.setWindowIcon(windowIcon);
                }
                extractMsgBox.setText(
                    QString(tr("Game successfully installed at %1")).arg(path));
                extractMsgBox.addButton(QMessageBox::Ok);
                extractMsgBox.setDefaultButton(QMessageBox::Ok);
                extractMsgBox.exec();
            };

            if (nfiles == 0) {
                showSuccessDialog();
                return;
            }

            if (nfiles > 0) {
                QVector<int> indices;
                indices.reserve(nfiles);
                for (int i = 0; i < nfiles; i++) {
                    indices.append(i);
                }

                ShowInstallOverlay(itemTitle, nfiles);

                auto hadExtractError = std::make_shared<std::atomic<bool>>(false);
                auto* futureWatcher = new QFutureWatcher<void>(this);
                activeExtractWatcher = futureWatcher;

                connect(futureWatcher, &QFutureWatcher<void>::finished, this, [=, this]() {
                    activeExtractWatcher = nullptr;
                    HideInstallOverlay();
                    futureWatcher->deleteLater();

                    if (installCanceled) {
                        QMessageBox::information(this, tr("Canceled"), tr("Installation was canceled."));
                        return;
                    }

                    if (hadExtractError->load()) {
                        QMessageBox::critical(this, tr("PKG ERROR"),
                                              tr("One or more files failed to extract."));
                        return;
                    }

                    showSuccessDialog();
                });

                connect(futureWatcher, &QFutureWatcher<void>::progressValueChanged, this,
                        [this, nfiles](int value) {
                            UpdateInstallProgress(value, nfiles);
                        });

                futureWatcher->setFuture(
                    QtConcurrent::map(indices, [this, hadExtractError](int index) {
                        if (!installCanceled && !pkg.ExtractFiles(index)) {
                            hadExtractError->store(true);
                        }
                    }));
            }
        }
    } else {
        QMessageBox::critical(this,
                              tr("PKG ERROR"),
                              tr("File doesn't appear to be a valid PKG file"));
    }
}

std::optional<std::filesystem::path> MainWindow::FindGameByID(const std::filesystem::path &dir,
                                                              const std::string &game_id,
                                                              int max_depth)
{
    if (max_depth < 0) {
        return std::nullopt;
    }

    // Check if this is the game we're looking for
    if (dir.filename() == game_id && std::filesystem::exists(dir / "sce_sys" / "param.sfo")) {
        auto eboot_path = dir / "eboot.bin";
        if (std::filesystem::exists(eboot_path)) {
            return eboot_path;
        }
    }

    // Recursively search subdirectories
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_directory()) {
            continue;
        }
        if (auto found = FindGameByID(entry.path(), game_id, max_depth - 1)) {
            return found;
        }
    }

    return std::nullopt;
}

MainWindow::~MainWindow()
{
    if (activeZarWatcher != nullptr) {
        installCanceled = true;
        activeZarWatcher->waitForFinished();
    }
    if (activeExtractWatcher != nullptr) {
        installCanceled = true;
        activeExtractWatcher->waitForFinished();
    }
    delete ui;
}
