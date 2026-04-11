// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Lib
//
// Source asset -- assembler source file reference.
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#include "source.h"
#include "source.rasm.h"
#include <project/project.h>

namespace RetrodevLib {

	//
	bool SourceBuild::Build(const SourceParams* params) {
		if (params == nullptr) {
			Log::Error(LogChannel::Build, "[Build] No build parameters provided.");
			return false;
		}
		//
		// Build items with no source files are valid -- they may exist solely to orchestrate
		// dependencies (e.g., a "meta-build" that triggers other build items in sequence).
		// If there are no sources, return success immediately after dependencies have run.
		//
		if (params->sources.empty())
			return true;
		//
		// Resolve project folder once -- all relative paths are anchored here
		//
		const std::string projectDir = Project::GetProjectFolder();
		//
		// Expand path variables (e.g. $(sdk)) in sources and include dirs before dispatching to any tool
		//
		std::vector<std::string> expandedSources;
		expandedSources.reserve(params->sources.size());
		for (const auto& source : params->sources)
			expandedSources.push_back(Project::ExpandPath(source));
		std::vector<std::string> expandedIncludeDirs;
		expandedIncludeDirs.reserve(params->includeDirs.size());
		for (const auto& dir : params->includeDirs)
			expandedIncludeDirs.push_back(Project::ExpandPath(dir));
		//
		// Dispatch to the selected tool back-end
		//
		if (params->tool == "RASM") {
			const std::string& toolOpts = [&]() -> const std::string& {
				static const std::string empty;
				auto it = params->toolOptions.find("RASM");
				return (it != params->toolOptions.end()) ? it->second : empty;
			}();
			for (const auto& source : expandedSources) {
				if (!RasmImpl::Build(source, expandedIncludeDirs, params->defines, toolOpts, projectDir))
					return false;
			}
			return true;
		}
		//
		// Unknown tool
		//
		char msg[256];
		std::snprintf(msg, sizeof(msg), "[Build] Unknown build tool: '%s'", params->tool.c_str());
		Log::Error(LogChannel::Build, "%s", msg);
		return false;
	}

}
