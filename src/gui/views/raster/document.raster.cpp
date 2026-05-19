// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Gui
//
// Raster document -- editor for raster effect code generation build items.
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#include "document.raster.h"
#include "document.raster.cpc.h"
#include <app/app.h>
#include <app/app.console.h>
#include <app/app.icons.mdi.h>
#include <convert/converters.h>
#include <views/main.view.project.h>

namespace RetrodevGui {
	//
	// Constructor: raster items have no source file
	//
	DocumentRaster::DocumentRaster(const std::string& name) : DocumentView(name, "") {}

	DocumentRaster::~DocumentRaster() {}
	//
	// Render the raster document
	//
	void DocumentRaster::Perform() {
		RetrodevLib::RasterParams* params = nullptr;
		if (!RetrodevLib::Project::RasterGetParams(m_name, &params) || !params) {
			ImGui::TextDisabled("Raster item not found: %s", m_name.c_str());
			return;
		}
		//
		// Top bar: rename field and target system combo on the same line
		//
		static char renameBuf[256] = {};
		static std::string lastLoaded;
		if (lastLoaded != m_name) {
			memset(renameBuf, 0, sizeof(renameBuf));
			if (m_name.size() < sizeof(renameBuf))
				memcpy(renameBuf, m_name.c_str(), m_name.size());
			lastLoaded = m_name;
		}
		//
		// Name field -- half the available width
		//
		float halfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Name:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(halfWidth - ImGui::CalcTextSize("Name:").x - ImGui::GetStyle().ItemSpacing.x);
		if (ImGui::InputText("##rasterName", renameBuf, sizeof(renameBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
			std::string newName = renameBuf;
			if (!newName.empty() && newName != m_name) {
				if (RetrodevLib::Project::RasterRename(m_name, newName)) {
					ProjectView::NotifyProjectChanged();
					m_name = newName;
					lastLoaded = newName;
					SetModified(true);
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Renamed raster item to: %s", newName.c_str());
				}
			}
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Press Enter to rename the raster item");
		ImGui::SameLine();
		//
		// Target system combo -- remaining half of the width
		//
		std::vector<std::string> systems = RetrodevLib::Converters::Get();
		if (params->targetSystem.empty() && !systems.empty()) {
			params->targetSystem = systems[0];
			SetModified(true);
			RetrodevLib::Project::MarkAsModified();
		}
		ImGui::AlignTextToFramePadding();
		ImGui::Text("System:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		if (ImGui::BeginCombo("##rasterSystem", params->targetSystem.c_str())) {
			for (const auto& sysName : systems) {
				bool isSelected = (params->targetSystem == sysName);
				if (ImGui::Selectable(sysName.c_str(), isSelected)) {
					if (params->targetSystem != sysName) {
						params->targetSystem = sysName;
						SetModified(true);
						RetrodevLib::Project::MarkAsModified();
					}
				}
				if (isSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::Separator();
		//
		// Remainder of the document: platform-specific panel
		//
		if (params->targetSystem == RetrodevLib::SupportedSystems::AmstradCPC) {
			static DocumentRasterCpc cpcPanel;
			m_cpcPanel = &cpcPanel;  // Track pointer for Save()
			cpcPanel.SetParent(this);  // Notify parent on modifications
			cpcPanel.SetOnModified([]() { RetrodevLib::Project::MarkAsModified(); });
			cpcPanel.SetProjectFolder(RetrodevLib::Project::GetProjectFolder());
			cpcPanel.SetRenderer(Application::GetRenderer());
			cpcPanel.RenderPanel(params);
			return;
		}
		ImGui::TextDisabled("No raster editor available for: %s", params->targetSystem.c_str());
	}

	bool DocumentRaster::Save() {
		// Serialize library state back to project parameters
		if (m_cpcPanel) {
			RetrodevLib::RasterParams* params = nullptr;
			if (RetrodevLib::Project::RasterGetParams(m_name, &params) && params) {
				m_cpcPanel->SaveProject(params);
				RetrodevLib::Project::MarkAsModified();
				SetModified(false);
				return true;
			}
		}
		return false;
	}
}
