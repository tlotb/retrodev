// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Gui
//
// Build document -- editor for build pipeline items (sources, output, debug).
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#include "document.build.h"
#include "document.build.settings.h"
#include <app/app.h>
#include <app/app.icons.mdi.h>
#include <app/app.console.h>
#include <views/main.view.project.h>
#include <algorithm>
#include <vector>

namespace RetrodevGui {
	//
	// Right-labelled InputText synced to a std::string.
	// Renders the field filling available width minus the label, with the label
	// shown as disabled text to the right.  Returns true when the value changed.
	//
	static bool EmuInput(const char* label, char* buf, size_t bufSize, std::string& str) {
		if (str != buf)
			std::snprintf(buf, bufSize, "%s", str.c_str());
		float labelWidth = ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemSpacing.x;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - labelWidth);
		std::string widgetId = std::string("##emu_") + label;
		bool changed = ImGui::InputText(widgetId.c_str(), buf, bufSize);
		if (changed)
			str = buf;
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", label);
		return changed;
	}
	//
	// File-picker row: InputText + small browse button that opens a popup listing
	// all project files matching extensions (e.g. {"dsk","sna"}).
	// popupId must be unique per row.  Returns true when the value changed.
	//
	static bool EmuFilePicker(const char* label, const char* popupId, char* buf, size_t bufSize, std::string& str, const std::vector<std::string>& extensions) {
		if (str != buf)
			std::snprintf(buf, bufSize, "%s", str.c_str());
		float browseWidth = ImGui::CalcTextSize(ICON_FOLDER_OPEN).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
		float labelWidth = ImGui::CalcTextSize(label).x + ImGui::GetStyle().ItemSpacing.x;
		bool changed = false;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth - labelWidth);
		std::string inputId = std::string("##emufile_") + label;
		if (ImGui::InputText(inputId.c_str(), buf, bufSize)) {
			str = buf;
			changed = true;
		}
		ImGui::SameLine();
		std::string btnId = std::string(ICON_FOLDER_OPEN "##browse_") + label;
		if (ImGui::Button(btnId.c_str()))
			ImGui::OpenPopup(popupId);
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", label);
		//
		// File picker popup: list matching files found under the project folder
		//
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
		if (ImGui::BeginPopup(popupId)) {
			std::vector<std::string> files = RetrodevLib::Project::GetFilesByExtensions(extensions);
			if (files.empty()) {
				ImGui::TextDisabled("No matching files found in project folder");
			} else {
				//
				// Child window with fixed max height for scrolling when list is long
				//
				float maxHeight = ImGui::GetFontSize() * 20.0f;
				if (ImGui::BeginChild("##FilePickerList", ImVec2(ImGui::GetFontSize() * 30.0f, maxHeight), false)) {
					for (const auto& f : files) {
						if (ImGui::Selectable(f.c_str())) {
							std::snprintf(buf, bufSize, "%s", f.c_str());
							str = buf;
							changed = true;
							ImGui::CloseCurrentPopup();
						}
					}
				}
				ImGui::EndChild();
			}
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
		return changed;
	}
	//
	// Constructor: name only, build items have no source file
	//
	DocumentBuild::DocumentBuild(const std::string& name) : DocumentView(name, "") {}

	DocumentBuild::~DocumentBuild() {}
	//
	// Render the Sources panel: ordered list of source files with add / remove / reorder controls
	//
	void DocumentBuild::RenderSources(RetrodevLib::SourceParams* params, float listHeight) {
		ImGui::SeparatorText(ICON_FILE_CODE " Sources");
		//
		// Source list
		//
		if (ImGui::BeginChild("##SourcesList", ImVec2(0.0f, listHeight), true)) {
			for (int i = 0; i < (int)params->sources.size(); i++) {
				bool selected = (m_selectedSourceIdx == i);
				if (ImGui::Selectable(params->sources[i].c_str(), selected))
					m_selectedSourceIdx = i;
			}
		}
		ImGui::EndChild();
		//
		// Add / Remove / Move controls
		//
		bool hasSelection = (m_selectedSourceIdx >= 0 && m_selectedSourceIdx < (int)params->sources.size());
		if (ImGui::Button(ICON_PLUS " Add##Src"))
			ImGui::OpenPopup("##SourcePickerPopup");
		//
		// Source picker popup: lists project source files not already in the build list
		//
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
		if (ImGui::BeginPopup("##SourcePickerPopup")) {
			std::vector<std::string> projectSources = RetrodevLib::Project::GetSourceFiles();
			bool anyAvailable = false;
			//
			// Child window with fixed max height for scrolling when list is long
			//
			float maxHeight = ImGui::GetFontSize() * 20.0f;
			if (ImGui::BeginChild("##SourcePickerList", ImVec2(ImGui::GetFontSize() * 30.0f, maxHeight), false)) {
				for (const auto& src : projectSources) {
					bool alreadyAdded = false;
					for (const auto& existing : params->sources) {
						if (existing == src) {
							alreadyAdded = true;
							break;
						}
					}
					if (alreadyAdded)
						continue;
					anyAvailable = true;
					if (ImGui::Selectable(src.c_str())) {
						params->sources.push_back(src);
						RetrodevLib::Project::MarkAsModified();
						SetModified(true);
						ImGui::CloseCurrentPopup();
					}
				}
			}
			ImGui::EndChild();
			if (!anyAvailable)
				ImGui::TextDisabled("No source files available");
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
		ImGui::SameLine();
		ImGui::BeginDisabled(!hasSelection);
		if (ImGui::Button(ICON_MINUS "##SrcRemove")) {
			params->sources.erase(params->sources.begin() + m_selectedSourceIdx);
			m_selectedSourceIdx = -1;
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Remove selected source");
		ImGui::SameLine();
		if (ImGui::Button(ICON_ARROW_UP "##SrcUp") && m_selectedSourceIdx > 0) {
			std::swap(params->sources[m_selectedSourceIdx], params->sources[m_selectedSourceIdx - 1]);
			m_selectedSourceIdx--;
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Move up");
		ImGui::SameLine();
		if (ImGui::Button(ICON_ARROW_DOWN "##SrcDown") && m_selectedSourceIdx < (int)params->sources.size() - 1) {
			std::swap(params->sources[m_selectedSourceIdx], params->sources[m_selectedSourceIdx + 1]);
			m_selectedSourceIdx++;
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Move down");
		ImGui::EndDisabled();
	}
	//
	// Render the Include Directories panel: search paths passed to the assembler/compiler (-I)
	//
	void DocumentBuild::RenderIncludeDirs(RetrodevLib::SourceParams* params, float listHeight) {
		ImGui::SeparatorText(ICON_FOLDER_SEARCH " Include Directories");
		//
		// Include dirs list
		//
		if (ImGui::BeginChild("##IncludeDirsList", ImVec2(0.0f, listHeight), true)) {
			for (int i = 0; i < (int)params->includeDirs.size(); i++) {
				bool selected = (m_selectedIncludeDirIdx == i);
				if (ImGui::Selectable(params->includeDirs[i].c_str(), selected))
					m_selectedIncludeDirIdx = i;
			}
		}
		ImGui::EndChild();
		//
		// Add / Remove controls
		//
		bool hasSelection = (m_selectedIncludeDirIdx >= 0 && m_selectedIncludeDirIdx < (int)params->includeDirs.size());
		if (ImGui::Button(ICON_PLUS " Add##Inc"))
			ImGui::OpenPopup("##IncDirPickerPopup");
		//
		// Folder picker popup: lists unique project folders not already in the include list
		//
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
		if (ImGui::BeginPopup("##IncDirPickerPopup")) {
			std::vector<std::string> projectFolders = RetrodevLib::Project::GetProjectFolders();
			bool anyAvailable = false;
			//
			// Child window with fixed max height for scrolling when list is long
			//
			float maxHeight = ImGui::GetFontSize() * 20.0f;
			if (ImGui::BeginChild("##IncDirPickerList", ImVec2(ImGui::GetFontSize() * 30.0f, maxHeight), false)) {
				for (const auto& folder : projectFolders) {
					bool alreadyAdded = false;
					for (const auto& existing : params->includeDirs) {
						if (existing == folder) {
							alreadyAdded = true;
							break;
						}
					}
					if (alreadyAdded)
						continue;
					anyAvailable = true;
					if (ImGui::Selectable(folder.c_str())) {
						params->includeDirs.push_back(folder);
						RetrodevLib::Project::MarkAsModified();
						SetModified(true);
						ImGui::CloseCurrentPopup();
					}
				}
			}
			ImGui::EndChild();
			if (!anyAvailable)
				ImGui::TextDisabled("No folders available");
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
		ImGui::SameLine();
		ImGui::BeginDisabled(!hasSelection);
		if (ImGui::Button(ICON_MINUS "##IncRemove")) {
			params->includeDirs.erase(params->includeDirs.begin() + m_selectedIncludeDirIdx);
			m_selectedIncludeDirIdx = -1;
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Remove selected directory");
		ImGui::EndDisabled();
	}
	//
	// Render the Defines panel: preprocessor macros injected before assembly (e.g. MYFLAG=1)
	//
	void DocumentBuild::RenderDefines(RetrodevLib::SourceParams* params, float listHeight) {
		ImGui::SeparatorText(ICON_CODE_TAGS " Defines");
		//
		// Defines list
		//
		if (ImGui::BeginChild("##DefinesList", ImVec2(0.0f, listHeight), true)) {
			for (int i = 0; i < (int)params->defines.size(); i++) {
				bool selected = (m_selectedDefineIdx == i);
				if (ImGui::Selectable(params->defines[i].c_str(), selected))
					m_selectedDefineIdx = i;
			}
		}
		ImGui::EndChild();
		//
		// Add / Remove controls
		//
		bool hasSelection = (m_selectedDefineIdx >= 0 && m_selectedDefineIdx < (int)params->defines.size());
		float btnAddWidth = ImGui::CalcTextSize(ICON_PLUS " Add##Def").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		float btnRemWidth = ImGui::CalcTextSize(ICON_MINUS "##DefRemove").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		float spacing = ImGui::GetStyle().ItemSpacing.x;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnAddWidth - btnRemWidth - spacing * 2.0f);
		ImGui::InputText("##DefAdd", m_defineAddBuf, sizeof(m_defineAddBuf));
		ImGui::SameLine();
		if (ImGui::Button(ICON_PLUS " Add##Def")) {
			if (m_defineAddBuf[0] != '\0') {
				params->defines.emplace_back(m_defineAddBuf);
				m_defineAddBuf[0] = '\0';
				RetrodevLib::Project::MarkAsModified();
				SetModified(true);
			}
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(!hasSelection);
		if (ImGui::Button(ICON_MINUS "##DefRemove")) {
			params->defines.erase(params->defines.begin() + m_selectedDefineIdx);
			m_selectedDefineIdx = -1;
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Remove selected define");
		ImGui::EndDisabled();
	}
	//
	// Render the Dependencies panel: ordered list of build items (bitmaps, tilesets, sprites,
	// maps, palettes) that must be processed before this build executes.
	//
	void DocumentBuild::RenderDependencies(RetrodevLib::SourceParams* params) {
		ImGui::SeparatorText(ICON_LINK " Dependencies  " ICON_INFORMATION_OUTLINE);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(
				"Order matters -- items are processed top-to-bottom before the build runs.\n\n"
				"If you use a Palette solver, place it at the top of the list.\n"
				"It must run first to write the solved palette assignments back into\n"
				"bitmaps, tilesets and sprites before those items are converted.\n"
				"If a bitmap or tileset is processed before the solver runs, the\n"
				"solved palette assignments are not yet in place and the conversion\n"
				"will use whatever palette was last stored in the project file.");
		//
		// List fills all remaining vertical space; one button-row is reserved below
		//
		float listHeight = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing();
		if (ImGui::BeginChild("##DepsList", ImVec2(0.0f, listHeight), true)) {
			for (int i = 0; i < (int)params->dependencies.size(); i++) {
				bool selected = (m_selectedDependencyIdx == i);
				if (ImGui::Selectable(params->dependencies[i].c_str(), selected))
					m_selectedDependencyIdx = i;
			}
		}
		ImGui::EndChild();
		//
		// Add / Remove / Move controls
		//
		bool hasSelection = (m_selectedDependencyIdx >= 0 && m_selectedDependencyIdx < (int)params->dependencies.size());
		if (ImGui::Button(ICON_PLUS " Add##Dep"))
			ImGui::OpenPopup("##DepPickerPopup");
		//
		// Dependency picker popup: lists all build items (bitmaps, tilesets, sprites, maps, palettes, builds)
		// not already present in the dependency list and that don't create circular or diamond dependencies
		//
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
		if (ImGui::BeginPopup("##DepPickerPopup")) {
			static const RetrodevLib::ProjectBuildType k_depTypes[] = {
				RetrodevLib::ProjectBuildType::Bitmap, RetrodevLib::ProjectBuildType::Tilemap, RetrodevLib::ProjectBuildType::Sprite,
				RetrodevLib::ProjectBuildType::Map,    RetrodevLib::ProjectBuildType::Palette,  RetrodevLib::ProjectBuildType::Raster,
				RetrodevLib::ProjectBuildType::Build,
			};
			static const char* k_depTypeLabels[] = {"Bitmap", "Tileset", "Sprite", "Map", "Palette", "Raster", "Build"};
			bool anyAvailable = false;
			//
			// Child window with fixed max height for scrolling when list is long
			//
			float maxHeight = ImGui::GetFontSize() * 20.0f;
			if (ImGui::BeginChild("##DepPickerList", ImVec2(ImGui::GetFontSize() * 30.0f, maxHeight), false)) {
				for (int t = 0; t < 7; t++) {
					std::vector<std::string> items = RetrodevLib::Project::GetBuildItemsByType(k_depTypes[t]);
					for (const auto& item : items) {
						//
						// Skip items already in the dependency list
						//
						bool alreadyAdded = false;
						for (const auto& existing : params->dependencies) {
							if (existing == item) {
								alreadyAdded = true;
								break;
							}
						}
						if (alreadyAdded)
							continue;
						//
						// Skip items that would create circular or diamond dependencies
						//
						if (!RetrodevLib::Project::BuildCanAddDependency(m_name, item))
							continue;
						anyAvailable = true;
						//
						// Show label: "name  [Type]" so the user can distinguish between items with the same name
						//
						std::string displayName = item + "  [" + k_depTypeLabels[t] + "]";
						if (ImGui::Selectable(displayName.c_str())) {
							params->dependencies.push_back(item);
							RetrodevLib::Project::MarkAsModified();
							SetModified(true);
							ImGui::CloseCurrentPopup();
						}
					}
				}
			}
			ImGui::EndChild();
			if (!anyAvailable)
				ImGui::TextDisabled("No build items available");
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
		ImGui::SameLine();
		ImGui::BeginDisabled(!hasSelection);
		if (ImGui::Button(ICON_MINUS "##DepRemove")) {
			params->dependencies.erase(params->dependencies.begin() + m_selectedDependencyIdx);
			m_selectedDependencyIdx = -1;
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Remove selected dependency");
		ImGui::SameLine();
		if (ImGui::Button(ICON_ARROW_UP "##DepUp") && m_selectedDependencyIdx > 0) {
			std::swap(params->dependencies[m_selectedDependencyIdx], params->dependencies[m_selectedDependencyIdx - 1]);
			m_selectedDependencyIdx--;
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Move up");
		ImGui::SameLine();
		if (ImGui::Button(ICON_ARROW_DOWN "##DepDown") && m_selectedDependencyIdx < (int)params->dependencies.size() - 1) {
			std::swap(params->dependencies[m_selectedDependencyIdx], params->dependencies[m_selectedDependencyIdx + 1]);
			m_selectedDependencyIdx++;
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Move down");
		ImGui::EndDisabled();
	}
	//
	// Source tab: left column = Dependencies, right column = Sources / Include Dirs / Defines.
	// A horizontal splitter lets the user resize the two columns independently.
	//
	void DocumentBuild::RenderTabSource(RetrodevLib::SourceParams* params) {
		const float thickness = Application::splitterThickness;
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		//
		// Initialise left panel width on first use (35% of available space)
		//
		if (m_srcTabLeftW <= 0.0f)
			m_srcTabLeftW = (avail.x - thickness - spacing) * 0.35f;
		//
		// Derive right panel width from available space each frame
		//
		float rightW = avail.x - m_srcTabLeftW - thickness - spacing;
		float minLeft = ImGui::GetFontSize() * 12.0f;
		float minRight = ImGui::GetFontSize() * 20.0f;
		//
		// Horizontal splitter: Dependencies (left) | Sources / Include Dirs / Defines (right)
		//
		ImGui::DrawSplitter(false, thickness, &m_srcTabLeftW, &rightW, minLeft, minRight);
		if (ImGui::BeginChild("##SrcTabLeft", ImVec2(m_srcTabLeftW, 0.0f), false)) {
			RenderDependencies(params);
		}
		ImGui::EndChild();
		ImGui::SameLine();
		if (ImGui::BeginChild("##SrcTabRight", ImVec2(rightW, 0.0f), false)) {
			//
			// Distribute available vertical space proportionally across the three lists.
			// Each list gets: available / 3, minus its own fixed chrome (SeparatorText + controls row).
			// The chrome cost per panel is one separator + one button row with spacing.
			//
			float fontSize = ImGui::GetFontSize();
			const float chromePerPanel = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
			const float minListH = fontSize * 2.0f;
			const float totalAvail = ImGui::GetContentRegionAvail().y;
			const float slot = (totalAvail - chromePerPanel * 3.0f) / 3.0f;
			const float srcH = std::max(slot, minListH);
			const float incH = std::max(slot, minListH);
			const float defH = std::max(slot, minListH);
			RenderSources(params, srcH);
			RenderIncludeDirs(params, incH);
			RenderDefines(params, defH);
		}
		ImGui::EndChild();
	}
	//
	// Build tab: tool selection combo, then tool-specific options below
	//
	void DocumentBuild::RenderTabBuild(RetrodevLib::SourceParams* params) {
		static const char* const k_tools[] = {"RASM"};
		static const int k_toolCount = 1;
		//
		// Tool selection combo
		//
		ImGui::SeparatorText(ICON_TOOLS " Build Tool");
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
		if (ImGui::BeginCombo("##ToolCombo", params->tool.c_str())) {
			for (int i = 0; i < k_toolCount; i++) {
				bool selected = (params->tool == k_tools[i]);
				if (ImGui::Selectable(k_tools[i], selected)) {
					params->tool = k_tools[i];
					RetrodevLib::Project::MarkAsModified();
					SetModified(true);
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::Spacing();
		//
		// Tool-specific options
		//
		if (params->tool == "RASM")
			m_rasm.RenderBuildOptions(params);
	}
	//
	// Output tab: tool-specific output format options
	//
	void DocumentBuild::RenderTabOutput(RetrodevLib::SourceParams* params) {
		if (params->tool.empty()) {
			ImGui::TextDisabled("Select a build tool in the Build tab first.");
			return;
		}
		if (params->tool == "RASM")
			m_rasm.RenderOutputOptions(params);
	}
	//
	// Debug tab: tool-specific symbol generation and debug options
	//
	void DocumentBuild::RenderTabDebug(RetrodevLib::SourceParams* params) {
		if (params->tool.empty()) {
			ImGui::TextDisabled("Select a build tool in the Build tab first.");
			return;
		}
		if (params->tool == "RASM")
			m_rasm.RenderDebugOptions(params);
		RenderTabDebugCommon(params);
	}
	//
	// Common emulator launch section rendered at the bottom of the Debug tab
	// regardless of which build tool is selected.
	//
	void DocumentBuild::RenderTabDebugCommon(RetrodevLib::SourceParams* params) {
		//
		// Char buffers for path/text InputText widgets -- synced from params strings each frame
		//
		static char emu_media_buf[512] = {};
		static char emu_sna_buf[512] = {};
		static char emu_sym_buf[512] = {};
		static char emu_cmd_buf[512] = {};
		ImGui::Spacing();
		ImGui::SeparatorText(ICON_PLAY " Emulator Launch");
		//
		// Emulator selector combo
		//
		RetrodevLib::SourceParams::EmulatorParams& ep = params->emulatorParams;
		RetrodevLib::SourceParams::EmulatorParams::CommonFields& c = ep.common;
		static const char* const k_emulators[] = {"None", "WinAPE", "RVM", "ACE-DL"};
		const char* currentEmu = ep.emulator.empty() ? "None" : ep.emulator.c_str();
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted("Emulator");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
		if (ImGui::BeginCombo("##EmuCombo", currentEmu)) {
			for (int i = 0; i < 4; i++) {
				bool selected = (i == 0 ? ep.emulator.empty() : ep.emulator == k_emulators[i]);
				if (ImGui::Selectable(k_emulators[i], selected)) {
					ep.emulator = (i == 0) ? "" : k_emulators[i];
					RetrodevLib::Project::MarkAsModified();
					SetModified(true);
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (ep.emulator.empty())
			return;
		//
		// Executable path -- stored in retrodev.ini (machine-local), not in the project file.
		// Sync the current setting into the params struct so the launcher always has a valid path.
		//
		ImGui::Spacing();
		ImGui::SeparatorText("Executable");
		const std::string& exePath = EmulatorSettings::GetExePath(ep.emulator.c_str());
		if (ep.emulator == "WinAPE")
			ep.winape.exePath = exePath;
		else if (ep.emulator == "RVM")
			ep.rvm.exePath = exePath;
		else if (ep.emulator == "ACE-DL")
			ep.acedl.exePath = exePath;
		//
		// Show the current path read-only with a browse button to open the native file dialog
		//
		float browseWidth = ImGui::CalcTextSize(ICON_FOLDER_OPEN).x + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browseWidth);
		ImGui::InputText("##EmuExePath", const_cast<char*>(exePath.c_str()), exePath.size() + 1, ImGuiInputTextFlags_ReadOnly);
		ImGui::SameLine();
		if (ImGui::Button(ICON_FOLDER_OPEN "##EmuExeBrowse"))
			EmulatorSettings::BrowseForExe(ep.emulator.c_str());
		if (ep.emulator == "ACE-DL")
			ImGui::TextDisabled("  ACE-DL is launched with its folder as the working directory");
		// -----------------------------------------------------------------------
		// Common fields
		// -----------------------------------------------------------------------
		ImGui::Spacing();
		ImGui::SeparatorText("Media");
		if (EmuFilePicker("Media file (disc / tape / cartridge)", "##EmuMediaPick", emu_media_buf, sizeof(emu_media_buf), c.mediaFile,
						  {"dsk", "hfe", "cdt", "wav", "xpr", "cpr", "sna", "dsk.gz"})) {
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
		if (EmuFilePicker("Snapshot", "##EmuSnaPick", emu_sna_buf, sizeof(emu_sna_buf), c.snapshot, {"sna"})) {
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
		ImGui::Spacing();
		ImGui::SeparatorText("Debug");
		if (ep.emulator != "RVM") {
			if (EmuFilePicker("Symbol file", "##EmuSymPick", emu_sym_buf, sizeof(emu_sym_buf), c.symbolFile, {"sym", "rasm"})) {
				RetrodevLib::Project::MarkAsModified();
				SetModified(true);
			}
		} else {
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("Symbol file -- not supported by RVM");
		}
		ImGui::Spacing();
		ImGui::SeparatorText("Startup");
		//
		// sendCPM checkbox -- takes priority over command field
		//
		bool sendCPM = c.sendCPM;
		if (ImGui::Checkbox("Send |CPM on startup", &sendCPM)) {
			c.sendCPM = sendCPM;
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
		ImGui::BeginDisabled(c.sendCPM);
		if (ep.emulator == "WinAPE") {
			if (EmuInput("Program to run (/A:)", emu_cmd_buf, sizeof(emu_cmd_buf), c.command)) {
				RetrodevLib::Project::MarkAsModified();
				SetModified(true);
			}
			ImGui::TextDisabled("  Leave empty to use /A without a program name");
		} else if (ep.emulator == "RVM") {
			if (EmuInput("BASIC command (-command=)", emu_cmd_buf, sizeof(emu_cmd_buf), c.command)) {
				RetrodevLib::Project::MarkAsModified();
				SetModified(true);
			}
			ImGui::TextDisabled("  Example: run\"disc\\n  (\\n = Enter key)");
		} else if (ep.emulator == "ACE-DL") {
			if (EmuInput("Auto-run file (-autoRunFile)", emu_cmd_buf, sizeof(emu_cmd_buf), c.command)) {
				RetrodevLib::Project::MarkAsModified();
				SetModified(true);
			}
			ImGui::TextDisabled("  Sends RUN\"<file> to the CPC on startup");
		}
		ImGui::EndDisabled();
		//
		// Machine selector -- only relevant for RVM; others read it from config
		//
		if (ep.emulator == "RVM") {
			ImGui::Spacing();
			ImGui::SeparatorText("Machine (-boot=)");
			static const char* const k_rvm_machines[] = {"cpc464", "cpc664", "cpc6128"};
			const char* currentMach = c.machine.empty() ? k_rvm_machines[2] : c.machine.c_str();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("Machine id");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
			if (ImGui::BeginCombo("##RvmMachine", currentMach)) {
				for (int i = 0; i < 3; i++) {
					bool selected = c.machine == k_rvm_machines[i];
					if (ImGui::Selectable(k_rvm_machines[i], selected)) {
						c.machine = k_rvm_machines[i];
						RetrodevLib::Project::MarkAsModified();
						SetModified(true);
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
		// -----------------------------------------------------------------------
		// ACE-DL -- specific options
		// -----------------------------------------------------------------------
		else if (ep.emulator == "ACE-DL") {
			ImGui::Spacing();
			ImGui::SeparatorText("ACE-DL Hardware");
			//
			// CRTC type: -1 = not specified, 0-4 = explicit type
			//
			static const char* const k_crtc_labels[] = {"Not set", "0", "1", "2", "3", "4"};
			int crtcComboIdx = (ep.acedl.crtc < 0 || ep.acedl.crtc > 4) ? 0 : ep.acedl.crtc + 1;
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("CRTC type (-crtc)");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
			if (ImGui::BeginCombo("##AceCrtc", k_crtc_labels[crtcComboIdx])) {
				for (int i = 0; i < 6; i++) {
					bool selected = (crtcComboIdx == i);
					if (ImGui::Selectable(k_crtc_labels[i], selected)) {
						ep.acedl.crtc = (i == 0) ? -1 : i - 1;
						RetrodevLib::Project::MarkAsModified();
						SetModified(true);
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			//
			// RAM size: 0 = not specified, otherwise one of 64/128/320/576
			//
			static const char* const k_ram_labels[] = {"Not set", "64 KB", "128 KB", "320 KB", "576 KB"};
			static const int k_ram_values[] = {0, 64, 128, 320, 576};
			int ramComboIdx = 0;
			for (int i = 1; i < 5; i++) {
				if (ep.acedl.ram == k_ram_values[i]) {
					ramComboIdx = i;
					break;
				}
			}
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("RAM (-ram)");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
			if (ImGui::BeginCombo("##AceRam", k_ram_labels[ramComboIdx])) {
				for (int i = 0; i < 5; i++) {
					bool selected = (ramComboIdx == i);
					if (ImGui::Selectable(k_ram_labels[i], selected)) {
						ep.acedl.ram = k_ram_values[i];
						RetrodevLib::Project::MarkAsModified();
						SetModified(true);
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			//
			// Firmware locale: -fuk / -ffr / -fsp / -fdk
			//
			static const char* const k_fw_labels[] = {"Default", "UK (-fuk)", "FR (-ffr)", "SP (-fsp)", "DK (-fdk)"};
			static const char* const k_fw_values[] = {"", "uk", "fr", "sp", "dk"};
			int fwComboIdx = 0;
			for (int i = 1; i < 5; i++) {
				if (ep.acedl.firmware == k_fw_values[i]) {
					fwComboIdx = i;
					break;
				}
			}
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("Firmware");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
			if (ImGui::BeginCombo("##AceFirmware", k_fw_labels[fwComboIdx])) {
				for (int i = 0; i < 5; i++) {
					bool selected = (fwComboIdx == i);
					if (ImGui::Selectable(k_fw_labels[i], selected)) {
						ep.acedl.firmware = k_fw_values[i];
						RetrodevLib::Project::MarkAsModified();
						SetModified(true);
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			//
			// Speed: 0 = not specified, 5-200 = percentage, or use "MAX"
			//
			static const char* const k_speed_labels[] = {"Default", "50%", "100%", "150%", "200%", "MAX"};
			static const int k_speed_values[] = {0, 50, 100, 150, 200, -1};
			int speedComboIdx = 0;
			for (int i = 1; i < 6; i++) {
				if (ep.acedl.speed == k_speed_values[i]) {
					speedComboIdx = i;
					break;
				}
			}
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled("Speed (-speed)");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
			if (ImGui::BeginCombo("##AceSpeed", k_speed_labels[speedComboIdx])) {
				for (int i = 0; i < 6; i++) {
					bool selected = (speedComboIdx == i);
					if (ImGui::Selectable(k_speed_labels[i], selected)) {
						ep.acedl.speed = k_speed_values[i];
						RetrodevLib::Project::MarkAsModified();
						SetModified(true);
					}
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			ImGui::Spacing();
			ImGui::SeparatorText("ACE-DL Options");
			bool alone = ep.acedl.alone;
			if (ImGui::Checkbox("Disable debug windows, emulator only (-alone)", &alone)) {
				ep.acedl.alone = alone;
				RetrodevLib::Project::MarkAsModified();
				SetModified(true);
			}
			bool skipCfg = ep.acedl.skipConfigFile;
			if (ImGui::Checkbox("Do not load/save configuration (-skipConfigFile)", &skipCfg)) {
				ep.acedl.skipConfigFile = skipCfg;
				RetrodevLib::Project::MarkAsModified();
				SetModified(true);
			}
		}
	}
	//
	// Main render entry point
	//
	void DocumentBuild::Perform() {
		//
		// Query build parameters pointer on every frame
		//
		RetrodevLib::SourceParams* params = nullptr;
		if (!RetrodevLib::Project::BuildGetParams(m_name, &params) || params == nullptr) {
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Failed to load build parameters for: %s", m_name.c_str());
			return;
		}
		//
		// Ensure a default tool is always set so Output / Debug tabs work
		// even when the Build tab has never been visited
		//
		if (params->tool.empty()) {
			params->tool = "RASM";
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
		//
		// Build item name -- editable for renaming (press Enter to apply)
		//
		static char buildNameBuf[256] = "";
		if (buildNameBuf[0] == '\0' || m_name != std::string(buildNameBuf)) {
			std::snprintf(buildNameBuf, sizeof(buildNameBuf), "%s", m_name.c_str());
		}
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Build Item Name:");
		ImGui::SameLine();
		if (ImGui::InputText("##buildName", buildNameBuf, sizeof(buildNameBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
			std::string newName(buildNameBuf);
			if (!newName.empty() && newName != m_name) {
				if (RetrodevLib::Project::RenameBuildItem(RetrodevLib::ProjectBuildType::Build, m_name, newName)) {
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Renamed build item '%s' to '%s'", m_name.c_str(), newName.c_str());
					m_name = newName;
					ProjectView::NotifyProjectChanged();
					RetrodevLib::Project::MarkAsModified();
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Error, "Failed to rename build item '%s' to '%s' (name may already exist)", m_name.c_str(), newName.c_str());
					std::snprintf(buildNameBuf, sizeof(buildNameBuf), "%s", m_name.c_str());
				}
			}
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Press Enter to rename the build item");
		ImGui::Separator();
		//
		// Bind document modified flag to the RASM renderer so it can mark the tab dirty
		//
		m_rasm.SetModifiedFlag(&m_modified);
		//
		// Service the emulator exe file dialog every frame so its modal renders regardless of which tab is active
		//
		EmulatorSettings::PollFileDialog();
		if (ImGui::BeginTabBar("##BuildTabs")) {
			if (ImGui::BeginTabItem(ICON_FILE_CODE " Source")) {
				if (ImGui::BeginChild("##BuildTabContent_Source", ImVec2(0.0f, 0.0f), false))
					RenderTabSource(params);
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem(ICON_WRENCH " Build")) {
				if (ImGui::BeginChild("##BuildTabContent_Build", ImVec2(0.0f, 0.0f), false))
					RenderTabBuild(params);
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem(ICON_EXPORT " Output")) {
				if (ImGui::BeginChild("##BuildTabContent_Output", ImVec2(0.0f, 0.0f), false))
					RenderTabOutput(params);
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem(ICON_BUG " Debug")) {
				if (ImGui::BeginChild("##BuildTabContent_Debug", ImVec2(0.0f, 0.0f), false))
					RenderTabDebug(params);
				ImGui::EndChild();
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
}
