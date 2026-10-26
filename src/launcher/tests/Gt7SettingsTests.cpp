#include "configuration.h"
#include "gt7Settings.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>

namespace {
int failures = 0;
void Check(bool pass, const char* what) {
	if (!pass) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		++failures;
	}
}
}

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	Configuration original;
	original.title_id = "PPSA01317";
	Check(Gt7ProfileArguments(original).isEmpty(), "existing/native defaults do not add a patch");
	original.gt7_desktop_height = 1440;
	original.gt7_vr_scale = 120;
	Check(Gt7ProfileArguments(original) == QStringList {"--gt7-desktop-height", "1440"},
	      "desktop internal size is a separate request");
	const auto output_before = original.screen_resolution;
	original.gt7_vr_enabled = true;
	Check(Gt7ProfileArguments(original) == QStringList {"--vr", "--gt7-vr-scale", "120"},
	      "VR selects only the VR profile even when desktop preset is remembered");
	Check(original.screen_resolution == output_before, "profile construction preserves output resolution");
	Configuration copied;
	copied.CopyGameInfoFrom(original);
	copied.CopyEmulatorSettingsFrom(original);
	Check(Gt7ProfileArguments(copied) == Gt7ProfileArguments(original), "per-game copy retains profile");
	QTemporaryDir temporary;
	Check(temporary.isValid(), "temporary settings directory");
	QSettings stored(temporary.filePath("settings.ini"), QSettings::IniFormat);
	original.WriteSettings(&stored);
	stored.sync();
	Configuration loaded;
	loaded.ReadSettings(&stored);
	loaded.title_id = original.title_id;
	Check(loaded.gt7_desktop_height == 1440 && loaded.gt7_vr_enabled && loaded.gt7_vr_scale == 120,
	      "settings round-trip retains desktop choice and VR choice independently");
	QSettings old_settings(temporary.filePath("old.ini"), QSettings::IniFormat);
	Configuration old;
	old.ReadSettings(&old_settings);
	Check(old.gt7_desktop_height == 0 && !old.gt7_vr_enabled && old.gt7_vr_scale == 100,
	      "older settings receive safe native/VR-off defaults");
	for (const auto height: {0, 1080, 1440, 2160}) {
		Configuration parsed;
		QString error;
		Check(parsed.SetGameSettings({{"gt7_desktop_height", QString::number(height)}}, error),
		      "supported desktop setting imports");
	}
	for (const auto scale: {100, 110, 120, 125}) {
		Configuration parsed;
		QString error;
		Check(parsed.SetGameSettings({{"gt7_vr_scale", QString::number(scale)}}, error),
		      "supported eye scale imports");
	}
	for (const auto bad: {"1620", "1440junk", "-1", "99999"}) {
		Configuration parsed;
		QString error;
		Check(!parsed.SetGameSettings({{"gt7_desktop_height", QString::fromLatin1(bad)}}, error),
		      "unsupported or malformed desktop imports rejected");
	}
	for (const auto bad: {"101", "1.10", "-1", "110junk"}) {
		Configuration parsed;
		QString error;
		Check(!parsed.SetGameSettings({{"gt7_vr_scale", QString::fromLatin1(bad)}}, error),
		      "unsupported or malformed VR imports rejected");
	}
	copied.title_id = "PPSA99999";
	Check(!HasGt7Profile(copied) && Gt7ProfileArguments(copied).isEmpty(),
	      "other titles do not receive GT7 flags inherited from settings");
	std::printf("GT7 launcher settings tests: %s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}
