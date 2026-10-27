#include "mainDialog.h"
#include "launcherLanguage.h"
#include "launcherTheme.h"

#include <QApplication>
#include <QTranslator>

int main(int argc, char* argv[]) {
	QApplication a(argc, argv);
	QTranslator  launcher_translator;
	QTranslator  qt_translator;
	(void)LauncherLanguage::Install(a, launcher_translator, qt_translator,
	                                LauncherLanguage::ReadStartupPreference());
	LauncherTheme::Initialize(a);

	MainDialog w;

	w.emit Start();

	w.show();

	return QApplication::exec();
}
