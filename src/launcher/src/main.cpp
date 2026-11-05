#include "mainDialog.h"
#include "launcherLanguage.h"
#include "launcherTheme.h"

#include <QApplication>
#include <QCheckBox>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QTimer>
#include <QTranslator>

int main(int argc, char* argv[]) {
	QApplication a(argc, argv);
	const auto arguments = a.arguments();
	const int screenshot_option = arguments.indexOf(QStringLiteral("--ui-screenshot"));
	const bool smoke = screenshot_option >= 0 && screenshot_option + 1 < arguments.size();
	if (smoke) {
		// Preview never changes real settings, starts the emulator, or prompts for game folders.
		qputenv("KYTY_LAUNCHER_UI_SMOKE", "1");
	}
	QTranslator  launcher_translator;
	QTranslator  qt_translator;
	(void)LauncherLanguage::Install(a, launcher_translator, qt_translator,
	                                LauncherLanguage::ReadStartupPreference());
	LauncherTheme::Initialize(a);

	MainDialog w;

	if (!smoke) {
		w.emit Start();
	} else {
		w.resize(1120, 760);
	}

	w.show();
	if (smoke) {
		const auto path = arguments.at(screenshot_option + 1);
		QTimer::singleShot(600, &w, [&a, &w, path] {
			const bool saved = w.grab().save(path);
			QJsonObject state;
			state.insert(QStringLiteral("title"), w.windowTitle());
			state.insert(QStringLiteral("width"), w.width());
			state.insert(QStringLiteral("height"), w.height());
			const auto* checkbox = w.findChild<QCheckBox*>(QStringLiteral("gt7_dlss_checkbox"));
			state.insert(QStringLiteral("dlss_checked"), checkbox != nullptr && checkbox->isChecked());
			state.insert(QStringLiteral("vr_entry_present"),
			             w.findChild<QPushButton*>(QStringLiteral("gt7_start_vr_button")) != nullptr);
			QFile metadata(path + QStringLiteral(".json"));
			if (metadata.open(QIODevice::WriteOnly)) {
				metadata.write(QJsonDocument(state).toJson());
			}
			a.exit(saved ? 0 : 1);
		});
	}

	return QApplication::exec();
}
