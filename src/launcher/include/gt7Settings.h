#ifndef LAUNCHER_INCLUDE_GT7_SETTINGS_H_
#define LAUNCHER_INCLUDE_GT7_SETTINGS_H_

#include "configuration.h"

#include <QDir>
#include <QProcessEnvironment>

inline bool IsGt7Title(const Configuration& info) {
	return info.title_id.compare(QStringLiteral("PPSA01317"), Qt::CaseInsensitive) == 0;
}

// Launcher 1.0.0 is desktop-only. Legacy VR keys remain readable for settings compatibility,
// but cannot turn into VR command-line arguments through any launcher entry point.
inline void ApplyGt7DesktopPreset(Configuration& info) {
	if (!IsGt7Title(info)) {
		return;
	}
	info.gt7_vr_enabled = false;
	info.printf_direction = Configuration::LogDirection::Silent;
	if (!info.gt7_dlss_enabled) {
		return;
	}
	info.gt7_desktop_height = 1080;
	info.screen_resolution = Configuration::Resolution::R2560X1440;
	info.present_mode = Configuration::PresentMode::Mailbox;
	info.tessellation_enabled = true;
	info.bindless_images_enabled = true;
	info.reflections_half_rate = false;
	info.skip_shader_hashes.clear();
	info.readback_linear_images = false;
	info.shader_optimization_type = Configuration::ShaderOptimizationType::Performance;
	info.shader_log_direction = Configuration::LogDirection::Silent;
	info.vulkan_validation_enabled = false;
	info.shader_validation_enabled = false;
	info.command_buffer_dump_enabled = false;
	info.profiler_enabled = false;
	info.renderdoc_enabled = false;
#if defined(_WIN32)
	info.red_zone_protection_enabled = true;
#endif
}

// These are profile requests, not authorization to patch an executable. The emulator verifies
// the exact executable fingerprint and instruction bytes before applying its in-memory patch.
inline bool HasGt7Profile(const Configuration& info) {
	return IsGt7Title(info) && (info.gt7_dlss_enabled || info.gt7_desktop_height != 0);
}

inline QStringList Gt7ProfileArguments(const Configuration& info) {
	if (!HasGt7Profile(info)) {
		return {};
	}
	return {QStringLiteral("--gt7-desktop-height"),
	        QString::number(info.gt7_dlss_enabled ? 1080 : info.gt7_desktop_height)};
}

inline QProcessEnvironment Gt7LaunchEnvironment(const Configuration& info,
                                                const QString& runtime_directory,
                                                QProcessEnvironment environment) {
	if (!IsGt7Title(info)) {
		return environment;
	}
	// Playtest/diagnostic shells must not leak capture or tuning overrides into ordinary play.
	for (const auto& key: environment.keys()) {
		if (key.startsWith(QStringLiteral("KYTY_"), Qt::CaseInsensitive) ||
		    key.startsWith(QStringLiteral("GT7_"), Qt::CaseInsensitive)) {
			environment.remove(key);
		}
	}
	const auto enabled = info.gt7_dlss_enabled ? QStringLiteral("1") : QStringLiteral("0");
	environment.insert(QStringLiteral("KYTY_FG_MANUAL_ONLY"), QStringLiteral("1"));
	environment.insert(QStringLiteral("KYTY_GT7_DLSS"), enabled);
	environment.insert(QStringLiteral("KYTY_GT7_FSR_FG"), enabled);
	environment.insert(QStringLiteral("KYTY_GT7_FSR_FG_OCCLUSION_GUARD"), enabled);
	if (info.gt7_dlss_enabled) {
		const QDir runtime(runtime_directory);
		environment.insert(QStringLiteral("KYTY_GT7_DLSS_BACKEND"),
		                   runtime.absoluteFilePath(QStringLiteral("kyty_dlss_backend.dll")));
		environment.insert(QStringLiteral("KYTY_GT7_DLSS_RUNTIME"), runtime.absolutePath());
	}
	return environment;
}

#endif
