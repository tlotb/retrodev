// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Gui
//
// Raster document -- Amstrad CPC CRTC raster panel implementation.
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#include "document.raster.h"
#include "document.raster.cpc.h"
#include <app/app.h>
#include <app/app.icons.mdi.h>
#include <views/text/langs/lang.asm.z80.h>
#include <convert/amstrad.cpc/amstrad.cpc.h>
#include <filesystem>
#include <fstream>
#include <array>
#include <algorithm>
#include <map>
#include <utility>
#include <SDL3/SDL.h>

namespace RetrodevGui {

	// ---------------------------------------------------------------------------
	// RenderPanel -- top-level entry: vertical splitter (main area | violations)
	// inside which the main area is split horizontally (visualizer | editor)
	// ---------------------------------------------------------------------------

	// Find next unique command name by scanning existing commands
	static std::string FindUniqueName(const std::string& prefix, const std::vector<RetrodevLib::CpcRasterCommand>& commands) {
		int maxNum = 0;
		for (const auto& cmd : commands) {
			std::string name = RetrodevLib::GetCommandName(cmd);
			if (name.find(prefix) == 0) {
				// Parse the number at the end (format: "Prefix N")
				size_t lastSpace = name.rfind(' ');
				if (lastSpace != std::string::npos && lastSpace + 1 < name.length()) {
					const char* numStart = name.c_str() + lastSpace + 1;
					int num = 0;
					for (const char* p = numStart; *p >= '0' && *p <= '9'; p++) {
						num = num * 10 + (*p - '0');
					}
					if (num > 0) {
						maxNum = std::max(maxNum, num);
					}
				}
			}
		}
		return prefix + " " + std::to_string(maxNum + 1);
	}

	// Resolve an effect's absolute scanline by recursively resolving relative effects
	static int ResolveEffectScanline(const std::vector<RetrodevLib::CpcRasterCommand>& commands, int effectIndex) {
		if (effectIndex < 0 || effectIndex >= (int)commands.size())
			return 0;
		const auto& cmd = commands[effectIndex];
		if (!std::holds_alternative<RetrodevLib::CpcEffectCommand>(cmd))
			return 0;
		const auto& effect = std::get<RetrodevLib::CpcEffectCommand>(cmd);
		if (!effect.enabled)
			return 0;

		if (effect.targetMode == RetrodevLib::EffectTargetMode::Absolute) {
			return effect.targetScanline;
		} else {  // Relative mode
			// Find previous enabled effect and resolve its scanline recursively
			for (int j = effectIndex - 1; j >= 0; j--) {
				const auto& prevCmd = commands[j];
				if (std::holds_alternative<RetrodevLib::CpcEffectCommand>(prevCmd)) {
					const auto& prevEffect = std::get<RetrodevLib::CpcEffectCommand>(prevCmd);
					if (prevEffect.enabled) {
						int prevResolved = ResolveEffectScanline(commands, j);
						return prevResolved + effect.targetScanline;
					}
				}
			}
			// No previous enabled effect found; treat as absolute 0
			return effect.targetScanline;
		}
	}

	static const ImGui::ILanguageDefinition* GetRasterAsmLanguage() {
		static Z80AsmLanguage s_z80AsmLanguage;
		return &s_z80AsmLanguage;
	}

	void DocumentRasterCpc::Reset() {
		if (m_monitorTex) {
			SDL_DestroyTexture(m_monitorTex);
			m_monitorTex = nullptr;
		}
		m_monitorTexW = 0;
		m_monitorTexH = 0;
		m_monitorPhaseLines = 0;
		m_selectedCommandIndex = -1;
		m_generatedAsm.clear();
		m_validationResult.entries.clear();
		m_timingReport.slots.clear();
		m_dirty = false;
		m_debounceTime = 0.0;
		m_sizesInitialized = false;
		m_generatorFieldsInit = false;
		m_generatorStatusVisible = false;
		m_generatorStatusBuf[0] = '\0';
		m_generatorWarnings.clear();
		m_frameSmcCheckboxStates.clear();
		m_framePatchFunctionCheckboxStates.clear();
		m_frameSmcLabelBuffers.clear();
		m_vmaEnableStates.clear();
		m_vmaSmcCheckboxStates.clear();
		m_vmaSmcLabelBuffers.clear();
		m_effectModeStates.clear();
		m_effectOffsetStates.clear();
		m_effectSmcCheckboxStates.clear();
		m_effectSmcLabelBuffers.clear();
		m_varUnrestrainedStates.clear();
		m_cpcCrtc = RetrodevLib::CPCRaster();
		m_initialized = false;
	}

	void DocumentRasterCpc::SetOnModified(std::function<void()> onModified) {
		m_onModified = std::move(onModified);
	}

	void DocumentRasterCpc::SetProjectFolder(const std::string& projectFolder) {
		m_projectFolder = projectFolder;
	}

	void DocumentRasterCpc::SetRenderer(SDL_Renderer* renderer) {
		m_renderer = renderer;
	}

	void DocumentRasterCpc::SyncUIStateFromCommands() {
		int idx = m_selectedCommandIndex;
		if (idx < 0 || idx >= (int)m_cpcCrtc.GetCommands().size())
			return;

		const auto& cmd = m_cpcCrtc.GetCommands()[idx];

		// Sync Frame command state
		if (auto* frameCmd = std::get_if<RetrodevLib::CpcFrameCommand>(&cmd)) {
			const auto& fr = frameCmd->frame;
			// Sync SMC checkbox states for registers that are actually rendered: R0-R7, R9
			// Note: R8 is unused, R12/R13 (VMA) are handled separately via MASK_R12
			for (int reg = 0; reg <= 9; reg++) {
				if (reg == 8) continue;  // Skip unused R8
				uint32_t maskBit = (reg == 9) ? RetrodevLib::RasterFrameCmd::MASK_R9 : (1u << reg);
				m_frameSmcCheckboxStates[{idx, reg}] = (fr.smcMask & maskBit) != 0;
				// Sync SMC patch function checkbox states (Phase 1: R5 only)
				m_framePatchFunctionCheckboxStates[{idx, reg}] = (fr.smcPatchFunctionMask & maskBit) != 0;
			}
			// Sync VMA state (R12/R13 pair uses MASK_R12)
			m_vmaEnableStates[idx] = (fr.activeMask & RetrodevLib::RasterFrameCmd::MASK_R12) != 0;
			// Sync VMA SMC state
			m_frameSmcCheckboxStates[{idx, 12}] = (fr.smcMask & RetrodevLib::RasterFrameCmd::MASK_R12) != 0;
		}
		// Sync Effect command state
		else if (auto* effectCmd = std::get_if<RetrodevLib::CpcEffectCommand>(&cmd)) {
			m_effectModeStates[idx] = (int)effectCmd->targetMode;
		}
		// Sync Variable command state
		else if (auto* varCmd = std::get_if<RetrodevLib::CpcVariableCommand>(&cmd)) {
			m_varUnrestrainedStates[idx] = varCmd->unrestrained;
		}
	}

	void DocumentRasterCpc::Initialize(RetrodevLib::RasterParams* params) {
		if (!params || m_initialized)
			return;
		// Load the project into the library instance (once on initialization)
		m_cpcCrtc.LoadProject(*params);
		// Sync UI state from params
		m_selectedCommandIndex = params->selectedCommand;
		// Create palette converter for color selection (GA Set Ink/Border commands)
		m_cpcPalette = RetrodevLib::Converters::GetPaletteConverter(
			params->targetSystem,
			params->targetMode,
			params->targetPaletteType
		);
		m_initialized = true;
	}

	void DocumentRasterCpc::SaveProject(RetrodevLib::RasterParams* params) {
		if (!params || !m_initialized)
			return;
		// Serialize library state back to parameters
		m_cpcCrtc.SaveProject(*params);
		// Sync UI state back to params
		params->selectedCommand = m_selectedCommandIndex;
	}

	void DocumentRasterCpc::RenderPanel(RetrodevLib::RasterParams* params) {
		if (!params)
			return;

		// Initialize on first use (library owns state from here on)
		Initialize(params);

		float fontSize = ImGui::GetFontSize();
		ImVec2 avail = ImGui::GetContentRegionAvail();
		//
		// Sync all UI state maps from command data (only when selected command changes)
		//
		SyncUIStateFromCommands();
		//
		// Auto-generate code with debounce timer
		//
		UpdateAutoGenerate(ImGui::GetIO().DeltaTime);
		//
		// Validate only when commands are marked dirty to avoid expensive validation every frame.
		// Validation is re-run when: code generation happens, or user navigates to a different command.
		//
		if (m_dirty || m_validationResult.entries.empty()) {
			m_cpcCrtc.Validate(m_validationResult);
		}
		//
		// One-time size initialisation for horizontal split
		//
		if (!m_sizesInitialized) {
			m_hSizeLeft  = avail.x * 0.60f;
			m_hSizeRight = avail.x - m_hSizeLeft - Application::splitterThickness - ImGui::GetStyle().ItemSpacing.x;
			m_vSizeBottom = fontSize * 8.0f;
			m_sizesInitialized = true;
		}
		//
		// Recompute top area height from available space minus bottom panel
		//
		m_vSizeTop = avail.y - m_vSizeBottom - Application::splitterThickness - ImGui::GetStyle().ItemSpacing.y;
		if (m_vSizeTop < fontSize * 10.0f)
			m_vSizeTop = fontSize * 10.0f;
		//
		// Recompute right width from available space
		//
		m_hSizeRight = avail.x - m_hSizeLeft - Application::splitterThickness - ImGui::GetStyle().ItemSpacing.x;
		//
		// Vertical splitter: top (main split) | bottom (violations)
		//
		ImGui::DrawSplitter(true, Application::splitterThickness, &m_vSizeTop, &m_vSizeBottom, fontSize * 10.0f, fontSize * 4.0f);
		//
		// Top area: horizontal splitter (visualizer | editor)
		//
		if (ImGui::BeginChild("##CpcMain", ImVec2(-1, m_vSizeTop), false)) {
			//
			// Horizontal splitter inside the top area
			//
			ImVec2 mainAvail = ImGui::GetContentRegionAvail();
			m_hSizeRight = mainAvail.x - m_hSizeLeft - Application::splitterThickness - ImGui::GetStyle().ItemSpacing.x;
			ImGui::DrawSplitter(false, Application::splitterThickness, &m_hSizeLeft, &m_hSizeRight, fontSize * 14.0f, fontSize * 16.0f);
			if (ImGui::BeginChild("##CpcVis", ImVec2(m_hSizeLeft, -1), true))
				RenderVisualizer();
			ImGui::EndChild();
			ImGui::SameLine();
			//
			// Skip over the splitter bar before the right child
			//
			ImVec2 cur = ImGui::GetCursorScreenPos();
			cur.x += Application::splitterThickness;
			ImGui::SetCursorScreenPos(cur);
			if (ImGui::BeginChild("##CpcEdit", ImVec2(m_hSizeRight, -1), true))
				RenderCommandEditor(params);
			ImGui::EndChild();
		}
		ImGui::EndChild();
		//
		// Skip over the vertical splitter bar
		//
		ImVec2 splitterSkip = ImGui::GetCursorScreenPos();
		splitterSkip.y += Application::splitterThickness;
		ImGui::SetCursorScreenPos(splitterSkip);
		//
		// Bottom panel: tabbed view with Violations and Timing
		//
		if (ImGui::BeginChild("##CpcBottomPanel", ImVec2(-1, m_vSizeBottom), true)) {
			if (ImGui::BeginTabBar("##BottomTabs", ImGuiTabBarFlags_None)) {
				//
				// Tab 1: Violations
				//
				if (ImGui::BeginTabItem("Violations")) {
					RenderViolationsPanel();
					ImGui::EndTabItem();
				}
				//
				// Tab 2: Timing
				//
				if (ImGui::BeginTabItem("Timing")) {
					RenderTimingPanel();
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}
		}
		ImGui::EndChild();
	}

	// ---------------------------------------------------------------------------
	// RenderVisualizer -- CRTC frame grid (design mode) or monitor image (monitor mode)
	// ---------------------------------------------------------------------------

	void DocumentRasterCpc::RenderVisualizer() {
		// Use the deserialized commands from the library instance
		//
		const auto& cmds = m_cpcCrtc.GetCommands();
		int selIdx = m_selectedCommandIndex;
		//
		// Fixed canvas: CRTC max 64 char columns x 39 char rows
		//
		const int maxCols     = 64;
		const int maxCharRows = 39;
		//
		// Use r9 from first active command (or default=7)
		//
		uint8_t globalR9 = 7;
		if (!cmds.empty() && std::holds_alternative<RetrodevLib::CpcFrameCommand>(cmds[0])) {
			const auto& frameCmd = std::get<RetrodevLib::CpcFrameCommand>(cmds[0]);
			if (frameCmd.frame.activeMask & RetrodevLib::RasterFrameCmd::MASK_R9)
				globalR9 = frameCmd.frame.r9;
		}
		const int scanPerChar = globalR9 + 1;
		const int totalLines  = maxCharRows * scanPerChar;
		//
		// Build slots: each Frame command merges onto the cumulative state and occupies
		// one full CRTC frame worth of scan lines starting where the previous one ended.
		//
		struct CmdSlot {
			RetrodevLib::RasterFrameCmd state;
			int startSl;
			int endSl;
			int cmdIdx;
		};
		std::vector<CmdSlot> slots;
		{
			RetrodevLib::RasterFrameCmd current;
			int sl = 0;
			for (int i = 0; i < (int)cmds.size(); i++) {
				if (!std::holds_alternative<RetrodevLib::CpcFrameCommand>(cmds[i]))
					continue;
				const auto& frameCmd = std::get<RetrodevLib::CpcFrameCommand>(cmds[i]);
				RetrodevLib::RasterFrameCmd merged = RetrodevLib::CpcRaster::CrtcSimulator::MergeFrame(current, frameCmd.frame);
				int slotH = RetrodevLib::CpcRaster::CrtcSimulator::TotalScanLines(merged);
				CmdSlot s;
				s.state   = merged;
				s.startSl = sl;
				s.endSl   = sl + slotH;
				s.cmdIdx  = i;
				slots.push_back(s);
				sl += slotH;
				current = merged;
				if (sl >= totalLines)
					break;
			}
		}
		//
		// Toggle button: switch between Design and Monitor mode
		//
		{
			const char* label = m_monitorMode ? ICON_MONITOR " Monitor" : ICON_VIEW_DASHBOARD " Design";
			if (ImGui::Button(label))
				m_monitorMode = !m_monitorMode;
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip(m_monitorMode
					? "Monitor mode: pixel-accurate raster simulation.\nClick to switch to Design mode (CRTC grid)."
					: "Design mode: abstract CRTC timing grid.\nClick to switch to Monitor mode (pixel-accurate raster simulation).");
		}
		ImGui::Separator();

		const float labelColW = 160.0f; // Increased padding for labels (row numbers, interrupts, frame names)

		//
		// --- MONITOR MODE ---
		//
		if (m_monitorMode) {
			//
			// Build MonitorSlot list for the monitor -- always covers exactly 312 PAL scan lines,
			// decoupled from the CRTC generator frame list.
			// - If frames sum to fewer than 312 lines the last slot is extended to 312, simulating
			//   the monitor beam continuing to paint with the last active CRTC state (out-of-sync).
			// - If frames sum to more than 312 lines the slots are passed as-is (not clamped) so
			//   RenderMonitorImage can detect the over-budget condition and suppress the VSync
			//   Y-anchor, causing the image to appear rolled/unsynchronised.
			//
			static constexpr int kMonitorLines = 312;
			std::vector<RetrodevLib::CpcRaster::MonitorSlot> monSlots;
			monSlots.reserve(slots.size());
			for (int si = 0; si < (int)slots.size(); si++) {
				RetrodevLib::CpcRaster::MonitorSlot ms;
				ms.state     = slots[si].state;
				ms.startSl   = slots[si].startSl;
				ms.endSl     = slots[si].endSl;
				ms.slotIndex = si;
				monSlots.push_back(ms);
			}
			//
			// Compute the actual CRTC scanline total from the slots (no clamping/extension).
			// Under- and over-budget totals must both reach the renderer so monitor drift can
			// reflect true out-of-sync behavior.
			//
			int crtcTotal = monSlots.empty() ? kMonitorLines : monSlots.back().endSl;
			//
			// Monitor lock behaviour:
			// - exact 312 lines: hard re-lock immediately (phase reset to 0)
			// - non-312 lines: free-run drift by frame delta
			//
			if (crtcTotal == kMonitorLines) {
				m_monitorPhaseLines = 0;
			} else {
				int deltaPhase = crtcTotal - kMonitorLines;
				m_monitorPhaseLines = (m_monitorPhaseLines + deltaPhase) % kMonitorLines;
				if (m_monitorPhaseLines < 0)
					m_monitorPhaseLines += kMonitorLines;
			}
			//
			// Generate pixel buffer
			//
			std::vector<uint32_t> pixels;
			int imgW = 0;
			int imgH = 0;
			RetrodevLib::CpcRaster::CrtcSimulator::RenderMonitorImage(monSlots, maxCols, crtcTotal, m_monitorPhaseLines, pixels, imgW, imgH);
			//
			// (Re)create SDL texture when dimensions change
			//
			if (m_monitorTex && (m_monitorTexW != imgW || m_monitorTexH != imgH)) {
				SDL_DestroyTexture(m_monitorTex);
				m_monitorTex  = nullptr;
				m_monitorTexW = 0;
				m_monitorTexH = 0;
			}
			if (!m_monitorTex && imgW > 0 && imgH > 0) {
				SDL_Renderer* rend = (m_renderer != nullptr) ? m_renderer : Application::GetRenderer();
				m_monitorTex = SDL_CreateTexture(rend, SDL_PIXELFORMAT_RGBA32,
					SDL_TEXTUREACCESS_STREAMING, imgW, imgH);
				if (m_monitorTex) {
					m_monitorTexW = imgW;
					m_monitorTexH = imgH;
				}
			}
			//
			// Upload pixel data every frame (image is small: 64 x 312 max)
			//
			if (m_monitorTex && !pixels.empty()) {
				SDL_UpdateTexture(m_monitorTex, nullptr, pixels.data(), imgW * (int)sizeof(uint32_t));
			}

			// Add padding for monitor view
			ImGui::Dummy(ImVec2(labelColW, 4.0f));  // Top and left padding
			ImGui::SameLine();

			//
				// Draw the texture at PAL 4:3 aspect ratio.
				// Drive by height first (fills vertical space), then clamp to available width.
				//
				ImVec2 avail = ImGui::GetContentRegionAvail();
				if (m_monitorTex && imgW > 0 && imgH > 0) {
					//
					// PAL 4:3 target: for a given display height, display width = height * 4/3.
					// If that exceeds available width, clamp to width and recompute height.
					//
					float dispH = avail.y - ImGui::GetTextLineHeightWithSpacing() - 30.0f;  // More space for legend
					float dispW = dispH * (4.0f / 3.0f);
					if (dispW > avail.x - labelColW) {
						dispW = avail.x - labelColW;
						dispH = dispW * (3.0f / 4.0f);
					}

					// Draw image with padding
					ImVec2 curPos = ImGui::GetCursorScreenPos();
					ImGui::Image((ImTextureID)(intptr_t)m_monitorTex, ImVec2(dispW, dispH));

					// Overlay interrupts over monitor image as well
					ImDrawList* dl = ImGui::GetWindowDrawList();
					float imgScaleY = dispH / (float)kMonitorLines;


				// Draw VSync bands on the monitor (semi-transparent yellow)
				//
				// VSync position depends on R7:
				// - R7 != 0: fires at R7 position, height determined by R3 (sync width upper nibble)
				// - R7 == 0: fires at END of frame, blanks exactly 2 character rows
				// For each slot, compute when VSync is active and draw a semi-transparent
				// yellow band for clarity.
				//
				const ImU32 colVSyncBand = IM_COL32(232, 200, 0, 60);  // semi-transparent yellow
				for (const auto& slot : monSlots) {
					const RetrodevLib::RasterFrameCmd& f = slot.state;
					if (f.disableVSync)
						continue;  // VSync suppressed
					int vsyncStartSl, vsyncEndSl;
					if (f.r7 == 0) {
						int totalFrameScanLines = (f.r4 + 1) * (f.r9 + 1) + f.r5;
						vsyncStartSl = slot.startSl + totalFrameScanLines - 2 * (f.r9 + 1);
						vsyncEndSl = slot.startSl + totalFrameScanLines;
					} else {
						vsyncStartSl = slot.startSl + f.r7 * (f.r9 + 1);
						int vsyncR3h = (f.r3 >> 4) & 0x0F;
						int vsyncWidth = (vsyncR3h == 0) ? 16 : vsyncR3h;
						vsyncEndSl = vsyncStartSl + vsyncWidth * (f.r9 + 1);
					}
					if (vsyncStartSl < slot.endSl) {
						// Apply phase to VSync start position
						int visualStart = (vsyncStartSl - m_monitorPhaseLines + kMonitorLines) % kMonitorLines;
						int visualEnd = (vsyncEndSl - m_monitorPhaseLines + kMonitorLines) % kMonitorLines;
						float py0 = curPos.y + visualStart * imgScaleY;
						float py1 = curPos.y + visualEnd * imgScaleY;
						if (visualEnd >= visualStart) {
							dl->AddRectFilled(ImVec2(curPos.x, py0), ImVec2(curPos.x + dispW, py1), colVSyncBand);
						}
					}
				}
					// Draw interrupt lines on top of the monitor
					int accumLines = 0;
					for (const auto& slot : monSlots)
						accumLines += RetrodevLib::CpcRaster::CrtcSimulator::TotalScanLines(slot.state);

					int intNum = 1;
					for (int intSl = 51; intSl < accumLines; intSl += 52) {
						if (intSl >= kMonitorLines && m_monitorPhaseLines == 0)
							break;

						// Apply phase (scroll drift) to monitor interrupt lines too
						// Keep it simple for now and just display modulo monitor lines
						int visualLine = (intSl - m_monitorPhaseLines + kMonitorLines) % kMonitorLines;

						float py = curPos.y + visualLine * imgScaleY;
						const ImU32 colInterrupt  = IM_COL32(220,  60,  60, 255);

						dl->AddLine(
							ImVec2(curPos.x, py),
							ImVec2(curPos.x + dispW, py),
							colInterrupt, 2.0f);

						char buf[24];
						SDL_snprintf(buf, sizeof(buf), "INT%d", intNum);
						dl->AddText(ImVec2(curPos.x - labelColW, py - 6.0f), colInterrupt, buf);
						intNum++;
					}

					// Draw Effect/Variable markers on the left ruler
					//
					// Iterate through all commands and draw small icons/pins at their target scanlines.
					//
					const ImU32 colEffectPin = IM_COL32(100, 200, 255, 255);  // light blue for effects
					const ImU32 colVariablePin = IM_COL32(200, 100, 255, 255);  // light purple for variables
					for (int ci = 0; ci < (int)cmds.size(); ci++) {
						if (std::holds_alternative<RetrodevLib::CpcFrameCommand>(cmds[ci]))
							continue;  // Skip Frame commands
						int targetSl = 0;
						ImU32 pinCol = colVariablePin;
						if (std::holds_alternative<RetrodevLib::CpcEffectCommand>(cmds[ci])) {
							const auto& effectCmd = std::get<RetrodevLib::CpcEffectCommand>(cmds[ci]);
							targetSl = effectCmd.targetScanline;
							pinCol = colEffectPin;
						} else if (std::holds_alternative<RetrodevLib::CpcVariableCommand>(cmds[ci])) {
							const auto& varCmd = std::get<RetrodevLib::CpcVariableCommand>(cmds[ci]);
							targetSl = varCmd.targetLine;
							pinCol = colVariablePin;
						}
						int visualLine = (targetSl - m_monitorPhaseLines + kMonitorLines) % kMonitorLines;
						float py = curPos.y + visualLine * imgScaleY;
						// Draw a small triangle/arrow pointing right
						float pinX = curPos.x - 10.0f;
						float pinY = py;
						dl->AddTriangleFilled(
							ImVec2(pinX - 3.0f, pinY - 4.0f),
							ImVec2(pinX - 3.0f, pinY + 4.0f),
							ImVec2(pinX + 3.0f, pinY),
							pinCol);
					}
				} else {
					ImGui::TextDisabled("No frame commands -- nothing to display.");
				}
			//
				// Legend with clear vertical gap and proper alignment
				//
				ImGui::Spacing();  // Add vertical gap before legend
				ImDrawList* dl = ImGui::GetWindowDrawList();
					ImVec2 lp = ImGui::GetCursorScreenPos();
					const float sw = 12.0f;
					const float sh = 12.0f;
					const float gap = 4.0f;
					const float txtCenterY = sh / 2.0f - ImGui::CalcTextSize("X").y / 2.0f;  // Vertically center text
					float lx = lp.x;
					//
					// VMA checker swatch (split left/right)
					//
					dl->AddRectFilled(ImVec2(lx,          lp.y), ImVec2(lx + sw / 2, lp.y + sh), 0xFF623000u);
					dl->AddRectFilled(ImVec2(lx + sw / 2, lp.y), ImVec2(lx + sw,     lp.y + sh), 0xFF8250DCu);
					dl->AddText(ImVec2(lx + sw + 3.0f, lp.y + txtCenterY), IM_COL32(200, 200, 200, 255), "VMA");
					lx += sw + 3.0f + ImGui::CalcTextSize("VMA").x + gap + 8.0f;
					//
					// Border
					//
					dl->AddRectFilled(ImVec2(lx, lp.y), ImVec2(lx + sw, lp.y + sh), 0xFF2A2A2Au);
					dl->AddText(ImVec2(lx + sw + 3.0f, lp.y + txtCenterY), IM_COL32(200, 200, 200, 255), "Border");
					lx += sw + 3.0f + ImGui::CalcTextSize("Border").x + gap + 8.0f;
					//
					// HSync: pulse start (bright purple) / pulse end (dark purple).
					// IM_COL32 is always RGBA independent of platform byte order.
					//
					dl->AddRectFilled(ImVec2(lx,          lp.y), ImVec2(lx + sw / 2, lp.y + sh), IM_COL32(176, 64, 208, 255));
					dl->AddRectFilled(ImVec2(lx + sw / 2, lp.y), ImVec2(lx + sw,     lp.y + sh), IM_COL32( 72, 24,  96, 255));
					dl->AddText(ImVec2(lx + sw + 3.0f, lp.y + txtCenterY), IM_COL32(200, 200, 200, 255), "HSync (start/end)");
					lx += sw + 3.0f + ImGui::CalcTextSize("HSync (start/end)").x + gap + 8.0f;
					//
					// VSync: first char row (bright yellow) / second char row (dark yellow).
					//
					dl->AddRectFilled(ImVec2(lx,          lp.y), ImVec2(lx + sw / 2, lp.y + sh), IM_COL32(232, 200,   0, 255));
					dl->AddRectFilled(ImVec2(lx + sw / 2, lp.y), ImVec2(lx + sw,     lp.y + sh), IM_COL32( 88,  72,   0, 255));
					dl->AddText(ImVec2(lx + sw + 3.0f, lp.y + txtCenterY), IM_COL32(200, 200, 200, 255), "VSync (row1/row2)");
					lx += sw + 3.0f + ImGui::CalcTextSize("VSync (row1/row2)").x + gap + 8.0f;
					//
					// H+V overlap
					//
					dl->AddRectFilled(ImVec2(lx, lp.y), ImVec2(lx + sw, lp.y + sh), 0xFF804888u);
					dl->AddText(ImVec2(lx + sw + 3.0f, lp.y + txtCenterY), IM_COL32(200, 200, 200, 255), "H+VSync");
					ImGui::Dummy(ImVec2(avail.x, sh + 2.0f));
			return;
		}
		//
		// --- DESIGN MODE ---
		//
		//
		// Layout constants
		//
		const float headerH   = 28.0f;  // Increased to give space for column numbers above the grid
		const float cellH     = 3.0f;
		float availW = ImGui::GetContentRegionAvail().x;
		float cellW  = (availW - labelColW) / (float)maxCols;
		if (cellW < 1.0f)
			cellW = 1.0f;
		//
		// Colors
		//
		const ImU32 colHSyncPulse = IM_COL32(176,  64, 208, 255);  // bright purple -- HSync pulse start half
		const ImU32 colHSyncBack  = IM_COL32( 72,  24,  96, 255);  // dark purple   -- HSync pulse end half
		const ImU32 colVSyncPulse = IM_COL32(232, 200,   0, 255);  // bright yellow -- VSync pulse first row
		const ImU32 colVSyncBack  = IM_COL32( 88,  72,   0, 255);  // dark yellow   -- VSync pulse second row
		const ImU32 colHVOverlap  = IM_COL32(128,  72, 136, 255);  // dark mauve    -- HSync+VSync overlap
		const ImU32 colBorder     = IM_COL32( 42,  42,  42, 255);  // dark          -- blanking
		const ImU32 colInterrupt  = IM_COL32(220,  60,  60, 255);  // red    -- GA interrupt
		const ImU32 colHeader     = IM_COL32(160, 160, 160, 255);  // gray   -- labels
		const ImU32 colBoxTotal   = IM_COL32(255, 180,  40, 230);  // amber  -- htot/vtot total frame
		const ImU32 colBoxDisp    = IM_COL32(255, 255, 255, 220);  // white  -- hdisp/vdisp display area
		//
		// Per-slot checker palette: each slot gets a distinct hue so adjacent frames are
		// clearly distinguishable. ColA is the base shade, ColB the lighter checker square.
		//
		static constexpr int kSlotColorCount = 6;
		static const ImU32 slotBaseColors[kSlotColorCount] = {
			IM_COL32( 48,  98, 186, 255),  // blue
			IM_COL32( 48, 150,  80, 255),  // green
			IM_COL32(170,  80,  48, 255),  // orange
			IM_COL32(130,  48, 160, 255),  // purple
			IM_COL32( 48, 160, 160, 255),  // teal
			IM_COL32(160, 130,  48, 255),  // gold
		};
		static const ImU32 slotLightColors[kSlotColorCount] = {
			IM_COL32( 80, 130, 220, 255),
			IM_COL32( 80, 190, 110, 255),
			IM_COL32(210, 110,  70, 255),
			IM_COL32(170,  80, 200, 255),
			IM_COL32( 80, 200, 200, 255),
			IM_COL32(200, 170,  80, 255),
		};
		ImVec2 origin = ImGui::GetCursorScreenPos();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		//
		// Column header every 10 chars
		//
		for (int c = 0; c < maxCols; c += 10) {
			char buf[8];
			SDL_snprintf(buf, sizeof(buf), "%d", c);
			// Position column headers above the grid, in the header space
			dl->AddText(ImVec2(origin.x + labelColW + c * cellW, origin.y + 10.0f), colHeader, buf);
		}
		//
		// Grid rendering: per scan line, per column
		//
		for (int cr = 0; cr < maxCharRows; cr++) {
			float pyRow = origin.y + headerH + cr * scanPerChar * cellH;
			char buf[8];
			SDL_snprintf(buf, sizeof(buf), "%d", cr);
			// Right-align row numbers to the grid edge without overlapping
			dl->AddText(ImVec2(origin.x + labelColW - 24.0f, pyRow), colHeader, buf);
			for (int sl = cr * scanPerChar; sl < (cr + 1) * scanPerChar && sl < totalLines; sl++) {
				float py = origin.y + headerH + sl * cellH;
				//
					// Find owning slot, compute slot-relative scan line and slot index
					//
					const RetrodevLib::RasterFrameCmd* f = nullptr;
					int slRel    = sl;
					int slotIdx  = 0;
					for (int si = 0; si < (int)slots.size(); si++) {
						if (sl >= slots[si].startSl && sl < slots[si].endSl) {
							f       = &slots[si].state;
							slRel   = sl - slots[si].startSl;
							slotIdx = si;
							break;
						}
					}
				//
				// If no slot owns this line use blanking color for all columns
				//
				if (!f) {
					float px0 = origin.x + labelColW;
					float px1 = px0 + maxCols * cellW;
					dl->AddRectFilled(ImVec2(px0, py), ImVec2(px1, py + cellH - 1.0f), colBorder);
					continue;
				}
				//
					// Determine row-level state: display and VSync.
					// VSync spans exactly 2 char rows: row 0 = bright pulse, row 1 = dark pulse.
					// Everything else (border, display) is coloured at column level.
					//
					bool isDisplay    = RetrodevLib::CpcRaster::CrtcSimulator::IsDisplayScanLine(*f, slRel);
					bool isVSyncPulse = RetrodevLib::CpcRaster::CrtcSimulator::IsVSyncScanLine(*f, slRel);
					ImU32 colVSyncRow = colBorder;
					if (isVSyncPulse) {
						int vsyncStartSl;
						if (f->r7 == 0) {
							int totalFrameScanLines = (f->r4 + 1) * (f->r9 + 1) + f->r5;
							vsyncStartSl = totalFrameScanLines - 2 * (f->r9 + 1);
						} else {
							vsyncStartSl = f->r7 * (f->r9 + 1);
						}
						int charRowInVSync = (slRel - vsyncStartSl) / (f->r9 + 1);
						colVSyncRow = (charRowInVSync == 0) ? colVSyncPulse : colVSyncBack;
					}
					//
					// Checker colors for the owning slot.
					//
					ImU32 slotColA = slotBaseColors[slotIdx % kSlotColorCount];
					ImU32 slotColB = slotLightColors[slotIdx % kSlotColorCount];
					for (int c = 0; c < maxCols; c++) {
						float px = origin.x + labelColW + c * cellW;
						bool isHSyncPulse = RetrodevLib::CpcRaster::CrtcSimulator::IsHSyncChar(*f, c);
						ImU32 color;
						if (isHSyncPulse) {
							if (isVSyncPulse) {
								color = colHVOverlap;
							} else {
								//
								// Split HSync pulse in two halves: first half = bright, second = dark.
								//
								int htotal    = f->r0 + 1;
								int syncStart = (int)f->r2 % htotal;
								int hw        = f->r3 & 0x0F;
								int colInPulse = ((int)c - syncStart + htotal) % htotal;
								color = (hw < 2 || colInPulse < hw / 2) ? colHSyncPulse : colHSyncBack;
							}
						} else if (isVSyncPulse) {
							color = colVSyncRow;
						} else if (isDisplay) {
							color = ((c + (slRel / (f->r9 + 1))) & 1) ? slotColA : slotColB;
						} else {
							color = colBorder;
						}
						dl->AddRectFilled(ImVec2(px, py), ImVec2(px + cellW - 1.0f, py + cellH - 1.0f), color);
					}
			}
		}
		//
		// Per-slot: draw htot/vtot box (dim) and hdisp/vdisp box (bright)
		//
		for (const auto& slot : slots) {
			const RetrodevLib::RasterFrameCmd& f = slot.state;
			int scanPChr = f.r9 + 1;
			//
			// Total frame box: (R0+1) chars wide x (R4+1) char rows + R5 scan lines tall.
			// Drawn 1px inset on the right/bottom so the edge is visible even when the
			// box boundary coincides with the canvas edge (e.g. default R0=63).
			//
			float totalBoxX0 = origin.x + labelColW;
			float totalBoxY0 = origin.y + headerH + slot.startSl * cellH;
			float totalBoxX1 = origin.x + labelColW + (f.r0 + 1) * cellW - 1.0f;
			float totalBoxY1 = origin.y + headerH + (slot.startSl + RetrodevLib::CpcRaster::CrtcSimulator::TotalScanLines(f)) * cellH - 1.0f;
			dl->AddRect(ImVec2(totalBoxX0, totalBoxY0), ImVec2(totalBoxX1, totalBoxY1), colBoxTotal, 0.0f, 0, 2.0f);
			//
			// Label the total dimensions at the top-right corner of the box
			//
			{
				char tbuf[32];
				SDL_snprintf(tbuf, sizeof(tbuf), "H%d V%d", f.r0 + 1, RetrodevLib::CpcRaster::CrtcSimulator::TotalScanLines(f));
				dl->AddText(ImVec2(totalBoxX0 + 2.0f, totalBoxY0 + 1.0f), colBoxTotal, tbuf);
			}
			//
			// Display box: R1 chars wide x R6 char rows tall
			//
			float dispBoxX1 = origin.x + labelColW + f.r1 * cellW;
			float dispBoxY1 = origin.y + headerH + (slot.startSl + f.r6 * scanPChr) * cellH;
			dl->AddRect(ImVec2(totalBoxX0, totalBoxY0), ImVec2(dispBoxX1, dispBoxY1), colBoxDisp, 0.0f, 0, 2.0f);
		}
		//
		// Selected command: extra bright outline on its display box
		//
		if (selIdx >= 0 && selIdx < (int)cmds.size()) {
			for (const auto& slot : slots) {
				if (slot.cmdIdx != selIdx)
					continue;
				const RetrodevLib::RasterFrameCmd& f = slot.state;
				int scanPChr = f.r9 + 1;
				float py0 = origin.y + headerH + slot.startSl * cellH;
				float py1 = origin.y + headerH + (slot.startSl + f.r6 * scanPChr) * cellH;
				float px0 = origin.x + labelColW;
				float px1 = origin.x + labelColW + f.r1 * cellW;
				dl->AddRect(ImVec2(px0, py0), ImVec2(px1, py1), IM_COL32(255, 255, 100, 255), 0.0f, 0, 2.0f);
				break;
			}
		}
		//
		// Compute absolute interrupt lines treating all slots as one continuous block
		//
		{
			int accumLines = 0;
			for (const auto& slot : slots)
				accumLines += RetrodevLib::CpcRaster::CrtcSimulator::TotalScanLines(slot.state);
			int intNum = 1;
			// Interrupts start at scanline 51 (zero-indexed: line 52)
			for (int intSl = 51; intSl < accumLines; intSl += 52) {
				float py = origin.y + headerH + intSl * cellH;
				dl->AddLine(
					ImVec2(origin.x + labelColW, py),
					ImVec2(origin.x + labelColW + maxCols * cellW, py),
					colInterrupt, 2.0f);
				char buf[24];
				SDL_snprintf(buf, sizeof(buf), "INT%d  sl:%d", intNum, intSl + 1);
				// Position interrupt label in left margin area, well before the preview rectangle
				dl->AddText(ImVec2(origin.x + 5.0f, py - 8.0f), colInterrupt, buf);
				intNum++;
			}
		}
		//
		// Draw Frame boundary lines (separating slots in the design grid) and label each frame
		//
		{
			const ImU32 colFrameBoundary = IM_COL32(255, 200, 0, 200);  // amber
			// Label the first frame at the top
			if (!slots.empty()) {
				float py = origin.y + headerH + slots[0].startSl * cellH;
				const char* frameName = "Frame";
				if (slots[0].cmdIdx >= 0 && slots[0].cmdIdx < (int)cmds.size()) {
					if (std::holds_alternative<RetrodevLib::CpcFrameCommand>(cmds[slots[0].cmdIdx])) {
						frameName = std::get<RetrodevLib::CpcFrameCommand>(cmds[slots[0].cmdIdx]).name.c_str();
					}
				}
				// Position first frame label in left margin area, well before the preview rectangle
				dl->AddText(ImVec2(origin.x + 5.0f, py + 1.0f), colFrameBoundary, frameName);
			}
			// Label subsequent frame boundaries
			for (int si = 1; si < (int)slots.size(); si++) {
				float py = origin.y + headerH + slots[si].startSl * cellH;
				dl->AddLine(
					ImVec2(origin.x + labelColW, py),
					ImVec2(origin.x + labelColW + maxCols * cellW, py),
					colFrameBoundary, 1.0f);
				// Get the actual frame command name from the command list
				const char* frameName = "Frame";
				if (slots[si].cmdIdx >= 0 && slots[si].cmdIdx < (int)cmds.size()) {
					if (std::holds_alternative<RetrodevLib::CpcFrameCommand>(cmds[slots[si].cmdIdx])) {
						frameName = std::get<RetrodevLib::CpcFrameCommand>(cmds[slots[si].cmdIdx]).name.c_str();
					}
				}
				// Position frame label in left margin area, well before the preview rectangle
				dl->AddText(ImVec2(origin.x + 5.0f, py - 8.0f), colFrameBoundary, frameName);
			}
		}
		//
		// Draw Effect/Variable markers on the left ruler in design mode
		//
		{
			const ImU32 colEffectMarker = IM_COL32(100, 200, 255, 255);  // light blue
			const ImU32 colVariableMarker = IM_COL32(200, 100, 255, 255);  // light purple
			for (int ci = 0; ci < (int)cmds.size(); ci++) {
				if (std::holds_alternative<RetrodevLib::CpcFrameCommand>(cmds[ci]))
					continue;
				int targetSl = 0;
				ImU32 markerCol = colVariableMarker;
				if (std::holds_alternative<RetrodevLib::CpcEffectCommand>(cmds[ci])) {
					const auto& effectCmd = std::get<RetrodevLib::CpcEffectCommand>(cmds[ci]);
					targetSl = effectCmd.targetScanline;
					markerCol = colEffectMarker;
				} else if (std::holds_alternative<RetrodevLib::CpcVariableCommand>(cmds[ci])) {
					const auto& varCmd = std::get<RetrodevLib::CpcVariableCommand>(cmds[ci]);
					targetSl = varCmd.targetLine;
					markerCol = colVariableMarker;
				}
				if (targetSl >= totalLines)
					continue;
				float py = origin.y + headerH + targetSl * cellH;
				// Draw a small triangle/arrow pointing right
				float markerX = origin.x + labelColW - 10.0f;
				float markerY = py + cellH / 2.0f;
				dl->AddTriangleFilled(
					ImVec2(markerX - 3.0f, markerY - 3.0f),
					ImVec2(markerX - 3.0f, markerY + 3.0f),
					ImVec2(markerX + 3.0f, markerY),
					markerCol);
			}
		}
		//
		// Reserve canvas area
		//
		ImGui::Dummy(ImVec2(labelColW + maxCols * cellW + 1.0f, headerH + totalLines * cellH + 1.0f));
		//
		// Legend with clear vertical gap and proper alignment
		//
		ImGui::Spacing();  // Add vertical gap before legend
		{
			ImDrawList* ldl = ImGui::GetWindowDrawList();
			ImVec2 lp = ImGui::GetCursorScreenPos();
			const float sw = 12.0f;
			const float sh = 12.0f;
			const float gap = 8.0f;
			const float txtCenterY = sh / 2.0f - ImGui::CalcTextSize("X").y / 2.0f;  // Vertically center text
			float lx = lp.x;
			ldl->AddRectFilled(ImVec2(lx, lp.y), ImVec2(lx + sw / 2, lp.y + sh), IM_COL32( 48,  98, 186, 255));
			ldl->AddRectFilled(ImVec2(lx + sw / 2, lp.y), ImVec2(lx + sw, lp.y + sh), IM_COL32( 80, 130, 220, 255));
			ldl->AddText(ImVec2(lx + sw + 3.0f, lp.y + txtCenterY), IM_COL32(200, 200, 200, 255), "VMA");
			lx += sw + 3.0f + ImGui::CalcTextSize("VMA").x + gap;
			ldl->AddRectFilled(ImVec2(lx, lp.y), ImVec2(lx + sw, lp.y + sh), IM_COL32(42, 42, 42, 255));
			ldl->AddText(ImVec2(lx + sw + 3.0f, lp.y + txtCenterY), IM_COL32(200, 200, 200, 255), "Border");
			lx += sw + 3.0f + ImGui::CalcTextSize("Border").x + gap;
			ldl->AddRectFilled(ImVec2(lx, lp.y), ImVec2(lx + sw / 2, lp.y + sh), IM_COL32(176,  64, 208, 255));
			ldl->AddRectFilled(ImVec2(lx + sw / 2, lp.y), ImVec2(lx + sw, lp.y + sh), IM_COL32( 72,  24,  96, 255));
			ldl->AddText(ImVec2(lx + sw + 3.0f, lp.y + txtCenterY), IM_COL32(200, 200, 200, 255), "HSync (start/end)");
			lx += sw + 3.0f + ImGui::CalcTextSize("HSync (start/end)").x + gap;
			ldl->AddRectFilled(ImVec2(lx, lp.y), ImVec2(lx + sw / 2, lp.y + sh), IM_COL32(232, 200,   0, 255));
			ldl->AddRectFilled(ImVec2(lx + sw / 2, lp.y), ImVec2(lx + sw, lp.y + sh), IM_COL32( 88,  72,   0, 255));
			ldl->AddText(ImVec2(lx + sw + 3.0f, lp.y + txtCenterY), IM_COL32(200, 200, 200, 255), "VSync (row1/row2)");
			lx += sw + 3.0f + ImGui::CalcTextSize("VSync (row1/row2)").x + gap;
			ldl->AddRectFilled(ImVec2(lx, lp.y), ImVec2(lx + sw, lp.y + sh), IM_COL32(128, 72, 136, 255));
			ldl->AddText(ImVec2(lx + sw + 3.0f, lp.y + txtCenterY), IM_COL32(200, 200, 200, 255), "H+VSync");
			ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, sh + 2.0f));
		}
	}

	// ---------------------------------------------------------------------------
	// RenderCommandEditor -- command list + register editor + action buttons
	// ---------------------------------------------------------------------------

	void DocumentRasterCpc::RenderCommandEditor(RetrodevLib::RasterParams* params) {
		if (!params)
			return;
		//
		// Generator Settings section (always visible at top)
		//
		ImGui::SeparatorText("Generator Settings");
		if (!m_generatorFieldsInit) {
			SDL_snprintf(m_ruptureNameBuf, sizeof(m_ruptureNameBuf), "%s", m_cpcCrtc.GetCpcConfig().ruptureName.c_str());
			SDL_snprintf(m_outputPathBuf, sizeof(m_outputPathBuf), "%s", m_cpcCrtc.GetCpcConfig().outputAsmPath.c_str());
			m_generatorFieldsInit = true;
		}
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Rupture");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		if (ImGui::InputText("##ruptureName", m_ruptureNameBuf, sizeof(m_ruptureNameBuf))) {
			m_cpcCrtc.GetCpcConfig().ruptureName = m_ruptureNameBuf;
			NotifyHostModified();
			MarkDirty();
		}
		ImGui::AlignTextToFramePadding();
		ImGui::Text("ASM output");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		if (ImGui::InputText("##outputAsmPath", m_outputPathBuf, sizeof(m_outputPathBuf))) {
			m_cpcCrtc.GetCpcConfig().outputAsmPath = m_outputPathBuf;
			NotifyHostModified();
			MarkDirty();
		}
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Generator mode");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		int mode = m_cpcCrtc.GetCpcConfig().generatorMode;
		if (ImGui::Combo("##generatorMode", &mode, "Interrupt-driven\0Timed raster loop\0\0")) {
			m_cpcCrtc.GetCpcConfig().generatorMode = (uint8_t)mode;
			NotifyHostModified();
			MarkDirty();
		}

		ImGui::AlignTextToFramePadding();
		ImGui::Text("Palette type");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		//
		// Get palette type options from CPC constants
		//
		const std::vector<std::string>& paletteTypes = RetrodevLib::ConverterAmstradCPC::CPCPaletteTypesList;
		int paletteTypeIndex = 0;
		for (int i = 0; i < (int)paletteTypes.size(); i++) {
			if (paletteTypes[i] == params->targetPaletteType) {
				paletteTypeIndex = i;
				break;
			}
		}
		if (ImGui::Combo("##paletteType", &paletteTypeIndex, "GA Palette\0ASIC Palette\0\0")) {
			params->targetPaletteType = paletteTypes[paletteTypeIndex];
			//
			// Update the palette converter to use the new palette type
			//
			m_cpcPalette = RetrodevLib::Converters::GetPaletteConverter(
				params->targetSystem,
				params->targetMode,
				params->targetPaletteType
			);
			NotifyHostModified();
			MarkDirty();
		}

		// Timing calibration inputs
		ImGui::Spacing();
		ImGui::AlignTextToFramePadding();
		ImGui::Text("Init C0 (char pos after sync)");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		int initC0 = m_cpcCrtc.GetCpcConfig().initC0;
		if (ImGui::InputInt("##initC0", &initC0, 1, 5)) {
			if (initC0 < 0) initC0 = 0;
			if (initC0 > 127) initC0 = 127;
			m_cpcCrtc.GetCpcConfig().initC0 = (uint8_t)initC0;
			NotifyHostModified();
			MarkDirty();
		}

		ImGui::AlignTextToFramePadding();
		ImGui::Text("Init C4 (char row counter)");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		int initC4 = m_cpcCrtc.GetCpcConfig().initC4;
		if (ImGui::InputInt("##initC4", &initC4, 1, 5)) {
			if (initC4 < 0) initC4 = 0;
			if (initC4 > 127) initC4 = 127;
			m_cpcCrtc.GetCpcConfig().initC4 = (uint8_t)initC4;
			NotifyHostModified();
			MarkDirty();
		}

		ImGui::AlignTextToFramePadding();
		ImGui::Text("Init C9 (raster line counter)");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
		int initC9 = m_cpcCrtc.GetCpcConfig().initC9;
		if (ImGui::InputInt("##initC9", &initC9, 1, 5)) {
			if (initC9 < 0) initC9 = 0;
			if (initC9 > 15) initC9 = 15;
			m_cpcCrtc.GetCpcConfig().initC9 = (uint8_t)initC9;
			NotifyHostModified();
			MarkDirty();
		}

		if (m_generatorStatusVisible && m_generatorStatusBuf[0] != '\0') {
			ImGui::Spacing();
			ImGui::TextColored(m_generatorStatusColor, "%s", m_generatorStatusBuf);
		}
		//
		// Display per-slot NOP budget warnings extracted from last generation pass
		//
		if (!m_generatorWarnings.empty()) {
			ImGui::Spacing();
			for (const auto& w : m_generatorWarnings)
				ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.1f, 1.0f), ICON_ALERT "  %s", w.c_str());
		}
		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();
		//
		// Two-tab view: Commands | Source
		//
		if (ImGui::BeginTabBar("##CommandsTabs", ImGuiTabBarFlags_None)) {
			//
			// Tab 1: Commands (command list + register editor)
			//
			if (ImGui::BeginTabItem("Commands")) {
				ImGui::SeparatorText("Commands");
				if (ImGui::Button(ICON_PLUS " Frame")) {
					//
					// Add a new Frame command with default CRTC values
					//
					RetrodevLib::CpcFrameCommand frameCmd;
					frameCmd.name = FindUniqueName("Frame", m_cpcCrtc.GetCommands());
					frameCmd.frame = RetrodevLib::RasterFrameCmd();  // Initialize with CPC defaults
					RetrodevLib::CpcRasterCommand newCmd = frameCmd;
					m_cpcCrtc.GetCommands().push_back(newCmd);
					m_selectedCommandIndex = (int)m_cpcCrtc.GetCommands().size() - 1;
					NotifyHostModified();
					MarkDirty();
				}
				ImGui::SameLine();
				if (ImGui::Button(ICON_PLUS " Effect")) {
					//
					// Add a new Effect command (CRTC/GA register write at a specific scanline)
					//
					RetrodevLib::CpcEffectCommand effectCmd;
					effectCmd.name = FindUniqueName("Effect", m_cpcCrtc.GetCommands());
					effectCmd.targetScanline = 10;  // Default to scanline 10
					RetrodevLib::CpcRasterCommand newCmd = effectCmd;
					m_cpcCrtc.GetCommands().push_back(newCmd);
					m_selectedCommandIndex = (int)m_cpcCrtc.GetCommands().size() - 1;
					NotifyHostModified();
					MarkDirty();
				}
				ImGui::SameLine();
				if (ImGui::Button(ICON_PLUS " Variable")) {
					//
					// Add a new Variable command (write a byte to a named memory location)
					//
					RetrodevLib::CpcVariableCommand varCmd;
					varCmd.name = FindUniqueName("Variable", m_cpcCrtc.GetCommands());
					varCmd.targetLine = 20;  // Default to scanline 20
					// Extract number from name (format: "Variable N")
					size_t lastSpace = varCmd.name.rfind(' ');
					varCmd.variable.varName = "rupture_Var" + varCmd.name.substr(lastSpace);
					varCmd.variable.value = 1;
					RetrodevLib::CpcRasterCommand newCmd = varCmd;
					m_cpcCrtc.GetCommands().push_back(newCmd);
					m_selectedCommandIndex = (int)m_cpcCrtc.GetCommands().size() - 1;
					NotifyHostModified();
					MarkDirty();
				}
				ImGui::SameLine();
				if (ImGui::Button(ICON_PLUS " Subroutine")) {
					//
					// Add a new Subroutine command (call user-defined subroutine at a specific scanline)
					//
					RetrodevLib::CpcSubroutineCommand subCmd;
					subCmd.name = FindUniqueName("Subroutine", m_cpcCrtc.GetCommands());
					subCmd.targetLine = 52;  // Default to scanline 52 (slot 1)
					// Extract number from name (format: "Subroutine N")
					// Generator adds the prefix automatically, so only store the base name
					size_t lastSpace = subCmd.name.rfind(' ');
					subCmd.subroutineName = "Sub" + subCmd.name.substr(lastSpace);
					RetrodevLib::CpcRasterCommand newCmd = subCmd;
					m_cpcCrtc.GetCommands().push_back(newCmd);
					m_selectedCommandIndex = (int)m_cpcCrtc.GetCommands().size() - 1;
					NotifyHostModified();
					MarkDirty();
				}
				ImGui::SameLine();
		//
		// Remove selected command
		//
		bool hasSelection = (m_selectedCommandIndex >= 0 && m_selectedCommandIndex < (int)m_cpcCrtc.GetCommands().size());
		if (!hasSelection)
			ImGui::BeginDisabled();
		if (ImGui::Button(ICON_MINUS)) {
			m_cpcCrtc.GetCommands().erase(m_cpcCrtc.GetCommands().begin() + m_selectedCommandIndex);
			if (m_selectedCommandIndex >= (int)m_cpcCrtc.GetCommands().size())
				m_selectedCommandIndex = (int)m_cpcCrtc.GetCommands().size() - 1;
			NotifyHostModified();
			MarkDirty();
		}
		if (!hasSelection)
			ImGui::EndDisabled();
		ImGui::SameLine();
		//
		// Move selected command up
		//
		bool atTop = (m_selectedCommandIndex <= 0 || !hasSelection);
		if (atTop)
			ImGui::BeginDisabled();
		if (ImGui::Button(ICON_ARROW_UP)) {
			std::swap(m_cpcCrtc.GetCommands()[m_selectedCommandIndex], m_cpcCrtc.GetCommands()[m_selectedCommandIndex - 1]);
			m_selectedCommandIndex--;
			NotifyHostModified();
			MarkDirty();
		}
		if (atTop)
			ImGui::EndDisabled();
		ImGui::SameLine();
		//
		// Move selected command down
		//
		bool atBottom = (m_selectedCommandIndex >= (int)m_cpcCrtc.GetCommands().size() - 1 || !hasSelection);
		if (atBottom)
			ImGui::BeginDisabled();
		if (ImGui::Button(ICON_ARROW_DOWN)) {
			std::swap(m_cpcCrtc.GetCommands()[m_selectedCommandIndex], m_cpcCrtc.GetCommands()[m_selectedCommandIndex + 1]);
			m_selectedCommandIndex++;
			NotifyHostModified();
			MarkDirty();
		}
		if (atBottom)
			ImGui::EndDisabled();
		ImGui::SameLine();
		//
		// Export ASM button (write current cached generated code to disk)
		//
		if (ImGui::Button(ICON_DOWNLOAD " Export ASM")) {
			m_cpcCrtc.GetCpcConfig().ruptureName = m_ruptureNameBuf;
			m_cpcCrtc.GetCpcConfig().outputAsmPath = m_outputPathBuf;
			if (m_cpcCrtc.GetCpcConfig().outputAsmPath.empty()) {
				SDL_snprintf(m_generatorStatusBuf, sizeof(m_generatorStatusBuf), "%s", "No ASM output path set for export.");
				m_generatorStatusColor = ImVec4(1.0f, 0.75f, 0.2f, 1.0f);
				m_generatorStatusVisible = true;
			} else {
				// Use the cached generated code if available; otherwise regenerate
				std::string code = m_generatedAsm;
				if (code.empty()) {
					code = m_cpcCrtc.GenerateCode(&m_timingReport);
				}
				if (code.empty()) {
					SDL_snprintf(m_generatorStatusBuf, sizeof(m_generatorStatusBuf), "%s", "Export skipped: generated code is empty.");
					m_generatorStatusColor = ImVec4(1.0f, 0.75f, 0.2f, 1.0f);
					m_generatorStatusVisible = true;
				} else {
					std::filesystem::path outPath = m_cpcCrtc.GetCpcConfig().outputAsmPath;

					// Resolve relative to the project folder instead of the process CWD
					if (!m_projectFolder.empty()) {
						outPath = std::filesystem::path(m_projectFolder) / outPath;
					}

					std::error_code ec;
					if (outPath.has_parent_path())
						std::filesystem::create_directories(outPath.parent_path(), ec);
					std::ofstream file(outPath, std::ios::binary | std::ios::trunc);
					if (!file.good()) {
						SDL_snprintf(m_generatorStatusBuf, sizeof(m_generatorStatusBuf), "%s", "Export failed: could not open output file.");
						m_generatorStatusColor = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
						m_generatorStatusVisible = true;
					} else {
						file.write(code.data(), (std::streamsize)code.size());
						if (!file.good()) {
							SDL_snprintf(m_generatorStatusBuf, sizeof(m_generatorStatusBuf), "%s", "Export failed: write error.");
							m_generatorStatusColor = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
							m_generatorStatusVisible = true;
						} else {
							SDL_snprintf(m_generatorStatusBuf, sizeof(m_generatorStatusBuf), "Exported ASM to: %s", outPath.string().c_str());
							m_generatorStatusColor = ImVec4(0.2f, 0.9f, 0.2f, 1.0f);
							m_generatorStatusVisible = true;
						}
					}
				}
			}
		}
		//
		// Scrollable selectable command list
		//
		float listH = ImGui::GetContentRegionAvail().y * 0.35f;
		if (ImGui::BeginChild("##CmdList", ImVec2(-1, listH), true)) {
			for (int i = 0; i < (int)m_cpcCrtc.GetCommands().size(); i++) {
				RetrodevLib::CpcRasterCommand cmd = m_cpcCrtc.GetCommands()[i];
				bool selected = (m_selectedCommandIndex == i);
				std::string cmdName = RetrodevLib::GetCommandName(cmd);

				// Get enabled state and type icon
				bool enabled = true;
				const char* typeIcon = "?";
				ImVec4 iconColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
				if (std::holds_alternative<RetrodevLib::CpcFrameCommand>(cmd)) {
					enabled = std::get<RetrodevLib::CpcFrameCommand>(cmd).enabled;
					typeIcon = ICON_BOX;
					iconColor = ImVec4(0.6f, 0.8f, 1.0f, 1.0f);  // Light blue
				} else if (std::holds_alternative<RetrodevLib::CpcEffectCommand>(cmd)) {
					enabled = std::get<RetrodevLib::CpcEffectCommand>(cmd).enabled;
					typeIcon = ICON_ANIMATION;
					iconColor = ImVec4(1.0f, 0.8f, 0.4f, 1.0f);  // Orange
				} else if (std::holds_alternative<RetrodevLib::CpcVariableCommand>(cmd)) {
					enabled = std::get<RetrodevLib::CpcVariableCommand>(cmd).enabled;
					typeIcon = ICON_CLOCK;
					iconColor = ImVec4(0.8f, 1.0f, 0.6f, 1.0f);  // Light green
				} else if (std::holds_alternative<RetrodevLib::CpcSubroutineCommand>(cmd)) {
					enabled = std::get<RetrodevLib::CpcSubroutineCommand>(cmd).enabled;
					typeIcon = ICON_FUNCTION;
					iconColor = ImVec4(1.0f, 0.7f, 1.0f, 1.0f);  // Light purple
				}

				// Grey out disabled commands (save initial state before checkbox modifies it)
				bool wasDisabled = !enabled;
				if (wasDisabled)
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

				// Checkbox + type icon + name
				char cbId[40];
				SDL_snprintf(cbId, sizeof(cbId), "##en%d", i);
				if (ImGui::Checkbox(cbId, &enabled)) {
					if (std::holds_alternative<RetrodevLib::CpcFrameCommand>(cmd)) {
						std::get<RetrodevLib::CpcFrameCommand>(cmd).enabled = enabled;
						m_cpcCrtc.GetCommands()[i] = cmd;
					} else if (std::holds_alternative<RetrodevLib::CpcEffectCommand>(cmd)) {
						std::get<RetrodevLib::CpcEffectCommand>(cmd).enabled = enabled;
						m_cpcCrtc.GetCommands()[i] = cmd;
					} else if (std::holds_alternative<RetrodevLib::CpcVariableCommand>(cmd)) {
						std::get<RetrodevLib::CpcVariableCommand>(cmd).enabled = enabled;
						m_cpcCrtc.GetCommands()[i] = cmd;
					} else if (std::holds_alternative<RetrodevLib::CpcSubroutineCommand>(cmd)) {
						std::get<RetrodevLib::CpcSubroutineCommand>(cmd).enabled = enabled;
						m_cpcCrtc.GetCommands()[i] = cmd;
					}
					NotifyHostModified();
					MarkDirty();
				}
				ImGui::SameLine();
				ImGui::TextColored(iconColor, "%s", typeIcon);
				ImGui::SameLine();
				if (ImGui::Selectable((cmdName).c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
					m_selectedCommandIndex = i;

				if (wasDisabled)
					ImGui::PopStyleColor();

				if (selected)
					ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndChild();
		//
		// Register editor for selected command (below command list)
		//
		hasSelection = (m_selectedCommandIndex >= 0 && m_selectedCommandIndex < (int)m_cpcCrtc.GetCommands().size());
		if (!hasSelection) {
			ImGui::TextDisabled("No command selected.");
		} else {
					RetrodevLib::CpcRasterCommand cmd = m_cpcCrtc.GetCommands()[m_selectedCommandIndex];
					bool changed = false;
					bool labelChanged = false;  // Track label-only changes separately (don't trigger MarkDirty)
					//
					// Command name field
					//
					static char nameBuf[128] = {};
					static int lastEditedIdx = -1;
					std::string currentName = GetCommandName(cmd);
					if (lastEditedIdx != m_selectedCommandIndex) {
						memset(nameBuf, 0, sizeof(nameBuf));
						if (currentName.size() < sizeof(nameBuf))
							memcpy(nameBuf, currentName.c_str(), currentName.size());
						lastEditedIdx = m_selectedCommandIndex;
					}
					ImGui::AlignTextToFramePadding();
					ImGui::Text("Name");
					ImGui::SameLine();
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
					ImGui::InputText("##cmdName", nameBuf, sizeof(nameBuf));
					// Commit name on Enter or when field loses focus
					if (ImGui::IsItemDeactivatedAfterEdit() || (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
						if (currentName != std::string(nameBuf)) {
							if (std::holds_alternative<RetrodevLib::CpcFrameCommand>(cmd))
								std::get<RetrodevLib::CpcFrameCommand>(cmd).name = nameBuf;
							else if (std::holds_alternative<RetrodevLib::CpcEffectCommand>(cmd))
								std::get<RetrodevLib::CpcEffectCommand>(cmd).name = nameBuf;
							else if (std::holds_alternative<RetrodevLib::CpcVariableCommand>(cmd))
								std::get<RetrodevLib::CpcVariableCommand>(cmd).name = nameBuf;
							else if (std::holds_alternative<RetrodevLib::CpcSubroutineCommand>(cmd))
								std::get<RetrodevLib::CpcSubroutineCommand>(cmd).name = nameBuf;
							changed = true;
						}
					}
					//
					// CRTC register sliders (Frame command only)
					//
					if (std::holds_alternative<RetrodevLib::CpcFrameCommand>(cmd)) {
						auto& frameCmd = std::get<RetrodevLib::CpcFrameCommand>(cmd);
						RetrodevLib::RasterFrameCmd& fr = frameCmd.frame;
						ImGui::Spacing();
						//
						// Each register row: checkbox (active flag) + label + slider + SMC controls
						// The checkbox controls the corresponding bit in activeMask.
						// A disabled row still shows the current value but does not write it.
						//
						auto regRow = [&](const char* label, uint8_t& reg, int lo, int hi, uint32_t maskBit, int regNum) {
							bool active = (fr.activeMask & maskBit) != 0;
							char cbId[40];
							SDL_snprintf(cbId, sizeof(cbId), "##cb%s", label);
							if (ImGui::Checkbox(cbId, &active)) {
								if (active)
									fr.activeMask |= maskBit;
								else
									fr.activeMask &= ~maskBit;
								changed = true;
							}
							if (ImGui::IsItemHovered())
								ImGui::SetTooltip("Enable / disable writing this register in the generated code");
							ImGui::SameLine();
							if (!active)
								ImGui::BeginDisabled();
							ImGui::AlignTextToFramePadding();

							// Render label with fixed column width (280px for all registers to align)
							ImGui::Text("%s", label);
							ImGui::SameLine(280.0f);  // Fixed position for slider - ensures all sliders align vertically

							int v = reg;
							ImGui::SetNextItemWidth(255.0f);  // Fixed slider width for precise frame register adjustment
							char id[40];
							SDL_snprintf(id, sizeof(id), "##%s", label);
							ImGui::AlignTextToFramePadding();
							if (ImGui::SliderInt(id, &v, lo, hi)) {
								reg = (uint8_t)v;
								changed = true;
							}
							ImGui::SameLine();  // SMC checkbox right after slider
							ImGui::AlignTextToFramePadding();

							auto stateKey = std::make_pair(m_selectedCommandIndex, regNum);
							if (m_frameSmcCheckboxStates.find(stateKey) == m_frameSmcCheckboxStates.end()) {
								m_frameSmcCheckboxStates[stateKey] = (fr.smcMask & maskBit) != 0;
							}
							bool& smcEnabled = m_frameSmcCheckboxStates[stateKey];
							if (ImGui::Checkbox(("SMC##" + std::string(label)).c_str(), &smcEnabled)) {
								if (smcEnabled)
									fr.smcMask |= maskBit;
								else
									fr.smcMask &= ~maskBit;
								changed = true;
							}
							if (ImGui::IsItemHovered())
								ImGui::SetTooltip("Emit SMC patch label for runtime patching of this register");

							if (smcEnabled) {
								ImGui::SameLine();  // SMC label field right after checkbox
								ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 4.0f);  // Take remaining space
								auto labelBufferKey = std::make_pair(m_selectedCommandIndex, regNum);
								if (m_frameSmcLabelBuffers.find(labelBufferKey) == m_frameSmcLabelBuffers.end()) {
									m_frameSmcLabelBuffers[labelBufferKey] = {};
									auto it = fr.smcLabelOverrides.find(regNum);
									if (it != fr.smcLabelOverrides.end()) {
										SDL_snprintf(m_frameSmcLabelBuffers[labelBufferKey].data(), m_frameSmcLabelBuffers[labelBufferKey].size(), "%s", it->second.c_str());
									} else {
										SDL_snprintf(m_frameSmcLabelBuffers[labelBufferKey].data(), m_frameSmcLabelBuffers[labelBufferKey].size(), "rupture_%s_R%d_patch", frameCmd.name.c_str(), regNum);
									}
								}
								if (ImGui::InputText(("##smcLabel_" + std::to_string(regNum)).c_str(), m_frameSmcLabelBuffers[labelBufferKey].data(), m_frameSmcLabelBuffers[labelBufferKey].size())) {
									fr.smcLabelOverrides[regNum] = m_frameSmcLabelBuffers[labelBufferKey].data();
									labelChanged = true;  // Label changes don't affect code generation, only output labels
								}

								// Patch function checkbox: R5 only (Phase 1)
								if (regNum == 5) {
									ImGui::Spacing();
									ImGui::AlignTextToFramePadding();
									auto patchStateKey = std::make_pair(m_selectedCommandIndex, regNum);
									if (m_framePatchFunctionCheckboxStates.find(patchStateKey) == m_framePatchFunctionCheckboxStates.end()) {
										m_framePatchFunctionCheckboxStates[patchStateKey] = (fr.smcPatchFunctionMask & maskBit) != 0;
									}
									bool& patchFuncEnabled = m_framePatchFunctionCheckboxStates[patchStateKey];
									if (ImGui::Checkbox(("Patch function##" + std::string(label)).c_str(), &patchFuncEnabled)) {
										if (patchFuncEnabled)
											fr.smcPatchFunctionMask |= maskBit;
										else
											fr.smcPatchFunctionMask &= ~maskBit;
										changed = true;
									}
									if (ImGui::IsItemHovered())
										ImGui::SetTooltip("Generate a callable Z80 patch function that atomically updates both the register\nand all dependent wait instructions when the frame boundary changes.");
								}
							}
							if (!active)
								ImGui::EndDisabled();
						};
						using F = RetrodevLib::RasterFrameCmd;
							regRow("R0  Horiz Total       [0..255]", fr.r0, 0, 255, F::MASK_R0, 0);
							regRow("R1  Horiz Displayed   [0..255]", fr.r1, 0, 255, F::MASK_R1, 1);
							regRow("R2  HSync Position    [0..255]", fr.r2, 0, 255, F::MASK_R2, 2);
							regRow("R3  Sync Widths       [0..255]", fr.r3, 0, 255, F::MASK_R3, 3);
							regRow("R4  Vert Total        [0..127]", fr.r4, 0, 127, F::MASK_R4, 4);
							regRow("R5  Vert Adjust       [0..31] ", fr.r5, 0,  31, F::MASK_R5, 5);
							regRow("R6  Vert Displayed    [0..127]", fr.r6, 0, 127, F::MASK_R6, 6);
							//
							// R7: disabled and greyed out when disableVSync is active
							//
							if (fr.disableVSync)
								ImGui::BeginDisabled();
							regRow("R7  VSync Position    [0..127]", fr.r7, 0, 127, F::MASK_R7, 7);
							if (fr.disableVSync)
								ImGui::EndDisabled();
							regRow("R9  Max Scan Line     [0..31] ", fr.r9, 0,  31, F::MASK_R9, 9);
							//
							// Disable VSync flag: when enabled, R7 is set to 255 in the generated code,
							// suppressing the VSync signal so this frame's lines accumulate into the next.
							//
							ImGui::Spacing();
							ImGui::Separator();
							{
								bool noVSync = fr.disableVSync;
								if (ImGui::Checkbox("##cbNoVSync", &noVSync)) {
									fr.disableVSync = noVSync;
									changed = true;
								}
								if (ImGui::IsItemHovered())
									ImGui::SetTooltip("When enabled, VSync is suppressed (R7=255 in generated code).\nThis frame's scan lines accumulate with the next frame(s) toward the 312-line total.\nUse for multi-frame split-screen or overscan effects.");
								ImGui::SameLine();
								ImGui::AlignTextToFramePadding();
								ImGui::Text("Disable VSync (R7=255)");
								ImGui::SameLine();
								ImGui::TextDisabled(ICON_INFORMATION_OUTLINE);
								if (ImGui::IsItemHovered())
									ImGui::SetTooltip("Suppresses VSync for this frame so multiple Frame commands can split\nthe 312-line total across the screen. The first Frame with VSync enabled\ncloses the group and is validated against the accumulated line count.");
							}
							//
							// R12/R13 VMA change flag: no stored value -- the generated code will
							// reference a game-supplied symbol for the actual screen base address.
							//
						ImGui::Spacing();
						ImGui::Separator();
						{
							// Use state map for VMA enable checkbox persistence (same pattern as other checkboxes)
							// Always sync with actual state when frame is selected (don't just initialize once)
							m_vmaEnableStates[m_selectedCommandIndex] = (fr.activeMask & F::MASK_R12) != 0;
							bool& vmaChange = m_vmaEnableStates[m_selectedCommandIndex];
							if (ImGui::Checkbox("##cbVMA", &vmaChange)) {
								if (vmaChange)
									fr.activeMask |= F::MASK_R12;
								else
									fr.activeMask &= ~F::MASK_R12;
								changed = true;
							}
							if (ImGui::IsItemHovered())
								ImGui::SetTooltip("When enabled, generated code will write R12/R13 to change the screen base address.\nThe actual address is supplied as a symbol by the application (e.g. Screen_Base).");
							ImGui::SameLine();
							ImGui::AlignTextToFramePadding();
							ImGui::Text("R12/R13  Change VMA address");
							ImGui::SameLine();
							ImGui::TextDisabled(ICON_INFORMATION_OUTLINE);
							if (ImGui::IsItemHovered())
								ImGui::SetTooltip("Tick this if this frame must redirect the CRTC VMA pointer to a different screen buffer.\nNo address value is stored here -- code generation emits a symbolic reference\nthat you define in your application (e.g. ScreenBuffer0, ScreenBuffer1).");

							// SMC controls for R12/R13 - use state map for persistence (same pattern as other registers)
							ImGui::SameLine();
							ImGui::AlignTextToFramePadding();
							auto vmaStateKey = std::make_pair(m_selectedCommandIndex, 12);  // 12 = R12/R13 key
							if (m_vmaSmcCheckboxStates.find(vmaStateKey) == m_vmaSmcCheckboxStates.end()) {
								m_vmaSmcCheckboxStates[vmaStateKey] = (fr.smcMask & F::MASK_R12) != 0;
							}
							bool& vmaSMCEnabled = m_vmaSmcCheckboxStates[vmaStateKey];
							if (ImGui::Checkbox("SMC##R12R13", &vmaSMCEnabled)) {
								if (vmaSMCEnabled)
									fr.smcMask |= F::MASK_R12;
								else
									fr.smcMask &= ~F::MASK_R12;
								changed = true;
							}
							if (ImGui::IsItemHovered())
								ImGui::SetTooltip("Emit SMC patch labels for runtime patching of R12/R13 VMA address");

							// SMC label override field (shown only when SMC is enabled) - use state map value, NOT data model
							if (vmaSMCEnabled) {
								ImGui::SameLine();
								ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 4.0f);
								auto vmaLabelKey = std::make_pair(m_selectedCommandIndex, 12);  // Use 12 as key for R12/R13 pair, keyed by frame
								if (m_frameSmcLabelBuffers.find(vmaLabelKey) == m_frameSmcLabelBuffers.end()) {
									m_frameSmcLabelBuffers[vmaLabelKey] = {};
									auto it = fr.smcLabelOverrides.find(12);
									if (it != fr.smcLabelOverrides.end()) {
										SDL_snprintf(m_frameSmcLabelBuffers[vmaLabelKey].data(), m_frameSmcLabelBuffers[vmaLabelKey].size(), "%s", it->second.c_str());
									} else {
										SDL_snprintf(m_frameSmcLabelBuffers[vmaLabelKey].data(), m_frameSmcLabelBuffers[vmaLabelKey].size(), "rupture_%s_R12R13_patch", frameCmd.name.c_str());
									}
								}
								if (ImGui::InputText("##smcR12R13Label", m_frameSmcLabelBuffers[vmaLabelKey].data(), m_frameSmcLabelBuffers[vmaLabelKey].size())) {
									fr.smcLabelOverrides[12] = m_frameSmcLabelBuffers[vmaLabelKey].data();
									labelChanged = true;  // Label changes don't affect code generation, only output labels
								}
								if (ImGui::IsItemHovered())
									ImGui::SetTooltip("Custom label name for R12/R13 SMC patch point (high byte at +2, low byte at +3)");
							}
						}
						//
						// Derived info (read-only)
						//
						ImGui::Spacing();
						int total = RetrodevLib::CpcRaster::CrtcSimulator::TotalScanLines(fr);
						ImGui::TextDisabled("Total scan lines: %d  |  HSync width: %d  |  VSync width: %d",
							total, fr.r3 & 0x0F, (fr.r3 >> 4) & 0x0F);
					}
					//
					// Effect command editor
					//
					else if (std::holds_alternative<RetrodevLib::CpcEffectCommand>(cmd)) {
						auto& effectCmd = std::get<RetrodevLib::CpcEffectCommand>(cmd);
						ImGui::Spacing();
						//
						// Target line mode selector
						//
						ImGui::Text("Target line mode:");
						ImGui::SameLine();
						ImGui::SetNextItemWidth(200.0f);
						const char* modeLabels[] = { "Absolute", "Relative" };
						if (m_effectModeStates.find(m_selectedCommandIndex) == m_effectModeStates.end()) {
							m_effectModeStates[m_selectedCommandIndex] = (int)effectCmd.targetMode;
						}
						int& modeIdx = m_effectModeStates[m_selectedCommandIndex];
						if (ImGui::Combo(("##targetMode_" + std::to_string(m_selectedCommandIndex)).c_str(), &modeIdx, modeLabels, 2)) {
							effectCmd.targetMode = (RetrodevLib::EffectTargetMode)modeIdx;
							changed = true;
						}

						ImGui::Spacing();
						if (modeIdx == 0) {  // 0 = Absolute
							ImGui::Text("Target scanline:");
							ImGui::SameLine();
							ImGui::SetNextItemWidth(120.0f);
							int targetScanline = effectCmd.targetScanline;
							if (ImGui::InputInt("##targetScanline_abs", &targetScanline, 1, 10)) {
								targetScanline = std::max(0, std::min(311, targetScanline));
								effectCmd.targetScanline = targetScanline;
								changed = true;
							}
							ImGui::TextDisabled("Range: 0..311 (frame scanlines)");
						} else {  // 1 = Relative
							ImGui::Text("Offset from previous Effect:");
							ImGui::SameLine();
							ImGui::SetNextItemWidth(120.0f);
							int offset = effectCmd.targetScanline;
							if (ImGui::InputInt("##targetScanline_rel", &offset, 1, 5)) {
								offset = std::max(0, offset);
								effectCmd.targetScanline = offset;
								changed = true;
							}
							ImGui::TextDisabled("Lines after previous (0=same line, 1=next line, 6=6 lines later, etc.)");
							//
							// Calculate resolved scanline for relative mode by recursively resolving previous effect
							//
							int resolvedScanline = ResolveEffectScanline(m_cpcCrtc.GetCommands(), m_selectedCommandIndex);
							ImGui::TextDisabled("Resolved scanline: ~%d", resolvedScanline);
							ImGui::TextDisabled("(First Effect defaults to Absolute mode at scanline %d)", effectCmd.targetScanline);
						}

						ImGui::Spacing();
						ImGui::Separator();
						ImGui::Text("Register writes: (%zu total, ~%d NOPs estimated)",
							effectCmd.writes.size(),
							(int)(effectCmd.writes.size() * 14));  // Conservative estimate: 14 NOPs per CRTC, 7 for GA

						ImGui::Spacing();
						//
						// Write list editor
						//
						std::vector<int> removeIndices;
						if (ImGui::BeginTable("##EffectWrites", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
							ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, 80.0f);
							ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthFixed, 160.0f);
							ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 220.0f);
							ImGui::TableSetupColumn("SMC", ImGuiTableColumnFlags_WidthStretch);
							ImGui::TableSetupColumn("##Remove", ImGuiTableColumnFlags_WidthFixed, 35.0f);
							ImGui::TableHeadersRow();
							for (size_t i = 0; i < effectCmd.writes.size(); ++i) {
								auto& write = effectCmd.writes[i];
								ImGui::TableNextRow();

								// Column 1: Target (GA / CRTC)
								ImGui::TableSetColumnIndex(0);
								const char* targetLabels[] = { "CRTC", "GA" };
								int targetIdx = (write.target == RetrodevLib::CpcRegTarget::GA) ? 1 : 0;
								ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
								if (ImGui::Combo(("##target_" + std::to_string(i)).c_str(), &targetIdx, targetLabels, 2)) {
									write.target = (targetIdx == 1) ? RetrodevLib::CpcRegTarget::GA : RetrodevLib::CpcRegTarget::CRTC;
									write.reg = 0;  // Reset register to first valid option
									changed = true;
								}

								// Column 2: Command (depends on target type)
								ImGui::TableSetColumnIndex(1);
								ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

								if (write.target == RetrodevLib::CpcRegTarget::GA) {
									const char* gaCommands[] = { "GA Set Ink", "GA Set Border", "GA Set Mode" };
									int gaCmd = write.reg;  // GA uses reg field to store command type
									if (ImGui::Combo(("##gaCmd_" + std::to_string(i)).c_str(), &gaCmd, gaCommands, 3)) {
										write.reg = gaCmd;
										write.value = 0;  // Reset value
										changed = true;
									}
								} else {
									// CRTC: show register name
									const char* crtcRegs[] = {
										"R0 Horiz Total", "R1 Horiz Displayed", "R2 HSync Position", "R3 Sync Widths",
										"R4 Vert Total", "R5 Vert Adjust", "R6 Vert Displayed", "R7 VSync Position",
										"R9 Max Scan Line", "R12 VMA High", "R13 VMA Low"
									};
									int crtcReg = write.reg;
									if (ImGui::Combo(("##crtcReg_" + std::to_string(i)).c_str(), &crtcReg, crtcRegs, 11)) {
										write.reg = crtcReg;
										changed = true;
									}
								}

								// Column 3: Value control
								ImGui::TableSetColumnIndex(2);
								ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

								if (write.target == RetrodevLib::CpcRegTarget::GA) {
									if (write.reg == 0) {
										// GA Set Ink: pen (0-15) + color palette (0-26)
										ImGui::AlignTextToFramePadding();
										ImGui::Text("Pen:");
										ImGui::SameLine();
										int pen = (write.value >> 4) & 0x0F;
										ImGui::SetNextItemWidth(60.0f);
										const char* penLabels[] = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15" };
										if (ImGui::Combo(("##gaPen_" + std::to_string(i)).c_str(), &pen, penLabels, 16)) {
											write.value = (write.value & 0x0F) | ((pen & 0x0F) << 4);
											changed = true;
										}
										ImGui::SameLine();
										ImGui::AlignTextToFramePadding();
										ImGui::Text("Color:");
										ImGui::SameLine();
										//
										// Color picker button showing selected color
										// Position button vertically centered in the frame
										//
										ImVec2 cursorPos = ImGui::GetCursorPos();
										float frameHeight = ImGui::GetFrameHeight();
										float buttonHeight = 16.0f;
										float offsetY = (frameHeight - buttonHeight) / 2.0f;
										ImGui::SetCursorPos(ImVec2(cursorPos.x, cursorPos.y + offsetY));
										int colorIndex = write.value & 0x0F;
										if (m_cpcPalette) {
											RetrodevLib::RgbColor sysColor = m_cpcPalette->GetSystemColorByIndex(colorIndex);
											ImVec4 selectedColor = ImVec4(sysColor.r / 255.0f, sysColor.g / 255.0f, sysColor.b / 255.0f, 1.0f);
											ImGui::PushStyleColor(ImGuiCol_Button, selectedColor);
											ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(selectedColor.x + 0.2f, selectedColor.y + 0.2f, selectedColor.z + 0.2f, 1.0f));
											if (ImGui::Button(("##inkColorButton_" + std::to_string(i)).c_str(), ImVec2(20, 16))) {
												m_inkColorPicker.Open();
											}
											ImGui::PopStyleColor(2);
											//
											// Color picker popup (call every frame)
											//
											int selectedInkColor = colorIndex;
											if (m_inkColorPicker.RenderPickerPopup(m_cpcPalette, selectedInkColor,
												("inkColorPicker_" + std::to_string(i)).c_str(),
												"Select Ink Color")) {
												write.value = (write.value & 0xF0) | (selectedInkColor & 0x0F);
												changed = true;
											}
										}
									} else if (write.reg == 1) {
										// GA Set Border: color palette (0-26)
										ImGui::AlignTextToFramePadding();
										ImGui::Text("Color picker:");
										ImGui::SameLine();
										//
										// Color picker button showing selected color
										// Position button vertically centered in the frame
										//
										ImVec2 cursorPos = ImGui::GetCursorPos();
										float frameHeight = ImGui::GetFrameHeight();
										float buttonHeight = 20.0f;
										float offsetY = (frameHeight - buttonHeight) / 2.0f;
										ImGui::SetCursorPos(ImVec2(cursorPos.x, cursorPos.y + offsetY));
										int colorIndex = write.value & 0x1F;
										if (colorIndex >= 27) colorIndex = 26;  // Clamp to valid range
										if (m_cpcPalette) {
											RetrodevLib::RgbColor sysColor = m_cpcPalette->GetSystemColorByIndex(colorIndex);
											ImVec4 selectedColor = ImVec4(sysColor.r / 255.0f, sysColor.g / 255.0f, sysColor.b / 255.0f, 1.0f);
											ImGui::PushStyleColor(ImGuiCol_Button, selectedColor);
											ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(selectedColor.x + 0.2f, selectedColor.y + 0.2f, selectedColor.z + 0.2f, 1.0f));
											if (ImGui::Button(("##borderColorButton_" + std::to_string(i)).c_str(), ImVec2(24, 20))) {
												m_borderColorPicker.Open();
											}
											ImGui::PopStyleColor(2);
											//
											// Color picker popup (call every frame)
											//
											int selectedBorderColor = colorIndex;
											if (m_borderColorPicker.RenderPickerPopup(m_cpcPalette, selectedBorderColor,
												("borderColorPicker_" + std::to_string(i)).c_str(),
												"Select Border Color")) {
												write.value = selectedBorderColor & 0x1F;
												changed = true;
											}
										}
									} else if (write.reg == 2) {
										// GA Set Mode: mode 0/1/2 + ROM disable
										ImGui::Text("Mode:");
										ImGui::SameLine();
										int mode = write.value & 0x03;
										ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
										if (ImGui::SliderInt(("##gaMode_" + std::to_string(i)).c_str(), &mode, 0, 2)) {
											write.value = (mode & 0x03) | 0xFC;  // Always disable ROMs
											changed = true;
										}
										ImGui::TextDisabled("ROMs always disabled (0x80 port: 0x8C/0x8D/0x8E)");
									}
								} else {
									// CRTC: value input (0-255)
									int val = write.value;
									if (ImGui::InputInt(("##crtcVal_" + std::to_string(i)).c_str(), &val, 1, 10)) {
										val = std::max(0, std::min(255, val));
										write.value = (uint8_t)val;
										changed = true;
									}
								}

								// Column 4: SMC patch point controls (now uses remaining space)
								ImGui::TableSetColumnIndex(3);
								auto stateKey = std::make_pair(m_selectedCommandIndex, i);
								if (m_effectSmcCheckboxStates.find(stateKey) == m_effectSmcCheckboxStates.end()) {
									m_effectSmcCheckboxStates[stateKey] = write.smcPatch;
								}
								bool& smcCheckboxState = m_effectSmcCheckboxStates[stateKey];
								if (ImGui::Checkbox(("##smc_" + std::to_string(m_selectedCommandIndex) + "_" + std::to_string(i)).c_str(), &smcCheckboxState)) {
									write.smcPatch = smcCheckboxState;
									changed = true;
								}
								if (ImGui::IsItemHovered())
									ImGui::SetTooltip("Emit SMC patch label for runtime patching");
								if (smcCheckboxState) {
									ImGui::SameLine();
									ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 40.0f);
									static std::map<int, std::array<char, 128>> m_effectSmcLabelBuffers;
									if (m_effectSmcLabelBuffers.find(i) == m_effectSmcLabelBuffers.end()) {
										m_effectSmcLabelBuffers[i] = {};
										if (!write.smcLabelOverride.empty()) {
											SDL_snprintf(m_effectSmcLabelBuffers[i].data(), m_effectSmcLabelBuffers[i].size(), "%s", write.smcLabelOverride.c_str());
										} else {
											std::string defaultLabel = "rupture_" + effectCmd.name + "_write" + std::to_string(i) + "_patch";
											SDL_snprintf(m_effectSmcLabelBuffers[i].data(), m_effectSmcLabelBuffers[i].size(), "%s", defaultLabel.c_str());
										}
									}
									if (ImGui::InputText(("##smcLabel_" + std::to_string(i)).c_str(), m_effectSmcLabelBuffers[i].data(), m_effectSmcLabelBuffers[i].size())) {
										write.smcLabelOverride = m_effectSmcLabelBuffers[i].data();
										// Label changes don't affect code generation, only output labels - don't set changed
									}
								}

								// Column 5: Remove button (small "-" button)
								ImGui::TableSetColumnIndex(4);
								if (ImGui::Button(("-##rem_" + std::to_string(i)).c_str(), ImVec2(-1, 0))) {
									removeIndices.push_back(i);
									changed = true;
								}
							}

							ImGui::EndTable();
						}

						// Remove marked writes in reverse order to avoid index shifting
						for (int idx = (int)removeIndices.size() - 1; idx >= 0; --idx) {
							effectCmd.writes.erase(effectCmd.writes.begin() + removeIndices[idx]);
						}

						// Add new write button
						ImGui::Spacing();
						if (ImGui::Button("+ Add Write", ImVec2(-1, 0))) {
							RetrodevLib::CpcRegWrite newWrite;
							newWrite.target = RetrodevLib::CpcRegTarget::GA;
							newWrite.reg = 0;
							newWrite.value = 0;
							newWrite.smcPatch = false;
							newWrite.smcLabelOverride = "";
							effectCmd.writes.push_back(newWrite);
							changed = true;
						}
					}
					//
					// Variable command editor
					//
					else if (std::holds_alternative<RetrodevLib::CpcVariableCommand>(cmd)) {
						auto& varCmd = std::get<RetrodevLib::CpcVariableCommand>(cmd);
						ImGui::Spacing();
						ImGui::Text("Target scanline:");
						ImGui::SameLine();
						ImGui::SetNextItemWidth(120.0f);
						int targetLine = varCmd.targetLine;
						if (ImGui::InputInt("##targetLine_var", &targetLine, 1, 10)) {
							targetLine = std::max(0, std::min(311, targetLine));
							varCmd.targetLine = targetLine;
							changed = true;
						}
						ImGui::TextDisabled("Range: 0..311 (frame scanlines)");

						ImGui::Spacing();
						if (m_varUnrestrainedStates.find(m_selectedCommandIndex) == m_varUnrestrainedStates.end()) {
							m_varUnrestrainedStates[m_selectedCommandIndex] = varCmd.unrestrained;
						}
						bool& unrestrained = m_varUnrestrainedStates[m_selectedCommandIndex];
						if (ImGui::Checkbox("Unrestrained placement", &unrestrained)) {
							varCmd.unrestrained = unrestrained;
							changed = true;
						}
						if (unrestrained) {
							ImGui::TextDisabled("Place at first available slot position (no explicit waits, saves NOPs)");
						} else {
							ImGui::TextDisabled("Place exactly at target scanline with necessary wait instructions");
						}

						ImGui::Spacing();
						ImGui::Text("Variable name:");
						ImGui::SameLine();
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
						static char varNameBuf[64] = {};
						static int lastVarEditIdx = -1;
						static std::string lastVarEditName;
						// Reset buffer if index changed OR if the command name changed (detects deletion/re-add at same index)
						if (lastVarEditIdx != m_selectedCommandIndex || lastVarEditName != varCmd.name) {
							memset(varNameBuf, 0, sizeof(varNameBuf));
							if (varCmd.variable.varName.size() < sizeof(varNameBuf))
								memcpy(varNameBuf, varCmd.variable.varName.c_str(), varCmd.variable.varName.size());
							lastVarEditIdx = m_selectedCommandIndex;
							lastVarEditName = varCmd.name;
						}
						if (ImGui::InputText("##varName", varNameBuf, sizeof(varNameBuf))) {
							varCmd.variable.varName = varNameBuf;
							changed = true;
						} else if (ImGui::IsItemDeactivated()) {
							if (varCmd.variable.varName != std::string(varNameBuf)) {
								varCmd.variable.varName = varNameBuf;
								changed = true;
							}
						}
						ImGui::Text("Value:");
						ImGui::SameLine();
						ImGui::SetNextItemWidth(120.0f);
						int varValue = varCmd.variable.value;
						if (ImGui::InputInt("##varValue", &varValue, 1, 10)) {
							varValue = std::max(0, std::min(255, varValue));
							varCmd.variable.value = (uint8_t)varValue;
							changed = true;
						}
						ImGui::TextDisabled("Value written to memory at this scanline (0..255)");
					}
					//
					// Subroutine command editor
					//
					else if (std::holds_alternative<RetrodevLib::CpcSubroutineCommand>(cmd)) {
						auto& subCmd = std::get<RetrodevLib::CpcSubroutineCommand>(cmd);
						ImGui::Spacing();
						ImGui::Text("Target scanline:");
						ImGui::SameLine();
						ImGui::SetNextItemWidth(100.0f);
						int targetLine = subCmd.targetLine;
						if (ImGui::SliderInt("##targetLine_sub", &targetLine, 0, 311)) {
							subCmd.targetLine = targetLine;
							changed = true;
						}
						int slotNum = targetLine / 52;
						ImGui::TextDisabled("Slot %d, scanlines %d..%d", slotNum, slotNum * 52, (slotNum + 1) * 52 - 1);

						ImGui::Spacing();
						ImGui::Text("Subroutine name:");
						ImGui::SameLine();
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
						static char subNameBuf[64] = {};
						static int lastSubEditIdx = -1;
						if (lastSubEditIdx != m_selectedCommandIndex) {
							memset(subNameBuf, 0, sizeof(subNameBuf));
							if (subCmd.subroutineName.size() < sizeof(subNameBuf))
								memcpy(subNameBuf, subCmd.subroutineName.c_str(), subCmd.subroutineName.size());
							lastSubEditIdx = m_selectedCommandIndex;
						}
						ImGui::InputText("##subName", subNameBuf, sizeof(subNameBuf));
						if (ImGui::IsItemDeactivatedAfterEdit() || (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
							if (subCmd.subroutineName != std::string(subNameBuf)) {
								subCmd.subroutineName = subNameBuf;
								changed = true;
							}
						}
						ImGui::TextDisabled("Expected label: rupture_%s", subNameBuf);
						ImGui::TextDisabled("Define this routine in your source file. Called at end of slot (cost: ~5 NOPs)");
					}
					//
					// Write back if any register changed
					// Note: labelChanged tracks SMC label overrides which don't affect code generation,
					// so we write them back but only call MarkDirty if actual parameters changed.
					//
					if (changed || labelChanged) {
						m_cpcCrtc.GetCommands()[m_selectedCommandIndex] = cmd;
						NotifyHostModified();
						if (changed)  // Only trigger code generation if parameters changed, not just labels
							MarkDirty();
					}
				}
				ImGui::EndTabItem();
			}
			//
			// Tab 2: Source (read-only generated ASM code with syntax highlighting)
			//
			if (ImGui::BeginTabItem("Source")) {
				// Initialize editor on first use
				static bool sourceEditorInit = false;
				if (!sourceEditorInit) {
					m_sourceEditor.SetLanguageDefinition(GetRasterAsmLanguage());
					m_sourceEditor.SetReadOnlyEnabled(true);
					sourceEditorInit = true;
				}

				// Update editor content if ASM was regenerated
				if (!m_generatedAsm.empty() && m_sourceEditor.GetText() != m_generatedAsm) {
					m_sourceEditor.SetText(m_generatedAsm);
				}

				// Render editor or placeholder
				if (!m_generatedAsm.empty()) {
					m_sourceEditor.Render("##SourceEditor", true, ImVec2(-1, -1), true);
				} else {
					ImGui::TextDisabled("No code generated yet. Add Frame commands and modify parameters to auto-generate code.");
				}

				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}

	// ---------------------------------------------------------------------------
	// RenderViolationsPanel -- always-on validation results below the main split
	// ---------------------------------------------------------------------------

	void DocumentRasterCpc::RenderViolationsPanel() {
		//
		// Header row: status icon + summary text
		//
		bool hasErrors = false;
		for (const auto& e : m_validationResult.entries) {
			if (e.severity == RetrodevLib::CpcValidationSeverity::Error)
				hasErrors = true;
		}
		if (m_validationResult.entries.empty()) {
			ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), ICON_CHECK_CIRCLE " No violations -- CRTC register set is valid.");
			return;
		}
		//
		// Summary line
		//
		char summary[128];
		int errCount  = 0;
		int warnCount = 0;
		for (const auto& e : m_validationResult.entries) {
			if (e.severity == RetrodevLib::CpcValidationSeverity::Error)
				errCount++;
			else
				warnCount++;
		}
		SDL_snprintf(summary, sizeof(summary), "%d error(s)  %d warning(s)", errCount, warnCount);
		if (hasErrors)
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), ICON_ALERT_CIRCLE "  %s", summary);
		else
			ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.0f, 1.0f), ICON_ALERT "  %s", summary);
		ImGui::Separator();
		//
		// Scrollable violations list
		//
		for (const auto& e : m_validationResult.entries) {
			if (e.severity == RetrodevLib::CpcValidationSeverity::Error)
				ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), ICON_ALERT_CIRCLE "  %s", e.message.c_str());
			else
				ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.15f, 1.0f), ICON_ALERT "  %s", e.message.c_str());
		}
	}

	// ---------------------------------------------------------------------------
	// Auto-generation helpers
	// ---------------------------------------------------------------------------

	void DocumentRasterCpc::SetParent(class DocumentRaster* parent) {
		m_parent = parent;
	}

	void DocumentRasterCpc::NotifyHostModified() {
		if (m_parent)
			m_parent->SetModified(true);
		if (m_onModified)
			m_onModified();
	}

	void DocumentRasterCpc::MarkDirty() {
		m_dirty = true;
		m_debounceTime = 0.0;  // reset debounce timer
	}

	void DocumentRasterCpc::UpdateAutoGenerate(double deltaTime) {
		// If we've never generated code and there are commands, generate immediately
		if (m_generatedAsm.empty() && !m_cpcCrtc.GetCommands().empty()) {
			m_generatedAsm = m_cpcCrtc.GenerateCode(&m_timingReport);
			m_dirty = false;
			m_debounceTime = 0.0;
			return;
		}

		if (!m_dirty)
			return;
		//
		// Accumulate elapsed time
		//
		m_debounceTime += deltaTime;
		if (m_debounceTime < m_debounceInterval)
			return;  // debounce interval not yet expired
		//
		// Sync in-memory changes (enable/disable toggles) before generating
		// This ensures that UI checkbox changes are reflected in the generated code
		RetrodevLib::RasterParams tempParams;
		SaveProject(&tempParams);
		//
		// Generate code
		//
		m_generatedAsm = m_cpcCrtc.GenerateCode(&m_timingReport);
		m_dirty = false;
		m_debounceTime = 0.0;
	}

	// ---------------------------------------------------------------------------
	// RenderTimingPanel -- per-slot NOP budget visualization (gauges)
	// ---------------------------------------------------------------------------

	void DocumentRasterCpc::RenderTimingPanel() {
		if (m_timingReport.slots.empty()) {
			ImGui::TextDisabled("No timing report available. Add Frame commands and they will be auto-generated.");
			return;
		}

		ImGui::SeparatorText("NOP Budget & Timing");

		const float barHeight = 20.0f;
		// Prepared for future use when expanding timing visualization
		const float barSpacing = 4.0f;
		(void)barSpacing;
		const float labelWidth = 140.0f;

		//
		// Helper: draw a horizontal stacked bar for overhead/wait/active/free
		//
		auto drawTimingBar = [&](const char* label, const RetrodevLib::CpcRaster::SlotTimingReport& slot) {
			ImGui::Text("%s", label);
			ImGui::SameLine(labelWidth);
			ImGui::AlignTextToFramePadding();

			float availW = ImGui::GetContentRegionAvail().x - 80.0f;
			int totalNops = slot.overheadNops + slot.waitNops + slot.activeNops + slot.freeNops;
			if (totalNops <= 0) {
				ImGui::TextDisabled("[No data]");
				return;
			}

			ImVec2 barPos = ImGui::GetCursorScreenPos();
			ImDrawList* dl = ImGui::GetWindowDrawList();

			// Colors for the four segments
			const ImU32 colOverhead = IM_COL32(128, 128, 128, 255);   // gray
			const ImU32 colWait = IM_COL32(220, 100, 100, 255);       // red
			const ImU32 colActive = IM_COL32(100, 200, 100, 255);     // green
			const ImU32 colFree = IM_COL32(100, 150, 255, 255);       // blue

			float x = barPos.x;
			//
			// Overhead segment
			//
			float overheadW = (availW * slot.overheadNops) / totalNops;
			dl->AddRectFilled(ImVec2(x, barPos.y), ImVec2(x + overheadW, barPos.y + barHeight), colOverhead);
			x += overheadW;
			//
			// Wait segment
			//
			float waitW = (availW * slot.waitNops) / totalNops;
			dl->AddRectFilled(ImVec2(x, barPos.y), ImVec2(x + waitW, barPos.y + barHeight), colWait);
			x += waitW;
			//
			// Active segment
			//
			float activeW = (availW * slot.activeNops) / totalNops;
			dl->AddRectFilled(ImVec2(x, barPos.y), ImVec2(x + activeW, barPos.y + barHeight), colActive);
			x += activeW;
			//
			// Free segment
			//
			float freeW = (availW * slot.freeNops) / totalNops;
			dl->AddRectFilled(ImVec2(x, barPos.y), ImVec2(x + freeW, barPos.y + barHeight), colFree);
			//
			// Border
			//
			dl->AddRect(ImVec2(barPos.x, barPos.y), ImVec2(barPos.x + availW, barPos.y + barHeight),
				IM_COL32(200, 200, 200, 255), 0.0f, 0, 1.0f);

			ImGui::Dummy(ImVec2(availW, barHeight));

			//
			// Show tooltip on hover with detailed NOP breakdown
			//
			if (ImGui::IsItemHovered()) {
				ImGui::BeginTooltip();
				ImGui::Text("Overhead: %d NOPs (%.1f%%)", slot.overheadNops,
					(100.0f * slot.overheadNops) / totalNops);
				ImGui::Text("Wait:     %d NOPs (%.1f%%)", slot.waitNops,
					(100.0f * slot.waitNops) / totalNops);
				ImGui::Text("Active:   %d NOPs (%.1f%%)", slot.activeNops,
					(100.0f * slot.activeNops) / totalNops);
				ImGui::Text("Free:     %d NOPs (%.1f%%)", slot.freeNops,
					(100.0f * slot.freeNops) / totalNops);
				ImGui::Text("Total:    %d NOPs", totalNops);
				ImGui::EndTooltip();
			}
		};

		//
		// Draw per-slot timing bars (interrupt mode: 6 slots, VSync+loop: 1 "frame")
		//
		for (const auto& slotReport : m_timingReport.slots) {
			char label[32];
			if (slotReport.slot >= 0) {
				SDL_snprintf(label, sizeof(label), "Slot %d (Int%d)", slotReport.slot, slotReport.slot + 1);
			} else {
				SDL_snprintf(label, sizeof(label), "Frame");
			}
			drawTimingBar(label, slotReport);
		}

		//
		// Spacing between slots and frame total
		//
		ImGui::Spacing();
		ImGui::Spacing();

		//
		// Summary: total budget across all slots
		//
		if (!m_timingReport.slots.empty()) {
			ImGui::Separator();
			int totalBudget = 0;
			float totalOverhead = 0, totalWait = 0, totalActive = 0, totalFree = 0;
			for (const auto& slot : m_timingReport.slots) {
				totalBudget += slot.overheadNops + slot.waitNops + slot.activeNops + slot.freeNops;
				totalOverhead += slot.overheadNops;
				totalWait += slot.waitNops;
				totalActive += slot.activeNops;
				totalFree += slot.freeNops;
			}

			// Draw total label on its own line
			ImGui::Text("TOTAL (%d NOPs)", totalBudget);

			// Draw full-width bar
			if (totalBudget > 0) {
				float availW = ImGui::GetContentRegionAvail().x;
				ImVec2 barPos = ImGui::GetCursorScreenPos();
				ImDrawList* dl = ImGui::GetWindowDrawList();

				const ImU32 colOverhead = IM_COL32(128, 128, 128, 255);
				const ImU32 colWait = IM_COL32(220, 100, 100, 255);
				const ImU32 colActive = IM_COL32(100, 200, 100, 255);
				const ImU32 colFree = IM_COL32(100, 150, 255, 255);

				float x = barPos.x;

				float overheadW = (availW * totalOverhead) / totalBudget;
				dl->AddRectFilled(ImVec2(x, barPos.y), ImVec2(x + overheadW, barPos.y + barHeight), colOverhead);
				x += overheadW;

				float waitW = (availW * totalWait) / totalBudget;
				dl->AddRectFilled(ImVec2(x, barPos.y), ImVec2(x + waitW, barPos.y + barHeight), colWait);
				x += waitW;

				float activeW = (availW * totalActive) / totalBudget;
				dl->AddRectFilled(ImVec2(x, barPos.y), ImVec2(x + activeW, barPos.y + barHeight), colActive);
				x += activeW;

				float freeW = (availW * totalFree) / totalBudget;
				dl->AddRectFilled(ImVec2(x, barPos.y), ImVec2(x + freeW, barPos.y + barHeight), colFree);

				dl->AddRect(ImVec2(barPos.x, barPos.y), ImVec2(barPos.x + availW, barPos.y + barHeight),
					IM_COL32(200, 200, 200, 255), 0.0f, 0, 1.0f);

				ImGui::Dummy(ImVec2(availW, barHeight));

				if (ImGui::IsItemHovered()) {
					ImGui::BeginTooltip();
					ImGui::Text("Overhead: %d NOPs (%.1f%%)", (int)totalOverhead,
						(100.0f * totalOverhead) / totalBudget);
					ImGui::Text("Wait:     %d NOPs (%.1f%%)", (int)totalWait,
						(100.0f * totalWait) / totalBudget);
					ImGui::Text("Active:   %d NOPs (%.1f%%)", (int)totalActive,
						(100.0f * totalActive) / totalBudget);
					ImGui::Text("Free:     %d NOPs (%.1f%%)", (int)totalFree,
						(100.0f * totalFree) / totalBudget);
					ImGui::Text("Total:    %d NOPs", totalBudget);
					ImGui::EndTooltip();
				}

				// Draw summary text on next line
				ImGui::Text("Overhead: %d | Wait: %d | Active: %d | Free: %d",
					(int)totalOverhead, (int)totalWait, (int)totalActive, (int)totalFree);
			}
		}
	}

}

