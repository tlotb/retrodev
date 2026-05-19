// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Gui
//
// Map editor document -- tile map painting with layers and parallax.
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#include "document.map.h"
#include <app/app.h>
#include <app/app.console.h>
#include <app/app.icons.mdi.h>
#include <views/main.view.project.h>
#include <convert/converters.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace RetrodevGui {
	//
	// Return sorted unique deleted-tile absolute indices for the slot's active variant
	//
	static std::vector<int> GetSlotDeletedTiles(const RetrodevLib::TilesetSlot& slot) {
		std::vector<int> deleted;
		if (slot.variants.empty())
			return deleted;
		int vi = std::max(0, std::min(slot.activeVariant, (int)slot.variants.size() - 1));
		RetrodevLib::TileExtractionParams* tileParams = nullptr;
		if (!RetrodevLib::Project::TilesetGetTileParams(slot.variants[vi], &tileParams) || tileParams == nullptr)
			return deleted;
		deleted = tileParams->DeletedTiles;
		std::sort(deleted.begin(), deleted.end());
		deleted.erase(std::unique(deleted.begin(), deleted.end()), deleted.end());
		return deleted;
	}
	//
	// Translate a tile index from compact space to absolute space using deleted slots
	//
	static int TranslateTileCompactToAbsolute(int compactIdx, const std::vector<int>& deleted) {
		int absIdx = compactIdx;
		for (int del : deleted) {
			if (del <= absIdx)
				absIdx++;
			else
				break;
		}
		return absIdx;
	}
	//
	// Translate a tile index from absolute space to compact space using deleted slots
	// Returns -1 when the absolute index points to a deleted slot (no compact representation)
	//
	static int TranslateTileAbsoluteToCompact(int absIdx, const std::vector<int>& deleted) {
		if (std::binary_search(deleted.begin(), deleted.end(), absIdx))
			return -1;
		int removedBefore = (int)(std::lower_bound(deleted.begin(), deleted.end(), absIdx) - deleted.begin());
		return absIdx - removedBefore;
	}
	//
	// Translate one map cell between compact and absolute tile index spaces
	//
	static uint16_t TranslateMapCell(uint16_t cellVal, const std::vector<int>& deleted, bool compactToAbsolute) {
		if (cellVal == 0)
			return 0;
		int slotBits = (int)(cellVal >> 12);
		int tileIdx = (int)(cellVal & 0x0FFF);
		int translatedIdx = compactToAbsolute ? TranslateTileCompactToAbsolute(tileIdx, deleted) : TranslateTileAbsoluteToCompact(tileIdx, deleted);
		if (translatedIdx < 0 || translatedIdx > 0x0FFF)
			return 0;
		return (uint16_t)((slotBits << 12) | translatedIdx);
	}
	//
	// Translate every map/group cell for all slot indices between compact and absolute spaces
	//
	static void TranslateMapParamsCells(RetrodevLib::MapParams* params, bool compactToAbsolute) {
		if (params == nullptr)
			return;
		for (int si = 0; si < (int)params->tilesets.size(); si++) {
			const std::vector<int> deleted = GetSlotDeletedTiles(params->tilesets[si]);
			if (deleted.empty())
				continue;
			int slotBit = si + 1;
			for (auto& layer : params->layers) {
				for (auto& cell : layer.data) {
					if (cell == 0 || (int)(cell >> 12) != slotBit)
						continue;
					cell = TranslateMapCell(cell, deleted, compactToAbsolute);
				}
			}
			for (auto& group : params->groups) {
				for (auto& cell : group.tiles) {
					if (cell == 0 || (int)(cell >> 12) != slotBit)
						continue;
					cell = TranslateMapCell(cell, deleted, compactToAbsolute);
				}
			}
		}
	}

	//
	// Constructor: name only, maps have no source file
	//
	DocumentMap::DocumentMap(const std::string& name) : DocumentView(name, "") {}

	DocumentMap::~DocumentMap() {}
	//
	// Load abs data from compact params (compact->absolute) on first open
	//
	void DocumentMap::LoadAbsData(RetrodevLib::MapParams* params) {
		m_absLayers = params->layers;
		m_absGroups = params->groups;
		//
		// Retrieve tileset deleted-tile lists and translate every cell compact->absolute
		//
        RetrodevLib::MapParams absParams = *params;
		absParams.layers = m_absLayers;
		absParams.groups = m_absGroups;
		TranslateMapParamsCells(&absParams, true);
		m_absLayers = std::move(absParams.layers);
		m_absGroups = std::move(absParams.groups);
		m_absDataLoaded = true;
	}
	//
	// Flush abs data back to compact params (absolute->compact) before project save
	//
	void DocumentMap::FlushAbsData(RetrodevLib::MapParams* params) {
		params->layers = m_absLayers;
		params->groups = m_absGroups;
		//
		// Translate every cell absolute->compact
		//
        TranslateMapParamsCells(params, false);
	}
	//
	// Flush absolute UI data back to compact lib params before project save
	//
	bool DocumentMap::Save() {
		if (!m_absDataLoaded)
			return true;
		RetrodevLib::MapParams* params = nullptr;
		if (!RetrodevLib::Project::MapGetParams(m_name, &params) || params == nullptr)
			return false;
		FlushAbsData(params);
		return true;
	}
	//
	// Main render entry point
	//
	void DocumentMap::Perform() {
		//
		// Query map parameters pointer on every frame (pointer may shift if project vector is mutated)
		//
		RetrodevLib::MapParams* params = nullptr;
		if (!RetrodevLib::Project::MapGetParams(m_name, &params) || params == nullptr) {
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Failed to load map parameters for: %s", m_name.c_str());
			return;
		}
		//
		// Load UI-side absolute data from compact lib params on first open
		//
		if (!m_absDataLoaded)
			LoadAbsData(params);
		//
		// Ensure abs layer data vectors are sized to each layer's width * height
		//
		for (int li = 0; li < (int)params->layers.size() && li < (int)m_absLayers.size(); li++) {
			RetrodevLib::MapLayer& absL = m_absLayers[li];
			const RetrodevLib::MapLayer& pL = params->layers[li];
			if (absL.width != pL.width || absL.height != pL.height) {
				absL.width = pL.width;
				absL.height = pL.height;
			}
			int expectedSize = absL.width * absL.height;
			if ((int)absL.data.size() != expectedSize)
				absL.data.resize(expectedSize, 0);
		}
      //
		// Ensure abs group tile vectors are sized to each group's width * height
		//
		for (int gi = 0; gi < (int)m_absGroups.size(); gi++) {
			RetrodevLib::TileGroup& absG = m_absGroups[gi];
			int expectedSize = absG.width * absG.height;
			if (expectedSize < 0)
				expectedSize = 0;
			if ((int)absG.tiles.size() != expectedSize)
				absG.tiles.resize(expectedSize, 0);
		}
		//
		// Build a working MapParams that uses abs layer/group data for all UI operations.
		// Metadata (tilesets, viewWidth/Height) comes from the lib params unchanged.
		//
		RetrodevLib::MapParams workingParams = *params;
		workingParams.layers = m_absLayers;
		workingParams.groups = m_absGroups;
		RetrodevLib::MapParams* wp = &workingParams;
		//
		// Clamp editing layer index to the valid range every frame
		//
		if (!wp->layers.empty())
			m_editingLayerIdx = std::max(0, std::min(m_editingLayerIdx, (int)wp->layers.size() - 1));
		//
		// Synchronise loaded tilesets with the project's tileset slot list
		//
		SyncLoadedTilesets(wp->tilesets);
		//
		// One-time splitter and pending-dimension initialisation
		//
		float fontSize = ImGui::GetFontSize();
		ImVec2 avail = ImGui::GetContentRegionAvail();
		if (!m_sizesInitialized) {
			m_hSizeRight = fontSize * 22.0f;
			m_hSizeLeft = avail.x - (m_hSizeRight + Application::splitterThickness + ImGui::GetStyle().ItemSpacing.x);
			if (!wp->layers.empty()) {
				int li = std::max(0, std::min(m_editingLayerIdx, (int)wp->layers.size() - 1));
				m_pendingWidth = wp->layers[li].width;
				m_pendingHeight = wp->layers[li].height;
			}
			m_sizesInitialized = true;
		}
		//
		// Recalculate left panel width on every frame to follow window resizes
		//
		m_hSizeLeft = avail.x - (m_hSizeRight + Application::splitterThickness + ImGui::GetStyle().ItemSpacing.x);
		float hMinLeft = fontSize * 15.0f;
		float hMinRight = fontSize * 15.0f;
		//
		// Horizontal splitter: canvas | tooling
		//
		ImGui::DrawSplitter(false, Application::splitterThickness, &m_hSizeLeft, &m_hSizeRight, hMinLeft, hMinRight);
		//
		// Left panel: top toolbar, canvas (no ImGui scroll), bottom scrollbar toolbar
		//
		if (ImGui::BeginChild("MapCanvasArea", ImVec2(m_hSizeLeft, 0), false, ImGuiWindowFlags_NoScrollbar)) {
			RenderCanvasToolbar(wp);
			float bottomH = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetStyle().ItemSpacing.y;
			if (ImGui::BeginChild("MapCanvasPanel", ImVec2(0, -bottomH), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
				RenderMapCanvas(wp);
			}
			ImGui::EndChild();
			RenderCanvasScrollbars(wp);
		}
		ImGui::EndChild();
		ImGui::SameLine();
		//
		// Right panel: tooling
		//
		if (ImGui::BeginChild("MapToolingPanel", ImVec2(m_hSizeRight, 0), true)) {
			ImGui::Indent(8.0f);
			RenderToolingPanel(wp);
			ImGui::Unindent(8.0f);
		}
		ImGui::EndChild();
		//
		// Write working layers/groups back to the UI abs arrays after any modifications
		//
		m_absLayers = std::move(workingParams.layers);
		m_absGroups = std::move(workingParams.groups);
		//
		// Sync non-cell metadata (widths, heights, speeds, offsets, visibility) back to lib params.
		// Cell data stays compact in lib; only structural fields are mirrored.
		//
		params->viewWidth = wp->viewWidth;
		params->viewHeight = wp->viewHeight;
		params->tilesets = wp->tilesets;
		//
		// Sync layer structural fields (name, dimensions, speed, offset, visibility) back to lib params.
		// Resize first so the per-field loop always covers the full abs layer count.
		//
		params->layers.resize(m_absLayers.size());
		for (int li = 0; li < (int)m_absLayers.size(); li++) {
			params->layers[li].name = m_absLayers[li].name;
			params->layers[li].width = m_absLayers[li].width;
			params->layers[li].height = m_absLayers[li].height;
			params->layers[li].mapSpeed = m_absLayers[li].mapSpeed;
			params->layers[li].offsetX = m_absLayers[li].offsetX;
			params->layers[li].offsetY = m_absLayers[li].offsetY;
			params->layers[li].visible = m_absLayers[li].visible;
		}
		//
		// Sync group names/dimensions (not cell data) back to lib params
		//
		params->groups.resize(m_absGroups.size());
		for (int gi = 0; gi < (int)m_absGroups.size(); gi++) {
			params->groups[gi].name = m_absGroups[gi].name;
			params->groups[gi].width = m_absGroups[gi].width;
			params->groups[gi].height = m_absGroups[gi].height;
		}
       //
		// Keep lib map params in compact index space while editing
		//
		FlushAbsData(params);
	}
	//
	// Render the toolbar strip above the map canvas
	//
	void DocumentMap::RenderCanvasToolbar(RetrodevLib::MapParams* params) {
		ImGui::Checkbox(ICON_EYE " Show viewable area", &m_showViewableArea);
		ImGui::SameLine();
		ImGui::TextDisabled(ICON_INFORMATION_OUTLINE);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Overlay a box on the canvas showing the viewable area size (set in Dimensions panel)");
		ImGui::SameLine();
		ImGui::Checkbox(ICON_GRID " Show grid", &m_showGrid);
		ImGui::SameLine();
		ImGui::TextDisabled(ICON_INFORMATION_OUTLINE);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Show or hide the tile grid lines on the canvas");
		ImGui::SameLine();
		//
		// Target system combobox: "Agnostic" disables aspect correction; system entries enable it
		//
		std::vector<std::string> aspectSystems = RetrodevLib::Converters::GetAspectSystems();
		ImGui::AlignTextToFramePadding();
		ImGui::Text("System:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
		if (ImGui::BeginCombo("##TargetSystem", m_targetSystem.c_str())) {
			for (const auto& sysName : aspectSystems) {
				bool selected = (m_targetSystem == sysName);
				if (ImGui::Selectable(sysName.c_str(), selected)) {
					m_targetSystem = sysName;
					//
					// Reset mode to first available for this system (or empty for Agnostic)
					//
					std::vector<std::string> modes;
					RetrodevLib::Converters::GetAspectData(m_targetSystem, "", m_aspectHScale, m_aspectVScale, modes);
					m_targetMode = modes.empty() ? "" : modes[0];
					if (!m_targetMode.empty())
						RetrodevLib::Converters::GetAspectData(m_targetSystem, m_targetMode, m_aspectHScale, m_aspectVScale, modes);
				}
				if (selected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::TextDisabled(ICON_INFORMATION_OUTLINE);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Select a target system to preview the map with its pixel aspect ratio");
		//
		// Target mode combobox: only shown when a non-agnostic system is selected
		//
		if (m_targetSystem != RetrodevLib::Converters::Agnostic) {
			std::vector<std::string> modes;
			float hs, vs;
			RetrodevLib::Converters::GetAspectData(m_targetSystem, m_targetMode, hs, vs, modes);
			if (!modes.empty()) {
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Mode:");
				ImGui::SameLine();
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
				if (ImGui::BeginCombo("##TargetMode", m_targetMode.c_str())) {
					for (const auto& modeStr : modes) {
						bool selected = (m_targetMode == modeStr);
						if (ImGui::Selectable(modeStr.c_str(), selected)) {
							m_targetMode = modeStr;
							std::vector<std::string> unused;
							RetrodevLib::Converters::GetAspectData(m_targetSystem, m_targetMode, m_aspectHScale, m_aspectVScale, unused);
						}
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
						ImGui::EndCombo();
					}
					ImGui::SameLine();
					ImGui::TextDisabled(ICON_INFORMATION_OUTLINE);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("Select the screen mode to apply the correct pixel aspect ratio");
			}
		}
	}
	//
	// Render the bottom scrollbar toolbar: custom H/V scroll controls stepping by map speed
	//
	void DocumentMap::RenderCanvasScrollbars(RetrodevLib::MapParams* params) {
		int vw = params->viewWidth > 0 ? params->viewWidth : 16;
		int vh = params->viewHeight > 0 ? params->viewHeight : 12;
		//
		// Step counts driven by the widest/tallest layer and its speed.
		// Every other layer scrolls at scrollPos * its own speed, clamped to its own extent.
		//
		int maxStepsX = 0;
		int maxStepsY = 0;
		if (!params->layers.empty()) {
			const RetrodevLib::MapLayer* refX = &params->layers[0];
			const RetrodevLib::MapLayer* refY = &params->layers[0];
			for (const auto& layer : params->layers) {
				if (layer.width > refX->width)
					refX = &layer;
				if (layer.height > refY->height)
					refY = &layer;
			}
			float stepX = refX->mapSpeed > 0.0f ? refX->mapSpeed : 1.0f;
			float stepY = refY->mapSpeed > 0.0f ? refY->mapSpeed : 1.0f;
			maxStepsX = (int)std::floor(std::max(0.0f, ((float)refX->width - (float)vw) / stepX));
			maxStepsY = (int)std::floor(std::max(0.0f, ((float)refY->height - (float)vh) / stepY));
		}
		//
		// Clamp step counts to valid range every frame
		//
		m_viewScrollX = std::max(0, std::min(m_viewScrollX, maxStepsX));
		m_viewScrollY = std::max(0, std::min(m_viewScrollY, maxStepsY));
		//
		// Horizontal scroll row: [<-] [slider X] [->]
		//
		float arrowW = ImGui::GetFrameHeight();
		float labelW = ImGui::CalcTextSize("X").x + ImGui::GetStyle().ItemSpacing.x;
		float sliderW = std::max(1.0f, ImGui::GetContentRegionAvail().x - arrowW * 2.0f - labelW - ImGui::GetStyle().ItemSpacing.x * 3.0f);
		ImGui::AlignTextToFramePadding();
		ImGui::Text("X");
		ImGui::SameLine();
		ImGui::BeginDisabled(m_viewScrollX <= 0 || maxStepsX <= 0);
		if (ImGui::ArrowButton("##ScrLeft", ImGuiDir_Left))
			m_viewScrollX = std::max(0, m_viewScrollX - 1);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(sliderW);
		ImGui::BeginDisabled(maxStepsX <= 0);
		ImGui::SliderInt("##ScrX", &m_viewScrollX, 0, maxStepsX);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(m_viewScrollX >= maxStepsX || maxStepsX <= 0);
		if (ImGui::ArrowButton("##ScrRight", ImGuiDir_Right))
			m_viewScrollX = std::min(maxStepsX, m_viewScrollX + 1);
		ImGui::EndDisabled();
		//
		// Vertical scroll row: [^] [slider Y] [v]
		//
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Y");
		ImGui::SameLine();
		ImGui::BeginDisabled(m_viewScrollY <= 0 || maxStepsY <= 0);
		if (ImGui::ArrowButton("##ScrUp", ImGuiDir_Up))
			m_viewScrollY = std::max(0, m_viewScrollY - 1);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(sliderW);
		ImGui::BeginDisabled(maxStepsY <= 0);
		ImGui::SliderInt("##ScrY", &m_viewScrollY, 0, maxStepsY);
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(m_viewScrollY >= maxStepsY || maxStepsY <= 0);
		if (ImGui::ArrowButton("##ScrDown", ImGuiDir_Down))
			m_viewScrollY = std::min(maxStepsY, m_viewScrollY + 1);
		ImGui::EndDisabled();
	}
	//
	// Render the map canvas (no ImGui scroll -- layout is driven by scroll state)
	//
	void DocumentMap::RenderMapCanvas(RetrodevLib::MapParams* params) {
		if (params->layers.empty()) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Map has no layers.");
			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Add a layer in the Layers panel.");
			return;
		}
		//
		// Get the editing layer (the one receiving paint operations)
		//
		int editIdx = std::max(0, std::min(m_editingLayerIdx, (int)params->layers.size() - 1));
		RetrodevLib::MapLayer& editLayer = params->layers[editIdx];
		if (editLayer.width <= 0 || editLayer.height <= 0) {
			ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Layer has no dimensions.");
			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Set width and height in the Dimensions panel.");
			return;
		}
		//
		// Panel geometry in screen space
		//
		ImVec2 panelOrigin = ImGui::GetCursorScreenPos();
		ImVec2 panelSize = ImGui::GetContentRegionAvail();
		if (panelSize.x <= 0 || panelSize.y <= 0)
			return;
		//
		// Effective viewable area size (fall back to editing layer dimensions)
		//
		int vw = params->viewWidth > 0 ? params->viewWidth : editLayer.width;
		int vh = params->viewHeight > 0 ? params->viewHeight : editLayer.height;
		//
		// Draw cell sizes: apply pixel aspect ratio for rendering; hit-test always uses m_cellSize
		//
		float drawCellW = m_cellSize * m_aspectHScale;
		float drawCellH = m_cellSize * m_aspectVScale;
		//
		// Editing layer drawing origin: used for mouse->tile conversion and all paint operations.
		// Scroll is clamped per layer so the layer never renders past its own right or bottom edge.
		//
		float editStep = editLayer.mapSpeed > 0.0f ? editLayer.mapSpeed : 1.0f;
		float editScrollX = std::min((float)m_viewScrollX * editStep, (float)std::max(0, editLayer.width - vw));
		float editScrollY = std::min((float)m_viewScrollY * editStep, (float)std::max(0, editLayer.height - vh));
		float editMapOriginX = panelOrigin.x + (panelSize.x - vw * drawCellW) * 0.5f - editScrollX * drawCellW + editLayer.offsetX * drawCellW;
		float editMapOriginY = panelOrigin.y + (panelSize.y - vh * drawCellH) * 0.5f - editScrollY * drawCellH + editLayer.offsetY * drawCellH;
		//
		// Invisible button spanning the full panel: captures mouse input
		//
		ImGui::InvisibleButton("##MapCanvas", panelSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
		bool leftActivated = ImGui::IsItemActivated() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
		bool leftActive = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
		bool leftDeactivated = ImGui::IsItemDeactivated();
		bool rightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
		//
		// Convert mouse position to editing layer tile coordinates
		// Division uses drawCellW/H to match the visual origin built with the same sizes
		//
		ImVec2 mousePos = ImGui::GetMousePos();
		int rawCol = (int)std::floor((mousePos.x - editMapOriginX) / drawCellW);
		int rawRow = (int)std::floor((mousePos.y - editMapOriginY) / drawCellH);
		bool mouseOnCanvas = rawCol >= 0 && rawCol < editLayer.width && rawRow >= 0 && rawRow < editLayer.height;
		int clampedCol = std::max(0, std::min(rawCol, editLayer.width - 1));
		int clampedRow = std::max(0, std::min(rawRow, editLayer.height - 1));
		//
		// Arrow key scrolling when the canvas is focused or hovered
		//
		if (ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) {
			const RetrodevLib::MapLayer* refX = &params->layers[0];
			const RetrodevLib::MapLayer* refY = &params->layers[0];
			for (const auto& layer : params->layers) {
				if (layer.width > refX->width)
					refX = &layer;
				if (layer.height > refY->height)
					refY = &layer;
			}
			float stepX = refX->mapSpeed > 0.0f ? refX->mapSpeed : 1.0f;
			float stepY = refY->mapSpeed > 0.0f ? refY->mapSpeed : 1.0f;
			int maxStepsX = (int)std::floor(std::max(0.0f, ((float)refX->width - (float)vw) / stepX));
			int maxStepsY = (int)std::floor(std::max(0.0f, ((float)refY->height - (float)vh) / stepY));
			if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
				m_viewScrollX = std::max(0, m_viewScrollX - 1);
			if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
				m_viewScrollX = std::min(maxStepsX, m_viewScrollX + 1);
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
				m_viewScrollY = std::max(0, m_viewScrollY - 1);
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
				m_viewScrollY = std::min(maxStepsY, m_viewScrollY + 1);
		}
		//
		// Group capture mode: left drag defines a region to capture as a new group
		//
		if (m_groupCapturing) {
			if (rightClicked) {
				m_groupCapturing = false;
				m_groupCaptureDragging = false;
			} else if (leftActivated && mouseOnCanvas) {
				m_groupCaptureDragging = true;
				m_groupCaptureStartCol = rawCol;
				m_groupCaptureStartRow = rawRow;
				m_groupCaptureCurCol = rawCol;
				m_groupCaptureCurRow = rawRow;
			} else if (leftActive && m_groupCaptureDragging) {
				m_groupCaptureCurCol = clampedCol;
				m_groupCaptureCurRow = clampedRow;
			} else if (leftDeactivated && m_groupCaptureDragging) {
				//
				// Finalize: capture editing layer region into a new group
				//
				int minCol = std::min(m_groupCaptureStartCol, m_groupCaptureCurCol);
				int maxCol = std::max(m_groupCaptureStartCol, m_groupCaptureCurCol);
				int minRow = std::min(m_groupCaptureStartRow, m_groupCaptureCurRow);
				int maxRow = std::max(m_groupCaptureStartRow, m_groupCaptureCurRow);
				RetrodevLib::TileGroup newGroup;
				newGroup.name = "Group " + std::to_string(params->groups.size() + 1);
				newGroup.width = maxCol - minCol + 1;
				newGroup.height = maxRow - minRow + 1;
				newGroup.tiles.resize(newGroup.width * newGroup.height, 0);
				for (int r = minRow; r <= maxRow; r++) {
					for (int c = minCol; c <= maxCol; c++)
						newGroup.tiles[(r - minRow) * newGroup.width + (c - minCol)] = editLayer.data[r * editLayer.width + c];
				}
				params->groups.push_back(std::move(newGroup));
				m_selectedGroupIdx = (int)params->groups.size() - 1;
				m_selectedTileIdx = -1;
				m_groupCapturing = false;
				m_groupCaptureDragging = false;
				std::strncpy(m_groupRenameBuffer, params->groups[m_selectedGroupIdx].name.c_str(), sizeof(m_groupRenameBuffer) - 1);
				m_groupRenameBuffer[sizeof(m_groupRenameBuffer) - 1] = '\0';
				m_groupRenameIdx = m_selectedGroupIdx;
				SetModified(true);
				RetrodevLib::Project::MarkAsModified();
			}
		} else {
			//
			// Left click paints, right click erases -- all operations on the editing layer
			//
			if ((leftActive || rightClicked) && mouseOnCanvas) {
				if (rightClicked) {
					//
					// Erase the group footprint if a group is selected, otherwise erase single cell
					//
					if (m_selectedGroupIdx >= 0 && m_selectedGroupIdx < (int)params->groups.size()) {
						const RetrodevLib::TileGroup& group = params->groups[m_selectedGroupIdx];
						bool anyChanged = false;
						for (int gr = 0; gr < group.height; gr++) {
							for (int gc = 0; gc < group.width; gc++) {
								int mapCol = rawCol + gc;
								int mapRow = rawRow + gr;
								if (mapCol >= 0 && mapCol < editLayer.width && mapRow >= 0 && mapRow < editLayer.height) {
									int mapIdx = mapRow * editLayer.width + mapCol;
									if (editLayer.data[mapIdx] != 0) {
										editLayer.data[mapIdx] = 0;
										anyChanged = true;
									}
								}
							}
						}
						if (anyChanged) {
							SetModified(true);
							RetrodevLib::Project::MarkAsModified();
						}
					} else {
						int cellIdx = rawRow * editLayer.width + rawCol;
						if (editLayer.data[cellIdx] != 0) {
							editLayer.data[cellIdx] = 0;
							SetModified(true);
							RetrodevLib::Project::MarkAsModified();
						}
					}
				} else if (leftActive && m_selectedGroupIdx >= 0 && m_selectedGroupIdx < (int)params->groups.size()) {
					//
					// Stamp group at the current mouse cell on the editing layer
					//
					const RetrodevLib::TileGroup& group = params->groups[m_selectedGroupIdx];
					bool anyChanged = false;
					for (int gr = 0; gr < group.height; gr++) {
						for (int gc = 0; gc < group.width; gc++) {
							int mapCol = rawCol + gc;
							int mapRow = rawRow + gr;
							if (mapCol >= 0 && mapCol < editLayer.width && mapRow >= 0 && mapRow < editLayer.height) {
								int mapIdx = mapRow * editLayer.width + mapCol;
								uint16_t newVal = group.tiles[gr * group.width + gc];
								if (editLayer.data[mapIdx] != newVal) {
									editLayer.data[mapIdx] = newVal;
									anyChanged = true;
								}
							}
						}
					}
					if (anyChanged) {
						SetModified(true);
						RetrodevLib::Project::MarkAsModified();
					}
				} else if (leftActive && m_selectedTilesetIdx >= 0 && m_selectedTileIdx >= 0) {
					int cellIdx = rawRow * editLayer.width + rawCol;
					uint16_t newVal = EncodeMapTile(m_selectedTilesetIdx, m_selectedTileIdx);
					if (editLayer.data[cellIdx] != newVal) {
						editLayer.data[cellIdx] = newVal;
						SetModified(true);
						RetrodevLib::Project::MarkAsModified();
					}
				}
			}
		}
		//
		// Rendering: three passes -- background, tiles (all visible layers), grid (editing layer)
		//
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		//
		// Background pass: dark fill for every cell of the editing layer to show its extent
		//
		{
			int firstCol = std::max(0, (int)std::floor((panelOrigin.x - editMapOriginX) / drawCellW));
			int lastCol = std::min(editLayer.width, (int)std::ceil((panelOrigin.x + panelSize.x - editMapOriginX) / drawCellW));
			int firstRow = std::max(0, (int)std::floor((panelOrigin.y - editMapOriginY) / drawCellH));
			int lastRow = std::min(editLayer.height, (int)std::ceil((panelOrigin.y + panelSize.y - editMapOriginY) / drawCellH));
			for (int row = firstRow; row < lastRow; row++) {
				for (int col = firstCol; col < lastCol; col++) {
					ImVec2 cellMin = ImVec2(editMapOriginX + col * drawCellW, editMapOriginY + row * drawCellH);
					ImVec2 cellMax = ImVec2(cellMin.x + drawCellW, cellMin.y + drawCellH);
					drawList->AddRectFilled(cellMin, cellMax, IM_COL32(30, 30, 30, 255));
				}
			}
		}
		//
		// Tile pass: render all visible layers bottom to top, non-empty cells only
		//
		for (const auto& layer : params->layers) {
			if (!layer.visible)
				continue;
			float layerStep = layer.mapSpeed > 0.0f ? layer.mapSpeed : 1.0f;
			float layerScrollX = std::min((float)m_viewScrollX * layerStep, (float)std::max(0, layer.width - vw));
			float layerScrollY = std::min((float)m_viewScrollY * layerStep, (float)std::max(0, layer.height - vh));
			float layerOriginX = panelOrigin.x + (panelSize.x - vw * drawCellW) * 0.5f - layerScrollX * drawCellW + layer.offsetX * drawCellW;
			float layerOriginY = panelOrigin.y + (panelSize.y - vh * drawCellH) * 0.5f - layerScrollY * drawCellH + layer.offsetY * drawCellH;
			int firstCol = std::max(0, (int)std::floor((panelOrigin.x - layerOriginX) / drawCellW));
			int lastCol = std::min(layer.width, (int)std::ceil((panelOrigin.x + panelSize.x - layerOriginX) / drawCellW));
			int firstRow = std::max(0, (int)std::floor((panelOrigin.y - layerOriginY) / drawCellH));
			int lastRow = std::min(layer.height, (int)std::ceil((panelOrigin.y + panelSize.y - layerOriginY) / drawCellH));
			for (int row = firstRow; row < lastRow; row++) {
				for (int col = firstCol; col < lastCol; col++) {
					uint16_t cellVal = layer.data[row * layer.width + col];
					if (cellVal == 0)
						continue;
					ImVec2 cellMin = ImVec2(layerOriginX + col * drawCellW, layerOriginY + row * drawCellH);
					ImVec2 cellMax = ImVec2(cellMin.x + drawCellW, cellMin.y + drawCellH);
					SDL_Texture* tex = GetCellTexture(cellVal);
					if (tex)
						drawList->AddImage((ImTextureID)(intptr_t)tex, cellMin, cellMax);
					else
						drawList->AddRectFilled(cellMin, cellMax, IM_COL32(80, 40, 40, 255));
				}
			}
		}
		//
		// Grid pass: cell borders drawn only for the editing layer
		//
		if (m_showGrid) {
			int firstCol = std::max(0, (int)std::floor((panelOrigin.x - editMapOriginX) / drawCellW));
			int lastCol = std::min(editLayer.width, (int)std::ceil((panelOrigin.x + panelSize.x - editMapOriginX) / drawCellW));
			int firstRow = std::max(0, (int)std::floor((panelOrigin.y - editMapOriginY) / drawCellH));
			int lastRow = std::min(editLayer.height, (int)std::ceil((panelOrigin.y + panelSize.y - editMapOriginY) / drawCellH));
			for (int row = firstRow; row < lastRow; row++) {
				for (int col = firstCol; col < lastCol; col++) {
					ImVec2 cellMin = ImVec2(editMapOriginX + col * drawCellW, editMapOriginY + row * drawCellH);
					ImVec2 cellMax = ImVec2(cellMin.x + drawCellW, cellMin.y + drawCellH);
					drawList->AddRect(cellMin, cellMax, IM_COL32(70, 70, 70, 255));
				}
			}
		}
		//
		// Viewable area overlay: when enabled, shade the off-screen area with 4 surrounding rects
		//
		if (m_showViewableArea) {
			float viewAreaX = panelOrigin.x + (panelSize.x - vw * drawCellW) * 0.5f;
			float viewAreaY = panelOrigin.y + (panelSize.y - vh * drawCellH) * 0.5f;
			ImVec2 viewMin = ImVec2(viewAreaX, viewAreaY);
			ImVec2 viewMax = ImVec2(viewAreaX + vw * drawCellW, viewAreaY + vh * drawCellH);
			ImVec2 panelMax = ImVec2(panelOrigin.x + panelSize.x, panelOrigin.y + panelSize.y);
			ImU32 overlayColor = IM_COL32(0, 50, 120, 170);
			//
			// Top strip
			//
			drawList->AddRectFilled(panelOrigin, ImVec2(panelMax.x, viewMin.y), overlayColor);
			//
			// Bottom strip
			//
			drawList->AddRectFilled(ImVec2(panelOrigin.x, viewMax.y), panelMax, overlayColor);
			//
			// Left strip (between top and bottom strips)
			//
			drawList->AddRectFilled(ImVec2(panelOrigin.x, viewMin.y), ImVec2(viewMin.x, viewMax.y), overlayColor);
			//
			// Right strip
			//
			drawList->AddRectFilled(ImVec2(viewMax.x, viewMin.y), ImVec2(panelMax.x, viewMax.y), overlayColor);
			//
			// Border of the viewable area
			//
			drawList->AddRect(viewMin, viewMax, IM_COL32(0, 200, 255, 220), 0.0f, 0, 2.0f);
		}
		//
		// Cursor preview and overlays use the editing layer origin
		//
		ImVec2 editMapOrigin = ImVec2(editMapOriginX, editMapOriginY);
		if (ImGui::IsItemHovered() && !m_groupCapturing && mouseOnCanvas) {
			bool groupActive = m_selectedGroupIdx >= 0 && m_selectedGroupIdx < (int)params->groups.size();
			if (groupActive)
				RenderGroupPreview(drawList, editMapOrigin, params->groups[m_selectedGroupIdx], rawCol, rawRow);
			else if (m_selectedTilesetIdx >= 0 && m_selectedTileIdx >= 0) {
				SDL_Texture* tex = GetCellTexture(EncodeMapTile(m_selectedTilesetIdx, m_selectedTileIdx));
				ImVec2 cellMin = ImVec2(editMapOriginX + rawCol * drawCellW, editMapOriginY + rawRow * drawCellH);
				ImVec2 cellMax = ImVec2(cellMin.x + drawCellW, cellMin.y + drawCellH);
				if (tex)
					drawList->AddImage((ImTextureID)(intptr_t)tex, cellMin, cellMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 160));
				else
					drawList->AddRectFilled(cellMin, cellMax, IM_COL32(100, 100, 200, 100));
				//
				// Single-cell hover highlight
				//
				ImVec2 cellMin2 = ImVec2(editMapOriginX + rawCol * drawCellW, editMapOriginY + rawRow * drawCellH);
				ImVec2 cellMax2 = ImVec2(cellMin2.x + drawCellW, cellMin2.y + drawCellH);
				drawList->AddRect(cellMin2, cellMax2, IM_COL32(255, 220, 0, 220), 0.0f, 0, 2.0f);
			}
		}
		//
		// Capture selection rectangle overlay while dragging
		//
		if (m_groupCapturing && m_groupCaptureDragging) {
			int minCol = std::min(m_groupCaptureStartCol, m_groupCaptureCurCol);
			int maxCol = std::max(m_groupCaptureStartCol, m_groupCaptureCurCol);
			int minRow = std::min(m_groupCaptureStartRow, m_groupCaptureCurRow);
			int maxRow = std::max(m_groupCaptureStartRow, m_groupCaptureCurRow);
			ImVec2 selMin = ImVec2(editMapOriginX + minCol * drawCellW, editMapOriginY + minRow * drawCellH);
			ImVec2 selMax = ImVec2(editMapOriginX + (maxCol + 1) * drawCellW, editMapOriginY + (maxRow + 1) * drawCellH);
			drawList->AddRectFilled(selMin, selMax, IM_COL32(100, 200, 255, 50));
			drawList->AddRect(selMin, selMax, IM_COL32(100, 200, 255, 220), 0.0f, 0, 2.0f);
		}
		//
		// Drag hint tooltip when capture mode is waiting for input
		//
		if (m_groupCapturing && ImGui::IsItemHovered())
			ImGui::SetTooltip("Click and drag to select the region for the group");
	}
	//
	// Render the right tooling panel
	//
	void DocumentMap::RenderToolingPanel(RetrodevLib::MapParams* params) {
		//
		// Map name: editable field; pressing Enter renames the build item
		//
		if (m_name != std::string(m_nameBuffer)) {
			std::strncpy(m_nameBuffer, m_name.c_str(), sizeof(m_nameBuffer) - 1);
			m_nameBuffer[sizeof(m_nameBuffer) - 1] = '\0';
		}
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Name:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::InputText("##MapName", m_nameBuffer, sizeof(m_nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
			std::string newName(m_nameBuffer);
			if (!newName.empty() && newName != m_name) {
				if (RetrodevLib::Project::MapRename(m_name, newName)) {
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Renamed map '%s' to '%s'", m_name.c_str(), newName.c_str());
					m_name = newName;
					ProjectView::NotifyProjectChanged();
					RetrodevLib::Project::MarkAsModified();
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Error, "Failed to rename map '%s' to '%s' (name may already exist)", m_name.c_str(), newName.c_str());
					std::strncpy(m_nameBuffer, m_name.c_str(), sizeof(m_nameBuffer) - 1);
					m_nameBuffer[sizeof(m_nameBuffer) - 1] = '\0';
				}
			}
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Press Enter to rename the map");
		ImGui::Separator();
		//
		// --- Export section ---
		//
        RetrodevLib::MapParams compactExportParams = *params;
		TranslateMapParamsCells(&compactExportParams, false);
		m_exportWidget.RenderMap(m_name, &compactExportParams);
		ImGui::Separator();
		//
		// --- Layers section ---
		//
		if (ImGui::CollapsingHeader("Layers")) {
			RenderLayersSection(params);
		}
		//
		// --- Dimensions section (operates on the editing layer) ---
		//
		bool dimensionsOpen = ImGui::CollapsingHeader("Dimensions");
		ImGui::SameLine();
		ImGui::TextDisabled(ICON_INFORMATION_OUTLINE);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Resize the editing layer. Enter the new dimensions and click Apply.\nRow and Col buttons add or remove single rows and columns at any edge.\nExisting tile data is preserved within the overlapping region.");
		if (dimensionsOpen) {
			if (params->layers.empty()) {
				ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Add a layer first.");
			} else {
				int li = std::max(0, std::min(m_editingLayerIdx, (int)params->layers.size() - 1));
				RetrodevLib::MapLayer& editLayer = params->layers[li];
				//
				// Re-sync pending dimensions when the editing layer changes
				//
				if (m_lastEditingLayerIdx != li) {
					m_pendingWidth = editLayer.width;
					m_pendingHeight = editLayer.height;
					m_lastEditingLayerIdx = li;
				}
				ImGui::Text("Width:");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputInt("##MapWidth", &m_pendingWidth)) {
					if (m_pendingWidth < 1)
						m_pendingWidth = 1;
					if (m_pendingWidth > 1024)
						m_pendingWidth = 1024;
				}
				ImGui::Text("Height:");
				ImGui::SetNextItemWidth(-1.0f);
				if (ImGui::InputInt("##MapHeight", &m_pendingHeight)) {
					if (m_pendingHeight < 1)
						m_pendingHeight = 1;
					if (m_pendingHeight > 1024)
						m_pendingHeight = 1024;
				}
				if (ImGui::Button(ICON_CHECK " Apply##MapDims", ImVec2(-1.0f, 0.0f))) {
					if (m_pendingWidth != editLayer.width || m_pendingHeight != editLayer.height) {
						ResizeMapData(&editLayer, m_pendingWidth, m_pendingHeight);
						SetModified(true);
						RetrodevLib::Project::MarkAsModified();
					}
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Resize the editing layer to the pending Width and Height.\nExisting tile data is preserved within the overlapping area.");
				ImGui::Separator();
				//
				// Row: [+Top] [+Bot] [-Top] [-Bot] all on one compact line
				//
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Row");
				ImGui::SameLine();
				float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f;
				if (ImGui::Button(ICON_TABLE_ROW_PLUS_BEFORE "##RowAddTop", ImVec2(btnW, 0.0f))) {
					AddRowTop(&editLayer);
					m_pendingHeight = editLayer.height;
					SetModified(true);
					RetrodevLib::Project::MarkAsModified();
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Add row at top");
				ImGui::SameLine();
				if (ImGui::Button(ICON_TABLE_ROW_PLUS_AFTER "##RowAddBot", ImVec2(btnW, 0.0f))) {
					AddRowBottom(&editLayer);
					m_pendingHeight = editLayer.height;
					SetModified(true);
					RetrodevLib::Project::MarkAsModified();
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Add row at bottom");
				ImGui::BeginDisabled(editLayer.height <= 1);
				ImGui::SameLine();
				if (ImGui::Button(ICON_TABLE_ROW_REMOVE "##RowRemTop", ImVec2(btnW, 0.0f))) {
					RemoveRowTop(&editLayer);
					m_pendingHeight = editLayer.height;
					SetModified(true);
					RetrodevLib::Project::MarkAsModified();
				}
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("Remove top row");
				ImGui::SameLine();
				if (ImGui::Button(ICON_TABLE_ROW_REMOVE "##RowRemBot", ImVec2(btnW, 0.0f))) {
					RemoveRowBottom(&editLayer);
					m_pendingHeight = editLayer.height;
					SetModified(true);
					RetrodevLib::Project::MarkAsModified();
				}
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("Remove bottom row");
				ImGui::EndDisabled();
				//
				// Col: [+Lft] [+Rgt] [-Lft] [-Rgt] all on one compact line
				//
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Col");
				ImGui::SameLine();
				if (ImGui::Button(ICON_TABLE_COLUMN_PLUS_BEFORE "##ColAddLft", ImVec2(btnW, 0.0f))) {
					AddColumnLeft(&editLayer);
					m_pendingWidth = editLayer.width;
					SetModified(true);
					RetrodevLib::Project::MarkAsModified();
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Add column at left");
				ImGui::SameLine();
				if (ImGui::Button(ICON_TABLE_COLUMN_PLUS_AFTER "##ColAddRgt", ImVec2(btnW, 0.0f))) {
					AddColumnRight(&editLayer);
					m_pendingWidth = editLayer.width;
					SetModified(true);
					RetrodevLib::Project::MarkAsModified();
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Add column at right");
				ImGui::BeginDisabled(editLayer.width <= 1);
				ImGui::SameLine();
				if (ImGui::Button(ICON_TABLE_COLUMN_REMOVE "##ColRemLft", ImVec2(btnW, 0.0f))) {
					RemoveColumnLeft(&editLayer);
					m_pendingWidth = editLayer.width;
					SetModified(true);
					RetrodevLib::Project::MarkAsModified();
				}
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("Remove left column");
				ImGui::SameLine();
				if (ImGui::Button(ICON_TABLE_COLUMN_REMOVE "##ColRemRgt", ImVec2(btnW, 0.0f))) {
					RemoveColumnRight(&editLayer);
					m_pendingWidth = editLayer.width;
					SetModified(true);
					RetrodevLib::Project::MarkAsModified();
				}
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					ImGui::SetTooltip("Remove right column");
				ImGui::EndDisabled();
			}
		}
		//
		// --- Viewport section ---
		//
		bool viewportOpen = ImGui::CollapsingHeader("Viewport");
		ImGui::SameLine();
		ImGui::TextDisabled(ICON_INFORMATION_OUTLINE);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("The visible tile area (width x height in tiles).\nUsed for the viewable-area canvas overlay and scroll range calculations.\nDoes not affect map data or tile layers.");
		if (viewportOpen) {
			//
			// Viewable area size: number of tiles visible on screen at once (shared across all layers)
			//
			ImGui::AlignTextToFramePadding();
			ImGui::Text("View Width:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
			if (ImGui::InputInt("##ViewWidth", &params->viewWidth)) {
				if (params->viewWidth < 1)
					params->viewWidth = 1;
				if (params->viewWidth > 1024)
					params->viewWidth = 1024;
				SetModified(true);
				RetrodevLib::Project::MarkAsModified();
			}
			ImGui::SameLine();
			ImGui::TextDisabled(ICON_INFORMATION_OUTLINE);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Width of the visible area in tiles. Used for the viewport overlay\nand to compute the horizontal scroll range.");
			ImGui::AlignTextToFramePadding();
			ImGui::Text("View Height:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
			if (ImGui::InputInt("##ViewHeight", &params->viewHeight)) {
				if (params->viewHeight < 1)
					params->viewHeight = 1;
				if (params->viewHeight > 1024)
					params->viewHeight = 1024;
				SetModified(true);
				RetrodevLib::Project::MarkAsModified();
			}
			ImGui::SameLine();
			ImGui::TextDisabled(ICON_INFORMATION_OUTLINE);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Height of the visible area in tiles. Used for the viewport overlay\nand to compute the vertical scroll range.");
		}
		//
		// --- Tilesets section ---
		//
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		bool tilesetsOpen = ImGui::CollapsingHeader("Tilesets");
		ImGui::SameLine();
		ImGui::TextDisabled(ICON_INFORMATION_OUTLINE);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Tile palettes used by this map. Each slot holds one or more tileset variants\nthat can be swapped without changing tile indices in the map.\nThe active variant is used on the canvas and in export scripts.");
		if (tilesetsOpen) {
			//
			// Build the list of project tilesets once; used by both the Add popup and the slot listbox
			//
			std::vector<std::string> projectTilesets = RetrodevLib::Project::GetBuildItemsByType(RetrodevLib::ProjectBuildType::Tilemap);
			//
			// Add Tileset button: creates a new slot with one initial variant
			//
			if (ImGui::Button(ICON_LAYERS_PLUS " Add Tileset...", ImVec2(-1.0f, 0.0f)))
				ImGui::OpenPopup("##AddTilesetPopup");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Add a tileset build item as a new slot in this map.");
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
			if (ImGui::BeginPopup("##AddTilesetPopup")) {
				if (projectTilesets.empty()) {
					ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No tilesets defined in the project.");
					ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Add a Tilemap build item first.");
				} else {
					bool anyAvailable = false;
					for (const auto& tsName : projectTilesets) {
						//
						// Skip tilesets already present in any slot's variant list
						//
						bool alreadyAdded = false;
						for (const auto& slot : params->tilesets) {
							for (const auto& v : slot.variants) {
								if (v == tsName) {
									alreadyAdded = true;
									break;
								}
							}
							if (alreadyAdded)
								break;
						}
						if (alreadyAdded)
							continue;
						anyAvailable = true;
						if (ImGui::Selectable(tsName.c_str())) {
							RetrodevLib::TilesetSlot newSlot;
							newSlot.variants.push_back(tsName);
							newSlot.activeVariant = 0;
							params->tilesets.push_back(std::move(newSlot));
							SyncLoadedTilesets(params->tilesets);
							SetModified(true);
							RetrodevLib::Project::MarkAsModified();
							ImGui::CloseCurrentPopup();
						}
					}
					if (!anyAvailable)
						ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "All project tilesets are already in this map.");
				}
				ImGui::EndPopup();
			}
			ImGui::PopStyleVar();
			//
			// Slot listbox: each entry shows active variant name and variant count
			//
			int removeSlotIdx = -1;
			ImVec2 listboxSize = ImVec2(-1.0f, ImGui::GetFontSize() * 6.0f);
			if (ImGui::BeginListBox("##TilesetList", listboxSize)) {
				for (int i = 0; i < (int)params->tilesets.size(); i++) {
					const auto& slot = params->tilesets[i];
					bool selected = (m_selectedTilesetIdx == i);
					//
					// Build label: active variant name with variant count when there is more than one
					//
					std::string label;
					bool isMissing = false;
					if (slot.variants.empty()) {
						label = "(empty slot)";
						isMissing = true;
					} else {
						int vi = std::max(0, std::min(slot.activeVariant, (int)slot.variants.size() - 1));
						const std::string& activeName = slot.variants[vi];
						//
						// Mark as missing when the active variant is not found in the project's build items
						//
						isMissing = std::find(projectTilesets.begin(), projectTilesets.end(), activeName) == projectTilesets.end();
						label = activeName;
						if (isMissing)
							label += "  [missing]";
						if ((int)slot.variants.size() > 1)
							label += "  (+" + std::to_string((int)slot.variants.size() - 1) + " variants)";
					}
					if (isMissing)
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.5f, 0.2f, 1.0f));
					if (ImGui::Selectable(label.c_str(), selected)) {
						m_selectedTilesetIdx = i;
						m_selectedTileIdx = -1;
					}
					if (isMissing)
						ImGui::PopStyleColor();
				}
				ImGui::EndListBox();
			}
			//
			// Variant controls for the selected slot
			//
			bool slotSelected = m_selectedTilesetIdx >= 0 && m_selectedTilesetIdx < (int)params->tilesets.size();
			if (slotSelected) {
				auto& slot = params->tilesets[m_selectedTilesetIdx];
				int vi = std::max(0, std::min(slot.activeVariant, (int)slot.variants.size() - 1));
				//
				// Variant switcher row: [<] [combo] [>] -- only shown when slot has more than one variant
				//
				if ((int)slot.variants.size() > 1) {
					float arrowW = ImGui::GetFrameHeight();
					float comboW = ImGui::GetContentRegionAvail().x - arrowW * 2.0f - ImGui::GetStyle().ItemSpacing.x * 2.0f;
					ImGui::BeginDisabled(vi == 0);
					if (ImGui::Button(ICON_CHEVRON_LEFT "##PrevVar", ImVec2(arrowW, 0.0f))) {
						slot.activeVariant = vi - 1;
						SyncLoadedTilesets(params->tilesets);
						SetModified(true);
						RetrodevLib::Project::MarkAsModified();
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					ImGui::SetNextItemWidth(comboW);
					if (ImGui::BeginCombo("##VariantCombo", slot.variants[vi].c_str())) {
						for (int v = 0; v < (int)slot.variants.size(); v++) {
							bool isActive = (v == vi);
							if (ImGui::Selectable(slot.variants[v].c_str(), isActive)) {
								slot.activeVariant = v;
								SyncLoadedTilesets(params->tilesets);
								SetModified(true);
								RetrodevLib::Project::MarkAsModified();
							}
							if (isActive)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					ImGui::SameLine();
					ImGui::BeginDisabled(vi >= (int)slot.variants.size() - 1);
					if (ImGui::Button(ICON_CHEVRON_RIGHT "##NextVar", ImVec2(arrowW, 0.0f))) {
						slot.activeVariant = vi + 1;
						SyncLoadedTilesets(params->tilesets);
						SetModified(true);
						RetrodevLib::Project::MarkAsModified();
					}
					ImGui::EndDisabled();
				}
				//
				// Add Variant: pick another project tileset to add to this slot
				//
				if (ImGui::Button(ICON_PLUS " Add Variant##AddVar", ImVec2(-1.0f, 0.0f)))
					ImGui::OpenPopup("##AddVariantPopup");
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
				if (ImGui::BeginPopup("##AddVariantPopup")) {
					std::vector<std::string> allTilesets = RetrodevLib::Project::GetBuildItemsByType(RetrodevLib::ProjectBuildType::Tilemap);
					if (allTilesets.empty()) {
						ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "No tilesets in project.");
					} else {
						bool anyAvailable = false;
						for (const auto& tsName : allTilesets) {
							//
							// Skip names already present in this slot
							//
							bool alreadyInSlot = false;
							for (const auto& v : slot.variants) {
								if (v == tsName) {
									alreadyInSlot = true;
									break;
								}
							}
							if (alreadyInSlot)
								continue;
							anyAvailable = true;
							if (ImGui::Selectable(tsName.c_str())) {
								slot.variants.push_back(tsName);
								slot.activeVariant = (int)slot.variants.size() - 1;
								SyncLoadedTilesets(params->tilesets);
								SetModified(true);
								RetrodevLib::Project::MarkAsModified();
								ImGui::CloseCurrentPopup();
							}
						}
						if (!anyAvailable)
							ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "All tilesets already in this slot.");
					}
					ImGui::EndPopup();
				}
				ImGui::PopStyleVar();
				//
				// Remove Variant: only enabled when the slot has more than one variant
				//
				ImGui::BeginDisabled((int)slot.variants.size() <= 1);
				if (ImGui::Button(ICON_MINUS " Remove Variant##RemVar", ImVec2(-1.0f, 0.0f))) {
					slot.variants.erase(slot.variants.begin() + vi);
					slot.activeVariant = std::min(vi, (int)slot.variants.size() - 1);
					SyncLoadedTilesets(params->tilesets);
					SetModified(true);
					RetrodevLib::Project::MarkAsModified();
				}
				ImGui::EndDisabled();
			}
			//
			// Remove Slot button: removes the entire slot from the map
			// Always accessible when a slot is selected, including stale (missing) slots
			//
			if (slotSelected) {
				const auto& selSlot = params->tilesets[m_selectedTilesetIdx];
				int selVi = std::max(0, std::min(selSlot.activeVariant, (int)selSlot.variants.size() - 1));
				bool selIsMissing = selSlot.variants.empty() ||
					std::find(projectTilesets.begin(), projectTilesets.end(), selSlot.variants[selVi]) == projectTilesets.end();
				if (selIsMissing)
					ImGui::TextColored(ImVec4(0.85f, 0.5f, 0.2f, 1.0f), ICON_ALERT_OUTLINE " This tileset no longer exists in the project.");
			}
			ImGui::BeginDisabled(!slotSelected);
			if (ImGui::Button(ICON_DELETE " Remove##TilesetRemove", ImVec2(-1.0f, 0.0f)))
				removeSlotIdx = m_selectedTilesetIdx;
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
				ImGui::SetTooltip("Remove the selected tileset slot and all its variants from the map.");
			ImGui::EndDisabled();
			if (removeSlotIdx >= 0) {
				params->tilesets.erase(params->tilesets.begin() + removeSlotIdx);
				SyncLoadedTilesets(params->tilesets);
				if (m_selectedTilesetIdx >= (int)params->tilesets.size())
					m_selectedTilesetIdx = (int)params->tilesets.size() - 1;
				m_selectedTileIdx = -1;
				SetModified(true);
				RetrodevLib::Project::MarkAsModified();
			}
		}
		//
		// --- Groups section ---
		//
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		bool groupsOpen = ImGui::CollapsingHeader("Groups");
		ImGui::SameLine();
		ImGui::TextDisabled(ICON_INFORMATION_OUTLINE);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Named multi-tile stamps captured from the editing layer.\nClick Add Group, drag a rectangle on the canvas to define the region,\nthen select the group and left-click on the canvas to stamp it.");
		if (groupsOpen) {
			RenderGroupsSection(params);
		}
		//
		// --- Tiles section ---
		//
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::CollapsingHeader("Tiles")) {
			if (m_selectedTilesetIdx < 0 || m_selectedTilesetIdx >= (int)m_loadedTilesets.size()) {
				ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Select a tileset above.");
			} else {
				LoadedTileset& ts = m_loadedTilesets[m_selectedTilesetIdx];
				if (!ts.loaded) {
					ImGui::Text("Loading tileset...");
				} else if (!ts.extractor || ts.extractor->GetTileCount() == 0) {
					ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Tileset has no extracted tiles.");
				} else {
					//
						// Display tile grid: wrap tiles across the available panel width.
						// Iterates absolute grid indices (GetTileAllCount/GetTileAll) so that
						// m_selectedTileIdx always holds a stable absolute index.
						// Deleted tile slots are rendered as disabled/unselectable placeholders.
						// Contained in its own child window so it scrolls independently
						//
						if (ImGui::BeginChild("##TilePalette", ImVec2(-1.0f, std::max(ImGui::GetFontSize() * 10.0f, ImGui::GetContentRegionAvail().y)), true)) {
							int tileCount = ts.extractor->GetTileAllCount();
							const float displaySize = 32.0f;
							const float tilePad = 4.0f;
							//
							// ImageButton adds FramePadding on each side; use the correct axis for each
							// dimension to avoid over-packing columns or under-counting visible rows.
							//
							float framePadX = ImGui::GetStyle().FramePadding.x;
							float framePadY = ImGui::GetStyle().FramePadding.y;
							float buttonWidth = displaySize + 2.0f * framePadX;
							float availWidth = ImGui::GetContentRegionAvail().x;
							int tilesPerRow = std::max(1, (int)((availWidth + tilePad) / (buttonWidth + tilePad)));
							int numRows = (tileCount + tilesPerRow - 1) / tilesPerRow;
							float itemHeight = displaySize + 2.0f * framePadY + ImGui::GetStyle().ItemSpacing.y;
							//
							// Render visible rows only using list clipper
							//
							ImGuiListClipper clipper;
							clipper.Begin(numRows, itemHeight);
							while (clipper.Step()) {
								for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
									for (int col = 0; col < tilesPerRow; col++) {
										int absIdx = row * tilesPerRow + col;
										if (absIdx >= tileCount)
											break;
										bool isDeleted = std::binary_search(ts.deletedTiles.begin(), ts.deletedTiles.end(), absIdx);
										if (col > 0)
											ImGui::SameLine(0.0f, tilePad);
										//
										// Deleted slots: shown as disabled placeholders, not selectable.
										// Size matches ImageButton total size: displaySize + 2 * FramePadding on each axis.
										//
										if (isDeleted) {
											ImGui::BeginDisabled(true);
											ImGui::PushID(absIdx);
											ImGui::Button("##Del", ImVec2(displaySize + 2.0f * framePadX, displaySize + 2.0f * framePadY));
											ImGui::PopID();
											ImGui::EndDisabled();
											continue;
										}
										//
										// Highlight the currently selected tile with a different button colour.
										// m_selectedTileIdx is an absolute index, matching layer.data encoding.
										//
										bool selected = (m_selectedTileIdx == absIdx);
										if (selected)
											ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
										ImGui::PushID(absIdx);
										auto tileImage = ts.extractor->GetTileAll(absIdx);
										SDL_Texture* tex = tileImage ? tileImage->GetTexture(Application::GetRenderer()) : nullptr;
										if (tex) {
											if (ImGui::ImageButton("##Tile", (ImTextureID)(intptr_t)tex, ImVec2(displaySize, displaySize))) {
												m_selectedTileIdx = absIdx;
												m_selectedGroupIdx = -1;
											}
										} else {
											if (ImGui::Button("?##Tile", ImVec2(displaySize, displaySize))) {
												m_selectedTileIdx = absIdx;
												m_selectedGroupIdx = -1;
											}
										}
										ImGui::PopID();
										if (selected)
											ImGui::PopStyleColor();
									}
								}
							}
							clipper.End();
						}
						ImGui::EndChild();
				}
			}
		}
	}
	//
	// Synchronise m_loadedTilesets with the given slot list,
	// reusing already-loaded entries and reloading when the active variant changes.
	// After loading, checks all variants in each slot for DeletedTiles discrepancies:
	// any index deleted in one variant is pushed into all others and a warning is logged.
	//
	void DocumentMap::SyncLoadedTilesets(const std::vector<RetrodevLib::TilesetSlot>& slots) {
		std::vector<LoadedTileset> newList;
		newList.reserve(slots.size());
		for (const auto& slot : slots) {
			//
			// Determine the active variant name for this slot (clamped to valid range)
			//
			std::string activeName;
			if (!slot.variants.empty()) {
				int vi = std::max(0, std::min(slot.activeVariant, (int)slot.variants.size() - 1));
				activeName = slot.variants[vi];
			}
			//
			// Reuse an existing entry if the same variant is already loaded
			//
			bool found = false;
			for (auto& existing : m_loadedTilesets) {
				if (existing.loadedVariantName == activeName && !activeName.empty()) {
					newList.push_back(std::move(existing));
					found = true;
					break;
				}
			}
			if (!found) {
				//
				// New or changed variant: create and load it
				//
				LoadedTileset ts;
				ts.loadedVariantName = activeName;
				ts.loaded = false;
				if (!activeName.empty())
					LoadTileset(ts);
				newList.push_back(std::move(ts));
			}
		}
		m_loadedTilesets = std::move(newList);
		//
		// Check every slot that has more than one variant for DeletedTiles discrepancies.
		// Build the union of all deleted indices across the slot's variants, then push
		// any missing index into variants that don't already have it.
		//
		for (int si = 0; si < (int)slots.size(); si++) {
			const RetrodevLib::TilesetSlot& slot = slots[si];
			if (slot.variants.size() <= 1)
				continue;
			//
			// Collect every variant's TileExtractionParams pointer and build the union
			//
			std::vector<RetrodevLib::TileExtractionParams*> allParams;
			allParams.reserve(slot.variants.size());
			for (const auto& variantName : slot.variants) {
				RetrodevLib::TileExtractionParams* tp = nullptr;
				RetrodevLib::Project::TilesetGetTileParams(variantName, &tp);
				allParams.push_back(tp);
			}
			std::vector<int> unionDeleted;
			for (RetrodevLib::TileExtractionParams* tp : allParams) {
				if (!tp)
					continue;
				for (int idx : tp->DeletedTiles) {
					if (!std::binary_search(unionDeleted.begin(), unionDeleted.end(), idx)) {
						unionDeleted.push_back(idx);
						std::sort(unionDeleted.begin(), unionDeleted.end());
					}
				}
			}
			if (unionDeleted.empty())
				continue;
			//
			// For each variant, push any union index it is missing and warn
			//
			for (int vi = 0; vi < (int)slot.variants.size(); vi++) {
				RetrodevLib::TileExtractionParams* tp = allParams[vi];
				if (!tp)
					continue;
				for (int idx : unionDeleted) {
					if (!std::binary_search(tp->DeletedTiles.begin(), tp->DeletedTiles.end(), idx)) {
						AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Map '%s' slot %d: tile %d is deleted in another variant but not in '%s' -- adding to keep index space in sync.",
							m_name.c_str(), si, idx, slot.variants[vi].c_str());
						tp->DeletedTiles.push_back(idx);
						std::sort(tp->DeletedTiles.begin(), tp->DeletedTiles.end());
					}
				}
			}
			//
			// Reload any loaded tileset entry whose params were just modified
			//
			for (int vi = 0; vi < (int)slot.variants.size(); vi++) {
				if (allParams[vi] == nullptr)
					continue;
				if (si < (int)m_loadedTilesets.size() && m_loadedTilesets[si].loadedVariantName == slot.variants[vi])
					LoadTileset(m_loadedTilesets[si]);
			}
		}
	}
	//
	// Load a single tileset entry: run conversion then tile extraction
	//
	void DocumentMap::LoadTileset(LoadedTileset& ts) {
		ts.loaded = false;
		ts.deletedTiles.clear();
		//
		// Resolve the source file path for this variant
		//
		std::string sourceFilePath = RetrodevLib::Project::TilesetGetSourcePath(ts.loadedVariantName);
		if (sourceFilePath.empty())
			return;
		ts.sourceImage = RetrodevLib::Image::ImageLoad(sourceFilePath);
		if (!ts.sourceImage)
			return;
		//
		// Retrieve conversion parameters
		//
		RetrodevLib::GFXParams* gfxParams = nullptr;
		if (!RetrodevLib::Project::TilesetGetCfg(ts.loadedVariantName, &gfxParams) || gfxParams == nullptr)
			return;
		ts.converter = RetrodevLib::Converters::GetBitmapConverter(gfxParams);
		if (!ts.converter)
			return;
		ts.converter->SetOriginal(ts.sourceImage);
		ts.converter->Convert(gfxParams);
		//
		// Retrieve tile extraction parameters and run extraction
		//
		RetrodevLib::TileExtractionParams* tileParams = nullptr;
		if (!RetrodevLib::Project::TilesetGetTileParams(ts.loadedVariantName, &tileParams) || tileParams == nullptr)
			return;
		ts.extractor = ts.converter->GetTileExtractor();
		if (!ts.extractor)
			return;
		auto convertedImage = ts.converter->GetConverted(gfxParams);
		if (!convertedImage)
			return;
		//
		// Extract compact (no deleted) tiles for count validation and
		// extract all tiles (including deleted slots) for absolute-index rendering.
		//
		ts.extractor->Extract(convertedImage, tileParams);
		ts.extractor->ExtractAll(convertedImage, tileParams);
		//
		// Copy and sort deleted tile indices so the palette can skip those slots
		//
		ts.deletedTiles = tileParams->DeletedTiles;
		std::sort(ts.deletedTiles.begin(), ts.deletedTiles.end());
		ts.loaded = true;
	}
	//
	// Return the SDL texture for a map cell word (0 = empty)
	//
	SDL_Texture* DocumentMap::GetCellTexture(uint16_t cellVal) {
		if (cellVal == 0)
			return nullptr;
		int tilesetIdx = DecodeTilesetIdx(cellVal);
		int tileIdx = DecodeTileIdx(cellVal);
		if (tilesetIdx < 0 || tilesetIdx >= (int)m_loadedTilesets.size())
			return nullptr;
		LoadedTileset& ts = m_loadedTilesets[tilesetIdx];
		if (!ts.loaded || !ts.extractor)
			return nullptr;
		//
		// tileIdx is an absolute index (project.cpp restores absolute on load)
		//
		auto tileImage = ts.extractor->GetTileAll(tileIdx);
		if (!tileImage)
			return nullptr;
		return tileImage->GetTexture(Application::GetRenderer());
	}
	//
	// Encode a (tilesetIndex, tileIndex) pair into a map cell word
	//
	uint16_t DocumentMap::EncodeMapTile(int tilesetIdx, int tileIdx) {
		return (uint16_t)(((tilesetIdx + 1) << 12) | (tileIdx & 0x0FFF));
	}
	int DocumentMap::DecodeTilesetIdx(uint16_t val) {
		return (int)(val >> 12) - 1;
	}
	int DocumentMap::DecodeTileIdx(uint16_t val) {
		return (int)(val & 0x0FFF);
	}
	//
	// Resize map data in-place, preserving existing tiles in the overlapping region
	//
	void DocumentMap::ResizeMapData(RetrodevLib::MapLayer* layer, int newWidth, int newHeight) {
		std::vector<uint16_t> newData(newWidth * newHeight, 0);
		int minW = std::min(layer->width, newWidth);
		int minH = std::min(layer->height, newHeight);
		for (int row = 0; row < minH; row++) {
			for (int col = 0; col < minW; col++)
				newData[row * newWidth + col] = layer->data[row * layer->width + col];
		}
		layer->width = newWidth;
		layer->height = newHeight;
		layer->data = std::move(newData);
	}
	//
	// Insert empty row at top (shifts all existing rows down by one)
	//
	void DocumentMap::AddRowTop(RetrodevLib::MapLayer* layer) {
		int newHeight = layer->height + 1;
		std::vector<uint16_t> newData(layer->width * newHeight, 0);
		for (int row = 0; row < layer->height; row++) {
			for (int col = 0; col < layer->width; col++)
				newData[(row + 1) * layer->width + col] = layer->data[row * layer->width + col];
		}
		layer->height = newHeight;
		layer->data = std::move(newData);
	}
	//
	// Append empty row at bottom
	//
	void DocumentMap::AddRowBottom(RetrodevLib::MapLayer* layer) {
		layer->height++;
		layer->data.resize(layer->width * layer->height, 0);
	}
	//
	// Insert empty column at left (shifts all columns right by one per row)
	//
	void DocumentMap::AddColumnLeft(RetrodevLib::MapLayer* layer) {
		int newWidth = layer->width + 1;
		std::vector<uint16_t> newData(newWidth * layer->height, 0);
		for (int row = 0; row < layer->height; row++) {
			for (int col = 0; col < layer->width; col++)
				newData[row * newWidth + (col + 1)] = layer->data[row * layer->width + col];
		}
		layer->width = newWidth;
		layer->data = std::move(newData);
	}
	//
	// Append empty column at right
	//
	void DocumentMap::AddColumnRight(RetrodevLib::MapLayer* layer) {
		int newWidth = layer->width + 1;
		std::vector<uint16_t> newData(newWidth * layer->height, 0);
		for (int row = 0; row < layer->height; row++) {
			for (int col = 0; col < layer->width; col++)
				newData[row * newWidth + col] = layer->data[row * layer->width + col];
		}
		layer->width = newWidth;
		layer->data = std::move(newData);
	}
	//
	// Remove the top row from the map (shifts all rows up by one)
	//
	void DocumentMap::RemoveRowTop(RetrodevLib::MapLayer* layer) {
		if (layer->height <= 1)
			return;
		int newHeight = layer->height - 1;
		std::vector<uint16_t> newData(layer->width * newHeight, 0);
		for (int row = 0; row < newHeight; row++) {
			for (int col = 0; col < layer->width; col++)
				newData[row * layer->width + col] = layer->data[(row + 1) * layer->width + col];
		}
		layer->height = newHeight;
		layer->data = std::move(newData);
	}
	//
	// Remove the bottom row from the map
	//
	void DocumentMap::RemoveRowBottom(RetrodevLib::MapLayer* layer) {
		if (layer->height <= 1)
			return;
		layer->height--;
		layer->data.resize(layer->width * layer->height);
	}
	//
	// Remove the leftmost column from every row (shifts all columns left by one)
	//
	void DocumentMap::RemoveColumnLeft(RetrodevLib::MapLayer* layer) {
		if (layer->width <= 1)
			return;
		int newWidth = layer->width - 1;
		std::vector<uint16_t> newData(newWidth * layer->height, 0);
		for (int row = 0; row < layer->height; row++) {
			for (int col = 0; col < newWidth; col++)
				newData[row * newWidth + col] = layer->data[row * layer->width + (col + 1)];
		}
		layer->width = newWidth;
		layer->data = std::move(newData);
	}
	//
	// Remove the rightmost column from every row
	//
	void DocumentMap::RemoveColumnRight(RetrodevLib::MapLayer* layer) {
		if (layer->width <= 1)
			return;
		int newWidth = layer->width - 1;
		std::vector<uint16_t> newData(newWidth * layer->height, 0);
		for (int row = 0; row < layer->height; row++) {
			for (int col = 0; col < newWidth; col++)
				newData[row * newWidth + col] = layer->data[row * layer->width + col];
		}
		layer->width = newWidth;
		layer->data = std::move(newData);
	}
	//
	// Render the layers section: add, remove, rename, reorder and select map layers
	//
	void DocumentMap::RenderLayersSection(RetrodevLib::MapParams* params) {
		//
		// Add Layer button
		//
		if (ImGui::Button(ICON_PLUS " Add Layer##AddLayer", ImVec2(-1.0f, 0.0f))) {
			RetrodevLib::MapLayer newLayer;
			newLayer.name = "Layer " + std::to_string(params->layers.size() + 1);
          newLayer.data.resize(newLayer.width * newLayer.height, 0);
			params->layers.push_back(std::move(newLayer));
			m_editingLayerIdx = (int)params->layers.size() - 1;
			m_lastEditingLayerIdx = -1;
			SetModified(true);
			RetrodevLib::Project::MarkAsModified();
		}
		//
		// Layer list: each row has a visibility toggle and a selectable label
		//
		bool layerSelected = m_editingLayerIdx >= 0 && m_editingLayerIdx < (int)params->layers.size();
		if (ImGui::BeginChild("##LayerList", ImVec2(-1.0f, ImGui::GetFontSize() * 6.0f), true)) {
			for (int i = 0; i < (int)params->layers.size(); i++) {
				auto& layer = params->layers[i];
				ImGui::PushID(i);
				//
				// Visibility toggle
				//
				if (layer.visible) {
					if (ImGui::SmallButton(ICON_EYE "##LayerVis"))
						layer.visible = false;
				} else {
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));
					if (ImGui::SmallButton(ICON_EYE_OFF "##LayerVis"))
						layer.visible = true;
					ImGui::PopStyleColor();
				}
				ImGui::SameLine();
				//
				// Selectable: selecting makes this the editing layer
				//
				bool selected = (m_editingLayerIdx == i);
				std::string label = layer.name + " (" + std::to_string(layer.width) + "x" + std::to_string(layer.height) + ")";
				if (ImGui::Selectable(label.c_str(), selected))
					m_editingLayerIdx = i;
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
		//
		// Move Up / Move Down buttons
		//
		float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		ImGui::BeginDisabled(!layerSelected || m_editingLayerIdx <= 0);
		if (ImGui::Button(ICON_ARROW_UP " Move Up##LayerUp", ImVec2(btnW, 0.0f))) {
			std::swap(params->layers[m_editingLayerIdx], params->layers[m_editingLayerIdx - 1]);
			m_editingLayerIdx--;
			m_lastEditingLayerIdx = -1;
			SetModified(true);
			RetrodevLib::Project::MarkAsModified();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!layerSelected || m_editingLayerIdx >= (int)params->layers.size() - 1);
		if (ImGui::Button(ICON_ARROW_DOWN " Move Down##LayerDown", ImVec2(btnW, 0.0f))) {
			std::swap(params->layers[m_editingLayerIdx], params->layers[m_editingLayerIdx + 1]);
			m_editingLayerIdx++;
			m_lastEditingLayerIdx = -1;
			SetModified(true);
			RetrodevLib::Project::MarkAsModified();
		}
		ImGui::EndDisabled();
		//
		// Remove Layer button
		//
		ImGui::BeginDisabled(!layerSelected);
		if (ImGui::Button(ICON_DELETE " Remove Layer##RemoveLayer", ImVec2(-1.0f, 0.0f))) {
			params->layers.erase(params->layers.begin() + m_editingLayerIdx);
			if (m_editingLayerIdx >= (int)params->layers.size())
				m_editingLayerIdx = (int)params->layers.size() - 1;
			m_lastEditingLayerIdx = -1;
			SetModified(true);
			RetrodevLib::Project::MarkAsModified();
		}
		ImGui::EndDisabled();
		//
		// Per-layer name and speed editing (shown when a layer is selected)
		//
		if (layerSelected) {
			auto& editLayer = params->layers[m_editingLayerIdx];
			//
			// Sync rename buffer when the selected layer changes
			//
			if (m_layerRenameIdx != m_editingLayerIdx) {
				std::strncpy(m_layerRenameBuffer, editLayer.name.c_str(), sizeof(m_layerRenameBuffer) - 1);
				m_layerRenameBuffer[sizeof(m_layerRenameBuffer) - 1] = '\0';
				m_layerRenameIdx = m_editingLayerIdx;
			}
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Name:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("##LayerName", m_layerRenameBuffer, sizeof(m_layerRenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
				std::string newName(m_layerRenameBuffer);
				if (!newName.empty()) {
					editLayer.name = newName;
					SetModified(true);
					RetrodevLib::Project::MarkAsModified();
				}
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Press Enter to rename the layer");
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Speed:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
			if (ImGui::InputFloat("##LayerSpeed", &editLayer.mapSpeed, 0.1f, 1.0f, "%.2f")) {
				if (editLayer.mapSpeed < 0.01f)
					editLayer.mapSpeed = 0.01f;
				SetModified(true);
				RetrodevLib::Project::MarkAsModified();
			}
			ImGui::SameLine();
			ImGui::TextDisabled(ICON_INFORMATION_OUTLINE);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Scroll speed in tiles per camera step.\n1.0 = moves one tile per step (typical foreground layer).\n\nParallax scroller: assign a different speed to each layer\n(e.g. 0.25 sky, 0.5 background, 1.0 foreground) to preview\nhow the planes move relative to each other on screen.\n\nScreen-by-screen game (non-scroller): set speed equal to\nthe viewable area width (or height) so that each camera step\nadvances a full screen, producing a clean room-to-room transition.");
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Offset X:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
			if (ImGui::InputFloat("##LayerOffsetX", &editLayer.offsetX, 0.5f, 1.0f, "%.2f")) {
				SetModified(true);
				RetrodevLib::Project::MarkAsModified();
			}
			ImGui::SameLine();
			ImGui::TextDisabled(ICON_INFORMATION_OUTLINE);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Horizontal position offset in tiles (fractional values allowed).");
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Offset Y:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
			if (ImGui::InputFloat("##LayerOffsetY", &editLayer.offsetY, 0.5f, 1.0f, "%.2f")) {
				SetModified(true);
				RetrodevLib::Project::MarkAsModified();
			}
			ImGui::SameLine();
			ImGui::TextDisabled(ICON_INFORMATION_OUTLINE);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Vertical position offset in tiles (fractional values allowed).");
		}
	}
	//
	// Render the groups section: add, rename, remove and select tile groups
	//
	void DocumentMap::RenderGroupsSection(RetrodevLib::MapParams* params) {
		//
		// Capture mode: show status and cancel button instead of Add Group
		//
		if (m_groupCapturing) {
			ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Drag on canvas to select region...");
			if (ImGui::Button(ICON_CLOSE " Cancel##CancelCapture", ImVec2(-1.0f, 0.0f))) {
				m_groupCapturing = false;
				m_groupCaptureDragging = false;
			}
		} else {
			if (ImGui::Button(ICON_SELECTION_DRAG " Add Group##AddGroup", ImVec2(-1.0f, 0.0f))) {
				m_groupCapturing = true;
				m_groupCaptureDragging = false;
				m_groupCaptureStartCol = -1;
				m_groupCaptureStartRow = -1;
				m_groupCaptureCurCol = -1;
				m_groupCaptureCurRow = -1;
				m_selectedGroupIdx = -1;
				m_selectedTileIdx = -1;
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Enter capture mode, then drag a rectangle on the canvas\nto define the tile region for this group stamp.");
		}
		//
		// Group list box
		//
		if (ImGui::BeginListBox("##GroupList", ImVec2(-1.0f, ImGui::GetFontSize() * 6.0f))) {
			for (int i = 0; i < (int)params->groups.size(); i++) {
				bool selected = (m_selectedGroupIdx == i);
				if (ImGui::Selectable(params->groups[i].name.c_str(), selected)) {
					m_selectedGroupIdx = i;
					m_selectedTileIdx = -1;
					m_groupCapturing = false;
					m_groupCaptureDragging = false;
					//
					// Sync rename buffer to the newly selected group
					//
					std::strncpy(m_groupRenameBuffer, params->groups[i].name.c_str(), sizeof(m_groupRenameBuffer) - 1);
					m_groupRenameBuffer[sizeof(m_groupRenameBuffer) - 1] = '\0';
					m_groupRenameIdx = i;
				}
			}
			ImGui::EndListBox();
		}
		//
		// Rename input: shown when a group is selected
		//
		bool groupSelected = m_selectedGroupIdx >= 0 && m_selectedGroupIdx < (int)params->groups.size();
		if (groupSelected) {
			//
			// Sync buffer if the selection changed since last frame
			//
			if (m_groupRenameIdx != m_selectedGroupIdx) {
				std::strncpy(m_groupRenameBuffer, params->groups[m_selectedGroupIdx].name.c_str(), sizeof(m_groupRenameBuffer) - 1);
				m_groupRenameBuffer[sizeof(m_groupRenameBuffer) - 1] = '\0';
				m_groupRenameIdx = m_selectedGroupIdx;
			}
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Name:");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.0f);
			if (ImGui::InputText("##GroupName", m_groupRenameBuffer, sizeof(m_groupRenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
				std::string newName(m_groupRenameBuffer);
				if (!newName.empty()) {
					params->groups[m_selectedGroupIdx].name = newName;
					SetModified(true);
					RetrodevLib::Project::MarkAsModified();
				}
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Press Enter to rename the group");
		}
		//
		// Remove button: enabled only when a group is selected
		//
		ImGui::BeginDisabled(!groupSelected);
		if (ImGui::Button(ICON_DELETE " Remove Group##RemoveGroup", ImVec2(-1.0f, 0.0f))) {
			params->groups.erase(params->groups.begin() + m_selectedGroupIdx);
			m_selectedGroupIdx = -1;
			m_groupRenameIdx = -1;
			SetModified(true);
			RetrodevLib::Project::MarkAsModified();
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			ImGui::SetTooltip("Remove the selected group stamp.");
		ImGui::EndDisabled();
	}
	//
	// Draw a semi-transparent preview of a tile group anchored at the given map cell
	//
	void DocumentMap::RenderGroupPreview(ImDrawList* drawList, ImVec2 canvasOrigin, const RetrodevLib::TileGroup& group, int anchorCol, int anchorRow) {
		float drawCellW = m_cellSize * m_aspectHScale;
		float drawCellH = m_cellSize * m_aspectVScale;
		//
		// Draw a border to show the overall group extent
		//
		ImVec2 extentMin = ImVec2(canvasOrigin.x + anchorCol * drawCellW, canvasOrigin.y + anchorRow * drawCellH);
		ImVec2 extentMax = ImVec2(canvasOrigin.x + (anchorCol + group.width) * drawCellW, canvasOrigin.y + (anchorRow + group.height) * drawCellH);
		drawList->AddRect(extentMin, extentMax, IM_COL32(255, 220, 0, 180), 0.0f, 0, 2.0f);
		//
		// Draw each non-empty tile semi-transparently
		//
		for (int gr = 0; gr < group.height; gr++) {
			for (int gc = 0; gc < group.width; gc++) {
				uint16_t cellVal = group.tiles[gr * group.width + gc];
				if (cellVal == 0)
					continue;
				int drawCol = anchorCol + gc;
				int drawRow = anchorRow + gr;
				ImVec2 cellMin = ImVec2(canvasOrigin.x + drawCol * drawCellW, canvasOrigin.y + drawRow * drawCellH);
				ImVec2 cellMax = ImVec2(cellMin.x + drawCellW, cellMin.y + drawCellH);
				SDL_Texture* tex = GetCellTexture(cellVal);
				if (tex)
					drawList->AddImage((ImTextureID)(intptr_t)tex, cellMin, cellMax, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 160));
				else
					drawList->AddRectFilled(cellMin, cellMax, IM_COL32(100, 100, 200, 100));
			}
		}
	}

}
