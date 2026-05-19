// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Gui
//
// Version check -- Linux fallback implementation.
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#include <app/version.check/version.check.h>

namespace RetrodevGui {

	static VersionCheckResult s_result;

	void VersionCheck::StartAsync(const std::string& /*manifestUrl*/) {
		s_result = VersionCheckResult{};
		s_result.state = VersionCheckState::Failed;
	}

	VersionCheckResult VersionCheck::Poll() {
		return s_result;
	}

	void VersionCheck::Shutdown() {
		s_result = VersionCheckResult{};
	}

}
