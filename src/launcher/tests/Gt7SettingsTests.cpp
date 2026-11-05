#include "configuration.h"
#include "gt7Settings.h"
#include "wheelMappingDialog.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTranslator>
#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
void Check(bool pass, const char* what) {
	if (!pass) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		++failures;
	}
}

class TranslationContextProbe final: public QTranslator {
public:
	bool isEmpty() const override { return false; }
	QString translate(const char* context, const char* source, const char*, int) const override {
		return std::strcmp(source, "Racing wheel") == 0 ? QString::fromLatin1(context) : QString();
	}
};
}

int main(int argc, char** argv) {
	QCoreApplication app(argc, argv);
	TranslationContextProbe probe;
	app.installTranslator(&probe);
	Check(WheelMappingDialog::tr("Racing wheel") == QStringLiteral("WheelMappingDialog"),
	      "wheel dialog uses its own translation context instead of QDialog");
	app.removeTranslator(&probe);
	Configuration original;
	original.title_id = "PPSA01317";
	Check(!original.controller.wheel_enabled, "racing wheel is disabled for new configurations");
	Check(original.console_language == 11, "new configurations default to Simplified Chinese");
	Check(original.gt7_dlss_enabled && Gt7ProfileArguments(original) == QStringList {"--gt7-desktop-height", "1080"}, "fresh launcher defaults to fixed DLSS desktop profile");
	original.gt7_dlss_enabled = false;
	original.gt7_desktop_height = 1440;
	original.gt7_vr_scale = 120;
	Check(Gt7ProfileArguments(original) == QStringList {"--gt7-desktop-height", "1440"},
	      "desktop internal size is a separate request");
	const auto output_before = original.screen_resolution;
	original.gt7_vr_enabled = true;
	Check(Gt7ProfileArguments(original) == QStringList {"--gt7-desktop-height", "1440"},
	      "legacy VR preferences cannot enable VR from the desktop launcher");
	Check(original.screen_resolution == output_before, "profile construction preserves output resolution");
	Configuration copied;
	copied.CopyGameInfoFrom(original);
	copied.CopyEmulatorSettingsFrom(original);
	Check(Gt7ProfileArguments(copied) == Gt7ProfileArguments(original), "per-game copy retains profile");
	QTemporaryDir temporary;
	Check(temporary.isValid(), "temporary settings directory");
	QSettings stored(temporary.filePath("settings.ini"), QSettings::IniFormat);
	original.controller.wheel_enabled = true;
	original.controller.wheel_config = temporary.filePath("wheel.json");
	original.WriteSettings(&stored);
	original.controller.WriteSettings(&stored);
	stored.sync();
	Configuration loaded;
	loaded.ReadSettings(&stored);
	loaded.controller.ReadSettings(&stored);
	loaded.title_id = original.title_id;
	Check(loaded.gt7_desktop_height == 1440 && loaded.gt7_vr_enabled && loaded.gt7_vr_scale == 120,
	      "settings round-trip retains desktop choice and VR choice independently");
	Check(loaded.controller.wheel_enabled && Gt7ProfileArguments(loaded) == Gt7ProfileArguments(original),
	      "global wheel preference survives settings round-trip alongside the VR profile");
	Check(!stored.contains("wheel_config") && loaded.controller.wheel_config.isEmpty(),
	      "wheel configuration path remains derived from the current settings location");
	loaded.controller.wheel_config = temporary.filePath("wheel.json");
	Configuration per_game;
	per_game.gt7_desktop_height = 2160;
	per_game.gt7_vr_enabled = true;
	per_game.gt7_vr_scale = 125;
	per_game.gt7_dlss_enabled = false;
	loaded.CopyEmulatorSettingsFrom(per_game);
	Check(loaded.controller.wheel_enabled && loaded.controller.wheel_config == original.controller.wheel_config &&
	          loaded.gt7_desktop_height == 2160 &&
	          Gt7ProfileArguments(loaded) == QStringList {"--gt7-desktop-height", "2160"},
	      "per-game rendering settings retain the global wheel preference and mapping path");
	loaded.controller.wheel_enabled = false;
	Check(Gt7ProfileArguments(loaded) == QStringList {"--gt7-desktop-height", "2160"},
	      "disabling the wheel preserves the desktop profile");
	QSettings old_settings(temporary.filePath("old.ini"), QSettings::IniFormat);
	Configuration old;
	old.ReadSettings(&old_settings);
	old.controller.ReadSettings(&old_settings);
	Check(old.console_language == 11, "missing language setting defaults to Simplified Chinese");
	Check(old.gt7_desktop_height == 0 && !old.gt7_vr_enabled && old.gt7_vr_scale == 100,
	      "older settings receive safe native/VR-off defaults");
	Check(old.gt7_dlss_enabled, "missing DLSS settings default to enabled");
	Check(!loaded.gt7_dlss_enabled, "disabled DLSS preference survives settings and per-game copy");
	Check(!old.controller.wheel_enabled, "older settings leave racing-wheel emulation disabled");
	QSettings explicit_settings(temporary.filePath("explicit-language.ini"), QSettings::IniFormat);
	explicit_settings.setValue(QStringLiteral("console_language"), 1);
	explicit_settings.sync();
	Configuration explicit_language;
	explicit_language.ReadSettings(&explicit_settings);
	Check(explicit_language.console_language == 1,
	      "existing explicit console language is preserved");
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
	Configuration preset;
	preset.title_id = "PPSA01317";
	preset.gt7_vr_enabled = true;
	preset.gt7_desktop_height = 2160;
	preset.screen_resolution = Configuration::Resolution::R3840X2160;
	preset.user_id = 1000;
	preset.reflections_half_rate = true;
	preset.skip_shader_hashes = "1234";
	ApplyGt7DesktopPreset(preset);
	Check(preset.screen_resolution == Configuration::Resolution::R2560X1440 &&
	          preset.gt7_desktop_height == 1080 && !preset.gt7_vr_enabled,
	      "preset overrides legacy resolution and VR without tunable alternatives");
	Check(preset.user_id == 1000 && preset.printf_direction == Configuration::LogDirection::Silent,
	      "preset preserves save identity and avoids expensive API trace logs");
	Check(!preset.reflections_half_rate && preset.skip_shader_hashes.isEmpty(),
	      "fixed preset cannot inherit old skipped shaders or half-rate reflections");
	QProcessEnvironment inherited;
	inherited.insert("PATH", "keep");
	inherited.insert("KYTY_GT7_FSR_FG_DEBUG", "0");
	inherited.insert("KYTY_GT7_FSR_FG_CAPTURE_OUTPUTS", "1");
	inherited.insert("KYTY_FG_FRAMES", "100");
	inherited.insert("KYTY_GT7_DLSS_TRACE", "1");
	inherited.insert("GT7_OLD_OVERRIDE", "1");
	const auto enabled_env = Gt7LaunchEnvironment(preset, temporary.path(), inherited);
	Check(enabled_env.value("KYTY_GT7_DLSS") == "1" &&
	          enabled_env.value("KYTY_GT7_FSR_FG") == "1" &&
	          enabled_env.value("KYTY_GT7_FSR_FG_OCCLUSION_GUARD") == "1",
	      "one enabled switch requests DLSS, frame generation and occlusion guard together");
	Check(enabled_env.value("KYTY_FG_MANUAL_ONLY") == "1" &&
	          !enabled_env.contains("KYTY_GT7_FSR_FG_DEBUG") &&
	          !enabled_env.contains("KYTY_GT7_FSR_FG_CAPTURE_OUTPUTS") &&
	          !enabled_env.contains("KYTY_FG_FRAMES") &&
	          !enabled_env.contains("KYTY_GT7_DLSS_TRACE") &&
	          !enabled_env.contains("GT7_OLD_OVERRIDE") && enabled_env.value("PATH") == "keep",
	      "ordinary play removes inherited debug flags and suppresses automatic captures");
	Check(enabled_env.value("KYTY_GT7_DLSS_BACKEND") == temporary.filePath("kyty_dlss_backend.dll") &&
	          enabled_env.value("KYTY_GT7_DLSS_RUNTIME") == temporary.path(),
	      "portable runtime paths resolve relative to the selected emulator");
	preset.gt7_dlss_enabled = false;
	preset.screen_resolution = Configuration::Resolution::R1920X1080;
	preset.printf_direction = Configuration::LogDirection::File;
	ApplyGt7DesktopPreset(preset);
	const auto disabled_env = Gt7LaunchEnvironment(preset, temporary.path(), enabled_env);
	Check(disabled_env.value("KYTY_GT7_DLSS") == "0" &&
	          disabled_env.value("KYTY_GT7_FSR_FG") == "0" &&
	          disabled_env.value("KYTY_GT7_FSR_FG_OCCLUSION_GUARD") == "0" &&
	          !disabled_env.contains("KYTY_GT7_DLSS_BACKEND"),
	      "turning off the switch really disables the entire preset despite inherited settings");
	Check(preset.screen_resolution == Configuration::Resolution::R1920X1080 &&
	          preset.printf_direction == Configuration::LogDirection::Silent,
	      "unchecked preset preserves desktop output but still disables high-volume API logs");
	copied.title_id = "PPSA99999";
	Check(!HasGt7Profile(copied) && Gt7ProfileArguments(copied).isEmpty(),
	      "other titles do not receive GT7 flags inherited from settings");
	std::printf("GT7 launcher settings tests: %s\n", failures == 0 ? "PASS" : "FAIL");
	return failures == 0 ? 0 : 1;
}
