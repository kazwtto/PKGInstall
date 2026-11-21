#include "i18n.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace {

QTranslator* appTranslator = nullptr;
QTranslator* qtTranslator = nullptr;

const QList<I18n::LanguageOption>& languageTable() {
    static const QList<I18n::LanguageOption> table = {
        {QStringLiteral("en"), QStringLiteral("English")},
        // "\u00ea" is the UTF-8 escape for the "ê" of Português.
        {QStringLiteral("pt_BR"), QString::fromUtf8("Portugu\u00eas (Brasil)")},
    };
    return table;
}

} // namespace

QList<I18n::LanguageOption> I18n::languages() {
    return languageTable();
}

QString I18n::displayName(const QString& code) {
    for (const LanguageOption& option : languageTable()) {
        if (option.code == code) {
            return option.name;
        }
    }
    return languageTable().first().name;
}

QString I18n::systemCode() {
    const QString localeName = QLocale::system().name();

    for (const LanguageOption& option : languageTable()) {
        if (localeName.compare(option.code, Qt::CaseInsensitive) == 0) {
            return option.code;
        }
    }

    const QString language = localeName.section(QLatin1Char('_'), 0, 0);
    if (!language.isEmpty()) {
        for (const LanguageOption& option : languageTable()) {
            if (option.code.section(QLatin1Char('_'), 0, 0).compare(language, Qt::CaseInsensitive) == 0) {
                return option.code;
            }
        }
    }

    return QStringLiteral("en");
}

QString I18n::resolveCode(const QString& code) {
    if (code.isEmpty() || code == QLatin1String(SystemLanguageCode)) {
        return systemCode();
    }
    return code;
}

bool I18n::apply(const QString& code) {
    if (appTranslator != nullptr) {
        QCoreApplication::removeTranslator(appTranslator);
        delete appTranslator;
        appTranslator = nullptr;
    }
    if (qtTranslator != nullptr) {
        QCoreApplication::removeTranslator(qtTranslator);
        delete qtTranslator;
        qtTranslator = nullptr;
    }

    if (code.isEmpty() || code == QStringLiteral("en")) {
        return true;
    }

    auto* app = new QTranslator;
    if (app->load(QStringLiteral(":/i18n/pkginstall_") + code + QStringLiteral(".qm"))) {
        QCoreApplication::installTranslator(app);
        appTranslator = app;
    } else {
        delete app;
    }

    // Translate Qt's own strings (dialog buttons, file dialog labels, ...) when available.
    auto* qt = new QTranslator;
    if (qt->load(QStringLiteral("qtbase_") + code,
                 QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(qt);
        qtTranslator = qt;
    } else {
        delete qt;
    }

    return appTranslator != nullptr;
}
