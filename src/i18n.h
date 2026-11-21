#pragma once

#include <QList>
#include <QString>

namespace I18n {

struct LanguageOption {
    QString code;
    QString name;
};

// Code used when the language follows the operating system.
inline constexpr const char* SystemLanguageCode = "system";

// Languages the application ships translations for ("en" is the source language).
QList<LanguageOption> languages();

QString displayName(const QString& code);

// Best shipped language for the current system locale.
QString systemCode();

// Turns "system" (or an empty value) into a concrete language code.
QString resolveCode(const QString& code);

// Installs the translators for the given language code, "en" restores the source strings.
bool apply(const QString& code);

} // namespace I18n
