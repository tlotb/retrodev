// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Gui
//
// Palette solver document -- shared palette computation across build items.
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#include "document.palette.h"
#include <app/app.h>
#include <app/app.console.h>
#include <app/app.icons.mdi.h>
#include <convert/converters.h>
#include <widgets/palette.widget.h>
#include <views/main.view.project.h>
#include <views/main.view.documents.h>
#include <assets/palette/palette.h>
#include <cstring>

namespace RetrodevGui {
	//
	// Called when any project build item changes -- clear the solution if a participant is affected
	//
	void DocumentPalette::OnProjectItemChanged(const std::string& itemName) {
		RetrodevLib::PaletteParams* params = nullptr;
		if (!RetrodevLib::Project::PaletteGetParams(m_name, &params) || !params)
			return;
		for (const auto& zone : params->zones) {
			for (const auto& p : zone.participants) {
				if (p.buildItemName == itemName) {
					m_hasSolution = false;
					m_solution = {};
					m_selectedSolutionZone = -1;
					m_selectedSolutionTag = -1;
					m_lastDisplayZone = -1;
					m_lastDisplayTag = -1;
					m_displayConverter = nullptr;
					m_lastOriginalParticipant = -1;
					m_originalConverter = nullptr;
					m_selectedPreviewKey = "";
					//
					// A participant changed externally -- the previous user validation is no
					// longer trustworthy; require a new solve + validate cycle.
					//
					params->userValidated = false;
					return;
				}
			}
		}
	}
	//
	// Constructor: palette items have no source file
	//
	DocumentPalette::DocumentPalette(const std::string& name) : DocumentView(name, "") {}

	DocumentPalette::~DocumentPalette() {}
	//
	// Ensure the live converter matches the current target system/mode in params.
	// Called every frame so changes in the left panel are reflected immediately.
	//
	void DocumentPalette::SyncConverter(RetrodevLib::PaletteParams* params) {
		if (!params)
			return;
		//
		// Nothing to do until a target system has been selected
		//
		if (params->targetSystem.empty())
			return;
		//
		// Use the selected zone's mode (or the first zone's mode) for the preview converter.
		// Mode is now per-zone so the converter only needs to reflect system + palette type.
		//
		std::string currentMode;
		if (m_selectedZone >= 0 && m_selectedZone < (int)params->zones.size())
			currentMode = params->zones[m_selectedZone].targetMode;
		else if (!params->zones.empty())
			currentMode = params->zones[0].targetMode;
		if (params->targetSystem == m_lastTargetSystem && params->targetPaletteType == m_lastTargetPaletteType && m_converter)
			return;
		//
		// If the target was already set (not first-time init), clear all zone participants
		// only when the system or palette type changes -- those determine which colors are
		// available.
		//
		bool wasInitialized = !m_lastTargetSystem.empty();
		bool paletteChanged = (params->targetSystem != m_lastTargetSystem || params->targetPaletteType != m_lastTargetPaletteType);
		m_lastTargetSystem = params->targetSystem;
		m_lastTargetPaletteType = params->targetPaletteType;
		if (wasInitialized && paletteChanged) {
			for (auto& zone : params->zones)
				zone.participants.clear();
			params->preloadedColors.clear();
			params->preloadedLocked.clear();
			params->userValidated = false;
			m_preloadedSelected = false;
			m_selectedParticipant = -1;
			m_lastOriginalParticipant = -1;
			m_originalConverter = nullptr;
			m_selectedPreviewKey = "";
			m_selectedSolutionZone = -1;
			m_selectedSolutionTag = -1;
			m_lastDisplayZone = -1;
			m_lastDisplayTag = -1;
			m_solution = {};
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
		//
		// Flush solution so thumbnails are rebuilt on next solve
		//
		m_hasSolution = false;
		m_displayConverter = nullptr;
		//
		// Build a GFXParams with system and mode set so GetBitmapConverter selects the right converter.
		// Mode is taken from the currently selected zone; any valid mode works for palette-type enumeration.
		//
		RetrodevLib::GFXParams gfx;
		gfx.SParams.TargetSystem = params->targetSystem;
		gfx.SParams.TargetMode = currentMode;
		m_converter = RetrodevLib::Converters::GetBitmapConverter(&gfx);
		if (!m_converter)
			return;
		//
		// Prime the converter with a 1x1 image so the palette mode is applied.
		// Without this the internal palette mode is never set and PaletteMaxColors() returns stale data.
		//
		auto dummyImage = RetrodevLib::Image::ImageCreate(1, 1);
		if (dummyImage) {
			gfx.RParams.TargetWidth = 1;
			gfx.RParams.TargetHeight = 1;
			m_converter->SetOriginal(dummyImage);
			m_converter->Convert(&gfx);
			m_converter->SetOriginal(nullptr);
		}
		//
		// Populate each pen with a sequential system color index so the preview shows
		// the full hardware palette rather than all-black unassigned pens.
		//
		auto palette = m_converter->GetPalette();
		if (palette) {
			int penCount = palette->PaletteMaxColors();
			int sysCount = palette->GetSystemMaxColors();
			for (int i = 0; i < penCount; i++)
				palette->PenSetColorIndex(i, i < sysCount ? i : 0);
			//
			// Ensure the preloaded arrays are sized to match the pen count.
			// Rebuild m_preloadGfx so the palette widget reflects saved pre-loaded state.
			//
			params->preloadedColors.resize(penCount, -1);
			params->preloadedLocked.resize(penCount, false);
			m_preloadGfx = RetrodevLib::GFXParams{};
			m_preloadGfx.SParams.TargetSystem = params->targetSystem;
			m_preloadGfx.SParams.TargetMode = currentMode;
			m_preloadGfx.SParams.PaletteType = params->targetPaletteType;
			m_preloadGfx.RParams.TargetWidth = 1;
			m_preloadGfx.RParams.TargetHeight = 1;
			m_preloadGfx.SParams.PaletteColors.assign(penCount, -1);
			m_preloadGfx.SParams.PaletteLocked.assign(penCount, false);
			m_preloadGfx.SParams.PaletteEnabled.assign(penCount, false);
			for (int i = 0; i < penCount; i++) {
				int col = params->preloadedColors[i];
				bool locked = params->preloadedLocked[i];
				m_preloadGfx.SParams.PaletteColors[i] = col;
				m_preloadGfx.SParams.PaletteLocked[i] = locked;
				m_preloadGfx.SParams.PaletteEnabled[i] = (locked && col >= 0);
			}
			// Build and persist the fake palette for the widget's lifetime
			m_preloadPalette = RetrodevLib::Converters::GetBitmapConverter(&m_preloadGfx);
			if (m_preloadPalette) {
				// Prime with a 1x1 dummy image so the palette is initialized
				auto dummy = RetrodevLib::Image::ImageCreate(1, 1);
				if (dummy) {
					m_preloadGfx.RParams.TargetWidth = 1;
					m_preloadGfx.RParams.TargetHeight = 1;
					m_preloadPalette->SetOriginal(dummy);
					m_preloadPalette->Convert(&m_preloadGfx);
					m_preloadPalette->SetOriginal(nullptr);
				}
			}
		}
	}
	//
	// Add a default zone, inheriting the line range from the previous last zone (or 0-199)
	//
	void DocumentPalette::AddZone(RetrodevLib::PaletteParams* params) {
		RetrodevLib::PaletteZone zone;
		zone.name = "Zone " + std::to_string((int)params->zones.size() + 1);
		if (!params->zones.empty()) {
			zone.lineStart = params->zones.back().lineEnd + 1;
			zone.lineEnd = zone.lineStart + 49;
			zone.targetMode = params->zones.back().targetMode;
		}
		params->zones.push_back(zone);
		m_selectedZone = (int)params->zones.size() - 1;
		m_selectedParticipant = -1;
		m_lastOriginalParticipant = -1;
		m_originalConverter = nullptr;
		m_selectedPreviewKey = "";
		m_hasSolution = false;
		m_solution = {};
		m_selectedSolutionZone = -1;
		m_selectedSolutionTag = -1;
		m_lastDisplayZone = -1;
		m_lastDisplayTag = -1;
		m_displayConverter = nullptr;
		params->userValidated = false;
		RetrodevLib::Project::MarkAsModified();
		SetModified(true);
	}
	//
	// Remove the zone at index, clamping the selection
	//
	void DocumentPalette::RemoveZone(RetrodevLib::PaletteParams* params, int index) {
		if (index < 0 || index >= (int)params->zones.size())
			return;
		params->zones.erase(params->zones.begin() + index);
		m_selectedZone = std::min(m_selectedZone, (int)params->zones.size() - 1);
		m_selectedParticipant = -1;
		m_lastOriginalParticipant = -1;
		m_originalConverter = nullptr;
		m_selectedPreviewKey = "";
		m_hasSolution = false;
		m_solution = {};
		m_selectedSolutionZone = -1;
		m_selectedSolutionTag = -1;
		m_lastDisplayZone = -1;
		m_lastDisplayTag = -1;
		m_displayConverter = nullptr;
		params->userValidated = false;
		RetrodevLib::Project::MarkAsModified();
		SetModified(true);
	}
	//
	// Bottom of the left panel:
	// Thumbnails are sourced from the solver's converters so they always show the
	// image as converted with the solved palette.
	//
	void DocumentPalette::RenderThumbnails(RetrodevLib::PaletteParams* params) {
		if (m_selectedZone < 0 || m_selectedZone >= (int)params->zones.size())
			return;
		const RetrodevLib::PaletteZone& zone = params->zones[m_selectedZone];
		float fontSize = ImGui::GetFontSize();
		ImGui::Separator();
		//
		// Collect unique Level tags in the selected zone for the combo
		//
		std::vector<std::string> levelTags;
		for (const auto& p : zone.participants) {
			if (p.role == RetrodevLib::PaletteParticipantRole::Level) {
				bool found = false;
				for (const auto& t : levelTags)
					if (t == p.tag) {
						found = true;
						break;
					}
				if (!found)
					levelTags.push_back(p.tag);
			}
		}
		//
		// Level tag combo -- always shown; disabled when no Level participants exist
		//
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Level tag");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		if (levelTags.empty()) {
			ImGui::BeginDisabled();
			if (ImGui::BeginCombo("##LevelTag", "(no tags)"))
				ImGui::EndCombo();
			ImGui::EndDisabled();
		} else {
			const char* preview = m_selectedLevelTag.empty() ? "(all)" : m_selectedLevelTag.c_str();
			if (ImGui::BeginCombo("##LevelTag", preview)) {
				bool selAll = m_selectedLevelTag.empty();
				if (ImGui::Selectable("(all)", selAll))
					m_selectedLevelTag = "";
				if (selAll)
					ImGui::SetItemDefaultFocus();
				for (const auto& tag : levelTags) {
					bool sel = (m_selectedLevelTag == tag);
					if (ImGui::Selectable(tag.c_str(), sel))
						m_selectedLevelTag = tag;
					if (sel)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
		//
		// Thumbnail scroll area -- fills remaining left panel height
		//
		if (ImGui::BeginChild("##Thumbnails", ImVec2(-1, -1), true)) {
			//
			// Show Always and ZoneAlways participants unconditionally.
			// Show Level participants whose tag matches m_selectedLevelTag,
			// or all Level participants when m_selectedLevelTag is empty.
			//
			const float thumbSize = 200.0f;
			for (int pi = 0; pi < (int)zone.participants.size(); pi++) {
				const RetrodevLib::PaletteParticipant& p = zone.participants[pi];
				//
				// Apply level tag filter
				//
				if (p.role == RetrodevLib::PaletteParticipantRole::Level) {
					if (!m_selectedLevelTag.empty() && p.tag != m_selectedLevelTag)
						continue;
				}
				if (p.buildItemName.empty())
						continue;
					//
					// Check whether the referenced build item still exists in the project
					//
					bool thumbMissing = false;
					{
						RetrodevLib::GFXParams* existCheck = nullptr;
						if (p.buildItemType == "Bitmap")
							RetrodevLib::Project::BitmapGetCfg(p.buildItemName, &existCheck);
						else if (p.buildItemType == "Tilemap")
							RetrodevLib::Project::TilesetGetCfg(p.buildItemName, &existCheck);
						else if (p.buildItemType == "Sprite")
							RetrodevLib::Project::SpriteGetCfg(p.buildItemName, &existCheck);
						thumbMissing = (existCheck == nullptr);
					}
					if (thumbMissing) {
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.1f, 1.0f));
						ImGui::TextUnformatted((std::string(ICON_ALERT_CIRCLE_OUTLINE) + "  " + p.buildItemName + " -- MISSING").c_str());
						ImGui::PopStyleColor();
						if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
							ImGui::SetTooltip("Build item '%s' no longer exists in the project.", p.buildItemName.c_str());
						continue;
					}
				std::string key = zone.name + ":" + p.buildItemType + ":" + p.buildItemName + ":" + std::to_string(pi);
				//
				// Find the converter for this participant from the solution.
				// Always/ScreenZone participants live in the base tag solution (index 0).
				// Level participants live in their tag's solution entry.
				//
				std::shared_ptr<RetrodevLib::IBitmapConverter> thumbConv;
				RetrodevLib::GFXParams* thumbGfx = nullptr;
				if (m_hasSolution && m_selectedZone < (int)m_solution.zones.size()) {
					const RetrodevLib::PaletteZoneSolution& zs = m_solution.zones[m_selectedZone];
					if (p.role == RetrodevLib::PaletteParticipantRole::Level) {
						//
						// Find the tag solution that matches this participant's tag
						//
						for (int ti = 1; ti < (int)zs.tagSolutions.size(); ti++) {
							const RetrodevLib::PaletteTagSolution& ts = zs.tagSolutions[ti];
							if (ts.tag == p.tag) {
								auto it = ts.converters.find(pi);
								if (it != ts.converters.end()) {
									thumbConv = it->second;
									auto git = ts.converterGfx.find(pi);
									if (git != ts.converterGfx.end())
										thumbGfx = const_cast<RetrodevLib::GFXParams*>(&git->second);
								}
								break;
							}
						}
					} else {
						//
						// Always and ZoneAlways participants are in the base solution
						//
						if (!zs.tagSolutions.empty()) {
							const RetrodevLib::PaletteTagSolution& base = zs.tagSolutions[0];
							auto it = base.converters.find(pi);
							if (it != base.converters.end()) {
								thumbConv = it->second;
								auto git = base.converterGfx.find(pi);
								if (git != base.converterGfx.end())
									thumbGfx = const_cast<RetrodevLib::GFXParams*>(&git->second);
							}
						}
					}
				}
				if (!thumbConv || !thumbGfx) {
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
					ImGui::TextUnformatted((p.buildItemName + " -- solve to preview").c_str());
					ImGui::PopStyleColor();
					continue;
				}
				auto previewImg = thumbConv->GetPreview(thumbGfx);
				SDL_Texture* tex = previewImg ? previewImg->GetTexture(Application::GetRenderer()) : nullptr;
				//
				// Role icon + item name label, centered over the thumbnail
				//
				const char* roleIcon = ICON_FILE_IMAGE;
				if (p.role == RetrodevLib::PaletteParticipantRole::Level)
					roleIcon = ICON_TAG;
				else if (p.role == RetrodevLib::PaletteParticipantRole::ZoneAlways)
					roleIcon = ICON_MONITOR;
				float panelW = ImGui::GetContentRegionAvail().x;
				//
				// Compute scaled image size up-front so label and image share the same center
				//
				float imgW = 0.0f, imgH = 0.0f, drawW = thumbSize, drawH = thumbSize;
				if (tex) {
					imgW = (float)previewImg->GetWidth();
					imgH = (float)previewImg->GetHeight();
					float scale = (imgW > thumbSize || imgH > thumbSize) ? std::min(thumbSize / imgW, thumbSize / imgH) : 1.0f;
					drawW = imgW * scale;
					drawH = imgH * scale;
				}
				float centerX = ImGui::GetCursorPosX() + (panelW - drawW) * 0.5f;
				//
				// Record the top-left cursor position for the selection highlight frame
				//
				ImVec2 itemTopLeft = ImGui::GetCursorScreenPos();
				bool isSelected = (m_selectedPreviewKey == key);
				//
				// Label line -- centered using the same anchor
				//
				std::string labelStr = std::string(roleIcon) + "  " + p.buildItemName;
				if (!p.tag.empty())
					labelStr += "  [" + p.tag + "]";
				float labelW = ImGui::CalcTextSize(labelStr.c_str()).x;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (panelW - labelW) * 0.5f);
				if (!p.tag.empty()) {
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.8f, 1.0f, 1.0f));
					ImGui::TextUnformatted(labelStr.c_str());
					ImGui::PopStyleColor();
				} else {
					ImGui::TextUnformatted(labelStr.c_str());
				}
				//
				// Thumbnail image centered horizontally
				//
				if (tex) {
					ImGui::SetCursorPosX(centerX);
					ImGui::Image(ImTextureRef(tex), ImVec2(drawW, drawH));
				} else {
					ImGui::SetCursorPosX(centerX);
					ImGui::BeginChild(("##nothumb" + std::to_string(pi)).c_str(), ImVec2(drawW, fontSize * 2.0f), false);
					ImGui::TextDisabled("No preview");
					ImGui::EndChild();
				}
				//
				// Invisible selectable spanning the full item height so clicking anywhere selects it
				//
				ImVec2 itemBotRight = ImGui::GetCursorScreenPos();
				ImVec2 selectableSize = ImVec2(panelW, itemBotRight.y - itemTopLeft.y);
				ImGui::SetCursorScreenPos(itemTopLeft);
				if (ImGui::InvisibleButton(("##sel_" + key).c_str(), selectableSize)) {
						m_selectedPreviewKey = key;
						//
						// Auto-select the solution entry that contains this participant.
						// Only search within m_selectedZone -- pi is an index into that zone's
						// participant array, so the same index may exist in other zones and must
						// not be matched there.
						//
						if (m_hasSolution && m_selectedZone < (int)m_solution.zones.size()) {
							const RetrodevLib::PaletteZoneSolution& szs = m_solution.zones[m_selectedZone];
							bool found = false;
							for (int sti = 0; sti < (int)szs.tagSolutions.size() && !found; sti++) {
								const RetrodevLib::PaletteTagSolution& sts = szs.tagSolutions[sti];
								if (sts.converters.find(pi) != sts.converters.end()) {
									m_selectedSolutionZone = m_selectedZone;
									m_selectedSolutionTag = sti;
									m_preloadedSelected = false;
									found = true;
								}
							}
							//
							// Always participants live only in the base solution (tagSolutions[0]).
							// If not found in direct converters, check the base solution as fallback.
							//
							if (!found && !szs.tagSolutions.empty()) {
								const RetrodevLib::PaletteTagSolution& base = szs.tagSolutions[0];
								if (base.converters.find(pi) != base.converters.end()) {
									m_selectedSolutionZone = m_selectedZone;
									m_selectedSolutionTag = 0;
									m_preloadedSelected = false;
								}
							}
						}
					}
				//
				// Highlight border for the selected thumbnail
				//
				if (isSelected) {
					ImDrawList* dl = ImGui::GetWindowDrawList();
					dl->AddRect(itemTopLeft, ImVec2(itemTopLeft.x + panelW, itemBotRight.y), IM_COL32(100, 180, 255, 200), 3.0f, 0, 2.0f);
				}
			}
			if (zone.participants.empty())
				ImGui::TextDisabled("No participants in this zone.");
		}
		ImGui::EndChild();
	}
	//
	// Left panel: target system/mode + zone list
	//
	void DocumentPalette::RenderLeftPanel(RetrodevLib::PaletteParams* params) {
		float fontSize = ImGui::GetFontSize();
		//
		// Target system
		//
		std::vector<std::string> systems = RetrodevLib::Converters::Get();
		if (params->targetSystem.empty() && !systems.empty())
			params->targetSystem = systems[0];
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Target System");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		if (ImGui::BeginCombo("##PalSys", params->targetSystem.c_str())) {
			for (const auto& s : systems) {
				bool sel = (s == params->targetSystem);
				if (ImGui::Selectable(s.c_str(), sel)) {
					params->targetSystem = s;
					m_converter.reset();
					RetrodevLib::Project::MarkAsModified();
					SetModified(true);
				}
				if (sel)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		//
		// Target palette type (populated from the converter if available)
		//
		if (m_converter) {
			std::vector<std::string> paletteTypes = m_converter->GetTargetPalettes();
			if (!paletteTypes.empty()) {
				if (params->targetPaletteType.empty())
					params->targetPaletteType = paletteTypes[0];
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Palette Type ");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1);
				if (ImGui::BeginCombo("##PalType", params->targetPaletteType.c_str())) {
					for (const auto& pt : paletteTypes) {
						bool sel = (pt == params->targetPaletteType);
						if (ImGui::Selectable(pt.c_str(), sel)) {
							params->targetPaletteType = pt;
							m_converter.reset();
							RetrodevLib::Project::MarkAsModified();
							SetModified(true);
						}
						if (sel)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}
		}
		ImGui::Separator();
		//
		// Zones header + add button
		//
		ImGui::AlignTextToFramePadding();
		ImGui::Text(ICON_LAYERS_TRIPLE "  Zones");
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - fontSize * 2.0f);
		if (ImGui::Button(ICON_PLUS "##AddZone"))
			AddZone(params);
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("Add a screen zone.\nA zone covers a scanline range and holds its own set\nof participating graphics. Use multiple zones to model\nraster-interrupt "
							  "color changes within a single frame.");
		ImGui::Separator();
		//
		// Zone list -- fixed height so the thumbnail area below has room
		//
		float zoneListHeight = fontSize * 10.0f;
		if (ImGui::BeginChild("##ZoneList", ImVec2(-1, zoneListHeight), true)) {
			for (int i = 0; i < (int)params->zones.size(); i++) {
				RetrodevLib::PaletteZone& zone = params->zones[i];
				bool selected = (i == m_selectedZone);
				std::string label =
					ICON_PALETTE_SWATCH "  " + zone.name + "  [" + std::to_string(zone.lineStart) + "-" + std::to_string(zone.lineEnd) + "]" + "##zone" + std::to_string(i);
				if (ImGui::Selectable(label.c_str(), selected)) {
					m_selectedZone = i;
					m_selectedParticipant = -1;
				}
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
					ImGui::SetTooltip(
						"Zone: %s\nScanlines %d - %d\nParticipants: %d\n\nGraphics assigned to this zone must all fit\ninto the palette pens available for these scanlines.",
						zone.name.c_str(), zone.lineStart, zone.lineEnd, (int)zone.participants.size());
				//
				// Context menu: remove zone
				//
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
				if (ImGui::BeginPopupContextItem()) {
					if (ImGui::MenuItem(ICON_DELETE "  Remove zone")) {
						ImGui::EndPopup();
						ImGui::PopStyleVar();
						RemoveZone(params, i);
						break;
					}
					ImGui::EndPopup();
				}
				ImGui::PopStyleVar();
			}
			if (params->zones.empty()) {
				ImGui::TextDisabled("No zones. Press " ICON_PLUS " to add one.");
			}
		}
		ImGui::EndChild();
		//
		// Thumbnail area: level tag filter + scrollable participant previews
		//
		RenderThumbnails(params);
	}
	//
	// Right panel: selected zone properties, participant list, and palette preview
	//
	void DocumentPalette::RenderRightPanel(RetrodevLib::PaletteParams* params) {
		if (m_selectedZone < 0 || m_selectedZone >= (int)params->zones.size()) {
			ImGui::TextDisabled("Select a zone on the left to edit it.");
			return;
		}
		RetrodevLib::PaletteZone& zone = params->zones[m_selectedZone];
		float fontSize = ImGui::GetFontSize();
		//
		// Zone name
		//
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Zone Name");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(fontSize * 14.0f);
		char nameBuf[128];
		std::strncpy(nameBuf, zone.name.c_str(), sizeof(nameBuf) - 1);
		nameBuf[sizeof(nameBuf) - 1] = '\0';
		if (ImGui::InputText("##ZoneName", nameBuf, sizeof(nameBuf))) {
			zone.name = nameBuf;
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
		//
		// Scanline range
		//
		ImGui::SameLine();
		ImGui::Spacing();
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Lines");
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("Scanline range for this zone (inclusive).\nOn retro hardware a raster interrupt fires at a specific\nscanline, swapping palette registers mid-frame "
							  "so the\nupper and lower screen portions can use different colors.");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(fontSize * 4.0f);
		if (ImGui::InputInt("##LineStart", &zone.lineStart, 0)) {
			zone.lineStart = std::max(0, zone.lineStart);
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::Text("-");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(fontSize * 4.0f);
		if (ImGui::InputInt("##LineEnd", &zone.lineEnd, 0)) {
			zone.lineEnd = std::max(zone.lineStart, zone.lineEnd);
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
		//
		// Target mode for this zone -- allows each zone to use a different screen mode
		//
		if (m_converter) {
			std::vector<std::string> modes = m_converter->GetTargetModes();
			if (zone.targetMode.empty() && !modes.empty())
				zone.targetMode = modes[0];
			ImGui::SameLine();
			ImGui::Spacing();
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Mode");
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
				ImGui::SetTooltip("Screen mode for this zone.\nDifferent zones can use different modes (e.g. Mode 0\ngameplay area + Mode 1 status bar) to model "
								  "hardware\nraster-interrupt mode switches mid-frame.");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(fontSize * 8.0f);
			if (ImGui::BeginCombo("##ZoneMode", zone.targetMode.c_str())) {
				for (const auto& m : modes) {
					bool sel = (m == zone.targetMode);
					if (ImGui::Selectable(m.c_str(), sel)) {
						zone.targetMode = m;
						m_hasSolution = false;
						m_solution = {};
						params->userValidated = false;
						RetrodevLib::Project::MarkAsModified();
						SetModified(true);
					}
					if (sel)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		}
		ImGui::Separator();
		//
		// Initialise inner horizontal split sizes on first use (or after reset)
		//
		float innerAvailX = ImGui::GetContentRegionAvail().x;
		if (m_rightSizeList <= 0.0f || m_rightSizePal <= 0.0f) {
			m_rightSizeList = innerAvailX * 0.40f;
			m_rightSizePal = innerAvailX - m_rightSizeList - Application::splitterThickness - ImGui::GetStyle().ItemSpacing.x;
		}
		m_rightSizePal = innerAvailX - m_rightSizeList - Application::splitterThickness - ImGui::GetStyle().ItemSpacing.x;
		float minList = fontSize * 14.0f;
		float minPal = fontSize * 10.0f;
		//
		// Horizontal splitter: participant list + editor (left) | original palette (right)
		//
		ImGui::DrawSplitter(false, Application::splitterThickness, &m_rightSizeList, &m_rightSizePal, minList, minPal);
		//
		// Left child: participant list header + scrollable list + editor
		//
		if (ImGui::BeginChild("##PartPanelLeft", ImVec2(m_rightSizeList, -1), true)) {
			//
			// Participant list header + add button
			//
			ImGui::AlignTextToFramePadding();
			ImGui::Text(ICON_IMAGE_MULTIPLE "  Participants");
			ImGui::SameLine();
			ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - fontSize * 2.0f);
			if (ImGui::Button(ICON_PLUS "##AddPart")) {
				RetrodevLib::PaletteParticipant p;
				p.buildItemName = "";
				p.buildItemType = "Bitmap";
				p.role = RetrodevLib::PaletteParticipantRole::Always;
				zone.participants.push_back(p);
				m_selectedParticipant = (int)zone.participants.size() - 1;
				m_lastOriginalParticipant = -1;
				m_originalConverter = nullptr;
				m_selectedPreviewKey = "";
				m_hasSolution = false;
				m_solution = {};
				m_selectedSolutionZone = -1;
				m_selectedSolutionTag = -1;
				m_lastDisplayZone = -1;
				m_lastDisplayTag = -1;
				m_displayConverter = nullptr;
				params->userValidated = false;
				RetrodevLib::Project::MarkAsModified();
				SetModified(true);
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
				ImGui::SetTooltip("Add a graphics build item as a participant in this zone.\nAll participants must share the zone's pen budget,\nso their combined colors must fit "
								  "within the\nmaximum pens the target system allows for this mode.");
			//
			// Participant scrollable list -- fills all space except the editor rows below
			//
			float editorReserve = (m_selectedParticipant >= 0 && m_selectedParticipant < (int)zone.participants.size())
									  ? (fontSize * 2.0f + ImGui::GetStyle().ItemSpacing.y * 3.0f + ImGui::GetStyle().FramePadding.y * 4.0f + 1.0f)
									  : 0.0f;
			float listHeight = ImGui::GetContentRegionAvail().y - editorReserve;
			if (listHeight < fontSize * 2.0f)
				listHeight = fontSize * 2.0f;
			if (ImGui::BeginChild("##PartList", ImVec2(-1, listHeight), true)) {
				static const char* kRoles[] = {"Always", "Level", "Zone Always"};
				for (int pi = 0; pi < (int)zone.participants.size(); pi++) {
					RetrodevLib::PaletteParticipant& p = zone.participants[pi];
					bool sel = (pi == m_selectedParticipant);
					ImGui::PushID(pi);
						//
						// Check whether the referenced build item still exists in the project
						//
						bool isMissing = false;
						if (!p.buildItemName.empty()) {
							RetrodevLib::GFXParams* existCheck = nullptr;
							if (p.buildItemType == "Bitmap")
								RetrodevLib::Project::BitmapGetCfg(p.buildItemName, &existCheck);
							else if (p.buildItemType == "Tilemap")
								RetrodevLib::Project::TilesetGetCfg(p.buildItemName, &existCheck);
							else if (p.buildItemType == "Sprite")
								RetrodevLib::Project::SpriteGetCfg(p.buildItemName, &existCheck);
							isMissing = (existCheck == nullptr);
						}
						//
						// Selectable row label: type-specific icon + type + name + role
						//
						const char* roleLabel = kRoles[(int)p.role];
						const char* typeIcon = ICON_IMAGE_MULTIPLE;
						if (isMissing)
							typeIcon = ICON_ALERT_CIRCLE_OUTLINE;
						else if (p.buildItemType == "Tilemap")
							typeIcon = ICON_VIEW_GRID;
						else if (p.buildItemType == "Sprite")
							typeIcon = ICON_HUMAN_MALE;
						else
							typeIcon = ICON_FILE_IMAGE;
						std::string roleTag = (p.role == RetrodevLib::PaletteParticipantRole::Level && !p.tag.empty()) ? (std::string(roleLabel) + " : " + p.tag) : roleLabel;
						std::string rowLabel = std::string(typeIcon) + "  [" + (p.buildItemType.empty() ? "?" : p.buildItemType) + "]  " +
											   (p.buildItemName.empty() ? "(none)" : p.buildItemName) + "  [" + roleTag + "]";
						if (isMissing)
							rowLabel += "  " ICON_ALERT "  MISSING";
						if (isMissing)
							ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.1f, 1.0f));
						if (ImGui::Selectable(rowLabel.c_str(), sel, ImGuiSelectableFlags_SpanAllColumns))
							m_selectedParticipant = pi;
						if (isMissing)
							ImGui::PopStyleColor();
						if (isMissing && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
							ImGui::SetTooltip("Build item '%s' no longer exists in the project.\nRemove or reassign this participant.", p.buildItemName.c_str());
					//
					// Context menu: remove participant
					//
					ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
					if (ImGui::BeginPopupContextItem()) {
						if (ImGui::MenuItem(ICON_DELETE "  Remove")) {
							ImGui::EndPopup();
							ImGui::PopStyleVar();
							zone.participants.erase(zone.participants.begin() + pi);
							m_selectedParticipant = std::min(m_selectedParticipant, (int)zone.participants.size() - 1);
							m_lastOriginalParticipant = -1;
							m_originalConverter = nullptr;
							m_selectedPreviewKey = "";
							m_hasSolution = false;
							m_solution = {};
							m_selectedSolutionZone = -1;
							m_selectedSolutionTag = -1;
							m_lastDisplayZone = -1;
							m_lastDisplayTag = -1;
							m_displayConverter = nullptr;
							params->userValidated = false;
							RetrodevLib::Project::MarkAsModified();
							SetModified(true);
							ImGui::PopID();
							break;
						}
						ImGui::EndPopup();
					}
					ImGui::PopStyleVar();
					ImGui::PopID();
				}
				if (zone.participants.empty())
					ImGui::TextDisabled("No participants. Press " ICON_PLUS " to add one.");
			}
			ImGui::EndChild();
			//
			// Selected participant editor
			//
			if (m_selectedParticipant >= 0 && m_selectedParticipant < (int)zone.participants.size()) {
				ImGui::Separator();
				RetrodevLib::PaletteParticipant& p = zone.participants[m_selectedParticipant];
				//
				// Build item combo
				//
				static const char* kBuildTypes[] = {"Bitmap", "Tilemap", "Sprite"};
				static const RetrodevLib::ProjectBuildType kBuildTypeEnums[] = {RetrodevLib::ProjectBuildType::Bitmap, RetrodevLib::ProjectBuildType::Tilemap,
																				RetrodevLib::ProjectBuildType::Sprite};
				struct BuildItemEntry {
					std::string name;
					std::string type;
					std::string label;
				};
				std::vector<BuildItemEntry> buildItems;
				for (int t = 0; t < 3; t++) {
					std::vector<std::string> names = RetrodevLib::Project::GetBuildItemsByType(kBuildTypeEnums[t]);
					for (const auto& n : names) {
						//
						// Only include items configured for the same system and mode as this palette document
						//
						RetrodevLib::GFXParams* itemCfg = nullptr;
						if (kBuildTypeEnums[t] == RetrodevLib::ProjectBuildType::Bitmap)
							RetrodevLib::Project::BitmapGetCfg(n, &itemCfg);
						else if (kBuildTypeEnums[t] == RetrodevLib::ProjectBuildType::Tilemap)
							RetrodevLib::Project::TilesetGetCfg(n, &itemCfg);
						else if (kBuildTypeEnums[t] == RetrodevLib::ProjectBuildType::Sprite)
							RetrodevLib::Project::SpriteGetCfg(n, &itemCfg);
						if (!itemCfg)
							continue;
						if (itemCfg->SParams.TargetSystem != params->targetSystem || itemCfg->SParams.PaletteType != params->targetPaletteType)
							continue;
						BuildItemEntry e;
						e.name = n;
						e.type = kBuildTypes[t];
						e.label = std::string("[") + kBuildTypes[t] + "]  " + n;
						buildItems.push_back(std::move(e));
					}
				}
				std::string currentLabel = p.buildItemName.empty() ? "(none)" : std::string("[") + p.buildItemType + "]  " + p.buildItemName;
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Build Item");
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
					ImGui::SetTooltip("Select a Bitmap, Tilemap or Sprite build item from the project.\nAll items from the project are listed grouped by type.");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(-1);
				if (ImGui::BeginCombo("##PartName", currentLabel.c_str())) {
					for (const auto& e : buildItems) {
						bool sel = (e.name == p.buildItemName && e.type == p.buildItemType);
						if (ImGui::Selectable(e.label.c_str(), sel)) {
							p.buildItemName = e.name;
							p.buildItemType = e.type;
							RetrodevLib::Project::MarkAsModified();
							SetModified(true);
						}
						if (sel)
							ImGui::SetItemDefaultFocus();
					}
					if (buildItems.empty())
						ImGui::TextDisabled("No build items in project.");
					ImGui::EndCombo();
				}
				//
				// Role
				//
				static const char* kRoleLabels[] = {"Always", "Level", "Zone Always"};
				static const char* kRoleTooltips[] = {
					"Always\n\nThis graphic is present in every level and every\npart of the screen. Its colors must always be\naccounted for in this zone's pen budget.",
					"Level\n\nThis graphic is only loaded for a specific game level.\nUse the Tag field to identify the level name.\nPens used by this graphic only need to fit "
					"when\nthat level is active.",
					"Zone Always\n\nThis graphic is always present within this zone across\nall levels. Its colors are added on top of the\nglobal Always base for this zone "
					"only.\n"
					"Unlike Always, these colors are not shared across zones."};
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Role      ");
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
					ImGui::SetTooltip("How often this graphic participates in the palette:\n  Always       - present in every zone and every level\n  Level        - only loaded "
									  "for one specific level\n  Zone Always  - always present within this zone, across all levels");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(fontSize * 10.0f);
				int roleIdx = (int)p.role;
				if (ImGui::BeginCombo("##PartRole", kRoleLabels[roleIdx])) {
					for (int r = 0; r < 3; r++) {
						bool sel = (roleIdx == r);
						if (ImGui::Selectable(kRoleLabels[r], sel)) {
							p.role = (RetrodevLib::PaletteParticipantRole)r;
							RetrodevLib::Project::MarkAsModified();
							SetModified(true);
						}
						if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
							ImGui::SetTooltip("%s", kRoleTooltips[r]);
						if (sel)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
				//
				// Tag (shown only for Level role)
				//
				if (p.role == RetrodevLib::PaletteParticipantRole::Level) {
					ImGui::SameLine();
					ImGui::AlignTextToFramePadding();
					ImGui::Text("Tag");
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
						ImGui::SetTooltip("Level tag\n\nEnter the name of the level this graphic belongs to\n(e.g. \"Level1\", \"World2\"). Only graphics sharing\nthe same "
										  "tag are combined when solving that level's palette.\nPress the arrow button to pick an existing tag.");
					ImGui::SameLine();
					//
					// Tag input + arrow button to pick an existing tag from this zone
					//
					float arrowW = ImGui::GetFrameHeight();
					ImGui::SetNextItemWidth(fontSize * 10.0f - arrowW - ImGui::GetStyle().ItemSpacing.x);
					char tagBuf[128];
					std::strncpy(tagBuf, p.tag.c_str(), sizeof(tagBuf) - 1);
					tagBuf[sizeof(tagBuf) - 1] = '\0';
					if (ImGui::InputText("##PartTag", tagBuf, sizeof(tagBuf))) {
						p.tag = tagBuf;
						RetrodevLib::Project::MarkAsModified();
						SetModified(true);
					}
					ImGui::SameLine();
					if (ImGui::ArrowButton("##TagPick", ImGuiDir_Down))
						ImGui::OpenPopup("##TagPicker");
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Pick an existing tag from this zone");
					//
					// Collect unique non-empty Level tags from the current zone
					//
					ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
					if (ImGui::BeginPopup("##TagPicker")) {
						std::vector<std::string> existingTags;
						for (const auto& other : zone.participants) {
							if (other.role != RetrodevLib::PaletteParticipantRole::Level || other.tag.empty())
								continue;
							bool found = false;
							for (const auto& t : existingTags)
								if (t == other.tag) {
									found = true;
									break;
								}
							if (!found)
								existingTags.push_back(other.tag);
						}
						if (existingTags.empty()) {
							ImGui::TextDisabled("No existing tags in this zone.");
						} else {
							for (const auto& t : existingTags) {
								bool sel = (t == p.tag);
								if (ImGui::Selectable(t.c_str(), sel)) {
									p.tag = t;
									RetrodevLib::Project::MarkAsModified();
									SetModified(true);
									ImGui::CloseCurrentPopup();
								}
							}
						}
						ImGui::EndPopup();
					}
					ImGui::PopStyleVar();
				}
			}
		}
		ImGui::EndChild();
		ImGui::SameLine();
		//
		// Right child: original palette for the selected participant
		//
		if (ImGui::BeginChild("##PartPanelRight", ImVec2(m_rightSizePal, -1), true)) {
			if (m_selectedParticipant >= 0 && m_selectedParticipant < (int)zone.participants.size()) {
				const RetrodevLib::PaletteParticipant& selP = zone.participants[m_selectedParticipant];
				if (!selP.buildItemName.empty()) {
					//
					// Rebuild the original converter when the selection changes
					//
					if (m_lastOriginalZone != m_selectedZone || m_lastOriginalParticipant != m_selectedParticipant) {
						m_lastOriginalZone = m_selectedZone;
						m_lastOriginalParticipant = m_selectedParticipant;
						m_originalConverter = nullptr;
						//
						// Fetch the item's own stored GFXParams (no occupied-slot injection)
						//
						RetrodevLib::GFXParams* stored = nullptr;
						if (selP.buildItemType == "Bitmap")
							RetrodevLib::Project::BitmapGetCfg(selP.buildItemName, &stored);
						else if (selP.buildItemType == "Tilemap")
							RetrodevLib::Project::TilesetGetCfg(selP.buildItemName, &stored);
						else if (selP.buildItemType == "Sprite")
							RetrodevLib::Project::SpriteGetCfg(selP.buildItemName, &stored);
						m_originalGfx = stored ? *stored : RetrodevLib::GFXParams{};
						//
						// Resolve the source image path
						//
						std::string srcPath;
						if (selP.buildItemType == "Bitmap")
							srcPath = RetrodevLib::Project::BitmapGetSourcePath(selP.buildItemName);
						else if (selP.buildItemType == "Tilemap")
							srcPath = RetrodevLib::Project::TilesetGetSourcePath(selP.buildItemName);
						else if (selP.buildItemType == "Sprite")
							srcPath = RetrodevLib::Project::SpriteGetSourcePath(selP.buildItemName);
						if (!srcPath.empty()) {
							const std::string& folder = RetrodevLib::Project::GetProjectFolder();
							if (srcPath.size() <= 1 || (srcPath[1] != ':' && srcPath[0] != '/'))
								srcPath = folder + "/" + srcPath;
						}
						//
						// Run a one-shot conversion to populate the original palette
						//
						auto origConv = RetrodevLib::Converters::GetBitmapConverter(&m_originalGfx);
						if (origConv && !srcPath.empty()) {
							auto srcImg = RetrodevLib::Image::ImageLoad(srcPath);
							if (srcImg) {
								if (m_originalGfx.RParams.TargetWidth == 0 || m_originalGfx.RParams.TargetHeight == 0) {
									m_originalGfx.RParams.TargetWidth = 1;
									m_originalGfx.RParams.TargetHeight = 1;
								}
								origConv->SetOriginal(srcImg);
								origConv->Convert(&m_originalGfx);
								m_originalConverter = origConv;
							}
						}
					}
					if (m_originalConverter) {
						ImGui::AlignTextToFramePadding();
						ImGui::TextDisabled(ICON_PALETTE "  Original");
						auto origPalette = m_originalConverter->GetPalette();
						if (origPalette)
							PaletteWidget::Render(&m_originalGfx, origPalette, false);
					}
				} else {
					ImGui::TextDisabled("Select a participant to view its original palette.");
				}
			} else {
				ImGui::TextDisabled("Select a participant to view its original palette.");
			}
		}
		ImGui::EndChild();
	}
	//
	// Write solved palette assignments back into each participant's stored GFXParams.
	// Delegates to PaletteSolver::Validate in the lib so automated tools can call it too.
	//
	void DocumentPalette::ValidateSolution(RetrodevLib::PaletteParams* params) {
		if (!m_hasSolution || !params)
			return;
		RetrodevLib::PaletteSolver::Validate(m_solution, params);
		//
		// Record that the user has explicitly accepted this solution so the build pipeline
		// will apply the assignments even when the palette fit was imperfect (overflow remaps).
		//
		params->userValidated = true;
		//
		// Notify all open documents that the affected build items have changed so they
		// can refresh their previews with the newly assigned palette.
		//
		for (int zi = 0; zi < (int)params->zones.size(); zi++) {
			for (const auto& p : params->zones[zi].participants) {
				if (!p.buildItemName.empty())
					DocumentsView::NotifyProjectItemChanged(p.buildItemName);
			}
		}
	}
	//
	// Pre-loaded palette widget: lets the user manually assign and lock pen slots before solving.
	// Locked slots are injected into the solver as immutable seed colors (first-pass always union).
	//
	void DocumentPalette::RenderPreloadPaletteWidget(RetrodevLib::PaletteParams* params) {
		if (!m_converter)
			return;
		if (!m_preloadPalette)
			return;
		auto pal = m_preloadPalette->GetPalette();
		if (!pal)
			return;
		int penCount = pal->PaletteMaxColors();
		//
		// Keep m_preloadGfx arrays in sync with the current pen count
		//
		if ((int)m_preloadGfx.SParams.PaletteColors.size() != penCount) {
			m_preloadGfx.SParams.PaletteColors.resize(penCount, -1);
			m_preloadGfx.SParams.PaletteLocked.resize(penCount, false);
			m_preloadGfx.SParams.PaletteEnabled.resize(penCount, false);
		}
		//
		// Render the palette widget; returns true when any pen was changed
		//
		if (PaletteWidget::Render(&m_preloadGfx, pal, false)) {
			params->preloadedColors.resize(penCount, -1);
			params->preloadedLocked.resize(penCount, false);
			for (int i = 0; i < penCount; i++) {
				params->preloadedColors[i] = m_preloadGfx.SParams.PaletteColors[i];
				params->preloadedLocked[i] = m_preloadGfx.SParams.PaletteLocked[i];
				//
				// Keep PaletteEnabled in sync: a locked+assigned pen is enabled,
				// anything else is disabled so the solver sees the correct active set.
				//
				bool shouldEnable = m_preloadGfx.SParams.PaletteLocked[i] && (m_preloadGfx.SParams.PaletteColors[i] >= 0);
				if (i < (int)m_preloadGfx.SParams.PaletteEnabled.size())
					m_preloadGfx.SParams.PaletteEnabled[i] = shouldEnable;
				pal->PenEnable(i, shouldEnable);
			}
			//
			// Any change to the pre-loaded palette invalidates the current solution
			//
			m_hasSolution = false;
			m_solution = {};
			m_selectedSolutionZone = -1;
			m_selectedSolutionTag = -1;
			m_lastDisplayZone = -1;
			m_lastDisplayTag = -1;
			m_displayConverter = nullptr;
			m_selectedPreviewKey = "";
			m_preloadedSelected = false;
			params->userValidated = false;
			RetrodevLib::Project::MarkAsModified();
			SetModified(true);
		}
	}
	//
	// Solve / validation panel: trigger PaletteSolver and display the result per zone and level
	//
	void DocumentPalette::RenderSolvePanel(RetrodevLib::PaletteParams* params) {
		//
		// Snapshot m_hasSolution once so mid-frame mutations (e.g. OnProjectItemChanged called
		// from ValidateSolution) cannot unbalance BeginDisabled/EndDisabled pairs.
		//
		const bool hasSolution = m_hasSolution;
		float fontSize = ImGui::GetFontSize();
		//
		// Solve and Validate buttons
		//
		ImGui::AlignTextToFramePadding();
		ImGui::Text(ICON_MAGNIFY_SCAN "  Validation");
		ImGui::SameLine();
		if (ImGui::Button(ICON_PLAY "  Solve##SolveBtn")) {
			m_solution = RetrodevLib::PaletteSolver::Solve(params, RetrodevLib::Project::GetProjectFolder());
			m_hasSolution = true;
			m_selectedSolutionZone = -1;
			m_selectedSolutionTag = -1;
			m_lastDisplayZone = -1;
			m_lastDisplayTag = -1;
			m_displayConverter = nullptr;
			m_selectedPreviewKey = "";
			m_preloadedSelected = true;
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("Run a three-pass solve for all zones.\n\n"
							  "  Pass 1 (global Always): Always participants from every zone are\n"
							  "  quantized together so the same pen slots hold the same colors\n"
							  "  in every zone and every level.\n\n"
							  "  Pass 2 (zone base): ZoneAlways participants are fitted on top of\n"
							  "  the global base, producing a per-zone stable palette.\n\n"
							  "  Pass 3 (levels): Level participants are fitted per (zone x tag)\n"
							  "  on top of the zone base, producing one palette per level.\n\n"
							  "The project is not modified by the solve.");
		ImGui::SameLine();
		if (!hasSolution)
			ImGui::BeginDisabled();
		if (ImGui::Button(ICON_CHECK "  Validate##ValidateBtn"))
			ValidateSolution(params);
		if (!hasSolution)
			ImGui::EndDisabled();
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("Write the solved palette assignments back into each participant's\n"
							  "stored build item configuration.\n\n"
							  "This updates the PaletteLocked, PaletteEnabled and PaletteColors\n"
							  "arrays for every Bitmap, Tilemap and Sprite involved in the solve\n"
							  "so that subsequent conversions use the solved palette.\n\n"
							  "The project is marked as modified and must be saved to persist the changes.");
		//
		// Overflow method combo -- controls how the solver handles color counts exceeding the pen budget
		//
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Overflow");
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
			ImGui::SetTooltip("How to handle palettes that exceed the hardware pen budget:\n\n"
							  "  Hard Cap      --  Truncate the union list at the pen limit. Priority\n"
							  "                   order (Always > Zone > Level) ensures the most\n"
							  "                   important colors survive. Dropped colors are remapped\n"
							  "                   to the nearest surviving entry.\n\n"
							  "  Soft Cap      --  For each overflow color, find the nearest accepted\n"
							  "                   entry and replace it with the system color closest to\n"
							  "                   their 50/50 RGB midpoint. Packs more perceptual\n"
							  "                   variety into fewer pens at some cost to accuracy.\n\n"
							  "  Weighted Blend--  Like Soft Cap but the blend is 67%% accepted + 33%%\n"
							  "                   overflow. Accepted colors shift only slightly toward\n"
							  "                   overflow neighbors, preserving dominant color\n"
							  "                   fidelity better while still gaining some coverage.\n\n"
							  "  Median        --  Cluster each overflow color with the accepted entry\n"
							  "                   it is nearest to, then replace the accepted entry with\n"
							  "                   the RGB centroid of the whole cluster. Multiple\n"
							  "                   overflow colors hitting the same accepted entry are\n"
							  "                   absorbed equally, spreading the compromise evenly.");
		ImGui::SameLine();
		static const char* kOverflowLabels[] = {"Hard Cap", "Soft Cap", "Weighted Blend", "Median"};
		int overflowIdx = (int)params->overflowMethod;
		ImGui::SetNextItemWidth(fontSize * 13.0f);
		if (ImGui::BeginCombo("##OverflowMethod", kOverflowLabels[overflowIdx])) {
			for (int i = 0; i < 4; i++) {
				bool sel = (overflowIdx == i);
				if (ImGui::Selectable(kOverflowLabels[i], sel)) {
					params->overflowMethod = (RetrodevLib::PaletteOverflowMethod)i;
					RetrodevLib::Project::MarkAsModified();
					SetModified(true);
				}
				if (sel)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		if (!hasSolution) {
			ImGui::Separator();
			ImGui::AlignTextToFramePadding();
			ImGui::TextDisabled(ICON_PALETTE "  Pre-loaded palette");
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
				ImGui::SetTooltip("Lock one or more pen slots before solving.\n"
								  "Locked colors are injected first into the solver's\n"
								  "global Always pass, guaranteeing they occupy specific\n"
								  "pen slots in every zone and every level solution.");
			RenderPreloadPaletteWidget(params);
			return;
		}
		//
		// Summary line with colour-coded status
		//
		if (m_solution.valid)
			ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), ICON_CHECK_CIRCLE "  %s", m_solution.summary.c_str());
		else
			ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), ICON_ALERT_CIRCLE "  %s", m_solution.summary.c_str());
		ImGui::Separator();
		//
		// Split: left = solution list, right = selected palette display
		//
		float listWidth = fontSize * 18.0f;
		float palWidth = ImGui::GetContentRegionAvail().x - listWidth - ImGui::GetStyle().ItemSpacing.x;
		if (palWidth < fontSize * 8.0f)
			palWidth = fontSize * 8.0f;
		//
		// Solution list -- one selectable row per (zone x tag)
		//
		if (ImGui::BeginChild("##SolveList", ImVec2(listWidth, -1), true)) {
			static const ImVec4 kColorOK = {0.3f, 1.0f, 0.3f, 1.0f};
			static const ImVec4 kColorBad = {1.0f, 0.4f, 0.4f, 1.0f};
			// static const ImVec4 kColorTagLabel = {0.7f, 0.85f, 1.0f, 1.0f};
			//
			// Pre-loaded entry -- always at the top of the list
			//
			if (ImGui::Selectable(ICON_PALETTE "  Pre-loaded##preloaded", m_preloadedSelected))
				m_preloadedSelected = true;
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
				ImGui::SetTooltip("Show the pre-loaded palette -- the seed colors that\nwere locked before solving.");
			ImGui::Separator();
			for (int zi = 0; zi < (int)m_solution.zones.size(); zi++) {
				const RetrodevLib::PaletteZoneSolution& zs = m_solution.zones[zi];
				const std::string& zoneName = (zi < (int)params->zones.size()) ? params->zones[zi].name : "Zone " + std::to_string(zi + 1);
				ImGui::PushStyleColor(ImGuiCol_Text, zs.baseFit ? kColorOK : kColorBad);
				ImGui::TextUnformatted((std::string(zs.baseFit ? ICON_CHECK_CIRCLE : ICON_ALERT_CIRCLE) + "  " + zoneName).c_str());
				ImGui::PopStyleColor();
				//
				// One selectable per tag solution in this zone
				//
				for (int ti = 0; ti < (int)zs.tagSolutions.size(); ti++) {
					const RetrodevLib::PaletteTagSolution& ts = zs.tagSolutions[ti];
					bool isSel = (m_selectedSolutionZone == zi && m_selectedSolutionTag == ti);
					//
					// Label: "Base" for tag == "", otherwise the tag name
					//
					std::string rowLabel;
					if (ts.tag.empty())
						rowLabel = std::string("  ") + ICON_PALETTE_SWATCH + "  Base  [" + std::to_string(ts.pensUsed) + "/" + std::to_string(zs.pensAvailable) + "]";
					else
						rowLabel = std::string("  ") + ICON_TAG + "  " + ts.tag + "  [" + std::to_string(ts.pensUsed) + "/" + std::to_string(zs.pensAvailable) + "]";
					rowLabel += "##zts" + std::to_string(zi) + "_" + std::to_string(ti);
					ImGui::PushStyleColor(ImGuiCol_Text, ts.fit ? kColorOK : kColorBad);
					if (ImGui::Selectable(rowLabel.c_str(), isSel)) {
						m_selectedSolutionZone = zi;
						m_selectedSolutionTag = ti;
						m_preloadedSelected = false;
					}
					ImGui::PopStyleColor();
				}
			}
			if (m_solution.zones.empty())
				ImGui::TextDisabled("No zones.");
		}
		ImGui::EndChild();
		ImGui::SameLine();
		//
		// Right side: selected palette display
		//
		if (ImGui::BeginChild("##SolvePalette", ImVec2(palWidth, -1), true)) {
			//
			// Pre-loaded palette view -- shown when the Pre-loaded entry is selected
			//
			if (m_preloadedSelected) {
				ImGui::AlignTextToFramePadding();
				ImGui::TextDisabled(ICON_PALETTE "  Pre-loaded palette");
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
					ImGui::SetTooltip("Lock one or more pen slots before solving.\n"
									  "Locked colors are injected first into the solver's\n"
									  "global Always pass, guaranteeing they occupy specific\n"
									  "pen slots in every zone and every level solution.");
				RenderPreloadPaletteWidget(params);
			}
			bool shown = m_preloadedSelected;
			if (!m_preloadedSelected && m_selectedSolutionZone >= 0 && m_selectedSolutionZone < (int)m_solution.zones.size()) {
				const RetrodevLib::PaletteZoneSolution& zs = m_solution.zones[m_selectedSolutionZone];
				if (m_selectedSolutionTag >= 0 && m_selectedSolutionTag < (int)zs.tagSolutions.size()) {
					const RetrodevLib::PaletteTagSolution& ts = zs.tagSolutions[m_selectedSolutionTag];
					//
					// Rebuild the display converter whenever the selection changes
					//
					if (m_lastDisplayZone != m_selectedSolutionZone || m_lastDisplayTag != m_selectedSolutionTag) {
						m_lastDisplayZone = m_selectedSolutionZone;
						m_lastDisplayTag = m_selectedSolutionTag;
						m_displayConverter = nullptr;
						//
						// Build a fresh GFXParams seeded with the solved pen assignments
						//
						m_displayGfx = RetrodevLib::GFXParams{};
						m_displayGfx.SParams.TargetSystem = ts.targetSystem;
						m_displayGfx.SParams.TargetMode = ts.targetMode;
						m_displayGfx.SParams.PaletteType = params->targetPaletteType;
						int maxPens = (int)ts.occupiedSlots.size();
						m_displayGfx.SParams.PaletteLocked.assign(maxPens, false);
						m_displayGfx.SParams.PaletteEnabled.assign(maxPens, false);
						m_displayGfx.SParams.PaletteColors.assign(maxPens, -1);
						for (int i = 0; i < maxPens; i++) {
							if (ts.occupiedSlots[i] >= 0) {
								m_displayGfx.SParams.PaletteLocked[i] = true;
								m_displayGfx.SParams.PaletteEnabled[i] = true;
								m_displayGfx.SParams.PaletteColors[i] = ts.occupiedSlots[i];
							}
						}
						//
						// Run a 1x1 conversion so the palette internalises the lock/enable arrays
						//
						auto conv = RetrodevLib::Converters::GetBitmapConverter(&m_displayGfx);
						if (conv) {
							auto dummy = RetrodevLib::Image::ImageCreate(1, 1);
							if (dummy) {
								m_displayGfx.RParams.TargetWidth = 1;
								m_displayGfx.RParams.TargetHeight = 1;
								conv->SetOriginal(dummy);
								conv->Convert(&m_displayGfx);
								conv->SetOriginal(nullptr);
							}
							m_displayConverter = conv;
						}
					}
					if (m_displayConverter) {
						const std::string& zoneName = (m_selectedSolutionZone < (int)params->zones.size()) ? params->zones[m_selectedSolutionZone].name
																										   : "Zone " + std::to_string(m_selectedSolutionZone + 1);
						std::string header = ICON_PALETTE "  " + zoneName;
						if (ts.tag.empty())
							header += " / Base";
						else
							header += " / " + ts.tag;
						header += "  (" + std::to_string(ts.pensUsed) + " / " + std::to_string(zs.pensAvailable) + " pens)";
						ImGui::AlignTextToFramePadding();
						if (ts.fit)
							ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", header.c_str());
						else
							ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", header.c_str());
						//
						// Helper lambda: render one participant result row.
						// Assigned-color swatches appear inline (SameLine) after the text.
						// PushID(r.participantIndex) ensures unique ImGui IDs across all participants.
						//
						auto renderParticipantResult = [&](const RetrodevLib::PaletteParticipantResult& r) {
							if (r.participantIndex < 0)
								return;
							const std::string& partName =
								(m_selectedSolutionZone < (int)params->zones.size() && r.participantIndex < (int)params->zones[m_selectedSolutionZone].participants.size())
									? params->zones[m_selectedSolutionZone].participants[r.participantIndex].buildItemName
									: "?";
							ImVec4 col;
							const char* icon;
							if (r.status == RetrodevLib::PaletteParticipantStatus::OK) {
								col = {0.3f, 1.0f, 0.3f, 1.0f};
								icon = ICON_CHECK;
							} else if (r.status == RetrodevLib::PaletteParticipantStatus::PenOverflow) {
								col = {1.0f, 0.4f, 0.4f, 1.0f};
								icon = ICON_PALETTE_SWATCH;
							} else if (r.status == RetrodevLib::PaletteParticipantStatus::Missing || r.status == RetrodevLib::PaletteParticipantStatus::ImageLoadFailed) {
								col = {1.0f, 0.8f, 0.2f, 1.0f};
								icon = ICON_HELP_CIRCLE;
							} else {
								col = {0.5f, 0.5f, 0.5f, 1.0f};
								icon = ICON_MINUS_CIRCLE;
							}
							ImGui::PushID(r.participantIndex);
							ImGui::PushStyleColor(ImGuiCol_Text, col);
							ImGui::TextUnformatted((std::string(icon) + "  " + partName + " -- " + r.message).c_str());
							ImGui::PopStyleColor();
							//
							// Overflow swatches inline after the participant text.
							// One swatch per overflow color in this tag solution; tooltip shows the full remap story.
							//
							if (r.status == RetrodevLib::PaletteParticipantStatus::OK && !ts.overflowRemaps.empty()) {
								float swatchSize = ImGui::GetFontSize() * 0.85f;
								for (int oi = 0; oi < (int)ts.overflowRemaps.size(); oi++) {
									const auto& rm = ts.overflowRemaps[oi];
									ImGui::SameLine(0.0f, oi == 0 ? 6.0f : 2.0f);
									ImGui::PushID(oi);
									ImVec4 ovCol(rm.overflowR / 255.0f, rm.overflowG / 255.0f, rm.overflowB / 255.0f, 1.0f);
									ImGui::ColorButton("##ov", ovCol, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(swatchSize, swatchSize));
									if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
										ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
										ImGui::BeginTooltip();
										ImGui::TextDisabled("Overflow color");
										float ttSwatch = ImGui::GetFontSize() * 1.1f;
										ImGui::ColorButton("##ttov", ovCol, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(ttSwatch, ttSwatch));
										ImGui::SameLine(0.0f, 6.0f);
										ImGui::Text("idx %d  \xc2\xb7  RGB(%d, %d, %d)", rm.overflowColorIndex, rm.overflowR, rm.overflowG, rm.overflowB);
										ImGui::Separator();
										if (rm.slot >= 0) {
											ImGui::TextDisabled("Nearest accepted  \xc2\xb7  slot %d", rm.slot);
											ImVec4 nearCol(rm.nearestR / 255.0f, rm.nearestG / 255.0f, rm.nearestB / 255.0f, 1.0f);
											ImGui::ColorButton("##ttnear", nearCol, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(ttSwatch, ttSwatch));
											ImGui::SameLine(0.0f, 6.0f);
											ImGui::Text("idx %d  \xc2\xb7  RGB(%d, %d, %d)", rm.nearestColorIndex, rm.nearestR, rm.nearestG, rm.nearestB);
											ImGui::Separator();
										}
										if (rm.dropped) {
											ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Dropped -- slot %d unchanged", rm.slot >= 0 ? rm.slot : 0);
										} else {
											bool slotChanged = (rm.resultColorIndex != rm.nearestColorIndex);
											ImGui::TextDisabled(slotChanged ? "Slot %d updated to:" : "Slot %d unchanged (overflow absorbed):", rm.slot);
											ImVec4 resCol(rm.resultR / 255.0f, rm.resultG / 255.0f, rm.resultB / 255.0f, 1.0f);
											ImGui::ColorButton("##ttres", resCol, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(ttSwatch, ttSwatch));
											ImGui::SameLine(0.0f, 6.0f);
											ImGui::Text("idx %d  \xc2\xb7  RGB(%d, %d, %d)", rm.resultColorIndex, rm.resultR, rm.resultG, rm.resultB);
										}
										ImGui::EndTooltip();
										ImGui::PopStyleVar();
									}
									ImGui::PopID();
								}
							}
							ImGui::PopID();
						};
						//
						// For level/screen-zone solutions show the inherited base participants first
						// (Always + ZoneAlways), then this tag's own participants.
						// The base pass results are in tagSolutions[0]; skip them if we're already
						// viewing the base solution (ts.tag.empty()) to avoid duplication.
						//
						if (!ts.tag.empty() && !zs.tagSolutions.empty()) {
							const RetrodevLib::PaletteTagSolution& base = zs.tagSolutions[0];
							if (!base.participantResults.empty()) {
								ImGui::TextDisabled("Inherited:");
								for (const auto& r : base.participantResults)
									renderParticipantResult(r);
							}
							if (!ts.participantResults.empty())
								ImGui::TextDisabled("This level:");
						}
						//
						// Display pen results for the participants in this tag solution
						//
						for (const auto& r : ts.participantResults)
							renderParticipantResult(r);
						//
						// Overflow remap strip -- one swatch per color that exceeded the pen budget.
						// Only the overflow colors are shown inline; all remap details are in the tooltip.
						//
						if (!ts.overflowRemaps.empty()) {
							ImGui::Separator();
							ImGui::AlignTextToFramePadding();
							ImGui::TextDisabled("Overflow (%d):", (int)ts.overflowRemaps.size());
							float swatchSize = ImGui::GetFontSize() * 0.95f;
							for (int ri = 0; ri < (int)ts.overflowRemaps.size(); ri++) {
								const auto& rm = ts.overflowRemaps[ri];
								ImGui::SameLine(0.0f, ri == 0 ? 6.0f : 3.0f);
								ImGui::PushID(ri);
								ImVec4 ovCol(rm.overflowR / 255.0f, rm.overflowG / 255.0f, rm.overflowB / 255.0f, 1.0f);
								ImGui::ColorButton("##ov", ovCol, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(swatchSize, swatchSize));
								if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
									ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
									ImGui::BeginTooltip();
									//
									// Header: overflow color
									//
									ImGui::TextDisabled("Overflow color");
									float ttSwatch = ImGui::GetFontSize() * 1.1f;
									ImGui::ColorButton("##ttov", ovCol, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(ttSwatch, ttSwatch));
									ImGui::SameLine(0.0f, 6.0f);
									ImGui::Text("idx %d  \xc2\xb7  RGB(%d, %d, %d)", rm.overflowColorIndex, rm.overflowR, rm.overflowG, rm.overflowB);
									ImGui::Separator();
									//
									// Nearest accepted slot
									//
									if (rm.slot >= 0) {
										ImGui::TextDisabled("Nearest accepted  \xc2\xb7  slot %d", rm.slot);
										ImVec4 nearCol(rm.nearestR / 255.0f, rm.nearestG / 255.0f, rm.nearestB / 255.0f, 1.0f);
										ImGui::ColorButton("##ttnear", nearCol, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(ttSwatch, ttSwatch));
										ImGui::SameLine(0.0f, 6.0f);
										ImGui::Text("idx %d  \xc2\xb7  RGB(%d, %d, %d)", rm.nearestColorIndex, rm.nearestR, rm.nearestG, rm.nearestB);
									}
									ImGui::Separator();
									//
									// Result: dropped or slot updated
									//
									if (rm.dropped) {
										ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Dropped -- slot %d unchanged", rm.slot >= 0 ? rm.slot : 0);
									} else {
										bool slotChanged = (rm.resultColorIndex != rm.nearestColorIndex);
										if (slotChanged)
											ImGui::TextDisabled("Slot %d updated to:", rm.slot);
										else
											ImGui::TextDisabled("Slot %d unchanged (overflow absorbed):", rm.slot);
										ImVec4 resCol(rm.resultR / 255.0f, rm.resultG / 255.0f, rm.resultB / 255.0f, 1.0f);
										ImGui::ColorButton("##ttres", resCol, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(ttSwatch, ttSwatch));
										ImGui::SameLine(0.0f, 6.0f);
										ImGui::Text("idx %d  \xc2\xb7  RGB(%d, %d, %d)", rm.resultColorIndex, rm.resultR, rm.resultG, rm.resultB);
									}
									ImGui::EndTooltip();
									ImGui::PopStyleVar();
								}
								ImGui::PopID();
							}
						}
						ImGui::Separator();
						//
						// Compact read-only solved palette strip -- one swatch per pen slot.
						// Occupied slots show their solved color; free slots show as dark grey.
						// Hover a swatch to see its pen number, system color index and RGB.
						//
						{
							float swatchSize = ImGui::GetFontSize() * 1.2f;
							int slotCount = (int)ts.occupiedSlots.size();
							ImGui::AlignTextToFramePadding();
							ImGui::TextDisabled("Palette:");
							for (int si = 0; si < slotCount; si++) {
								ImGui::SameLine(0.0f, si == 0 ? 6.0f : 2.0f);
								ImGui::PushID(si);
								int colorIdx = ts.occupiedSlots[si];
								ImVec4 slotCol;
								if (colorIdx >= 0) {
									auto livePal = m_displayConverter->GetPalette();
									RetrodevLib::RgbColor rgb = livePal->GetSystemColorByIndex(colorIdx);
									slotCol = ImVec4(rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f, 1.0f);
								} else {
									slotCol = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
								}
								ImGui::ColorButton("##ps", slotCol, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoBorder, ImVec2(swatchSize, swatchSize));
								if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
									if (colorIdx >= 0) {
										auto livePal = m_displayConverter->GetPalette();
										RetrodevLib::RgbColor rgb = livePal->GetSystemColorByIndex(colorIdx);
										ImGui::SetTooltip("Pen %d  \xc2\xb7  idx %d  \xc2\xb7  RGB(%d,%d,%d)", si, colorIdx, rgb.r, rgb.g, rgb.b);
									} else {
										ImGui::SetTooltip("Pen %d  \xc2\xb7  free", si);
									}
								}
								ImGui::PopID();
							}
						}
						//
						// Solution preview: display the selected left-panel thumbnail as converted
						// by the solver with the solution palette -- use the solver's own converter
						// so the image is exactly what the solver produced, not a re-conversion.
						//
						if (!m_selectedPreviewKey.empty()) {
							//
							// Decode key: "zoneName:buildItemType:buildItemName"
							// Find the participant index in the current zone by matching type+name
							//
							std::shared_ptr<RetrodevLib::IBitmapConverter> previewConv;
							const RetrodevLib::GFXParams* previewGfx = nullptr;
							//
							// Key format: "zoneName:buildItemType:buildItemName:pi"
							// Find the three colon positions to extract type, name and participant index.
							//
							size_t c1 = m_selectedPreviewKey.find(':');
							size_t c2 = (c1 != std::string::npos) ? m_selectedPreviewKey.find(':', c1 + 1) : std::string::npos;
							size_t c3 = (c2 != std::string::npos) ? m_selectedPreviewKey.find(':', c2 + 1) : std::string::npos;
							if (c1 != std::string::npos && c2 != std::string::npos && c3 != std::string::npos) {
								std::string keyType = m_selectedPreviewKey.substr(c1 + 1, c2 - c1 - 1);
								std::string keyName = m_selectedPreviewKey.substr(c2 + 1, c3 - c2 - 1);
								int keyPi = std::stoi(m_selectedPreviewKey.substr(c3 + 1));
								//
								// Look up by participant index directly -- handles duplicate build items.
								// Fall back to base solution for Always/ZoneAlways participants.
								//
								(void)keyType;
								(void)keyName;
								auto cit = ts.converters.find(keyPi);
								if (cit != ts.converters.end()) {
									previewConv = cit->second;
									auto git = ts.converterGfx.find(keyPi);
									if (git != ts.converterGfx.end())
										previewGfx = &git->second;
								} else if (!zs.tagSolutions.empty()) {
									const RetrodevLib::PaletteTagSolution& base = zs.tagSolutions[0];
									auto bit = base.converters.find(keyPi);
									if (bit != base.converters.end()) {
										previewConv = bit->second;
										auto git = base.converterGfx.find(keyPi);
										if (git != base.converterGfx.end())
											previewGfx = &git->second;
									}
								}
							}
							if (previewConv && previewGfx) {
								ImGui::Separator();
								auto previewImg = previewConv->GetPreview(previewGfx);
								SDL_Texture* tex = previewImg ? previewImg->GetTexture(Application::GetRenderer()) : nullptr;
								if (tex) {
									//
									// Look up (or default-create) the persistent zoom state for this key
									//
									ImGui::ZoomableState& zoomSt = m_previewZoomStates[m_selectedPreviewKey];
									//
									// Configure and render the zoomable preview -- fills remaining panel space
									// Name: extract the build item name segment (between c2 and c3)
									//
									size_t nc1 = m_selectedPreviewKey.find(':');
									size_t nc2 = (nc1 != std::string::npos) ? m_selectedPreviewKey.find(':', nc1 + 1) : std::string::npos;
									size_t nc3 = (nc2 != std::string::npos) ? m_selectedPreviewKey.find(':', nc2 + 1) : std::string::npos;
									std::string displayName = (nc2 != std::string::npos && nc3 != std::string::npos)
										? m_selectedPreviewKey.substr(nc2 + 1, nc3 - nc2 - 1)
										: m_selectedPreviewKey;
									zoomSt.name = ICON_FILE_IMAGE "  " + displayName;
									zoomSt.textureSize = ImVec2((float)previewImg->GetWidth(), (float)previewImg->GetHeight());
									zoomSt.logicalSize = ImVec2((float)previewGfx->RParams.TargetWidth, (float)previewGfx->RParams.TargetHeight);
									zoomSt.maintainAspectRatio = true;
									zoomSt.showPixelGrid = true;
									zoomSt.showInfo = true;
									ImGui::Zoomable(ImTextureRef(tex), ImVec2((float)previewImg->GetWidth(), (float)previewImg->GetHeight()), &zoomSt);
								} else {
									ImGui::TextDisabled("No preview available.");
								}
							} else {
								ImGui::Separator();
								ImGui::TextDisabled(ICON_FILE_IMAGE "  Select a thumbnail on the left to preview it with this palette.");
							}
						} else {
							ImGui::Separator();
							ImGui::TextDisabled(ICON_FILE_IMAGE "  Select a thumbnail on the left to preview it with this palette.");
						}
						shown = true;
					}
				}
			}
			if (!shown)
				ImGui::TextDisabled("Select a solution on the left to view its palette.");
		}
		ImGui::EndChild();
	}
	//
	// Main render entry point
	//
	void DocumentPalette::Perform() {
		//
		// Fetch palette params pointer every frame (vector may reallocate)
		//
		RetrodevLib::PaletteParams* params = nullptr;
		if (!RetrodevLib::Project::PaletteGetParams(m_name, &params) || params == nullptr) {
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Failed to load palette parameters for: %s", m_name.c_str());
			return;
		}
		//
		// Ensure a default zone exists
		//
		if (params->zones.empty()) {
			RetrodevLib::PaletteZone defaultZone;
			defaultZone.name = "Main";
			defaultZone.lineStart = 0;
			defaultZone.lineEnd = 199;
			params->zones.push_back(defaultZone);
			m_selectedZone = 0;
			RetrodevLib::Project::MarkAsModified();
		}
		//
		// Auto-select the first zone when the document is opened with existing zones
		//
		if (m_selectedZone < 0 && !params->zones.empty())
			m_selectedZone = 0;
		//
		// Keep converter in sync with the selected target
		//
		SyncConverter(params);
		//
		// Default targetPaletteType once the converter is available so it is never empty
		//
		if (params->targetPaletteType.empty() && m_converter) {
			std::vector<std::string> paletteTypes = m_converter->GetTargetPalettes();
			if (!paletteTypes.empty())
				params->targetPaletteType = paletteTypes[0];
		}
		float fontSize = ImGui::GetFontSize();
		//
		// Build item name -- editable for renaming (press Enter to apply)
		//
		static char paletteNameBuf[256] = "";
		if (paletteNameBuf[0] == '\0' || m_name != std::string(paletteNameBuf)) {
			std::strncpy(paletteNameBuf, m_name.c_str(), sizeof(paletteNameBuf) - 1);
			paletteNameBuf[sizeof(paletteNameBuf) - 1] = '\0';
		}
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Build Item Name:");
		ImGui::SameLine();
		if (ImGui::InputText("##paletteName", paletteNameBuf, sizeof(paletteNameBuf), ImGuiInputTextFlags_EnterReturnsTrue)) {
			std::string newName(paletteNameBuf);
			if (!newName.empty() && newName != m_name) {
				if (RetrodevLib::Project::RenameBuildItem(RetrodevLib::ProjectBuildType::Palette, m_name, newName)) {
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Renamed palette '%s' to '%s'", m_name.c_str(), newName.c_str());
					m_name = newName;
					ProjectView::NotifyProjectChanged();
					RetrodevLib::Project::MarkAsModified();
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Error, "Failed to rename palette '%s' to '%s' (name may already exist)", m_name.c_str(), newName.c_str());
					std::strncpy(paletteNameBuf, m_name.c_str(), sizeof(paletteNameBuf) - 1);
					paletteNameBuf[sizeof(paletteNameBuf) - 1] = '\0';
				}
			}
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Press Enter to rename the palette");
		ImGui::Separator();
		ImVec2 avail = ImGui::GetContentRegionAvail();
		//
		// One-time size initialisation: horizontal split and vertical split for the right panel
		//
		if (!m_sizesInitialized) {
			m_hSizeLeft = fontSize * 20.0f;
			m_hSizeRight = avail.x - m_hSizeLeft - Application::splitterThickness - ImGui::GetStyle().ItemSpacing.x;
			m_vSizeTop = fontSize * 14.0f;
			m_vSizeBottom = avail.y - m_vSizeTop - Application::splitterThickness - ImGui::GetStyle().ItemSpacing.y;
			m_sizesInitialized = true;
		}
		//
		// Keep right panel width in sync with window resizes
		//
		m_hSizeRight = avail.x - m_hSizeLeft - Application::splitterThickness - ImGui::GetStyle().ItemSpacing.x;
		//
		// Horizontal splitter: left (zones) | right (editor + solve)
		//
		ImGui::DrawSplitter(false, Application::splitterThickness, &m_hSizeLeft, &m_hSizeRight, fontSize * 12.0f, fontSize * 20.0f);
		//
		// Left panel
		//
		if (ImGui::BeginChild("##PalLeft", ImVec2(m_hSizeLeft, -1), true))
			RenderLeftPanel(params);
		ImGui::EndChild();
		ImGui::SameLine();
		//
		// Right panel: vertical splitter between zone editor (top) and solve panel (bottom)
		//
		if (ImGui::BeginChild("##PalRight", ImVec2(m_hSizeRight, -1), true)) {
			float rightAvailY = ImGui::GetContentRegionAvail().y;
				float vMinTop = fontSize * 8.0f;
				float vMinBottom = fontSize * 8.0f;
				//
				// Derive bottom height from available space so new space goes to the bottom panel
				//
				m_vSizeBottom = rightAvailY - m_vSizeTop - Application::splitterThickness - ImGui::GetStyle().ItemSpacing.y;
				if (m_vSizeBottom < vMinBottom) {
					m_vSizeBottom = vMinBottom;
					m_vSizeTop = rightAvailY - m_vSizeBottom - Application::splitterThickness - ImGui::GetStyle().ItemSpacing.y;
					if (m_vSizeTop < vMinTop)
						m_vSizeTop = vMinTop;
				}
			//
			// Vertical splitter between zone/participant editor and solve panel
			//
			ImGui::DrawSplitter(true, Application::splitterThickness, &m_vSizeTop, &m_vSizeBottom, vMinTop, vMinBottom);
			//
			// Top: zone name, scanlines, participant list and per-participant palette preview
			//
			if (ImGui::BeginChild("##PalRightTop", ImVec2(-1, m_vSizeTop), true))
				RenderRightPanel(params);
			ImGui::EndChild();
			//
			// Jump the splitter
			//
			ImVec2 splitterSkip = ImGui::GetCursorScreenPos();
			splitterSkip.y += Application::splitterThickness;
			ImGui::SetCursorScreenPos(splitterSkip);
			//
			// Bottom: solve/validate buttons + solution result tree + solved palette display
			//
			if (ImGui::BeginChild("##PalRightBottom", ImVec2(-1, m_vSizeBottom), true))
				RenderSolvePanel(params);
			ImGui::EndChild();
		}
		ImGui::EndChild();
	}

}
