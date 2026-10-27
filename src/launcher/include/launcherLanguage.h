#ifndef LAUNCHER_LANGUAGE_H
#define LAUNCHER_LANGUAGE_H

#include <QString>

class QCoreApplication;
class QSettings;
class QTranslator;

namespace LauncherLanguage {

inline constexpr char SimplifiedChinese[] = "zh_CN";
inline constexpr char English[]           = "en_US";

[[nodiscard]] QString Default();
[[nodiscard]] QString Normalize(const QString& language);
[[nodiscard]] QString Read(QSettings& settings);
[[nodiscard]] QString ReadStartupPreference();
void                  Write(QSettings& settings, const QString& language);

// Installs both the launcher catalogue and Qt's own widget translations.
// The translators must outlive the application.
[[nodiscard]] bool Install(QCoreApplication& application, QTranslator& launcher_translator,
                           QTranslator& qt_translator, const QString& language);

} // namespace LauncherLanguage

#endif // LAUNCHER_LANGUAGE_H
