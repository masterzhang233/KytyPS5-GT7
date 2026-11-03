#include "common/subsystems.h"

#include <cstdlib>
#include <cstring>

namespace Common {

static Subsystems* g_active_subsystems = nullptr;

Subsystems::Subsystems(bool print): m_print(print) {
	m_active.reserve(16);
	g_active_subsystems = this;
}

Subsystems::~Subsystems() {
	Destroy();
	if (g_active_subsystems == this) {
		g_active_subsystems = nullptr;
	}
}

void Subsystems::Destroy() {
	const auto* setting = std::getenv("KYTY_SHUTDOWN_TRACE");
	const bool trace = setting != nullptr && std::strcmp(setting, "1") == 0;
	for (auto it = m_active.rbegin(); it != m_active.rend(); ++it) {
		if (it->shutdown != nullptr) {
			if (trace) {
				std::fprintf(stderr, "Shutdown trace: begin subsystem %s\n", it->name);
				std::fflush(stderr);
			}
			it->shutdown();
			if (trace) {
				std::fprintf(stderr, "Shutdown trace: end subsystem %s\n", it->name);
				std::fflush(stderr);
			}
		}
	}
	m_active.clear();
}

void Subsystems::EmergencyShutdown() {
	for (auto it = m_active.rbegin(); it != m_active.rend(); ++it) {
		if (it->emergency_shutdown != nullptr) {
			it->emergency_shutdown();
		}
	}
	m_active.clear();
}

void Subsystems::EmergencyShutdownActive() {
	if (g_active_subsystems != nullptr) {
		g_active_subsystems->EmergencyShutdown();
	}
}

} // namespace Common
