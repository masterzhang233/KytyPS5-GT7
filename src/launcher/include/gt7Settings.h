#ifndef LAUNCHER_INCLUDE_GT7_SETTINGS_H_
#define LAUNCHER_INCLUDE_GT7_SETTINGS_H_

#include "configuration.h"

// These are profile requests, not authorization to patch an executable. The emulator verifies
// the exact file hash and original instruction bytes before loading a mandatory GT7 patch.
inline bool HasGt7Profile(const Configuration& info) {
	return info.title_id.compare(QStringLiteral("PPSA01317"), Qt::CaseInsensitive) == 0 &&
	       (info.gt7_vr_enabled || info.gt7_desktop_height != 0);
}

inline QStringList Gt7ProfileArguments(const Configuration& info) {
	if (!HasGt7Profile(info)) {
		return {};
	}
	if (info.gt7_vr_enabled) {
		return {QStringLiteral("--vr"), QStringLiteral("--gt7-vr-scale"),
		        QString::number(info.gt7_vr_scale)};
	}
	return {QStringLiteral("--gt7-desktop-height"), QString::number(info.gt7_desktop_height)};
}

#endif
