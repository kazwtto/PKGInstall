#include "mainWindow.h"

#include <QApplication>

#include "src/appsettings.h"
#include "src/i18n.h"
#include "src/theme.h"

int main(int argc, char* argv[]) {
    QApplication a(argc, argv);

    QApplication::setApplicationName(QStringLiteral("PKGInstall"));
    QApplication::setApplicationDisplayName(QStringLiteral("PKGInstall"));
    QApplication::setApplicationVersion(QStringLiteral(PKGINSTALL_VERSION));

    const UiPreferences preferences = AppSettings::loadUiPreferences();
    I18n::apply(I18n::resolveCode(preferences.language));
    Theme::apply(preferences.appearance);

    MainWindow w;
    w.show();
    return a.exec();
}
