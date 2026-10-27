#include "launcherLanguage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

#include <memory>

namespace {

constexpr char SettingsFile[]  = "Kyty.ini";
constexpr char SettingsOrg[]   = "Kyty";
constexpr char SettingsApp[]   = "Kyty";
constexpr char LauncherGroup[] = "Launcher";
constexpr char LanguageKey[]   = "interface_language";

std::unique_ptr<QSettings> OpenSettings() {
	if (QFile::exists(QDir::current().absoluteFilePath(QString::fromLatin1(SettingsFile)))) {
		return std::make_unique<QSettings>(QString::fromLatin1(SettingsFile), QSettings::IniFormat);
	}
#ifdef __linux__
	return std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope,
	                                   QString::fromLatin1(SettingsOrg),
	                                   QString::fromLatin1(SettingsApp));
#else
	return std::make_unique<QSettings>(QSettings::IniFormat, QSettings::SystemScope,
	                                   QString::fromLatin1(SettingsOrg),
	                                   QString::fromLatin1(SettingsApp));
#endif
}

bool LoadQtTranslation(QTranslator& translator, const QString& language) {
	const QStringList file_names = {
	    QStringLiteral("qtbase_") + language,
	    QStringLiteral("qt_") + language,
	};
	const QStringList search_paths = {
	    QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("translations")),
	    QLibraryInfo::path(QLibraryInfo::TranslationsPath),
	};
	for (const auto& path: search_paths) {
		for (const auto& file_name: file_names) {
			if (translator.load(file_name, path)) {
				return true;
			}
		}
	}
	return false;
}

} // namespace

namespace LauncherLanguage {

QString Default() {
	return QString::fromLatin1(SimplifiedChinese);
}

QString Normalize(const QString& language) {
	auto value = language.trimmed();
	value.replace(QLatin1Char('-'), QLatin1Char('_'));
	if (value.startsWith(QStringLiteral("en"), Qt::CaseInsensitive)) {
		return QString::fromLatin1(English);
	}
	if (value.startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)) {
		return QString::fromLatin1(SimplifiedChinese);
	}
	return Default();
}

QString Read(QSettings& settings) {
	settings.beginGroup(QString::fromLatin1(LauncherGroup));
	const auto value = settings.value(QString::fromLatin1(LanguageKey), Default()).toString();
	settings.endGroup();
	return Normalize(value);
}

QString ReadStartupPreference() {
	const auto settings = OpenSettings();
	return Read(*settings);
}

void Write(QSettings& settings, const QString& language) {
	settings.beginGroup(QString::fromLatin1(LauncherGroup));
	settings.setValue(QString::fromLatin1(LanguageKey), Normalize(language));
	settings.endGroup();
}

bool Install(QCoreApplication& application, QTranslator& launcher_translator,
             QTranslator& qt_translator, const QString& language) {
	const auto normalized = Normalize(language);
	QLocale::setDefault(QLocale(normalized));
	if (normalized == QLatin1String(English)) {
		return true;
	}

	const bool launcher_loaded =
	    launcher_translator.load(QStringLiteral(":/i18n/launcher_zh_CN.qm"));
	if (launcher_loaded) {
		application.installTranslator(&launcher_translator);
	}
	if (LoadQtTranslation(qt_translator, normalized)) {
		application.installTranslator(&qt_translator);
	}
	return launcher_loaded;
}

} // namespace LauncherLanguage
