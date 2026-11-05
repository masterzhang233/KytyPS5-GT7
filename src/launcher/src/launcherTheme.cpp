#include "launcherTheme.h"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QStyleFactory>

namespace LauncherTheme {

void Initialize(QApplication& application) {
	application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
	QPalette palette;
	palette.setColor(QPalette::Window, QColor(20, 24, 33));
	palette.setColor(QPalette::WindowText, QColor(235, 237, 244));
	palette.setColor(QPalette::Base, QColor(16, 21, 30));
	palette.setColor(QPalette::AlternateBase, QColor(27, 34, 46));
	palette.setColor(QPalette::Text, QColor(235, 237, 244));
	palette.setColor(QPalette::Button, QColor(36, 43, 56));
	palette.setColor(QPalette::ButtonText, QColor(235, 237, 244));
	palette.setColor(QPalette::Highlight, QColor(176, 37, 56));
	palette.setColor(QPalette::HighlightedText, Qt::white);
	palette.setColor(QPalette::Link, QColor(255, 117, 132));
	palette.setColor(QPalette::Disabled, QPalette::Text, QColor(117, 128, 147));
	palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(117, 128, 147));
	application.setPalette(palette);
	QFile style_sheet_file(QStringLiteral(":/styles/launcher.qss"));
	if (style_sheet_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		application.setStyleSheet(QString::fromUtf8(style_sheet_file.readAll()));
	}
}
} // namespace LauncherTheme
