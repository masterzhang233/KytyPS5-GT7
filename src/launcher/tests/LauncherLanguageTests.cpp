#include "launcherLanguage.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTranslator>

#include <cstdlib>
#include <iostream>

namespace {

void Check(bool condition, const char* message) {
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		std::exit(EXIT_FAILURE);
	}
}

} // namespace

int main(int argc, char* argv[]) {
	QCoreApplication application(argc, argv);

	Check(LauncherLanguage::Default() == QStringLiteral("zh_CN"),
	      "first-run language is not Simplified Chinese");
	Check(LauncherLanguage::Normalize(QStringLiteral("zh-CN")) == QStringLiteral("zh_CN"),
	      "zh-CN alias was not normalized");
	Check(LauncherLanguage::Normalize(QStringLiteral("en-GB")) == QStringLiteral("en_US"),
	      "English alias was not normalized");
	Check(LauncherLanguage::Normalize(QStringLiteral("unsupported")) == QStringLiteral("zh_CN"),
	      "invalid language did not fall back to Simplified Chinese");

	QTemporaryDir directory;
	Check(directory.isValid(), "temporary directory creation failed");
	QSettings settings(directory.filePath(QStringLiteral("launcher.ini")), QSettings::IniFormat);
	Check(LauncherLanguage::Read(settings) == QStringLiteral("zh_CN"),
	      "empty settings did not use Simplified Chinese");
	LauncherLanguage::Write(settings, QStringLiteral("en-US"));
	settings.sync();
	Check(LauncherLanguage::Read(settings) == QStringLiteral("en_US"),
	      "English preference did not round-trip");
	Check(settings.value(QStringLiteral("Launcher/interface_language")).toString() ==
	          QStringLiteral("en_US"),
	      "language preference was written outside the launcher group");

	QTranslator launcher_translator;
	QTranslator qt_translator;
	Check(LauncherLanguage::Install(application, launcher_translator, qt_translator,
	                                QStringLiteral("zh_CN")),
	      "embedded Simplified Chinese catalog could not be loaded");
	Check(QCoreApplication::translate("MainDialogPrivate", "Diagnostic logs") ==
	          QStringLiteral("诊断日志"),
	      "embedded catalog did not translate launcher text");

	std::cout << "Launcher language tests passed\n";
	return EXIT_SUCCESS;
}
