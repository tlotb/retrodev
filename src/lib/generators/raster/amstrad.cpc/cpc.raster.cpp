// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Lib
//
// Amstrad CPC CRTC -- raster command types, frame simulation helpers, validation and Z80 code generation.
//
// (c) TLOTB 2026
//
// ===============================================================================
// CRITICAL RULE: DO NOT DELETE COMPENDIUM COMMENTS (APPLIES TO ENTIRE FILE)
// ===============================================================================
// This file implements the CPC CRTC timing simulator and code generator. ALL
// timing constraints, counter update rules, and register write windows are
// derived from the CPC Programmers Reference Manual (Compendium).
//
// EVERY function, lambda, and constraint in this file that relates to:
// - Register write timing
// - Counter progression (C0, C4, C5, C9)
// - Frame boundaries
// - Interrupt slots
// - VMA latching
//
// MUST be documented with Compendium citations (section + line number).
// These comments are the ONLY way to verify correctness and debug errors.
//
// RULE: When modifying scheduler logic, simulator, or constraints:
// 1. Cite the Compendium (sec.X, line XXXX)
// 2. Explain what the rule says
// 3. Explain how the code implements it
// 4. NEVER delete or shorten these comments
//
// Reference: See ./.claude/crtc_register_constraints.md for complete rules.
// ===============================================================================
//

#include "cpc.raster.h"
#include "cpc.raster.params.h"
#include <project/metadata/meta.raster.h>
#include <project/metadata/meta.raster.cpc.h>
#include <convert/amstrad.cpc/amstrad.cpc.h>
#include <glaze/glaze.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdarg>
#include <map>
#include <set>

namespace RetrodevLib {

	//
	// Debug system: structured logging with compile-time enable.
	// When CPC_RASTER_DEBUG is defined in the header, logs to file with tags and counter state.
	// When disabled, all calls are zero-cost no-ops via macro.
	//
	#ifdef CPC_RASTER_DEBUG
	static void RasterDbg(const char* tag, const char* fmt, ...) {
		FILE* log = fopen(CPC_RASTER_DEBUG_PATH, "a");
		if (log) {
			fprintf(log, "%s ", tag);
			va_list args;
			va_start(args, fmt);
			vfprintf(log, fmt, args);
			va_end(args);
			fprintf(log, "\n");
			fflush(log);
			fclose(log);
		}
	}
	#else
	#define RasterDbg(tag, fmt, ...) ((void)0)
	#endif

	namespace CpcRaster {

	// Minimal type probe for Load() to determine command type without fragile string parsing
	struct TypeProbe {
		int type = 0;
	};

	}  // namespace CpcRaster

}  // namespace RetrodevLib

// Glaze metadata for internal TypeProbe struct (Load() helper)
namespace glz {
	template <> struct meta<RetrodevLib::CpcRaster::TypeProbe> {
		using T = RetrodevLib::CpcRaster::TypeProbe;
		static constexpr auto value = object("type", &T::type);
	};
}

namespace RetrodevLib {

	namespace CpcRaster {

	// ---------------------------------------------------------------------------
	// Generator helpers
	// ---------------------------------------------------------------------------

	static void AppendLine(std::string& out, const std::string& line) {
		out += line;
		out += '\n';
	}


	static void AppendBlankLine(std::string& out) {
		out += '\n';
	}

	static std::string FormatHexByte(uint8_t value) {
		char buf[8];
		std::snprintf(buf, sizeof(buf), "#%02X", (unsigned)value);
		return buf;
	}

	static std::string NormalizeRuptureName(const std::string& ruptureName) {
		if (!ruptureName.empty())
			return ruptureName;
		return "rupture";
	}

	static std::string NormalizeAsmLabelToken(const std::string& name) {
		std::string out;
		out.reserve(name.size());
		for (char c : name) {
			bool isAlphaNum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
			if (isAlphaNum || c == '_')
				out += c;
			else
				out += '_';
		}
		if (out.empty())
			out = "frame";
		if (out[0] >= '0' && out[0] <= '9')
			out = "_" + out;
		return out;
	}


	static void EmitRawCrtcWrite(std::string& out, uint8_t reg, uint8_t value) {
		AppendLine(out, "\tld bc,#BC" + FormatHexByte(reg).substr(1));
		AppendLine(out, "\tout (c),c");
		AppendLine(out, "\tld bc,#BD" + FormatHexByte(value).substr(1));
		AppendLine(out, "\tout (c),c");
	}

	static void EmitCrtcWrite(std::string& out, uint8_t reg, uint8_t value) {
		switch (reg) {
		case 0: AppendLine(out, "\tCRTC_SetHTOT " + FormatHexByte(value)); return;
		case 1: AppendLine(out, "\tCRTC_SetHDIS " + FormatHexByte(value)); return;
		case 2: AppendLine(out, "\tCRTC_SetHSyncPos " + FormatHexByte(value)); return;
		case 3: AppendLine(out, "\tCRTC_SetSyncWidth "+ FormatHexByte(value)); return;
		case 4: AppendLine(out, "\tCRTC_SetVTOT " + FormatHexByte(value)); return;
		case 5: AppendLine(out, "\tCRTC_SetVTAdj " + FormatHexByte(value)); return;
		case 6: AppendLine(out, "\tCRTC_SetVDIS " + FormatHexByte(value)); return;
		case 7: AppendLine(out, "\tCRTC_SetVSyncPos " + FormatHexByte(value)); return;
		case 8: AppendLine(out, "\tCRTC_SetInterlaceSkew " + FormatHexByte(value)); return;
		case 9: AppendLine(out, "\tCRTC_SetMaxRaster " + FormatHexByte(value)); return;
		default:
			EmitRawCrtcWrite(out, reg, value);
			return;
		}
	}

	// GA hardware color bytes (from macros.ga.asm GA_COLOR_* constants)
	// Mapping: color index (0-26) -> GA_COLOR_n (pre-mixed GA_CMD_COLOR_SET|hw_index)
	static constexpr uint8_t kGaColorBytes[27] = {
		0x54, 0x44, 0x55, 0x5C, 0x58, 0x5D, 0x4C, 0x45, 0x4D, 0x56, 0x46, 0x57, 0x5E,
		0x40, 0x5F, 0x4E, 0x47, 0x4F, 0x52, 0x42, 0x53, 0x5A, 0x59, 0x5B, 0x4A, 0x43, 0x4B
	};

	static void EmitGaWrite(std::string& out, uint8_t gaCmd, uint8_t pen, uint8_t colorIndex) {
		// Emit GA writes by calling the macro equivalents from macros.ga.asm.
		// gaCmd: 0=SetInk, 1=SetBorder, 2=SetMode
		// For SetInk/SetBorder: pen/colorIndex specify pen number and color
		// For SetMode: pen parameter holds the full mode command byte

		if (gaCmd == 0) {
			// SetInk: GA_SetInk Pen, Color (expands to pen select + color write)
			uint8_t penValue = pen & 0x0F;  // 0-15
			uint8_t colorValue = (colorIndex < 27) ? kGaColorBytes[colorIndex] : 0x54;  // Fallback to black
			AppendLine(out, "\tGA_SetInk " + std::to_string(penValue) + "," + FormatHexByte(colorValue));
		} else if (gaCmd == 1) {
			// SetBorder: GA_SetBorder Color (expands to border pen select + color write)
			uint8_t colorValue = (colorIndex < 27) ? kGaColorBytes[colorIndex] : 0x54;  // Fallback to black
			AppendLine(out, "\tGA_SetBorder " + FormatHexByte(colorValue));
		} else if (gaCmd == 2) {
			// SetMode: GA_SetMode Mode (expands to single mode/ROM command write)
			AppendLine(out, "\tGA_SetMode " + FormatHexByte(pen));
		}
	}

	static CpcRasterTimingMode ResolveTimingMode(uint8_t mode) {
		if (mode == (uint8_t)CpcRasterTimingMode::TimedRasterLoop)
			return CpcRasterTimingMode::TimedRasterLoop;
		return CpcRasterTimingMode::InterruptDriven;
	}

	static void EmitCrtcWordWrite(std::string& out, uint8_t regHi, uint8_t regLo, const std::string& label) {
		if (regHi == 12 && regLo == 13) {
			// Use the new immediate-form CRTC_SetAddress macro (28 NOPs, BC-only)
			AppendLine(out, "\tCRTC_SetAddress " + label);
			return;
		}
		// Fallback for other register pairs (not typically used)
		AppendLine(out, "\tld hl," + label);
		AppendLine(out, "\tld bc,#BC" + FormatHexByte(regHi).substr(1));
		AppendLine(out, "\tout (c),c");
		AppendLine(out, "\tld bc,#BD00");
		AppendLine(out, "\tld c,h");
		AppendLine(out, "\tout (c),c");
		AppendLine(out, "\tld bc,#BC" + FormatHexByte(regLo).substr(1));
		AppendLine(out, "\tout (c),c");
		AppendLine(out, "\tld bc,#BD00");
		AppendLine(out, "\tld c,l");
		AppendLine(out, "\tout (c),c");
	}

	// ---------------------------------------------------------------------------
	// CRTC state simulator -- helper functions for safe-window analysis
	// Note: AdvanceCrtcLine logic is now implemented in CrtcSimulator::advanceOneChar()
	// and related methods in the CpcRaster namespace. See CrtcSimulator for details.
	//
	// SimulateCrtcWith312Lines -- Full CRTC simulation with mid-frame register writes
	//
	// Simulates CRTC counter progression through up to 312 scanlines with dynamic register
	// changes applied at specified scanlines. This is used to verify frame boundary timing
	// and detect when register writes trigger frame completion or counter state changes.
	//
	// Parameters:
	//   initialState: Starting CRTC register values (R0-R9, R12/R13)
	//   writes: Map of register writes, keyed by scanline: {scanline -> [(reg, value), ...]}
	//   initialCtr: Initial counter state (usually zero, frame start)
	//
	// Returns:
	//   Vector of frame boundary scanline numbers (where C4 transitions to 0).
	//   Always includes 0 as the first boundary (frame 0 starts at scanline 0).
	//
	// Frame Boundary Detection (Compendium sec.11, sec.12):
	//   A frame is complete when:
	//     1. C4==R4 AND C9==R9 (end of main frame rows) AND R5==0, OR
	//     2. C4==R4 AND C9==R9 AND C5>=R5 (end of R5 adjustment phase)
	//   The frameComplete flag is set by CrtcSimulator when this condition is detected.
	//
	// VMA Latching (Compendium sec.20.3):
	//   VMA is loaded with R12/R13 when frameComplete is detected.
	//   This happens at the frame boundary and is critical for display memory setup.
	//
	// Register Write Application:
	//   Writes are applied immediately at their scheduled scanline, before advancing the simulator.
	//   This allows register changes to take effect at precisely timed points in the frame.
	//
	// Simulation Limits:
	//   Simulates up to 312 scanlines (standard PAL CPC frame).
	//   Register writes beyond 312 are ignored.
	//   Frame boundaries are recorded only if sl + 1 < 312 (prevents overflow).
	//
	static std::vector<int> SimulateCrtcWith312Lines(
		const RasterFrameCmd& initialState,
		const std::map<int, std::vector<std::pair<uint8_t, uint8_t>>>& writes,
		const CrtcCounters& initialCtr = CrtcCounters{}) {

		std::vector<int> frameBoundaries;
		frameBoundaries.push_back(0); // Frame 0 always starts at scanline 0

		// Use CrtcSimulator to compute frame boundaries
		CpcRaster::CrtcSimulator sim;
		sim.init(initialCtr, initialState);

		for (int sl = 0; sl < 312; sl++) {
			// Apply any register writes at this scanline BEFORE advancing CRTC
			auto it = writes.find(sl);
			if (it != writes.end()) {
				for (const auto& write : it->second) {
					uint8_t reg = write.first;
					uint8_t val = write.second;
					sim.applyRegWrite(reg, val);
				}
			}

			// Advance CRTC by 64 NOPs (one scanline = 64 character clocks)
			// (Compendium sec.11/12: frame complete when C4==R4 && (C9==R9 && R5==0 || C5>=R5))
			sim.advance(64);

			// Check if frame boundary was detected (C4 wrapped from R4 to 0)
			if (sim.counters().frameComplete && sl + 1 <= 312) {
				frameBoundaries.push_back(sl + 1);
			}
		}

		// CRITICAL FIX: If simulation stops before scanline 312, extend the last boundary to 312.
		// This accounts for the initial C9=2 state (post-VSYNC) which makes the first frame 2 scanlines
		// shorter. The missing scanlines must still be covered by extending the last frame to reach 312.
		if (frameBoundaries.size() > 0 && frameBoundaries.back() < 312) {
			frameBoundaries.back() = 312;
		}

		return frameBoundaries;
	}

	//
	// FrameLength -- Calculate exact scanlines for a CRTC frame state
	// Uses CrtcSimulator::runFrame() with proper R5 adjustment phase handling
	// (Compendium sec.11: C5 increments per scanline, not per character row).
	//
	static int FrameLength(const RasterFrameCmd& state) {
		CpcRaster::CrtcSimulator sim;
		sim.init(CrtcCounters{}, state);
		int nopsPerScanline = state.r0 + 1;  // character clocks per scanline
		return sim.runFrame() / nopsPerScanline;
	}

	// ---------------------------------------------------------------------------
	// CrtcSimulator::RenderMonitorImage -- pixel-accurate RGBA32 monitor raster image
	// ---------------------------------------------------------------------------

	void CrtcSimulator::RenderMonitorImage(
		const std::vector<MonitorSlot>& slots,
		int maxCols,
		int totalScanLines,
		int monitorPhaseLines,
		std::vector<uint32_t>& outPixels,
		int& outWidth,
		int& outHeight) {
		//
		// PAL canvas: always 312 lines tall and 8 pixels per character column so the
		// image can be displayed at a 4:3 aspect ratio to match an actual monitor.
		// The canvas is fixed at 312 lines regardless of totalScanLines.  When the
		// CRTC frame is over-budget (totalScanLines > 312) the excess scan lines wrap
		// back to the top of the canvas (modulo 312), overwriting earlier content --
		// exactly the rolling/overlapping artifact a real PAL monitor shows when out of sync.
		// When the frame is under-budget the unreached rows stay black.
		//
		const int kPixPerChar  = 8;
		const int kPalHeight   = 312;
		outWidth  = maxCols * kPixPerChar;
		outHeight = kPalHeight;
		outPixels.assign((size_t)(outWidth * kPalHeight), 0xFF000000u);
		//
		// Zone colours -- RGBA32 (R in lowest byte, A in highest).
		// HSync and VSync each have two phases:
		//   Pulse  = the active sync signal (bright shade)
		//   Back porch = the settling period after the pulse (dark shade)
		// In screen-mapped coordinates the pulse lands at the right/bottom edge
		// and the back porch lands at the left/top edge of the image.
		//
		const uint32_t colBorder     = 0xFF2A2A2Au;  // grey          -- border / blanking
		const uint32_t colHSyncPulse = 0xFFD040B0u;  // bright purple -- HSync active pulse
		const uint32_t colHSyncBack  = 0xFF60184Au;  // dark purple   -- HSync back porch
		const uint32_t colVSyncPulse = 0xFFE8C800u;  // bright cyan   -- VSync active pulse
		const uint32_t colVSyncBack  = 0xFF584800u;  // dark cyan     -- VSync back porch
		const uint32_t colHVPulse    = 0xFF884880u;  // dark mauve    -- H+V overlap
		//
		// Per-slot checker palette -- mirrors the design-mode grid colours.
		//
		static constexpr int kSlotColors = 6;
		static const uint32_t slotColA[kSlotColors] = {
			0xFF623000u,  // blue
			0xFF306230u,  // green
			0xFF3050AAu,  // orange
			0xFFA03082u,  // purple
			0xFFA0A030u,  // teal
			0xFF3082A0u,  // gold
		};
		static const uint32_t slotColB[kSlotColors] = {
			0xFF8250DCu,
			0xFF50BE50u,
			0xFF4670D2u,
			0xFFC850C8u,
			0xFFC8C850u,
			0xFF50C8C8u,
		};
			//
			// Locate the first slot that actually emits a VSync pulse.
			// vsyncEndLine is the first absolute scan line AFTER that pulse -- this
			// is the beam-Y=0 reference (top of monitor image).
			//
			// When totalScanLines != 312 the CRTC timing is non-standard (frame over/under budget).
			// In that case the monitor cannot lock VSync so we leave vsyncEndLine = 0, which means
			// the image origin stays at scan line 0 and the content appears rolled/unsynchronised
			// rather than anchored to a false sync position.
			//
			int vsyncEndLine = 0;
			bool foundVSync  = false;
			if (totalScanLines == 312) {
				for (const auto& s : slots) {
					if (!s.state.disableVSync) {
						int effectiveR7 = (s.state.r7 == 0) ? s.state.r4 : s.state.r7;
						int vsStart  = s.startSl + effectiveR7 * (s.state.r9 + 1);
						vsyncEndLine = vsStart + 2 * (s.state.r9 + 1);
						if (vsyncEndLine > totalScanLines)
							vsyncEndLine = totalScanLines;
						foundVSync = true;
						break;
					}
				}
			}
			(void)foundVSync;
		//
		// Main rasterisation loop: iterate CRTC output order (sl, ch), compute the
		// beam position (screenX, screenY) for each character clock, and write the
		// appropriate colour into the output image at that beam position.
		//
		// Beam horizontal position:
		//   screenX = (ch - hsyncEndChar + R0+1) % (R0+1)
		//   where hsyncEndChar = (R2 + hsyncWidth) % (R0+1)
		//
		// This places the first character after HSync (back porch start) at screenX=0
		// and the HSync pulse characters at screenX = (R0+1 - hsyncWidth) .. R0.
		//
		// Beam vertical position:
		//   screenY = (sl - vsyncEndLine + kPalHeight) % kPalHeight
		//
		// The modulo is always kPalHeight (312), the fixed PAL canvas height.
		// When totalScanLines > 312 the excess scan lines wrap back to the top of the
		// canvas, producing the overwrite/roll artifact of an out-of-sync monitor.
		//
		for (int sl = 0; sl < totalScanLines; sl++) {
			//
			// Find owning slot
			//
			const MonitorSlot* ownerSlot = nullptr;
			for (const auto& s : slots) {
				if (sl >= s.startSl && sl < s.endSl) {
					ownerSlot = &s;
					break;
				}
			}
			//
			// Compute beam screen Y -- always modulo kPalHeight (312-line canvas).
			//
			int screenY = (sl + monitorPhaseLines - vsyncEndLine + kPalHeight) % kPalHeight;
			if (!ownerSlot)
				continue;
			const RasterFrameCmd& f = ownerSlot->state;
			int slRel   = sl - ownerSlot->startSl;
			int charRow = slRel / (f.r9 + 1);
			bool isVSyncPulseRow = IsVSyncScanLine(f, slRel);
			bool isDispRow       = IsDisplayScanLine(f, slRel);
			int  sidx            = ownerSlot->slotIndex % kSlotColors;
			//
			// VSync row colour: first char row = bright, second = dark.
			//
			uint32_t colVSyncRow = 0;
			if (isVSyncPulseRow) {
				int effectiveR7 = (f.r7 == 0) ? f.r4 : f.r7;
				int vsyncStartSl   = effectiveR7 * (f.r9 + 1);
				int charRowInVSync = (slRel - vsyncStartSl) / (f.r9 + 1);
				colVSyncRow = (charRowInVSync == 0) ? colVSyncPulse : colVSyncBack;
			}
			int hsyncWidth = f.r3 & 0x0F;
			int htotal     = f.r0 + 1;
			int syncStart  = (int)f.r2 % htotal;
			//
			// HSync end char: first char after the pulse (beam retrace point).
			//
			int hsyncEndChar = hsyncWidth > 0 ? ((int)f.r2 + hsyncWidth) % htotal : 0;
			for (int ch = 0; ch < maxCols; ch++) {
				//
				// Beam screen X: chars after HSync end = left edge of next monitor line.
				// Back-porch chars (hsyncEndChar..htotal-1 mapped to screenX 0..bp-1)
				// belong to screenY+1 -- beam retrace already fired.
				//
				int screenX        = (ch - hsyncEndChar + htotal) % htotal;
				bool isHSyncPulse  = IsHSyncChar(f, ch);
				int backPorchWidth = hsyncWidth > 0
					? (((int)f.r2 + hsyncWidth <= htotal) ? (htotal - (int)f.r2 - hsyncWidth) : 0)
					: 0;
				bool isHBackPorch  = !isHSyncPulse && hsyncWidth > 0 && (screenX < backPorchWidth);
				int  screenY_draw  = isHBackPorch ? (screenY + 1) % kPalHeight : screenY;
				bool isActiveVMA   = isDispRow && (ch < (int)f.r1);
				uint32_t colChar;
				if (isHSyncPulse) {
					if (isVSyncPulseRow) {
						colChar = colHVPulse;
					} else {
						//
						// Split HSync pulse: first half = bright, second = dark.
						//
						int colInPulse = ((int)ch - syncStart + htotal) % htotal;
						colChar = (hsyncWidth < 2 || colInPulse < hsyncWidth / 2) ? colHSyncPulse : colHSyncBack;
					}
				} else if (isVSyncPulseRow) {
					colChar = colVSyncRow;
				} else if (isActiveVMA) {
					colChar = ((ch + charRow) & 1) ? slotColA[sidx] : slotColB[sidx];
				} else {
					colChar = colBorder;
				}
				//
				// Write kPixPerChar pixels at the beam position in the output image
				//
				int pxBase = screenY_draw * outWidth + screenX * kPixPerChar;
				if (pxBase >= 0 && pxBase + kPixPerChar <= outWidth * kPalHeight) {
					for (int p = 0; p < kPixPerChar; p++)
						outPixels[(size_t)(pxBase + p)] = colChar;
				}
			}
		}
	}

	}  // namespace CpcRaster

	// Bring CpcRaster names into RetrodevLib scope for helper functions
	using namespace CpcRaster;

	// ---------------------------------------------------------------------------
	// CPCRaster::Load / Save -- opaque string <-> CpcRasterCommand
	// ---------------------------------------------------------------------------

	CpcRasterCommand CPCRaster::Load(const std::string& blob) {
		if (blob.empty()) {
			// Default to Frame command
			CpcFrameCommand frameCmd;
			return frameCmd;
		}

		// Parse JSON to determine type field using glaze
		// Uses CpcRaster::TypeProbe defined at namespace level for proper reflection metadata
		CpcRaster::TypeProbe probe;
		(void)glz::read_json(probe, blob);  // Silently ignores missing "type" field, defaults to type=0 (Frame)

		CpcRasterCommandType type = (CpcRasterCommandType)probe.type;

		// Deserialize based on type
		if (type == CpcRasterCommandType::Frame) {
			CpcFrameCommand cmd;
			(void)glz::read_json(cmd, blob);
			return cmd;
		} else if (type == CpcRasterCommandType::Effect) {
			CpcEffectCommand cmd;
			(void)glz::read_json(cmd, blob);
			return cmd;
		} else if (type == CpcRasterCommandType::Variable) {
			CpcVariableCommand cmd;
			(void)glz::read_json(cmd, blob);
			return cmd;
		} else {  // Subroutine
			CpcSubroutineCommand cmd;
			(void)glz::read_json(cmd, blob);
			return cmd;
		}
	}

	std::string CPCRaster::Save(const CpcRasterCommand& cmd) {
		std::string out;

		// Serialize based on the active variant type
		// Glaze metadata (from meta.raster.h) is now included, so all fields will be serialized
		if (std::holds_alternative<CpcFrameCommand>(cmd)) {
			const auto& frameCmd = std::get<CpcFrameCommand>(cmd);
			(void)glz::write_json(frameCmd, out);
		} else if (std::holds_alternative<CpcEffectCommand>(cmd)) {
			const auto& effectCmd = std::get<CpcEffectCommand>(cmd);
			(void)glz::write_json(effectCmd, out);
		} else if (std::holds_alternative<CpcVariableCommand>(cmd)) {
			const auto& varCmd = std::get<CpcVariableCommand>(cmd);
			(void)glz::write_json(varCmd, out);
		} else if (std::holds_alternative<CpcSubroutineCommand>(cmd)) {
			const auto& subCmd = std::get<CpcSubroutineCommand>(cmd);
			(void)glz::write_json(subCmd, out);
		}

		return out;
	}

	// ---------------------------------------------------------------------------
	//
	// CPCRaster::Validate -- CRTC Constraint Validation (Compendium v1.8)
	// =====================================================================
	// Validates CRTC register values against hard limits and logical constraints
	// derived from the CPC Programmers Reference Manual (Compendium).
	//
	// Rules are evaluated against the MERGED (cumulative) CRTC state for each
	// Frame slot, so overlapping register overwrites are correctly accounted for.
	//
	// VALIDATION RULES (All cited from Compendium):
	//
	// Register Hard Limits (sec.4.3 / sec.13.1 - CRTC 6845 datasheet):
	//   R0: 8-bit (0..255), but R0=0 causes special CRTC 0 freeze behavior
	//   R1: 8-bit, must be <= R0 (displayed <= total horizontal)
	//   R2: 8-bit, must be <= R0 (HSync position within line width)
	//   R3: 8-bit, split into 4-bit HSync width (lower) and 4-bit VSync width (upper)
	//   R4: 7-bit (0..127) - Vertical Total
	//   R5: 5-bit (0..31) - Vertical Adjust
	//   R6: 8-bit - Vertical Displayed (border start)
	//   R7: 8-bit - Vertical Sync Position
	//   R9: 5-bit (0..31) - Max Raster (scanlines per character row - 1)
	//
	// Logical Constraints (from Compendium sections):
	//   - R1 <= R0 (Compendium sec.17.1): displayed width must not exceed total
	//   - R2 >= R1 (Compendium sec.15.1): HSync should start after display region
	//   - R2 <= R0 (Compendium sec.15): HSync position within horizontal total
	//   - HSync width > 0 (Compendium sec.14.6): width 0 disables HSync on some types
	//   - HSync + R2 fit within R0+1 (Compendium sec.14.4): prevent HSync wrap
	//   - R5 <= R9 (Compendium sec.11.1): adjust phase should fit one character row
	//   - R6 <= R4+1 (Compendium sec.18.1, sec.6.1.3): displayed rows <= total rows
	//   - R7 <= R4 (Compendium sec.16.1): VSync position must be within frame
	//   - Total scanlines = 312 per screen group (Compendium sec.22.2/22.3): 50 Hz PAL timing
	//
	// Screen Group Timing (Compendium sec.22.2 / 22.3):
	//   Frames with disableVSync=true accumulate into a screen group.
	//   When disableVSync=false, frame closes the group and total must equal 312 scanlines
	//   for stable 50 Hz PAL signal.
	//

	// ---------------------------------------------------------------------------
	// RuptureSchedule -- per-frame action scheduler with safe-window scanline placement
	// ---------------------------------------------------------------------------
	//
	// One register write to schedule.  scanline is absolute (0..311).
	// isVma=true means emit a CRTC_SetAddress R12/R13 word write using vmaLabel.
	//
	struct ScheduledAction {
		int scanline      = 0;   // final placed scanline (absolute 0..311)
		int preferredSl   = 0;   // earliest safe scanline from RegSafeWriteScanline (pre-budget)
		int latestSl      = 0;   // latest scanline where this write is still functionally safe
		int nopOffset     = 0;   // NOP position within scanline (for sub-line timed-path output)
		int actualCost    = 0;   // Actual cost (NOPs) of this write at placed (scanline, nopOffset)
		// Maximum NOP position (inclusive) within the scanline at which this write may START.
		// -1 = unconstrained.  Used to keep R7 writes before the HSYNC window (sec.13.2.2 / sec.16.4.3).
		int maxNopOffset  = -1;
		// Minimum NOP position (inclusive) within the scanline at which this write may START.
		// -1 = unconstrained.  Used to force R1 (HDIS) to be placed late on frame boundary.
		int minNopOffset  = -1;
		uint8_t reg       = 0;
		uint8_t value     = 0;
		uint8_t gaColorIdx = 0;     // Color index for GA Set Ink/Border (0-26)
		int gaCmd         = -1;     // GA command type: 0=SetInk, 1=SetBorder, 2=SetMode, -1=not GA
		bool isVma        = false;
		bool unrestrained = false; // Variable placed without explicit waits (best-effort in slot)
		bool isFollowingEffect = false; // True if this is a 2nd+ write in the same effect
		bool isAbsoluteEffect = false; // True if effect is absolute-scheduled (vs relative)
		int cmdIndex      = -1;     // Index of Effect/Frame/Variable/Subroutine command in m_cpcCommands (-1 if N/A)
		int effectWriteIndex = -1;   // Index of this write within the effect (for definition order)
		int frameIndex    = -1;
		std::string vmaLabel;
		std::string smcLabel;       // SMC patch label (for self-modifying code)
		std::string varName;        // variable name (for Variable commands, reg==255)
		std::string subroutineName; // subroutine name (for Subroutine commands, reg==254)
		std::string comment;
	};
	//
	// NOPs consumed by each write type (from macros.crtc.asm / macros.ga.asm timings):
	// CRTC_SetHTOT imm = 14 us = 14 NOPs; CRTC_SetHTOTReg = 13 us = 13 NOPs;
	// CRTC_SetAddress (R12+R13) = 28 us = 28 NOPs; GA_SetInk/SetBorder = 13 us = 13 NOPs; GA_SetMode = 7 us = 7 NOPs.
	//
	static constexpr int kCrtcWriteNops = 14;         // ld bc + out + ld bc + out = 3+4+3+4 = 14 us
	static constexpr int kGaSetInkNops = 13;         // SetInk/SetBorder: ld bc + out + ld c + out = 3+4+2+4 = 13 us
	static constexpr int kGaSetModeNops = 7;         // SetMode: ld bc + out = 3+4 = 7 us
	static constexpr int kVmaWriteNops = 28;         // CRTC_SetAddress (R12+R13): 4x(3+4) = 28 us
	static constexpr int kSubroutineCallNops = 6;    // call nnnn = 6 us
	static constexpr int kVariableWriteNops = 6;     // ld a,n + ld(nnnn),a = 2+4 = 6 us

	//
	// Register sentinel values for special action types (not CRTC registers).
	// These use register numbers outside the valid CRTC range (0-13) for dispatch.
	//
	static constexpr uint8_t kRegGA [[maybe_unused]] = 253;          // Gate Array write (reg==253)
	static constexpr uint8_t kRegSubroutine [[maybe_unused]] = 254;  // Subroutine call (reg==254)
	static constexpr uint8_t kRegVariable [[maybe_unused]] = 255;    // Variable write (reg==255)

	//
	// SchedulerConfig -- Encapsulates scheduling options and behavior
	// (F-2 consolidation: replaces scattered boolean parameters with structured config)
	//
	struct SchedulerConfig {
		//
		// When true, treat the first VMA write specially: position it to wrap around
		// to the previous scanline (for Interrupt-driven handlers where frame boundary
		// may occur mid-interrupt). When false, use normal placement (for simpler paths).
		//
		bool wrapFirstVmaToPreviousScanline = false;

		//
		// Reserved for future scheduling options (budget scaling, constraint relaxation, etc.)
		// Keeps this struct extensible without function signature changes.
		//
	};

	//
	// Full line budget = 64 NOPs per scanline (one raster line on CPC).
	// This is the actual available budget; the interrupt overhead and WaitScanlines
	// overhead are accounted for separately during code emission.
	//
	static constexpr int kLineNopBudget = 64;

	//
	// Lookup table for register number to mask bit.
	// Maps CRTC register index (0-13) to its corresponding mask bit.
	// MASK_R9 = (1U << 8) and MASK_R12 = (1U << 9) use non-sequential bits to avoid collisions.
	// R8, R10, R11, R13 don't have individual masks (0).
	//
	static constexpr uint32_t kRegisterMasks[] = {
		RasterFrameCmd::MASK_R0,   // [0]  R0
		RasterFrameCmd::MASK_R1,   // [1]  R1
		RasterFrameCmd::MASK_R2,   // [2]  R2
		RasterFrameCmd::MASK_R3,   // [3]  R3
		RasterFrameCmd::MASK_R4,   // [4]  R4
		RasterFrameCmd::MASK_R5,   // [5]  R5
		RasterFrameCmd::MASK_R6,   // [6]  R6
		RasterFrameCmd::MASK_R7,   // [7]  R7
		0,                         // [8]  R8 (not writable)
		RasterFrameCmd::MASK_R9,   // [9]  R9 = (1U << 8), not (1U << 9)
		0,                         // [10] R10 (not writable)
		0,                         // [11] R11 (not writable)
		RasterFrameCmd::MASK_R12,  // [12] R12/VMA = (1U << 9)
		RasterFrameCmd::MASK_R12,  // [13] R13 uses R12's mask
	};

	// Helper: NOP cost for a GA write based on command type.
	static int GaWriteNops(int gaCmd) {
		return (gaCmd == 0 || gaCmd == 1) ? kGaSetInkNops : kGaSetModeNops;
	}

	// Calculate the NOP cost of a scheduled action (dispatch on action type).
	// Must match code generation logic exactly.
	//
	static int ActionWriteNops(const ScheduledAction& act) {
		if (act.isVma) {
			return kVmaWriteNops;
		} else if (act.reg == CpcRaster::kRegGA) {
			return GaWriteNops(act.gaCmd);
		} else if (act.reg == CpcRaster::kRegSubroutine) {
			return kSubroutineCallNops;
		} else if (act.reg == CpcRaster::kRegVariable) {
			return kVariableWriteNops;
		} else {  // CRTC
			return kCrtcWriteNops;
		}
	}

	//
	// EmitCrtcWriteWithSmc -- Emit CRTC write with optional SMC label
	//
	// Consolidated helper for CRTC register emission used in both VSync+Loop and Interrupt paths.
	// Handles: redundancy detection, SMC label emission, register write, and state tracking.
	// Optional activeMask parameter for interrupt-specific behavior.
	//
	// Parameters:
	//   act: ScheduledAction with reg, value, frameIndex
	//   knownState: CrtcKnownState for redundancy detection and update
	//   cpcCommands: Array of commands for SMC label lookup
	//   prefix: ASM label prefix
	//   activeMask: Optional frame activeMask (if non-zero, check before SMC emission)
	//
	// Returns: NOP cost (0 if skipped, kCrtcWriteNops if emitted)
	//
	static int EmitCrtcWriteWithSmc(
		std::string& out,
		const ScheduledAction& act,
		CpcRaster::CrtcKnownState& knownState,
		const std::vector<CpcRasterCommand>& cpcCommands,
		const std::string& prefix,
		uint32_t activeMask = 0) {

		// Check for redundancy: skip if not first frame and value already set.
		// BUT: if SMC is enabled for this register, always emit it (label is needed for patching).
		bool isFirstFrame = (act.frameIndex == 0);
		bool hasSmc = false;
		if (act.frameIndex >= 0 && act.frameIndex < (int)cpcCommands.size()) {
			const CpcRasterCommand& frameCmd = cpcCommands[(size_t)act.frameIndex];
			if (std::holds_alternative<CpcFrameCommand>(frameCmd)) {
				const auto& origFrame = std::get<CpcFrameCommand>(frameCmd).frame;
				if (act.reg < 14) {
					hasSmc = (origFrame.smcMask & kRegisterMasks[act.reg]) != 0;
				}
			}
		}
		bool shouldSkip = !isFirstFrame && !hasSmc && knownState.matches(act.reg, act.value);
		if (shouldSkip) {
			AppendLine(out, "\t; [R" + std::to_string(act.reg) + " skipped] already set");
			return 0;  // optimized away
		}

		// Emit SMC patch label if marked in frame command
		if (act.frameIndex >= 0 && act.frameIndex < (int)cpcCommands.size()) {
			const CpcRasterCommand& frameCmd = cpcCommands[(size_t)act.frameIndex];
			if (std::holds_alternative<CpcFrameCommand>(frameCmd)) {
				const auto& origFrame = std::get<CpcFrameCommand>(frameCmd).frame;
				// Optional activeMask check (for interrupt path): only emit if register is active
				if (activeMask == 0 || (origFrame.activeMask & kRegisterMasks[act.reg])) {
					if (act.reg < 14 && (origFrame.smcMask & kRegisterMasks[act.reg])) {
						std::string label;
						if (origFrame.smcLabelOverrides.count(act.reg) > 0) {
							label = origFrame.smcLabelOverrides.at(act.reg);
						} else {
							label = prefix + "_" + NormalizeAsmLabelToken(GetCommandName(frameCmd)) + "_R" + std::to_string((int)act.reg) + "_patch";
						}
						// CRTC macro structure: ld bc,PORT+REG (3) + out (2) + ld bc,PORT+VALUE (3) + out (2)
						// Value immediate is at byte offset 6 within macro
						AppendLine(out, label + " equ $+6  ; R" + std::to_string((int)act.reg) + " value immediate in CRTC macro");
					}
				}
			}
		}

		// Emit the actual CRTC write
		EmitCrtcWrite(out, act.reg, act.value);
		knownState.set(act.reg, act.value);
		return kCrtcWriteNops;
	}

	// Unified Z80 code emission for all scheduled action types.
	// Dispatches on action type (VMA, GA, Subroutine, Variable, CRTC) and emits the appropriate code.
	// Handles SMC patch label generation for CRTC writes.
	// Returns the NOP cost of the emitted code.
	//
	static int EmitAction(std::string& out, const ScheduledAction& act, const std::string& prefix,
	                       CpcRaster::CrtcKnownState& knownState, const std::vector<CpcRasterCommand>& cpcCommands) {
		if (act.isVma) {
			// VMA: CRTC_SetAddress R12/R13 with label
			EmitCrtcWordWrite(out, 12, 13, act.vmaLabel);
			return kVmaWriteNops;
		} else if (act.reg == CpcRaster::kRegGA) {
			// GA: SetInk/SetBorder or SetMode
			EmitGaWrite(out, act.gaCmd, act.value, act.gaColorIdx);
			return GaWriteNops(act.gaCmd);
		} else if (act.reg == CpcRaster::kRegSubroutine) {
			// Subroutine: call with optional prefix (auto-generated names get prefix, user-provided don't)
			// Auto-generated names follow pattern "SubN" (e.g., "Sub1", "Sub2")
			// User-provided names are used as-is
			std::string fullName = act.subroutineName;
			bool isAutoGenerated = !act.subroutineName.empty() &&
				act.subroutineName[0] == 'S' && act.subroutineName[1] == 'u' && act.subroutineName[2] == 'b' &&
				(act.subroutineName.size() > 3 && std::isdigit(act.subroutineName[3]));
			if (isAutoGenerated) {
				fullName = prefix + "_" + act.subroutineName;
			}
			AppendLine(out, "\tcall " + fullName);
			AppendLine(out, "\t; NOTE: user must define " + fullName + " in source");
			return kSubroutineCallNops;
		} else if (act.reg == CpcRaster::kRegVariable) {
			// Variable: ld a,value + ld (varName),a
			AppendLine(out, "\tld a," + std::to_string(act.value));
			AppendLine(out, "\tld (" + act.varName + "),a");
			return kVariableWriteNops;
		} else {
			// CRTC: use consolidated helper for write + SMC emission
			return EmitCrtcWriteWithSmc(out, act, knownState, cpcCommands, prefix);
		}
	}

	//
	// Get the default value for a CRTC register from a blank RasterFrameCmd.
	// Used to determine if a hoisted register differs from its default value.
	//
	static uint8_t GetDefaultRegValue(uint8_t reg) {
		const RasterFrameCmd defaults;
		switch (reg) {
			case 0: return defaults.r0;
			case 1: return defaults.r1;
			case 2: return defaults.r2;
			case 3: return defaults.r3;
			case 4: return defaults.r4;
			case 5: return defaults.r5;
			case 6: return defaults.r6;
			case 7: return defaults.r7;
			case 9: return defaults.r9;
			case 12: return defaults.r12;
			case 13: return defaults.r13;
			default: return 0;
		}
	}

	//
	// Emit hoisted (constant-across-all-frames) registers if they differ from defaults.
	// Consolidates repeated emission logic from both timing mode paths.
	//
	static void EmitHoistedRegistersLoop(std::string& out, const CrtcKnownState& hoistedRegs, RasterTimingReport& report) {
		for (uint8_t reg = 0; reg <= 13; reg++) {
			if (!hoistedRegs.isKnown(reg))
				continue;
			uint8_t value = hoistedRegs.regValues[reg];
			uint8_t defaultValue = GetDefaultRegValue(reg);
			if (value != defaultValue) {
				EmitCrtcWrite(out, reg, value);
				report.hoisteRegs.push_back({reg, value});
			}
		}
	}

	//
	// EmitStartBoilerplate -- Shared Z80 sequence for handler/loop initialization
	//
	// Emits the common di + push bc + hoisted registers + pop bc sequence used in both
	// VSync+Loop and Interrupt-Driven path _Start sections. Consolidates duplication.
	//
	static void EmitStartBoilerplate(
		std::string& out,
		const CrtcKnownState& hoistedRegs,
		RasterTimingReport& report) {

		AppendLine(out, "\t; Initialise constant CRTC registers.");
		AppendLine(out, "\t; IMPORTANT: Caller must load B with desired GA screen mode before calling this.");
		AppendLine(out, "\t; This function preserves B across CRTC writes using push bc / pop bc so the");
		AppendLine(out, "\t; mode value is not lost when CRTC register macros use B for port output.");
		AppendLine(out, "\tdi");
		AppendLine(out, "\tpush bc\t\t\t; preserve GA_MODE (passed in B by caller)");
		EmitHoistedRegistersLoop(out, hoistedRegs, report);
		AppendLine(out, "\tpop bc\t\t\t; restore GA_MODE to B");
	}

	//
	// Emit SMC patch point header comment.
	// Both VSync+Loop and Interrupt modes use the same mechanism: $ in assembly means current address,
	// so patch labels point to specific instruction bytes using $+offset.
	// Example: "ld a,#00 equ $+1" means patch the value byte (#01 offset from label position).
	//
	static void EmitSmcPatchPoints(std::string& out, const std::string& prefix,
	                                 const std::vector<CpcRasterCommand>& cpcCommands) {
		std::vector<std::string> smcPoints;
		std::vector<std::string> patchFunctions;
		for (int i = 0; i < (int)cpcCommands.size(); i++) {
			const CpcRasterCommand& cmd = cpcCommands[i];
			if (!std::holds_alternative<CpcFrameCommand>(cmd))
				continue;
			const auto& frameCmd = std::get<CpcFrameCommand>(cmd);
			const auto& fr = frameCmd.frame;
			std::string frameName = NormalizeAsmLabelToken(frameCmd.name);

			// Check each register for SMC patch points
			for (int reg = 0; reg <= 13; reg++) {
				uint32_t maskBit = kRegisterMasks[reg];
				if (maskBit == 0 || !(fr.smcMask & maskBit) || !(fr.activeMask & maskBit))
					continue;

				std::string label;
				if (reg == 12 || reg == 13) {
					// VMA uses key 12
					if (fr.smcLabelOverrides.count(12) > 0) {
						label = fr.smcLabelOverrides.at(12);
					} else {
						label = prefix + "_" + frameName + "_R12R13_patch";
					}
					smcPoints.push_back("; " + label + "_R12 equ $+1  ; Frame \"" + frameCmd.name + "\" VMA high byte");
					smcPoints.push_back("; " + label + "_R13 equ $+1  ; Frame \"" + frameCmd.name + "\" VMA low byte");
					break;  // Skip R13 since we handle both
				} else {
					if (fr.smcLabelOverrides.count(reg) > 0) {
						label = fr.smcLabelOverrides.at(reg);
					} else {
						label = prefix + "_" + frameName + "_R" + std::to_string(reg) + "_patch";
					}
					smcPoints.push_back("; " + label + " equ $+1  ; Frame \"" + frameCmd.name + "\" R" + std::to_string(reg));

					// Check if patch function is enabled for this register (Phase 1: R5 only)
					if (reg == 5 && (fr.smcPatchFunctionMask & maskBit)) {
						std::string funcName = prefix + "_Patch" + frameName + "_R5";
						patchFunctions.push_back("; " + funcName + "  -- entry: A = signed delta");
					}
				}
			}
		}

		if (!smcPoints.empty() || !patchFunctions.empty()) {
			AppendLine(out, "; SMC patch points -- patch these addresses at runtime to change register values:");
			AppendLine(out, "; ------------------------------------------------------------------------------");
			for (const auto& point : smcPoints) {
				AppendLine(out, point);
			}
			if (!patchFunctions.empty()) {
				AppendBlankLine(out);
				AppendLine(out, "; SMC patch functions (callable) -- call with delta value in register A:");
				AppendLine(out, "; -----------------------------------------------------------------------");
				for (const auto& func : patchFunctions) {
					AppendLine(out, func);
				}
			}
			AppendBlankLine(out);
		}
	}

	//
	// BuildLineStateWithRegisterWrites -- Unified CRTC state builder for precise timing (all paths)
	//
	// CRITICAL ARCHITECTURAL FUNCTION: Produces per-scanline CRTC counter snapshots used for
	// scheduling decisions and timing calculations in BOTH VSync+Loop and Interrupt-Driven paths.
	// Output is authoritative for all timing decisions.
	//
	// COUNTER PROGRESSION (Compendium sec.12 - CRTC Frame Structure):
	// ================================================================
	// Each scanline (CLK period of 64 NOPs in wall-clock time) advances CRTC state as:
	//
	// C0 (horizontal counter):
	//    Range 0..R0+1 (character positions per scanline)
	//    RULE: Increments every character clock (8 pixel clocks). Set to 0 at start of line.
	//    Resets to 0 on each scanline, never carries between scanlines.
	//    (Compendium sec.13: "C0 is cleared at the start of each scanline")
	//
	// C4 (vertical frame counter):
	//    Range 0..R4+1 (character rows per frame)
	//    RULE: Increments at END of each character row (when C9 == R9, then C9 resets to 0).
	//    When C4 exceeds R4, frame boundary is reached and C4 resets to 0.
	//    (Compendium sec.12.2: "The frame is considered complete when C4 > R4")
	//
	// C5 (vertical adjust counter):
	//    Range 0..R5 (adjust scanlines following frame completion)
	//    RULE: Increments 1x per scanline during adjust phase ONLY.
	//    Adjust phase starts when C4 > R4 and ends when C5 >= R5.
	//    When C5 >= R5, vertical frame is truly complete and next frame starts at C4=0, C9=0.
	//    (Compendium sec.12.4: "Vertical adjust phase: C4 > R4 until C5 >= R5")
	//
	// C9 (scanline within row counter):
	//    Range 0..R9+1 (scanlines per character row - 1, then wraps)
	//    RULE: Increments 1x per scanline. When C9 == R9, next scanline resets to 0 and C4++.
	//    (Compendium sec.12.3: "C9 increments each scanline until C9 == R9, then resets")
	//
	// FRAME BOUNDARY DETECTION (Compendium sec.22 - Screen Groups):
	// =============================================================
	// A complete CPC frame is 312 scanlines (standard PAL rate, 50 Hz).
	// Detection occurs when:
	//   1) C4 > R4 (frame height exceeded) AND
	//   2) C5 >= R5 (adjust phase complete)
	// At this point: frameComplete=true, next scanline starts new frame with C4=0, C9=0.
	// The adjust phase ensures smooth transitions between rows in lower-resolution modes.
	//
	// ABSOLUTION NOP TRACKING (Wall-clock timing for Z80 execution):
	// ==============================================================
	// absNop = cumulative NOP count from start of frame build.
	// Calculated as: absNop[scanline] = (absScanline * 64)
	// where absScanline = total scanlines elapsed since frame start.
	// Used to resolve absolute timing for Effect padding and register write validation.
	// NEVER mix absNop with relative NOP offsets (lineNopPos); they address different timing models.
	//
	// INPUT: schedule = vector of ScheduledAction (register writes, VMA writes, effects, subroutines)
	//        Acts must be presorted by scanline.
	//
	// OUTPUT: lineState[0..311] where each entry contains per-scanline snapshot:
	//         - c0, c4, c5, c9, inAdjust: CRTC counter state at START of scanline
	//         - absNop: cumulative NOPs from frame start (for absolute timing)
	//         - frameComplete: true if this is the last scanline of the frame
	//
	static void BuildLineStateWithRegisterWrites(
		std::vector<CpcRaster::CrtcCounters>& lineState,
		const std::vector<ScheduledAction>& schedule) {

		// Extract register write positions from schedule
		std::map<int, std::vector<std::pair<uint8_t, uint8_t>>> registerWrites;
		for (const auto& act : schedule) {
			if (!act.isVma) {
				registerWrites[act.scanline].push_back({act.reg, act.value});
			}
		}

		// Simulate all 312 scanlines with register writes applied at their scheduled positions
		RasterFrameCmd state;  // Start with default register values
		CpcRaster::CrtcSimulator sim;
		sim.init(CpcRaster::CrtcCounters{}, state);
		int absNopAccumulator = 0;

		for (int sl = 0; sl < 312; sl++) {
			// Apply any register writes scheduled at this scanline BEFORE advancing CRTC
			auto it = registerWrites.find(sl);
			if (it != registerWrites.end()) {
				for (const auto& write : it->second) {
					uint8_t reg = write.first;
					uint8_t val = write.second;
					sim.applyRegWrite(reg, val);
				}
			}

			// Record CRTC state at this scanline
			CpcRaster::CrtcCounters snapCtr = sim.counters();
			snapCtr.absNop = absNopAccumulator;
			lineState[(size_t)sl] = snapCtr;

			// Advance CRTC for this scanline (64 NOPs wall-clock per scanline)
			sim.advance(64);
			absNopAccumulator = sim.counters().absScanline * 64;

			// FRAME BOUNDARY DETECTION (Compendium sec.22.2-22.3):
			// When C4 > R4 AND C5 >= R5, the vertical frame is complete.
			// Reset counters for the next frame. This ensures frame-to-frame transitions
			// are correct and prevents counter wraparound errors that would desync timing.
			if (sim.counters().frameComplete) {
				CpcRaster::CrtcCounters resetCtr = sim.counters();
				resetCtr.c4 = 0;
				resetCtr.c9 = 0;
				resetCtr.c5 = 0;
				resetCtr.inAdjust = false;
				resetCtr.frameComplete = false;
				sim.init(resetCtr, state);
			}
		}
	}

	//
	// Returns the preferred absolute scanline to write CRTC register reg within the
	// frame spanning [frameStartSl, frameEndSl).  Implements the safe-window rules
	// described in the compendium:
	// - R7 (VSync position): defer until a few scanlines before C4 reaches r7, so that
	//   the write takes effect without requiring a wait inside the current interrupt.
	// - R12/R13 (VMA): must be visible to the CRTC before its C4=0 event; target the
	//   last scanline of the preceding frame so the action lands in the prior interrupt.
	// - All other registers: write at the first scanline of the frame (frame start is
	//   the line where the new values need to be active).
	//
	//
	// Get the constraint rule for a specific CRTC register.
	// Each register has different timing constraints based on CRTC state and frame structure.
	// Returns a RegisterRule with lambdas that compute safe windows and NOP offset constraints.
	//
	static RegisterRule GetRegisterRule(uint8_t reg) {
		using namespace CpcRaster;
		RegisterRule rule;

		if (reg == 0) {
			//
			// R0 (HORIZONTAL TOTAL) - Compendium sec.13.6
			// ====================================================
			// Defines total character positions per scanline: C0 counts 0..(R0+1)
			//
			// UPDATE CONDITION (sec.13.6): C0 <= R0 position
			// - On all CRTC types: Update considered while C0 is early in line
			// - CRTC 0/2: If C0 > 1, update deferred to next line
			// - CRTC 1: Has phase shift vs CRTC 0/2 (OUTI vs OUT(C),reg timing)
			// - CRTC 3/4: C0 never exceeds R0 when properly updated
			//
			// SCHEDULER RULE: Latest safe write = before C4 reaches R4
			// (earliest point of last character row where new R0 must be stable)
			//
			rule.getEarliestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				UNUSED(frameEndSl); UNUSED(st); UNUSED(lineState);
				return frameStartSl;
			};
			rule.getLatestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				UNUSED(st);
				// Scan lineState for when C4 reaches R4 (start of last char row per Compendium logic)
				// Latest write must happen before this transition so R0 is stable for C0 comparisons
				for (int sl = frameStartSl; sl < frameEndSl && sl < 312; sl++) {
					if (lineState[(size_t)sl].c4 >= (int)st.r4)
						return sl - 1;
				}
				return frameEndSl - 1;
			};
			rule.getNopMinOffset = [](int scanline, int frameStartSl, const RasterFrameCmd& st, uint8_t value, const CrtcCounters& ctr) { UNUSED(scanline); UNUSED(frameStartSl); UNUSED(st); UNUSED(value); UNUSED(ctr); return -1; };
			rule.getNopMaxOffset = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) { UNUSED(scanline); UNUSED(ctr); UNUSED(st); UNUSED(value); return -1; };
			rule.checkWrite = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) { UNUSED(scanline); UNUSED(ctr); UNUSED(st); UNUSED(value); return std::string(""); };
		}
		else if (reg == 1) {
			//
			// R1 (HORIZONTAL DISPLAY / HDIS) - Compendium sec.17.3
			// ====================================================
			// Defines number of characters displayed before BORDER begins.
			// Display ends when C0 == R1 (Compendium sec.17: "DYNAMIC R1 UPDATE")
			//
			// UPDATE CONDITION: Can be updated multiple times per line
			// - When C0==R1, display ends and BORDER begins
			// - Even during BORDER, VMA pointer continues to count
			// - If R1 updated during BORDER: next C0==R1 condition triggers end-of-display
			//
			// SCHEDULER RULE: Write on LAST line of PREVIOUS frame
			// (ensures old R1 period has completed, new R1 takes effect next frame)
			//
			rule.getEarliestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				UNUSED(frameEndSl); UNUSED(st); UNUSED(lineState);
				// Compendium sec.17.3: R1 can be updated multiple times per line.
				// Write on last scanline of previous frame to ensure old display period completed.
				if (frameStartSl == 0)
					return 0;  // First frame: write at frame start
				return frameStartSl - 1;  // Other frames: write on previous frame's last scanline
			};
			rule.getLatestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				UNUSED(frameEndSl); UNUSED(st);
				// Compendium sec.17.3: Display ends when C0==R1. Must write before old display ends.
				// Search backward from frame boundary to find last scanline before C0 reaches old R1.
				// The getNopMinOffset constraint handles NOP-level safety (C0 > old_R1).
				// At scanline level: latest is the scanline before frame starts (where old display ends).
				if (frameStartSl > 0) {
					// Check the scanline before this frame if C0 is still in safe range
					if (lineState[(size_t)(frameStartSl - 1)].c0 < 64)  // C0 hasn't wrapped around yet
						return frameStartSl - 1;
				}
				return frameStartSl;
			};
			// R1 NOP offset: Must write after old display ends (C0 > old_R1)
			// Use CrtcSimulator to advance through NOPs until display has ended (per Compendium logic)
			// Threshold: use conservative value with VSYNC jitter margin (-3 NOPs safety).
			// Standard CPC display width is 40 chars; use 37 to account for +/- 2 NOP VSYNC variance.
			rule.getNopMinOffset = [](int scanline, int frameStartSl, const RasterFrameCmd& st, uint8_t value, const CrtcCounters& ctr) {
				UNUSED(value);
				if (scanline != frameStartSl - 1 || frameStartSl == 0)
					return -1;
				int threshold = std::max((int)st.r1, 37);  // Reduced from 40 to 37 for VSYNC safety margin
				CrtcSimulator sim;
				sim.init(ctr, st);
				// Advance through NOPs until C0 passes old display end point
				for (int nop = 0; nop < 64; nop++) {
					if (sim.counters().c0 > threshold)
						return nop;
					sim.advance(1);
				}
				return 0;
			};
			// R1 timing constraints are complex due to C0 synchronization requirements.
			// The minNopOffset handles the safety requirement (after old display ends).
			// maxNopOffset unconstrained, but prefer early placement (via waitCost in place()).
			// Note: Due to VSYNC jitter (+/- 2 NOPs), placing near scanline boundaries risks edge cases.
			// This is handled by scheduling preferences, not hard constraints.
			rule.getNopMaxOffset = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) { UNUSED(scanline); UNUSED(ctr); UNUSED(st); UNUSED(value); return -1; };
			rule.checkWrite = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) { UNUSED(scanline); UNUSED(ctr); UNUSED(st); UNUSED(value); return std::string(""); };
		}
		else if (reg == 2) {
			//
			// R2 (HORIZONTAL SYNC POSITION) - Compendium sec.15
			// ===================================================
			// Defines where horizontal sync pulse starts on each scanline.
			// HSYNC begins when C0 reaches R2 (Compendium sec.15).
			//
			// UPDATE CONDITION (sec.15): Update while C0 <= R2 position
			// - Evaluated independently each line
			// - HSYNC pulse width controlled by R3[3:0]
			// - Must remain consistent with R0 changes
			// - Minimum sync width: 2 characters
			//
			// SCHEDULER RULE: Safe anywhere in frame
			// (HSYNC is evaluated locally on each scanline; changes affect next line's timing)
			//
			rule.getEarliestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				UNUSED(frameEndSl); UNUSED(st); UNUSED(lineState);
				return frameStartSl;
			};
			rule.getLatestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				UNUSED(frameStartSl); UNUSED(st); UNUSED(lineState);
				return frameEndSl - 1;
			};
			rule.getNopMinOffset = [](int scanline, int frameStartSl, const RasterFrameCmd& st, uint8_t value, const CrtcCounters& ctr) { UNUSED(scanline); UNUSED(frameStartSl); UNUSED(st); UNUSED(value); UNUSED(ctr); return -1; };
			rule.getNopMaxOffset = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) { UNUSED(scanline); UNUSED(ctr); UNUSED(st); UNUSED(value); return -1; };
			rule.checkWrite = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) { UNUSED(scanline); UNUSED(ctr); UNUSED(st); UNUSED(value); return std::string(""); };
		}
		else if (reg == 3) {
			//
			// R3 (HORIZONTAL/VERTICAL SYNC WIDTH) - Compendium sec.14.5, sec.9.3.4.3
			// =========================================================================
			// Controls width of HSYNC and VSYNC pulses.
			//   - Bits[3:0]: Horizontal sync width (C3l counter, Compendium sec.14.5)
			//   - Bits[7:4]: Vertical sync width (C3h counter, Compendium sec.9.3.4.3)
			//
			// UPDATE CONDITION (sec.14.5, sec.9.3.4):
			// - Can be updated during HSYNC or VSYNC pulse
			// - CRTC variant differences:
			//   * CRTC 0/2: Mode changes processed with pixel cooking delay
			//   * CRTC 1: Different pixel cooking timing
			//   * CRTC 4: No R3.JIT mode changes allowed
			// - If written during pulse: takes effect immediately
			//
			// SCHEDULER RULE: Conservative - write early in frame before VSYNC region.
			// Use emulator to find safe window before C4 reaches R7 (VSYNC fires).
			//
			rule.getEarliestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				UNUSED(frameEndSl); UNUSED(st); UNUSED(lineState);
				// Compendium sec.14.5: R3 can be updated during sync pulses.
				// Conservative: write at frame start to avoid mid-pulse changes.
				return frameStartSl;
			};
			rule.getLatestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				// Compendium sec.14.5: Use emulator state to find safe window before VSYNC region.
				// Find last scanline before C4 reaches R7 (VSYNC fires).
				if ((int)st.r7 == 0) {
					// VSYNC at frame boundary: avoid end of frame
					return frameEndSl - 1;
				}
				for (int sl = frameEndSl - 1; sl >= frameStartSl; sl--) {
					if (lineState[(size_t)sl].c4 < (int)st.r7)
						return sl;
				}
				return frameStartSl;
			};
			rule.getNopMinOffset = [](int scanline, int frameStartSl, const RasterFrameCmd& st, uint8_t value, const CrtcCounters& ctr) { UNUSED(scanline); UNUSED(frameStartSl); UNUSED(st); UNUSED(value); UNUSED(ctr); return -1; };
			rule.getNopMaxOffset = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) { UNUSED(scanline); UNUSED(ctr); UNUSED(st); UNUSED(value); return -1; };
			rule.checkWrite = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) { UNUSED(scanline); UNUSED(ctr); UNUSED(st); UNUSED(value); return std::string(""); };
		}
		else if (reg == 4) {
			//
			// R4 (VERTICAL TOTAL) - Compendium sec.12
			// ==========================================
			// Defines maximum character row number (C4 counts 0..R4).
			// Frame length = (R4+1) * (R9+1) + R5 scanlines.
			//
			// UPDATE CONDITION (sec.12): C0 <= R0 position (early in scanline)
			// - Write timing critical to frame length stability
			// - Constraint (sec.12.2): C4 must NOT exceed new R4 value after write
			// - Latest safe write: Before C4 equals NEW R4 value
			// - If written too late: Frame boundary may shift incorrectly
			//
			// CRTC 0/2 "Last Line" State (sec.12):
			// - When C4==R4 AND C9==R9, frame is marked as "last line"
			// - Evaluated only when C0 < 2 (NOT in border region)
			// - Next line C4 resets to 0 (after R5 adjustment if any)
			//
			// SCHEDULER RULE: Write before C4 reaches new R4 value
			// (Must be stable before last character row begins)
			//
			rule.getEarliestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				UNUSED(frameEndSl); UNUSED(st); UNUSED(lineState);
				return frameStartSl;
			};
			rule.getLatestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				// R4 latest: scan forward until C4 == st.r4 (start of last char row) → latest write is sl-1
				// lineState is computed with final R4 applied, so C4 progression reflects the new R4 value
				for (int sl = frameStartSl; sl < frameEndSl && sl < 312; sl++) {
					if (lineState[(size_t)sl].c4 == (int)st.r4)
						return sl - 1;
				}
				return frameEndSl - 1;
			};
			// R4 must be written before C0=2 (Compendium sec.12.2 / sec.10.3.1)
			rule.getNopMinOffset = [](int scanline, int frameStartSl, const RasterFrameCmd& st, uint8_t value, const CrtcCounters& ctr) { UNUSED(scanline); UNUSED(frameStartSl); UNUSED(st); UNUSED(value); UNUSED(ctr); return -1; };
			// Use CrtcSimulator to find last NOP where C0 < 2
			rule.getNopMaxOffset = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) {
				UNUSED(scanline); UNUSED(value);
				CrtcSimulator sim;
				sim.init(ctr, st);
				int lastSafe = 0;
				for (int nop = 0; nop < 64; nop++) {
					if (sim.counters().c0 >= 2)
						break;
					lastSafe = nop;
					sim.advance(1);
				}
				return lastSafe;
			};
			rule.checkWrite = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) {
				UNUSED(scanline); UNUSED(st);
				if (ctr.c4 > (int)value) {
					return std::string(" [NOTE: C4=") + std::to_string(ctr.c4) + " > target R4=" + std::to_string(value) + " at write time]";
				}
				return std::string("");
			};
		}
		else if (reg == 5) {
			//
			// R5 (VERTICAL TOTAL ADJUST) - Compendium sec.11
			// ==============================================
			// Defines additional scanlines after main frame rows complete.
			// Total frame length = (R4+1)*(R9+1) + R5 scanlines.
			//
			// UPDATE CONDITION (sec.11.4): C0 <= R0 position (early in scanline)
			// - Must be set before R5 adjustment phase begins
			// - Adjustment phase enters when C4==R4 AND C9==R9 (all main rows complete)
			//
			// CRTC Variant Timing (sec.11.2):
			//   CRTC 0 (11.2.2): [Specific rules for R5 in adjustment]
			//   CRTC 1 (11.2.3/11.2.4): C5 increments EVERY individual scanline
			//                           (dissociated from C9 during R5 phase)
			//                           Frame boundary when C5 >= R5
			//   CRTC 2 (11.2.5): [Similar to CRTC 0]
			//   CRTC 3/4 (11.2.6): [Specific rules]
			//
			// SCHEDULER RULE: Write before adjustment phase begins
			// (inAdjust flag signals start of C5 counting)
			//
			rule.getEarliestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				UNUSED(frameEndSl); UNUSED(st); UNUSED(lineState);
				return frameStartSl;
			};
			rule.getLatestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				UNUSED(st);
				// Find first scanline where CRTC enters adjust phase; latest write is one before
				for (int sl = frameStartSl; sl < frameEndSl && sl < 312; sl++) {
					if (lineState[(size_t)sl].inAdjust)
						return sl - 1;
				}
				return frameEndSl - 1;
			};
			rule.getNopMinOffset = [](int scanline, int frameStartSl, const RasterFrameCmd& st, uint8_t value, const CrtcCounters& ctr) { UNUSED(scanline); UNUSED(frameStartSl); UNUSED(st); UNUSED(value); UNUSED(ctr); return -1; };
			rule.getNopMaxOffset = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) { UNUSED(scanline); UNUSED(ctr); UNUSED(st); UNUSED(value); return -1; };
			rule.checkWrite = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) { UNUSED(scanline); UNUSED(ctr); UNUSED(st); UNUSED(value); return std::string(""); };
		}
		else if (reg == 6) {
			//
			// R6 (VERTICAL DISPLAYED) - Compendium sec.11.7
			// =============================================
			// Defines where BORDER display begins (character row boundary).
			// Display mode ends when C4 == R6.
			// BORDER display begins at row C4=R6 (Compendium sec.11.7).
			//
			// UPDATE CONDITION: Safe to update at frame start
			// - Constraint: R6 <= R4 (if R6 > R4, entire frame is displayed, no border)
			// - Change affects next frame's display/border boundary
			//
			// SCHEDULER RULE: Write at frame start (safest point)
			// (Affects display/border transition for upcoming frame)
			//
			rule.getEarliestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				UNUSED(frameEndSl); UNUSED(st); UNUSED(lineState);
				return frameStartSl;
			};
			rule.getLatestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				// R6 latest: scan forward until C4 >= st.r6 (border begins) → latest write is sl-1
				for (int sl = frameStartSl; sl < frameEndSl && sl < 312; sl++) {
					if (lineState[(size_t)sl].c4 >= (int)st.r6)
						return sl - 1;
				}
				return frameEndSl - 1;
			};
			rule.getNopMinOffset = [](int scanline, int frameStartSl, const RasterFrameCmd& st, uint8_t value, const CrtcCounters& ctr) { UNUSED(scanline); UNUSED(frameStartSl); UNUSED(st); UNUSED(value); UNUSED(ctr); return -1; };
			rule.getNopMaxOffset = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) { UNUSED(scanline); UNUSED(ctr); UNUSED(st); UNUSED(value); return -1; };
			rule.checkWrite = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) { UNUSED(scanline); UNUSED(ctr); UNUSED(st); UNUSED(value); return std::string(""); };
		}
		else if (reg == 7) {
			//
			// R7 (VERTICAL SYNC POSITION) - Compendium sec.16.4
			// ==================================================
			// Defines which character row triggers VSYNC pulse.
			// VSYNC begins when C4 == R7 (at C9=0, start of character row).
			//
			// UPDATE CONDITION (sec.16.4): Write before HSYNC window completes
			// - Write timing constraint: Compendium sec.16.4.1.2
			// - VSYNC can be disabled per-frame basis
			// - If R7 > R4: VSYNC never fires (frame too short)
			// - If R7 == 0: VSYNC triggered at frame boundary (C4==0)
			//
			// CRTC Variant Rules:
			// - All CRTC types: VSYNC fires when C4==R7 at C9==0
			// - Pulse width controlled by R3[7:4] (V VSYNC width)
			//
			// SCHEDULER RULE: Write before C4 reaches R7 and before HSYNC window
			// (Must be stable before VSYNC trigger point is reached)
			//
			rule.getEarliestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				if (st.disableVSync) {
					return frameStartSl;
				}
				// Special case: R7=0 triggers VSYNC at frame boundary (C4=0).
				// Must write AFTER C4 has moved away from 0 to avoid immediate VSYNC at end of current line.
				if ((int)st.r7 == 0) {
					// Find first scanline where C4 > 0 (safe for R7=0 write)
					for (int sl = frameStartSl; sl < frameEndSl && sl < 312; sl++) {
						if (lineState[(size_t)sl].c4 > 0) {
							return sl;
						}
					}
					return frameStartSl;  // Fallback if no C4>0 found (unlikely)
				}
				// General case: find first scanline where C4 > new R7 value (safe for R7 write)
				for (int sl = frameStartSl; sl < frameEndSl && sl < 312; sl++) {
					if (lineState[(size_t)sl].c4 > (int)st.r7) {
						return sl;
					}
				}
				return frameStartSl;
			};
			rule.getLatestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				if (st.disableVSync || (int)st.r7 == 0) {
					UNUSED(lineState);
					return frameEndSl - 1;
				}
				// Find first scanline where c4 reaches R7 (VSYNC fires); latest write is one before
				for (int sl = frameStartSl; sl < frameEndSl && sl < 312; sl++) {
					if (lineState[(size_t)sl].c4 >= (int)st.r7)
						return sl - 1;
				}
				return frameEndSl - 1;
			};
			rule.getNopMinOffset = [](int scanline, int frameStartSl, const RasterFrameCmd& st, uint8_t value, const CrtcCounters& ctr) { UNUSED(scanline); UNUSED(frameStartSl); UNUSED(st); UNUSED(value); UNUSED(ctr); return -1; };
			// R7 must be written BEFORE HSYNC window (Compendium sec.13.2.2 / sec.16.4.3).
			// Use CrtcSimulator to find last NOP before HSYNC starts.
			rule.getNopMaxOffset = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) {
				UNUSED(scanline); UNUSED(value);
				CrtcSimulator sim;
				sim.init(ctr, st);
				int lastSafe = 0;
				for (int nop = 0; nop < 64; nop++) {
					if (sim.counters().hsync)
						break;
					lastSafe = nop;
					sim.advance(1);
				}
				return lastSafe;
			};
			rule.checkWrite = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) {
				UNUSED(scanline); UNUSED(value);
				if (!st.disableVSync && ctr.c4 > (int)st.r7) {
					return std::string(" [NOTE: C4=") + std::to_string(ctr.c4) + " > R7=" + std::to_string(st.r7) + " VSYNC may have already fired]";
				}
				return std::string("");
			};
		}
		else if (reg == 9) {
			//
			// R9 (MAX RASTER / SCANLINES PER CHARACTER ROW) - Compendium sec.10
			// ==================================================================
			// Defines number of scanlines per character row (C9 counts 0..R9).
			// Each character row has (R9+1) scanlines. Frame length = (R4+1)*(R9+1)+R5.
			//
			// UPDATE CONDITION (sec.10.2): C0 <= R0 position (early in scanline)
			// - Write timing: Must happen before C9 first reaches new R9 value
			// - Affects character row structure and frame length calculation
			// - Late writes may cause C9 overflow (varies by CRTC type)
			//
			// CRTC Variant Rules (Compendium sec.10.3):
			//   CRTC 0 (10.3.1): If R9 == C9, C9→0 on next line
			//                    If R9 < C9, C9 overflows to 31 before counting back
			//                    If R9 > C9, C9 increments normally
			//                    EXCEPTION: On last line of frame, update doesn't prevent C4/C9 reset
			//
			//   CRTC 1 (10.3.2): If R9 == C9, C9→0, C4 increments (or resets if C4==R4)
			//                    If R9 < C9, C9 overflows to 31
			//                    If R9 > C9, C9 increments normally
			//                    NO EXCEPTION on last line (simpler than CRTC 0/2)
			//
			//   CRTC 2 (10.3.3): Similar to CRTC 0 with "Last Line" state logic
			//
			//   CRTC 3/4 (10.3.4): C9 cannot overflow: if R9 <= C9, then C9→0 immediately
			//
			// SCHEDULER RULE: Write before C9 reaches new R9 value for first time
			// (Ensures stable character row length)
			//
			rule.getEarliestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				UNUSED(frameEndSl); UNUSED(st); UNUSED(lineState);
				return frameStartSl;
			};
			rule.getLatestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				// Find first scanline where c9 reaches R9 (first char-row ends); latest write is one before
				for (int sl = frameStartSl; sl < frameEndSl && sl < 312; sl++) {
					if (lineState[(size_t)sl].c9 >= (int)st.r9)
						return sl - 1;
				}
				return frameEndSl - 1;
			};
			// R9 must be written before C0=2 (Compendium sec.12.2 / sec.10.3.1)
			rule.getNopMinOffset = [](int scanline, int frameStartSl, const RasterFrameCmd& st, uint8_t value, const CrtcCounters& ctr) { UNUSED(scanline); UNUSED(frameStartSl); UNUSED(st); UNUSED(value); UNUSED(ctr); return -1; };
			// Use CrtcSimulator to find last NOP where C0 < 2
			rule.getNopMaxOffset = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) {
				UNUSED(scanline); UNUSED(value);
				CrtcSimulator sim;
				sim.init(ctr, st);
				int lastSafe = 0;
				for (int nop = 0; nop < 64; nop++) {
					if (sim.counters().c0 >= 2)
						break;
					lastSafe = nop;
					sim.advance(1);
				}
				return lastSafe;
			};
			rule.checkWrite = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) {
				UNUSED(scanline); UNUSED(st); UNUSED(value);
				if (ctr.c9 > 0) {
					return std::string(" [NOTE: C9=") + std::to_string(ctr.c9) + " > 0 at write time, row already started]";
				}
				return std::string("");
			};
		}
		else if (reg == 12 || reg == 13) {
			//
			// R12/R13 (VIDEO MEMORY ADDRESS / VMA) - Compendium sec.20.3 - CRITICAL
			// ===================================================================
			// VMA (R12 high byte, R13 low byte) defines the starting address for video display.
			// Controls which memory location the CRTC reads for frame rendering.
			//
			// VMA LATCHING RULES (CRITICAL - sec.20.3):
			//
			// CRTC 0 (sec.20.3.1, line 10873):
			//   Latch condition: C4==0 AND C0==0 (frame boundary only)
			//   Action: VMA' & VMA loaded with R12/R13
			//   Updates are immediate
			//
			// CRTC 1 (sec.20.3.2, line 10883): ← **SPECIAL CASE - LOADS EVERY C4=0**
			//   Latch condition: C4==0 (ANY C0, C9 value)
			//   Action: VMA loaded with R12/R13
			//   Updates are immediate
			//   WARNING (sec.20.3.2, lines 10886-10890):
			//     "Don't change address too early when C4=0"
			//     Example: 007 Living Daylights - address set too early caused display corruption
			//   Safe Window Strategy for CRTC1:
			//     Write R12/R13 during previous frame while C4 > 0
			//     Avoid writing during intermediate C4=0 points within a frame
			//     Exception: R4=0 frames have C4 always 0 - only safe at frame boundary
			//
			// CRTC 2 (sec.20.3.3, line 10908):
			//   Latch condition: C4==0 AND C9==0 AND C0==0 (frame boundary)
			//   Action: VMA' & VMA initialized with R12/R13
			//   Updates are immediate
			//   VMA' = transient pointer updated when C0==R1 and C9==R9
			//
			// CRTC 3 & 4 (sec.20.3.4, line 10927):
			//   Latch condition: C4==0 AND C0==0
			//   Action: VMA' & VMA loaded with R12/R13
			//   Updates are immediate
			//
			// SCHEDULER RULE (UNIVERSAL - all CRTC types):
			// Write VMA during previous frame's C4>0 region, before next frame's C4=0 boundary.
			// This is safe for all CRTC types and complies with CRTC1's strictest latching rule.
			//
			rule.getEarliestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				UNUSED(frameEndSl);
				// Compendium sec.20.3: VMA for frame at frameStartSl must be written during previous frame.
				// Use emulator to find: when did previous frame start (C4=0 boundary)?
				// Then find first C4>0 after that point where write is safe.
				if (frameStartSl == 0) {
					// Wrap-around: first frame's VMA written by last frame.
					// In a standard 5-frame layout, the last frame (BorderBottom) always starts at scanline 262.
					// For frame 0 (BorderTop), safe window is from last frame's C4>0 (262+) until frame boundary (311).
					int lastFrameStart = 262;
					// Find first C4>0 after lastFrameStart
					CpcRaster::CrtcSimulator querySim;
					querySim.init(lineState[(size_t)lastFrameStart], st);
					for (int sl = lastFrameStart; sl <= 311; sl++) {
						if (querySim.counters().c4 > 0) {
							return sl;
						}
						querySim.advance(64);
					}
					// Fallback: if no C4>0 found, return last frame start
					return lastFrameStart;
				} else {
					// Normal case: find previous frame's C4=0 boundary, then first C4>0 after it
					int prevFrameStart = frameStartSl;
					for (int sl = frameStartSl - 1; sl >= 0; sl--) {
						if (lineState[(size_t)sl].c4 == 0) {
							prevFrameStart = sl;
							break;
						}
					}
					// Use emulator to find first C4>0 after previous frame boundary
					CpcRaster::CrtcSimulator querySim;
					querySim.init(lineState[(size_t)prevFrameStart], st);
					for (int sl = prevFrameStart; sl < frameStartSl; sl++) {
						if (querySim.counters().c4 > 0) {
							return sl;
						}
						querySim.advance(64);
					}
					return prevFrameStart;
				}
			};
			rule.getLatestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				UNUSED(frameStartSl); UNUSED(st); UNUSED(lineState);
				//
				// CRITICAL VMA LATCHING RULE (Compendium sec.20.3):
				// Writing VMA when C4=0 (any C9 on CRTC1, or C9=0 on other types) → VMA latches IMMEDIATELY for current frame.
				// Therefore, safe window for writing VMA for frame N:
				//   - Starts: Frame N-1, once C4 > 0 (after frame boundary)
				//   - Ends: Frame N, at C4=0 AND C9=0 point (just before it latches)
				// Since we schedule within current interrupt, latest safe scanline is frameEndSl-1 (last scanline before next frame's C4=0).
				//
				return frameEndSl - 1;
			};
			rule.getNopMinOffset = [](int scanline, int frameStartSl, const RasterFrameCmd& st, uint8_t value, const CrtcCounters& ctr) { UNUSED(scanline); UNUSED(frameStartSl); UNUSED(st); UNUSED(value); UNUSED(ctr); return -1; };
			rule.getNopMaxOffset = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) { UNUSED(scanline); UNUSED(ctr); UNUSED(st); UNUSED(value); return -1; };
			rule.checkWrite = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) {
				UNUSED(scanline); UNUSED(value);
				if (ctr.c4 == 0 && st.r4 > 0) {
					return std::string(" [WARNING: Writing VMA at C4=0; latches immediately per Compendium sec.20.3]");
				}
				return std::string("");
			};
		}
		else {
			// Default for unknown registers
			rule.getEarliestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				UNUSED(frameEndSl); UNUSED(st); UNUSED(lineState);
				return frameStartSl;
			};
			rule.getLatestScanline = [](int frameStartSl, int frameEndSl, const RasterFrameCmd& st, const std::vector<CrtcCounters>& lineState) {
				UNUSED(frameEndSl); UNUSED(st); UNUSED(lineState);
				return frameStartSl;
			};
			rule.getNopMinOffset = [](int scanline, int frameStartSl, const RasterFrameCmd& st, uint8_t value, const CrtcCounters& ctr) { UNUSED(scanline); UNUSED(frameStartSl); UNUSED(st); UNUSED(value); UNUSED(ctr); return -1; };
			rule.getNopMaxOffset = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) { UNUSED(scanline); UNUSED(ctr); UNUSED(st); UNUSED(value); return -1; };
			rule.checkWrite = [](int scanline, const CrtcCounters& ctr, const RasterFrameCmd& st, uint8_t value) { UNUSED(scanline); UNUSED(ctr); UNUSED(st); UNUSED(value); return std::string(""); };
		}

		return rule;
	}

	//
	// Build a sorted list of ScheduledAction for all frames.
	// Each register write is assigned to its safe-window scanline; if multiple writes
	// would exceed kLineNopBudget NOPs on the same line, the overflow is pushed to
	// the next available scanline within the same frame.
	// Only writes that actually change the CRTC register value are emitted
	// (last-written state is tracked starting from hardware defaults).
	//
	struct GeneratedFrame {
		int cmdIndex = -1;
		int startSl  = 0;
		int endSl    = 0;
		CpcRasterCommand cmd;
		RasterFrameCmd state;
	};

	//
	// EmitEffectPadding -- Shared effect padding for absolute effects (UNIFIED)
	//
	// Absolute effects (independently scheduled with preceding wait) are positioned
	// in blanking area via user-configurable padding define. Relative effects
	// (following previous) auto-align at the same C0 without padding.
	// Only emit padding for first write of absolute effects.
	// Used by both VSync+Loop and Interrupt paths.
	//
	// Uses direct cmdIndex lookup instead of comment string parsing for robustness.
	// Comment parsing is fragile if effect names contain ':' or format changes.
	//
	static void EmitEffectPadding(
		std::string& out,
		const ScheduledAction& act,
		const std::string& prefix,
		const std::vector<CpcRasterCommand>* pCommands = nullptr) {

		bool shouldEmitPadding = act.isAbsoluteEffect && !act.isFollowingEffect;

		if (shouldEmitPadding && pCommands && act.cmdIndex >= 0 && act.cmdIndex < (int)pCommands->size()) {
			const CpcRasterCommand& cmd = (*pCommands)[(size_t)act.cmdIndex];
			if (std::holds_alternative<CpcEffectCommand>(cmd)) {
				const auto& effectCmd = std::get<CpcEffectCommand>(cmd);
				// Use effect name directly from command struct, not from comment string
				std::string effectName = effectCmd.name;

				// Sanitize name for assembler symbol
				std::string sanitized = effectName;
				for (char& c : sanitized) {
					if (!std::isalnum(c) && c != '_') c = '_';
				}

				// Emit WaitNops using the padding define (defined in file header)
				std::string padDefine = prefix + "_" + NormalizeAsmLabelToken(sanitized) + "_Padding";
				AppendLine(out, "\tWaitNops " + padDefine + "\t; position effect in blanking area");
			}
		} else if (shouldEmitPadding && (!pCommands || act.cmdIndex < 0)) {
			// Fallback to comment parsing if commands not available (backward compatibility)
			size_t startPos = act.comment.find("[Effect] ");
			if (startPos != std::string::npos) {
				startPos += 9;
				size_t endPos = act.comment.find(":", startPos);
				std::string effectName = (endPos != std::string::npos) ?
					act.comment.substr(startPos, endPos - startPos) : "Effect";

				std::string sanitized = effectName;
				for (char& c : sanitized) {
					if (!std::isalnum(c) && c != '_') c = '_';
				}

				std::string padDefine = prefix + "_" + NormalizeAsmLabelToken(sanitized) + "_Padding";
				AppendLine(out, "\tWaitNops " + padDefine + "\t; position effect in blanking area");
			}
		}
	}

	// CalculateActualCostAtPosition: Query emulator to determine cost and validity
	// of a write at a specific (scanline, nopOffset) position
	struct ActualCostResult {
		bool isValid = false;
		int actualCostNops = 0;
		std::string validationWarning;
	};

	static ActualCostResult CalculateActualCostAtPosition(
		const ScheduledAction& act,
		int targetScanline,
		int targetNopOffset,
		const std::vector<CpcRaster::CrtcCounters>& lineState,
		const RasterFrameCmd& frameState,
		int frameStartSl
	) {
		using namespace CpcRaster;
		ActualCostResult result;
		// Boundary check
		if (targetScanline < 0 || targetScanline >= 312 || targetNopOffset < 0 || targetNopOffset >= 64) {
			result.isValid = false;
			return result;
		}
		// Get register rule for this write
		RegisterRule rule = GetRegisterRule(act.reg);
		// Check NOP offset constraints
		int minNop = rule.getNopMinOffset(targetScanline, frameStartSl, frameState, act.value, lineState[(size_t)targetScanline]);
		int maxNop = rule.getNopMaxOffset(targetScanline, lineState[(size_t)targetScanline], frameState, act.value);
		if (minNop >= 0 && targetNopOffset < minNop) {
			result.isValid = false;
			return result;
		}
		if (maxNop >= 0 && targetNopOffset > maxNop) {
			result.isValid = false;
			return result;
		}
		// Calculate actual cost (use helper to ensure consistency with code generation)
		int writeCostNops = ActionWriteNops(act);
		// Check for warnings
		std::string warnings = rule.checkWrite(targetScanline, lineState[(size_t)targetScanline], frameState, act.value);
		result.isValid = true;
		result.actualCostNops = writeCostNops;
		result.validationWarning = warnings;
		return result;
	}

	std::vector<ScheduledAction> BuildRuptureSchedule(
		const std::vector<GeneratedFrame>& frames,
		const std::string& prefix,
		const CpcRasterParams& config,
		std::vector<std::string>& outErrors,
		std::vector<std::string>& /*outWarnings*/,
		const SchedulerConfig& schedulerConfig = SchedulerConfig(),
		const std::vector<CpcRasterCommand>* pCommands = nullptr) {
		std::vector<int> lineBudget(312, kLineNopBudget);
		std::vector<int> lineNopPos(312, 0);   // NOP position within each scanline
		// Scanlines 0-2 are consumed by the Int1 sync sequence (GA_VSyncWaitON + WaitNops 18
		// lands at C9=1, then the mandatory 2-scanline VSYNC post-wait per Compendium sec.20.3.1
		// / sec.13.2.1 occupies C9=1 and C9=2).  Nothing can be scheduled there.
		if (schedulerConfig.wrapFirstVmaToPreviousScanline) {
			lineBudget[0] = 0;
			lineBudget[1] = 0;
			lineBudget[2] = 0;
		}
		RasterFrameCmd lastWritten;
		// Initialize with hardware defaults so oldR1 reads the correct value
		lastWritten.r0 = 63;      // HTOT
		lastWritten.r1 = 40;      // HDIS â€” critical for frame boundary R1 constraint
		lastWritten.r2 = 46;      // HSYNC pos
		lastWritten.r3 = 0x8E;    // sync widths
		lastWritten.r4 = 38;      // VTOT
		lastWritten.r5 = 0;       // VTADJ
		lastWritten.r6 = 25;      // VDIS
		lastWritten.r7 = 30;      // VSYNC pos
		lastWritten.r9 = 7;       // MAXRAS
		std::vector<ScheduledAction> result;
		//
		// Build per-line CRTC state table for scheduler validation.
		// Use CrtcSimulator to walk every frame so each lineState[sl] holds the
		// counter state at the START of scanline sl.
		// Must be built before the place lambda so lineState is in scope for the VMA C4=0 check.
		//
		// NOTE (C-8 justification): Similar lineState construction exists in Interrupt handler
		// generation (line 2720), but these are NOT consolidated because they serve different
		// purposes:
		// - BuildRuptureSchedule (here): Validates action placement DURING scheduling.
		//   Uses frame-aware iteration, includes initCtr from config (VSYNC state), no register writes applied.
		// - Interrupt path (line 2720): Generates precise timing AFTER scheduling.
		//   Uses full 312-scanline iteration, applies scheduled register writes before advancing,
		//   tracks absNop for handler distance calculation, resets on frame boundaries (Compendium sec.12).
		// Merging these would create fragile code serving neither purpose well.
		//
		std::vector<CrtcCounters> lineState(312);
		{
			int sl = 0;
			for (int fi = 0; fi < (int)frames.size(); fi++) {
				const RasterFrameCmd& st = frames[(size_t)fi].state;
				int frameEnd = frames[(size_t)fi].endSl;
				// CRITICAL: Frame 0 starts with VSYNC sync state (from config), NOT C9=0!
				// Subsequent frames reset to C9=0 at frame start (frame boundary)
				CrtcCounters initCtr{};
				if (fi == 0) {
					initCtr.c9 = config.initC9;   // From config: VSYNC sync state (default 2)
					initCtr.c4 = config.initC4;   // From config: VSYNC sync state (default 0)
					initCtr.c0 = config.initC0;   // From config: VSYNC sync state (default 31)
					initCtr.c5 = config.initC5;   // From config: VSYNC sync state (default 0)
				}
				CpcRaster::CrtcSimulator sim;
				sim.init(initCtr, st);
				while (sl < frameEnd && sl < 312) {
					lineState[(size_t)sl] = sim.counters();
					sim.advance(64);  // 64 NOPs per scanline
					sl++;
				}
			}
			CrtcCounters ctr{};
			while (sl < 312) {
				lineState[(size_t)sl] = ctr;
				sl++;
			}
		}
		//
		// Helper: place act at the earliest scanline >= act.preferredSl
		// EMULATOR-DRIVEN: Uses CalculateActualCostAtPosition to query actual cost at each candidate position
		//
		auto place = [&](ScheduledAction act, const RasterFrameCmd& st, int fStart) {

			int sl   = act.preferredSl;
			int limit = act.latestSl + 1;

			// Do not push to the next interrupt slot UNLESS the safe window requires it
			// (e.g., frame-defining registers like R1 at frame boundary may span slots)
			int currentSlot = act.preferredSl / 52;
			int currentSlotEnd = (currentSlot + 1) * 52;
			// Only cap limit if the latest safe write is within current slot.
			// If latestSl >= currentSlotEnd, the safe window requires crossing to next slot.
			if (limit > currentSlotEnd && act.latestSl < currentSlotEnd) {
				// Safe window is entirely within current slot; can stay within slot boundary
				limit = currentSlotEnd;

			}
			//
			// EMULATOR-DRIVEN SCHEDULING LOOP WITH WAIT MINIMIZATION
			// Collect valid placements, score by wait cost, choose minimum-wait option.
			// Prefer scanlines already in use (lineNopPos[sl] > 0) as they require no cross-scanline wait.
			//
			struct Candidate {
				int scanline;
				int nopOffset;
				int waitCost;
				ScheduledAction action;
				ActualCostResult costResult;
			};
			std::vector<Candidate> candidates;

			for (int testSl = sl; testSl < limit; testSl++) {
				// Effect writes don't require C0=0; padding positions them correctly in blanking area.
				// Start from current position for all effect writes to keep multi-write effects together.
				bool isEffect = (act.comment.find("[Effect]") != std::string::npos);
				int startNopOffset = isEffect ? lineNopPos[testSl] : 0;

				if (act.isVma) {
					RasterDbg("[PLACE_SCAN]", "  testing scanline %d, lineNopPos=%d lineB=%d", testSl, lineNopPos[testSl], lineBudget[testSl]);
				}

				for (int nopOffset = startNopOffset; nopOffset < 64; nopOffset++) {
					// Query emulator for actual cost and validity at this position
					ActualCostResult costResult = CalculateActualCostAtPosition(
						act, testSl, nopOffset, lineState, st, fStart
					);

					if (!costResult.isValid) {
						if (act.isVma && nopOffset == startNopOffset) {
							RasterDbg("[PLACE_SCAN]", "    nopOffset=%d: INVALID", nopOffset);
						}
						continue;
					}
					// Check NOP offset constraints from RegisterRule
					if (act.minNopOffset >= 0 && nopOffset < act.minNopOffset) {
						if (act.isVma) RasterDbg("[PLACE_SCAN]", "    nopOffset=%d: REJECTED (minNop=%d)", nopOffset, act.minNopOffset);
						continue;
					}
					if (act.maxNopOffset >= 0 && nopOffset > act.maxNopOffset) {
						if (act.isVma) RasterDbg("[PLACE_SCAN]", "    nopOffset=%d: REJECTED (maxNop=%d)", nopOffset, act.maxNopOffset);
						continue;
					}
					// Check if action fits within remaining scanline budget (64 NOPs total)
					int spaceNeeded = nopOffset + costResult.actualCostNops;
					if (spaceNeeded > 64) {
						if (act.isVma) RasterDbg("[PLACE_SCAN]", "    nopOffset=%d: REJECTED (spaceNeeded=%d > 64)", nopOffset, spaceNeeded);
						continue;
					}

					// Valid placement! Score it by wait cost.
					// Cost = 0 if scanline already in use, otherwise distance * 64 NOPs
					int waitCost = (lineNopPos[testSl] > 0) ? 0 : (testSl - act.preferredSl) * 64;

					if (act.isVma) {
						RasterDbg("[PLACE_SCAN]", "    nopOffset=%d: VALID (spaceNeeded=%d waitCost=%d)", nopOffset, spaceNeeded, waitCost);
					}

					ScheduledAction testAct = act;
					testAct.scanline = testSl;
					testAct.nopOffset = nopOffset;
					testAct.actualCost = costResult.actualCostNops;

					candidates.push_back({testSl, nopOffset, waitCost, testAct, costResult});
				}
			}

			// Choose placement with minimum wait cost
			bool placed = false;
			if (act.isVma) {
				RasterDbg("[PLACE]", "  safe window: sl %d..%d, found %u candidates", sl, act.latestSl, (unsigned)candidates.size());
			}
			if (!candidates.empty()) {
				// Sort by scanline (prefer earlier positions within safe window)
				// Earlier positions minimize wait cost since they're closer to interrupt start
				std::sort(candidates.begin(), candidates.end(),
					[](const Candidate& a, const Candidate& b) {
						return a.scanline < b.scanline;
					});

				const Candidate& chosen = candidates[0];
				if (act.isVma) {
					RasterDbg("[PLACE]", "  chose scanline %d (earliest of candidates)", chosen.scanline);
				}
				int spaceNeeded = chosen.nopOffset + chosen.costResult.actualCostNops;
				lineBudget[chosen.scanline] = 64 - spaceNeeded;
				lineNopPos[chosen.scanline] = spaceNeeded;
				result.push_back(chosen.action);
				placed = true;
			}
			if (!placed) {
				outErrors.push_back("[Action placement failed] " + act.comment + " (reg=" + std::to_string(act.reg) +
					", preferred=" + std::to_string(act.preferredSl) + ", latest=" + std::to_string(act.latestSl) +
					"): no valid scanline found within safe window. Budget exhausted.");
			}
			//
			// VMA HARD CONSTRAINT: if placement failed and this is a VMA write, try latestSl as last resort
			//
			if (act.isVma && !placed) {
				RasterDbg("[PLACE]", "  no candidates found, attempting VMA hard-constraint fallback at latestSl=%d", act.latestSl);

				if (act.latestSl >= 0 && act.latestSl < 312) {
					int safeSl = act.latestSl;
					int spaceAvailable = lineBudget[safeSl];
					if (spaceAvailable >= kVmaWriteNops) {
						RasterDbg("[PLACE]", "  fallback succeeded: placed at sl %d", safeSl);

						act.scanline = safeSl;
						act.nopOffset = lineNopPos[safeSl];
						act.actualCost = kVmaWriteNops;
						lineBudget[safeSl] -= kVmaWriteNops;
						lineNopPos[safeSl] += kVmaWriteNops;
						result.push_back(act);
						placed = true;
					} else {
						outErrors.push_back("[VMA placement fallback failed] " + act.comment +
							": insufficient budget at safest scanline " + std::to_string(safeSl) +
							" (available=" + std::to_string(spaceAvailable) + ", needed=" +
							std::to_string(kVmaWriteNops) + ")");
					}
				} else {
					outErrors.push_back("[VMA placement fallback out of range] " + act.comment +
						": latestSl=" + std::to_string(act.latestSl) + " is outside valid scanline range [0,312)");
				}
			}
			if (placed) {

			}
		};
		//
		// Per-frame scheduling loop.
		// schedReg: schedule a single-register write with safe-window deferral support.
		//
		for (int fi = 0; fi < (int)frames.size(); fi++) {
			const GeneratedFrame& fr = frames[(size_t)fi];
			const RasterFrameCmd& st = fr.state;
			// Extract original frame command for activeMask reference (merged state has MASK_ALL)
			const auto& frameCmd = std::get<CpcFrameCommand>(fr.cmd);
			const RasterFrameCmd& originalFrame = frameCmd.frame;
			std::string fTag = GetCommandName(fr.cmd);
			int fStart = fr.startSl;
			int fEnd   = fr.endSl;
			//
			// Helper: compute earliest/latest using RegisterRule, build action, add CRTC-sim note, then place.
			//
			auto schedReg = [&](uint8_t reg, uint8_t val, const std::string& desc, int earliestOverride = -1) {
				RegisterRule rule = GetRegisterRule(reg);
				int earliest = rule.getEarliestScanline(fStart, fEnd, st, lineState);
				// Allow callers to push earliest forward (e.g. R4 must come after R1).
				if (earliestOverride >= 0 && earliestOverride > earliest)
					earliest = earliestOverride;
				int latest   = rule.getLatestScanline(fStart, fEnd, st, lineState);
				ScheduledAction act;
				act.preferredSl = earliest;
				act.latestSl    = (latest >= earliest) ? latest : earliest;
				act.reg         = reg;
				act.value       = val;
				act.isVma       = false;
				act.frameIndex  = frames[fi].cmdIndex;  // Use actual command index, not frames vector index
				act.comment     = desc;

				// Get NOP offset constraints from the rule
				if (earliest >= 0 && earliest < 312) {
					int minOfs = rule.getNopMinOffset(earliest, fStart, st, val, lineState[(size_t)earliest]);
					int maxOfs = rule.getNopMaxOffset(earliest, lineState[(size_t)earliest], st, val);
					if (minOfs >= 0)
						act.minNopOffset = minOfs;
					if (maxOfs >= 0)
						act.maxNopOffset = maxOfs;
				}

				// CRTC sim validation: annotate if counter state at write time looks unsafe
				if (earliest >= 0 && earliest < 312) {
					const CrtcCounters& ctr = lineState[(size_t)earliest];
					std::string warning = rule.checkWrite(earliest, ctr, st, val);
					if (!warning.empty())
						act.comment += warning;
				}
				place(act, st, fStart);
			};
			//
			// Single-register writes -- only emit when value actually changed.
			// R1 (HDIS) is scheduled before R4 (VTOT): HDIS takes one scanline to become
			// effective (Compendium sec.17.3/17.4.1).  Writing VTOT first can cause the CRTC to
			// start the new character-row count while HDIS still reflects the old display width,
			// clamping the first display row incorrectly.  We track where R1 lands and pass that
			// scanline+1 as the earliestOverride for R4 so they always land on separate scanlines.
			//
			int r1PlacedSl = -1;
			//
			// Helper: check if a register has SMC enabled (for any register with label patching)
			//
			auto regHasSmc = [&](uint32_t maskBit) -> bool {
				return (originalFrame.smcMask & maskBit) != 0;
			};
			//
			// When a register has SMC enabled, it must be emitted even if value matches lastWritten,
			// because the SMC label is required for runtime patching (patch functions).
			//
			if ((st.activeMask & RasterFrameCmd::MASK_R0) && (regHasSmc(RasterFrameCmd::MASK_R0) || lastWritten.r0 != st.r0)) {
				schedReg(0, st.r0, fTag + ": R0=" + std::to_string(st.r0));
				lastWritten.r0 = st.r0;
			}
			// On first frame, write registers marked in ORIGINAL frame command, regardless of value.
			// On subsequent frames, only write if value changed from last actually-written value.
			bool isFirstFrame = (fi == 0);
			bool r1InOriginal = (originalFrame.activeMask & RasterFrameCmd::MASK_R1) != 0;
			bool r1Changed = lastWritten.r1 != st.r1;
			if (r1InOriginal && (isFirstFrame || r1Changed)) {
				schedReg(1, st.r1, fTag + ": R1=" + std::to_string(st.r1));
				lastWritten.r1 = st.r1;
				if (!result.empty() && result.back().reg == 1 && result.back().frameIndex == fi)
					r1PlacedSl = result.back().scanline;
			}
			if ((st.activeMask & RasterFrameCmd::MASK_R2) && (regHasSmc(RasterFrameCmd::MASK_R2) || lastWritten.r2 != st.r2)) {
				schedReg(2, st.r2, fTag + ": R2=" + std::to_string(st.r2));
				lastWritten.r2 = st.r2;
			}
			if ((st.activeMask & RasterFrameCmd::MASK_R3) && (regHasSmc(RasterFrameCmd::MASK_R3) || lastWritten.r3 != st.r3)) {
				schedReg(3, st.r3, fTag + ": R3=" + std::to_string(st.r3));
				lastWritten.r3 = st.r3;
			}
			if ((st.activeMask & RasterFrameCmd::MASK_R4) && (regHasSmc(RasterFrameCmd::MASK_R4) || lastWritten.r4 != st.r4)) {
				// If R1 was also scheduled for this frame, push R4 to the scanline after R1
				// so HDIS is effective before VTOT changes the frame boundary (sec.17.3/17.4.1).
				schedReg(4, st.r4, fTag + ": R4=" + std::to_string(st.r4),
				         (r1PlacedSl >= fStart) ? r1PlacedSl + 1 : -1);
				lastWritten.r4 = st.r4;
			}
			if ((st.activeMask & RasterFrameCmd::MASK_R5) && (regHasSmc(RasterFrameCmd::MASK_R5) || lastWritten.r5 != st.r5)) {
				schedReg(5, st.r5, fTag + ": R5=" + std::to_string(st.r5));
				lastWritten.r5 = st.r5;
			}
			if ((st.activeMask & RasterFrameCmd::MASK_R6) && (regHasSmc(RasterFrameCmd::MASK_R6) || lastWritten.r6 != st.r6)) {
				schedReg(6, st.r6, fTag + ": R6=" + std::to_string(st.r6));
				lastWritten.r6 = st.r6;
			}
			//
			// R7 (VSync position) must be written in two cases:
			// 1. Explicitly set in frame activeMask, OR
			// 2. disableVSync=true (must write R7=255 to disable VSync in hardware)
			//
			if ((st.activeMask & RasterFrameCmd::MASK_R7) || st.disableVSync) {
				uint8_t r7val = st.disableVSync ? 255 : st.r7;
				uint8_t lwVal = lastWritten.disableVSync ? 255 : lastWritten.r7;
				if (r7val != lwVal) {
					std::string desc = st.disableVSync ? fTag + ": R7=255 (VSYNC off)" : fTag + ": R7=" + std::to_string(st.r7);
					schedReg(7, r7val, desc);
					lastWritten.r7 = st.r7;
					lastWritten.disableVSync = st.disableVSync;
				}
			}
			if ((st.activeMask & RasterFrameCmd::MASK_R9) && (regHasSmc(RasterFrameCmd::MASK_R9) || lastWritten.r9 != st.r9)) {
				schedReg(9, st.r9, fTag + ": R9=" + std::to_string(st.r9));
				lastWritten.r9 = st.r9;
			}
			// R12 (VMA): Always schedule if enabled OR if SMC is enabled for VMA labels
			if (st.activeMask & RasterFrameCmd::MASK_R12) {
				// VMA safe window is determined by RegisterRule: write during previous frame’s C4>0,
				// before next frame’s C4=0. RegisterRule uses emulator to find the window automatically.
				std::string lbl = prefix + "_VMA_" + NormalizeAsmLabelToken(GetCommandName(fr.cmd));

				// Schedule using the rule, which handles all CRTC types uniformly
				RegisterRule rule = GetRegisterRule(12);
				int earliest = rule.getEarliestScanline(fStart, fEnd, st, lineState);
				int latest   = rule.getLatestScanline(fStart, fEnd, st, lineState);

				ScheduledAction act;
				act.preferredSl = earliest;
				act.latestSl    = (latest >= earliest) ? latest : earliest;
				act.reg         = 12;
				act.value       = 0;
				act.isVma       = true;
				act.frameIndex  = frames[fi].cmdIndex;
				act.vmaLabel    = lbl;
				act.comment     = fTag + ": VMA=" + lbl;
				place(act, st, fStart);
			}
		}
		//
		// Resolve Relative Effect target lines before scheduling
		//
		int lastEffectResolvedSl = -1;
		std::map<int, int> resolvedEffectTargets;  // cmdIndex -> resolved scanline
		if (pCommands) {
			for (int ci = 0; ci < (int)pCommands->size(); ci++) {
				const CpcRasterCommand& cmd = (*pCommands)[ci];
				if (std::holds_alternative<CpcEffectCommand>(cmd)) {
					const auto& effectCmd = std::get<CpcEffectCommand>(cmd);
					if (!effectCmd.enabled)
						continue;
					int resolvedSl = effectCmd.targetScanline;
					if (effectCmd.targetMode == EffectTargetMode::Relative && lastEffectResolvedSl >= 0) {
						resolvedSl = lastEffectResolvedSl + effectCmd.targetScanline;
					}
					resolvedEffectTargets[ci] = resolvedSl;
					lastEffectResolvedSl = resolvedSl;
				}
			}
		}
		//
		// Helper: find which frame contains a given target scanline
		// Returns (frameState, frameStartSl) for constraint calculations
		//
		auto getFrameForScanline = [&](int targetScanline) {
			// Default to first frame if not found
			const RasterFrameCmd* frameSt = frames.empty() ? nullptr : &frames[0].state;
			int frameStart = frames.empty() ? 0 : frames[0].startSl;
			// Search for the frame that contains this scanline
			for (const auto& fr : frames) {
				if (targetScanline >= fr.startSl && targetScanline < fr.endSl) {
					frameSt = &fr.state;
					frameStart = fr.startSl;
					break;
				}
			}
			return std::make_pair(frameSt, frameStart);
		};


		//
		// Process Effect and Variable commands from instance
		// Each action is associated with the frame that contains its target scanline
		//
		if (pCommands) {
			for (int ci = 0; ci < (int)pCommands->size(); ci++) {
				const CpcRasterCommand& cmd = (*pCommands)[ci];
				if (std::holds_alternative<CpcEffectCommand>(cmd)) {
					const auto& effectCmd = std::get<CpcEffectCommand>(cmd);
					if (!effectCmd.enabled)
						continue;
					// Effect command: one or more register writes at a specific scanline
					int resolvedTargetLine = resolvedEffectTargets[ci];
					// Find the frame that contains this effect's target scanline
					auto [effectFrameSt, effectFrameStart] = getFrameForScanline(resolvedTargetLine);
					// Expand window for effect writes to keep them within the same 52-line slot
					// This prevents multiple writes in the same effect from being split across scanlines
					int effectSlot = resolvedTargetLine / 52;
					int effectSlotEnd = (effectSlot + 1) * 52;
					for (size_t wi = 0; wi < effectCmd.writes.size(); wi++) {
						const auto& write = effectCmd.writes[wi];
						ScheduledAction act;
						act.preferredSl = resolvedTargetLine;
						// First write of effect can flex to next scanline if needed.
						// Subsequent writes must stay on same scanline to maintain write order.
						act.latestSl = (wi == 0) ?
							std::min(resolvedTargetLine + 1, effectSlotEnd - 1) :
							resolvedTargetLine;
						// Track command and write indices for direct lookup (no string parsing)
						act.cmdIndex = ci;
						act.effectWriteIndex = (int)wi;
						if (write.target == CpcRegTarget::GA) {
							act.reg = CpcRaster::kRegGA;  // Special marker for GA write
							act.gaCmd = write.reg;  // 0=SetInk, 1=SetBorder, 2=SetMode
							// For SetInk: extract pen from bits 7-4, color from bits 3-0
							// For SetBorder: color is in bits 4-0
							// For SetMode: value is the full mode command byte
							if (write.reg == 0) {  // SetInk
								act.gaColorIdx = write.value & 0x0F;  // bits 3-0 = color index
								act.value = (write.value >> 4) & 0x0F;  // bits 7-4 = pen
							} else if (write.reg == 1) {  // SetBorder
								act.gaColorIdx = write.value & 0x1F;  // bits 4-0 = color index
								act.value = 0;  // unused for border
							} else if (write.reg == 2) {  // SetMode
								act.gaColorIdx = 0;  // unused
								act.value = write.value;  // mode command byte
							}
							act.comment = "[Effect] " + effectCmd.name + ": GA cmd " + std::to_string(write.reg);
						} else {
							act.reg = write.reg;
							act.comment = "[Effect] " + effectCmd.name + ": R" + std::to_string(write.reg) + "=" + std::to_string(write.value);
							act.value = write.value;
						}
						act.isVma = false;
						act.frameIndex = -1;
						//
						// Effect writes must execute at C0=0 (start of scanline) to maintain
						// raster timing accuracy. Only the first write needs clean scanline constraint;
						// subsequent writes in the same effect can be placed immediately after.
						//
						act.isFollowingEffect = (wi > 0);
						act.isAbsoluteEffect = (effectCmd.targetMode == EffectTargetMode::Absolute);
						act.minNopOffset = 0;
						place(act, *effectFrameSt, effectFrameStart);
					}
				} else if (std::holds_alternative<CpcVariableCommand>(cmd)) {
					const auto& varCmd = std::get<CpcVariableCommand>(cmd);
					if (!varCmd.enabled)
						continue;
					// Variable command: write a byte value to a named memory location
					int varTargetLine = varCmd.targetLine;
					auto [varFrameSt, varFrameStart] = getFrameForScanline(varTargetLine);
					ScheduledAction act;
					if (varCmd.unrestrained) {
						// Unrestrained: place ASAP without specific timing requirement
						// Set earliest at frame start (place immediately, no waits)
						// Set latest very late (allow placement anywhere in frame if needed)
						// This avoids unnecessary waits for non-timing-critical variables
						act.preferredSl = varFrameStart;           // Prefer frame start (asap, no waits)
						act.latestSl = varFrameStart + 312;        // Allow placement anywhere within standard PAL frame (no timing constraint)
					} else {
						// Restrained: place exactly at targetLine with waits
						act.preferredSl = varCmd.targetLine;
						act.latestSl = varCmd.targetLine;
					}
					act.reg = CpcRaster::kRegVariable;  // Special marker for variable (not a CRTC register)
					act.value = varCmd.variable.value;
					act.isVma = false;
					act.unrestrained = varCmd.unrestrained;
					act.frameIndex = -1;
					act.varName = varCmd.variable.varName;
					act.comment = std::string("[Variable] ") + (varCmd.unrestrained ? "[unrestrained] " : "") + varCmd.variable.varName + "=" + std::to_string(varCmd.variable.value);
					place(act, *varFrameSt, varFrameStart);
				} else if (std::holds_alternative<CpcSubroutineCommand>(cmd)) {
					const auto& subCmd = std::get<CpcSubroutineCommand>(cmd);
					if (!subCmd.enabled)
						continue;
					// Subroutine command: call user-supplied routine at a specific scanline
					int subTargetLine = subCmd.targetLine;
					auto [subFrameSt, subFrameStart] = getFrameForScanline(subTargetLine);
					ScheduledAction act;
					act.preferredSl = subCmd.targetLine;
					act.latestSl = subCmd.targetLine;
					act.reg = 254;  // Special marker for subroutine call (not a CRTC register)
					act.value = 0;
					act.isVma = false;
					act.frameIndex = -1;
					act.subroutineName = subCmd.subroutineName;
					act.comment = "[Subroutine] call " + subCmd.subroutineName;
					place(act, *subFrameSt, subFrameStart);
				}
			}
		}
		std::stable_sort(result.begin(), result.end(), [](const ScheduledAction& a, const ScheduledAction& b) {
			if (a.scanline != b.scanline)
				return a.scanline < b.scanline;
			// Emission order within the same scanline:
			// R1 (HDIS) must come before R4 (VTOT) -- HDIS takes one line to take effect
			// and must be settled before VTOT changes the frame boundary (sec.17.3/17.4.1).
			// R9/R7 follow R4 so frame-counting is stable before sync position is set.
			// Subroutine calls sort last (priority kRegSubroutine)
			// For effect writes (GA commands with same reg=kRegGA), preserve definition order.
			int aPrio = (a.reg == 1) ? 0 : (a.reg == 4) ? 1 : (a.reg == 9) ? 2 : (a.reg == 7) ? 3 : (a.reg == CpcRaster::kRegSubroutine) ? CpcRaster::kRegSubroutine : a.reg + 4;
			int bPrio = (b.reg == 1) ? 0 : (b.reg == 4) ? 1 : (b.reg == 9) ? 2 : (b.reg == 7) ? 3 : (b.reg == CpcRaster::kRegSubroutine) ? CpcRaster::kRegSubroutine : b.reg + 4;
			if (aPrio != bPrio)
				return aPrio < bPrio;
			// For same priority (especially effect writes with reg=253), use definition order
			if (a.effectWriteIndex >= 0 && b.effectWriteIndex >= 0)
				return a.effectWriteIndex < b.effectWriteIndex;
			return false;
		});

		//
		// Resolve SMC patch labels for Frame commands (including VMA/R12/R13)
		//
		{
			for (auto& act : result) {
				// Only process CRTC register writes (reg < 16) and VMA (reg == 12 with isVma flag)
				if (act.reg >= 16 || act.frameIndex < 0)
					continue;
				// Get the original frame command to check smcMask
				if (!pCommands || act.frameIndex >= (int)pCommands->size())
					continue;
				const CpcRasterCommand& frameCmd = (*pCommands)[(size_t)act.frameIndex];
				if (!std::holds_alternative<CpcFrameCommand>(frameCmd))
					continue;
				const auto& origFrame = std::get<CpcFrameCommand>(frameCmd).frame;
				// Check if this register is marked for SMC in smcMask AND is actually being written (activeMask)
				// Use kRegisterMasks lookup table to handle R9 correctly (MASK_R9 != (1U << 9))
				uint32_t regMask = act.isVma ? RasterFrameCmd::MASK_R12 : (act.reg < 14 ? kRegisterMasks[act.reg] : 0);
				if (regMask == 0 || !(origFrame.smcMask & regMask) || !(origFrame.activeMask & regMask))
					continue;
				// Resolve the label: check override map, else generate default
				std::string label;
				if (act.isVma) {
					// For R12/R13 VMA, use R12 as key in smcLabelOverrides
					if (origFrame.smcLabelOverrides.count(12) > 0) {
						label = origFrame.smcLabelOverrides.at(12);
					} else {
						label = prefix + "_" + NormalizeAsmLabelToken(GetCommandName(frameCmd)) + "_R12R13_patch";
					}
				} else {
					if (origFrame.smcLabelOverrides.count(act.reg) > 0) {
						label = origFrame.smcLabelOverrides.at(act.reg);
					} else {
						label = prefix + "_" + NormalizeAsmLabelToken(GetCommandName(frameCmd)) + "_R" + std::to_string((int)act.reg) + "_patch";
					}
				}
				act.smcLabel = label;
			}
		}

		//
		// Resolve SMC patch labels for Effect commands
		// NOTE: This matches by comment string (Effect name) + checking for SMC-marked writes.
		// To fully disambiguate in complex cases, the struct would need to store cmdIndex/writeIndex.
		//
		{
			for (auto& act : result) {
				// Only process Effect writes that are marked for SMC
				// Use cmdIndex and effectWriteIndex for direct lookup (no comment string parsing)
				if (act.cmdIndex < 0 || act.effectWriteIndex < 0)
					continue;
				if (!pCommands || act.cmdIndex >= (int)pCommands->size())
					continue;
				const CpcRasterCommand& cmd = (*pCommands)[(size_t)act.cmdIndex];
				if (!std::holds_alternative<CpcEffectCommand>(cmd))
					continue;
				const auto& effectCmd = std::get<CpcEffectCommand>(cmd);
				if (!effectCmd.enabled || act.effectWriteIndex >= (int)effectCmd.writes.size())
					continue;
				// Found matching Effect command and write; verify it's marked for SMC
				const auto& write = effectCmd.writes[(size_t)act.effectWriteIndex];
				if (!write.smcPatch)
					continue;
				// Verify write type matches (defensive sanity check)
				bool isGa = (act.reg == CpcRaster::kRegGA);
				bool writeIsGa = (write.target == CpcRegTarget::GA);
				if (isGa != writeIsGa)
					continue;
				if (!isGa && (int)act.reg != write.reg)
					continue;
				if (act.value != write.value)
					continue;
				// This is the matching SMC write; resolve its label
				std::string label;
				if (!write.smcLabelOverride.empty()) {
					label = write.smcLabelOverride;
				} else {
					// Generate default label
					std::string target = writeIsGa ? "GA" : ("R" + std::to_string((int)write.reg));
					label = prefix + "_" + NormalizeAsmLabelToken(effectCmd.name) + "_" + NormalizeAsmLabelToken(target) + "_patch";
				}
				if (act.smcLabel.empty())
					act.smcLabel = label;
			}
		}

		return result;
	}

	// ---------------------------------------------------------------------------
	// RasterHandlerConfig -- Encapsulates timing mode differences
	// ---------------------------------------------------------------------------
	//
	// Both VSync+Loop and Interrupt-Driven raster handlers share the same core
	// scheduling and action emission logic. This struct captures their differences
	// so schedule emission can be unified in one place.
	//
	struct RasterHandlerConfig {
		CpcRasterTimingMode mode;
		std::string headerComment;        // "timed layout" vs "interrupt layout"
		std::string handlerName;          // "Loop" or "Int1" prefix
		bool isInterruptDriven;           // true if Interrupt, false if VSync+Loop
		int numSlots;                     // 1 for VSync+Loop, 6 for Interrupt

		static RasterHandlerConfig CreateTimedRasterLoop() {
			return { CpcRasterTimingMode::TimedRasterLoop, "timed layout", "Loop", false, 1 };
		}

		static RasterHandlerConfig CreateInterruptDriven() {
			return { CpcRasterTimingMode::InterruptDriven, "interrupt layout", "Int1", true, 6 };
		}
	};

	//
	// Unified schedule emission for both raster handler types.
	// Timing metrics from a complete raster schedule emission.
	struct ScheduleTimingMetrics {
		int waitNops = 0;       // NOPs spent waiting between actions
		int activeNops = 0;     // NOPs spent performing register writes
		int elidedNops = 0;     // NOPs that would have been spent on redundant writes
	};

	// Encapsulates the iteration strategy (continuous vs slot-based) while keeping
	// action emission (EmitAction) in ONE place for both paths. Also calculates
	// and returns timing metrics for the full schedule.
	// Both paths now use the SAME simulator-driven timing (lineState with absNop tracking)
	// and the SAME effect padding/frame transition logic. Only difference is handler framing.
	//
	static ScheduleTimingMetrics EmitRasterSchedule(
		std::string& out,
		const std::string& prefix,
		const std::vector<ScheduledAction>& schedule,
		CpcRaster::CrtcKnownState& knownState,
		const std::vector<CpcRasterCommand>& cpcCommands,
		const RasterHandlerConfig& config,
		const std::vector<CpcRaster::CrtcCounters>* pLineState = nullptr) {  // UNIFIED: optional lineState

		ScheduleTimingMetrics metrics;
		int lastSl = 0;
		int lastFrameIndex = -1;
		int handlerAbsNop = 0;  // For interrupt mode: track current handler position

		if (!config.isInterruptDriven) {
			// VSync+Loop path: emit all actions sequentially in continuous loop
			// NOW USES SIMULATOR TIMING (lineState.absNop) for same precision as Interrupt path
			for (const auto& act : schedule) {
				// Emit wait if scanline advanced
				int deltaSl = act.scanline - lastSl;
				if (act.frameIndex != lastFrameIndex) {
					lastFrameIndex = act.frameIndex;
					AppendLine(out, "\t; Frame: " + GetCommandName(cpcCommands[(size_t)act.frameIndex]));
				}
				if (deltaSl > 0) {
					int waitNops = 0;
					if (pLineState) {
						// UNIFIED: Use simulator-driven timing (absNop) for precise NOP calculation
						int targetAbsNop = (*pLineState)[(size_t)act.scanline].absNop + act.nopOffset;
						waitNops = targetAbsNop - handlerAbsNop;
					} else {
						// Fallback: naive scanline-based (for backward compat)
						waitNops = deltaSl * 64 + act.nopOffset;
					}
					metrics.waitNops += waitNops;
					AppendLine(out, "\t; Wait " + std::to_string(deltaSl) + " scanline(s) -> " + std::to_string(waitNops) + " NOPs");
					AppendLine(out, "\tld b," + std::to_string(deltaSl));
					AppendLine(out, "\tcall WaitScanlines_Routine");
					if (act.nopOffset > 0 && !pLineState)  // Only emit WaitNops if not using lineState
						AppendLine(out, "\tWaitNops " + std::to_string(act.nopOffset));
					if (pLineState) {
						handlerAbsNop = (*pLineState)[(size_t)act.scanline].absNop + act.nopOffset;
					}
					lastSl = act.scanline;
				}
				AppendLine(out, "\t; " + act.comment);

				// Emit VMA SMC labels if needed (BEFORE the macro expansion)
				if (act.isVma) {
					std::string vmaLabel;
					if (act.frameIndex >= 0 && act.frameIndex < (int)cpcCommands.size()) {
						const CpcRasterCommand& frameCmd = cpcCommands[(size_t)act.frameIndex];
						if (std::holds_alternative<CpcFrameCommand>(frameCmd)) {
							const auto& origFrame = std::get<CpcFrameCommand>(frameCmd).frame;
							if (origFrame.smcMask & RasterFrameCmd::MASK_R12) {
								// Generate VMA label
								if (origFrame.smcLabelOverrides.count(12) > 0) {
									vmaLabel = origFrame.smcLabelOverrides.at(12);
								} else {
									vmaLabel = prefix + "_" + NormalizeAsmLabelToken(GetCommandName(frameCmd)) + "_R12R13_patch";
								}
								AppendLine(out, vmaLabel + "_R12 equ $+6  ; VMA high byte (R12 immediate in macro)");
								AppendLine(out, vmaLabel + "_R13 equ $+16  ; VMA low byte (R13 immediate in macro)");
							}
						}
					}
				}

				// Use unified EmitAction helper for all action types
				int actNops = EmitAction(out, act, prefix, knownState, cpcCommands);
				if (actNops > 0) {
					metrics.activeNops += actNops;
				} else if (!act.isVma && act.reg < CpcRaster::kRegGA) {
					// CRTC write was optimized away due to redundancy
					metrics.elidedNops += kCrtcWriteNops;
				}

				// UNIFIED: Emit effect padding if this is an absolute effect write
				EmitEffectPadding(out, act, prefix, &cpcCommands);

				// Clear VMA state if needed
				if (act.isVma) {
					knownState.clear(12);
					knownState.clear(13);
				}
			}
		} else {
			// Interrupt-Driven path: group actions by slot (6 x 52-line periods)
			std::vector<std::vector<ScheduledAction>> slotActions(config.numSlots);
			for (const auto& act : schedule) {
				int slot = act.scanline / 52;
				if (slot < 0) slot = 0;
				if (slot >= config.numSlots) slot = config.numSlots - 1;
				slotActions[(size_t)slot].push_back(act);
			}

			// Emit each slot's actions
			for (int slot = 0; slot < config.numSlots; slot++) {
				for (const auto& act : slotActions[(size_t)slot]) {
					// Emit wait if scanline advanced
					int deltaSl = act.scanline - lastSl;
					if (act.frameIndex != lastFrameIndex) {
						lastFrameIndex = act.frameIndex;
						AppendLine(out, "\t; Frame: " + GetCommandName(cpcCommands[(size_t)act.frameIndex]));
					}
					if (deltaSl > 0) {
						int waitNops = deltaSl * 64;
						metrics.waitNops += waitNops;
						AppendLine(out, "\t; Wait " + std::to_string(deltaSl) + " scanline(s) -> " + std::to_string(waitNops) + " NOPs");
						AppendLine(out, "\tld b," + std::to_string(deltaSl));
						AppendLine(out, "\tcall WaitScanlines_Routine");
						if (act.nopOffset > 0)
							AppendLine(out, "\tWaitNops " + std::to_string(act.nopOffset));
						lastSl = act.scanline;
					}
					AppendLine(out, "\t; " + act.comment);

					// Emit VMA SMC labels if needed (BEFORE the macro expansion)
					if (act.isVma) {
						std::string vmaLabel;
						if (act.frameIndex >= 0 && act.frameIndex < (int)cpcCommands.size()) {
							const CpcRasterCommand& frameCmd = cpcCommands[(size_t)act.frameIndex];
							if (std::holds_alternative<CpcFrameCommand>(frameCmd)) {
								const auto& origFrame = std::get<CpcFrameCommand>(frameCmd).frame;
								if (origFrame.smcMask & RasterFrameCmd::MASK_R12) {
									// Generate VMA label
									if (origFrame.smcLabelOverrides.count(12) > 0) {
										vmaLabel = origFrame.smcLabelOverrides.at(12);
									} else {
										vmaLabel = prefix + "_" + NormalizeAsmLabelToken(GetCommandName(frameCmd)) + "_R12R13_patch";
									}
									AppendLine(out, vmaLabel + "_R12 equ $+6  ; VMA high byte (R12 immediate in macro)");
									AppendLine(out, vmaLabel + "_R13 equ $+16  ; VMA low byte (R13 immediate in macro)");
								}
							}
						}
					}

					// Use unified EmitAction helper for all action types
					int actNops = EmitAction(out, act, prefix, knownState, cpcCommands);
					if (actNops > 0) {
						metrics.activeNops += actNops;
					} else if (!act.isVma && act.reg < CpcRaster::kRegGA) {
						metrics.elidedNops += kCrtcWriteNops;
					}

					// UNIFIED: Emit effect padding if this is an absolute effect write
					EmitEffectPadding(out, act, prefix, &cpcCommands);

					// Clear VMA state if needed
					if (act.isVma) {
						knownState.clear(12);
						knownState.clear(13);
					}
				}
			}
		}

		return metrics;
	}

	// ---------------------------------------------------------------------------
	// CPCRaster::GenerateCode -- Z80 assembly stub
	// ---------------------------------------------------------------------------

	std::string CPCRaster::GenerateCode(CpcRaster::RasterTimingReport* outReport) {
		using namespace CpcRaster;
		//
		// ===============================================================================
		// CRITICAL RULE: DO NOT DELETE COMPENDIUM COMMENTS
		// ===============================================================================
		// This generator must be accurate. EVERY register write constraint, EVERY counter
		// update rule, and EVERY timing condition is derived from the CPC CRTC Compendium
		// (CPC Programmers Reference Manual, section 4+). All Compendium citations,
		// section references, and explanatory comments are SACRED DOCUMENTATION - they
		// are the SOURCE OF TRUTH about how the CRTC state machine works.
		//
		// RULE: When modifying any scheduling logic, register rule, or simulator:
		// 1. Extract the rule from Compendium (specific section + line number)
		// 2. Add it as a comment in the code with full citation
		// 3. Verify the code matches the Compendium rule EXACTLY
		// 4. NEVER remove or abbreviate Compendium comments - they are debugging aids
		//
		// If the generated code is wrong, the Compendium comments help trace WHY.
		// If you delete them, future debugging becomes impossible.
		// ===============================================================================
		//
		CpcRasterTimingMode timingMode = ResolveTimingMode(m_cpcConfig.generatorMode);
		std::string prefix = NormalizeRuptureName(m_cpcConfig.ruptureName);
		std::string out;
		std::vector<GeneratedFrame> frames;
		frames.reserve(m_cpcCommands.size());
		RasterFrameCmd current;
		int currentSl = 0;
		for (int i = 0; i < (int)m_cpcCommands.size(); i++) {
			const CpcRasterCommand& cmd = m_cpcCommands[i];
			if (!std::holds_alternative<CpcFrameCommand>(cmd))
				continue;
			const auto& frameCmd = std::get<CpcFrameCommand>(cmd);
			if (!frameCmd.enabled)
				continue;
			RasterFrameCmd merged = CrtcSimulator::MergeFrame(current, frameCmd.frame);
			GeneratedFrame frame;
			frame.cmdIndex = i;
			frame.startSl = currentSl;
			int scanlines = FrameLength(merged);
			frame.endSl = currentSl + scanlines;
			frame.cmd = cmd;
			frame.state = merged;
			frames.push_back(frame);
			current = merged;
			currentSl = frame.endSl;
		}
		if (frames.empty()) {
			if (outReport) {
				outReport->errors.push_back("[No Frame commands] GenerateCode requires at least one enabled Frame command. Add a Frame command to define the CRTC register set.");
			}
			return std::string();
		}

		// Initialize the timing report
		RasterTimingReport report;
		report.slots.reserve(6);

		// Detect constant registers for hoisting and initialize known state
		CrtcKnownState hoistedRegs = DetectConstantRegisters();
		CrtcKnownState knownState;
		knownState.resetToDefaults();  // Start with CPC defaults
		// Mark hoisted registers as known (they're set in prefix_Start)
		for (uint8_t reg = 0; reg <= 13; reg++) {
			if (hoistedRegs.isKnown(reg)) {
				uint8_t value = hoistedRegs.regValues[reg];
				uint8_t defaultValue = GetDefaultRegValue(reg);
				if (value != defaultValue)
					knownState.set(reg, value);  // hoisted registers are known
			}
		}

		if (timingMode == CpcRasterTimingMode::TimedRasterLoop) {
			//
			// ===================================================================
			// VSync+Loop path -- UNIFIED TIMING LOGIC
			// ===================================================================
			// As of this refactoring:
			// ✅ UNIFIED: Both VSync+Loop and Interrupt use BuildLineStateWithRegisterWrites()
			//    and EmitRasterSchedule() with lineState for CRTC simulator-driven timing.
			//    This ensures both paths calculate NOP waits with identical precision
			//    using simulator state (absNop tracking) rather than naive scanline counts.
			//
			// ⏳ TODO: Still need to add effect padding customization and frame transition
			//    logging to VSync+Loop to match Interrupt path completeness. These are
			//    orthogonal to the timing logic unification.
			//
			// Difference: VSync+Loop generates one continuous loop (no interrupt overhead).
			//            Interrupt generates 6 handlers with per-slot overhead accounting.
			//

			AppendLine(out, "; ---------------------------------------------------------------------------");
			AppendLine(out, ";");
			AppendLine(out, "; Retrodev Generated Code");
			AppendLine(out, ";");
			AppendLine(out, "; CRTC raster timed layout: " + prefix);
			AppendLine(out, "; Auto-generated -- do not edit manually.");
			AppendLine(out, ";");
			AppendLine(out, "; ---------------------------------------------------------------------------");
			AppendBlankLine(out);

			// Emit SMC patch point header comment
			EmitSmcPatchPoints(out, prefix, m_cpcCommands);

			AppendLine(out, "include \"cpc.crtc.waiters.asm\"");
			AppendLine(out, "include \"macros.ga.asm\"");
			AppendBlankLine(out);
			AppendLine(out, prefix + "_Start:");
			AppendLine(out, "\t; Initialise constant CRTC registers, then run the timed raster loop.");
			EmitStartBoilerplate(out, hoistedRegs, report);
			AppendLine(out, "\t; Sync to VSYNC rising edge so WaitScanlines counts are frame-accurate");
			AppendLine(out, "\tGA_VSyncWaitON");
			AppendLine(out, "\tcall " + prefix + "_Loop");
			AppendLine(out, "\tei");
			AppendLine(out, "\tret");
			AppendBlankLine(out);
			AppendLine(out, prefix + "_Loop:");
			AppendLine(out, "\t; Synchronise to VSYNC rising edge every frame");
			AppendLine(out, "\tGA_VSyncWaitON");
			AppendBlankLine(out);
			// ---------------------------------------------------------------------------
			// Build 312-line schedule and emit waits + CRTC writes in scanline order
			// ---------------------------------------------------------------------------
			std::vector<std::string> schedErrors, schedWarnings;
			SchedulerConfig schedulerConfig;
			schedulerConfig.wrapFirstVmaToPreviousScanline = false;
			std::vector<ScheduledAction> schedule = BuildRuptureSchedule(frames, prefix, m_cpcConfig, schedErrors, schedWarnings, schedulerConfig, &m_cpcCommands);
			if (outReport) {
				outReport->errors.insert(outReport->errors.end(), schedErrors.begin(), schedErrors.end());
				outReport->warnings.insert(outReport->warnings.end(), schedWarnings.begin(), schedWarnings.end());
			}

			// Build lineState with absNop tracking (UNIFIED: same as Interrupt path)
			// This enables precise NOP-aware wait calculations for timing accuracy
			std::vector<CpcRaster::CrtcCounters> lineState(312);
			BuildLineStateWithRegisterWrites(lineState, schedule);

			// Collect timing metrics
			// Use unified schedule emission for all actions (now with simulator-driven timing)
			ScheduleTimingMetrics scheduleMetrics = EmitRasterSchedule(
				out, prefix, schedule, knownState, m_cpcCommands,
				RasterHandlerConfig::CreateTimedRasterLoop(),
				&lineState);  // UNIFIED: Pass lineState to both paths

			AppendBlankLine(out);
			AppendLine(out, "\t; End of frame -- free time before loop restart");
			AppendLine(out, "\tjr " + prefix + "_Loop");

			// Populate timing report for the full frame
			SlotTimingReport frameReport;
			frameReport.slot = -1;  // -1 for full frame in loop mode
			frameReport.overheadNops = 0;  // GA_VSyncWaitON is non-deterministic; not counted here
			frameReport.waitNops = scheduleMetrics.waitNops;
			frameReport.activeNops = scheduleMetrics.activeNops;
			frameReport.elidedNops = scheduleMetrics.elidedNops;
			frameReport.freeNops = 19968 - scheduleMetrics.waitNops - scheduleMetrics.activeNops;  // 312 lines x 64 NOPs/line
			report.slots.push_back(frameReport);
			report.totalWaitNops = scheduleMetrics.waitNops;
			report.totalActiveNops = scheduleMetrics.activeNops;
			report.totalElidedNops = scheduleMetrics.elidedNops;
			report.totalFreeNops = frameReport.freeNops;

			if (outReport) {
				*outReport = report;
			}
			return out;
		}

		//
		// ===================================================================
		// Interrupt path -- UNIFIED TIMING LOGIC
		// ===================================================================
		// As of this refactoring:
		// ✅ UNIFIED: Both VSync+Loop and Interrupt use BuildLineStateWithRegisterWrites()
		//    to build simulator state with absNop tracking. The Interrupt path builds
		//    lineState during boundary convergence (line 2785+), while VSync+Loop builds
		//    it before EmitRasterSchedule. Both achieve identical timing precision.
		//
		// ✅ COMPLETE: Interrupt path has full implementation:
		//    - Effect padding customization (lines 2981-3003)
		//    - Frame transition logging (lines 2917-2943)
		//    - Per-interrupt slot overhead accounting (lines 2805-2813)
		//    - Precise emulator-driven NOP calculations using lineState.absNop
		//
		// Difference: Interrupt generates 6 handlers with per-52-line slot overhead.
		//            VSync+Loop generates one continuous loop (no interrupt overhead).
		//

		AppendLine(out, "; ---------------------------------------------------------------------------");
		AppendLine(out, ";");
		AppendLine(out, "; Retrodev Generated Code");
		AppendLine(out, ";");
		AppendLine(out, "; CRTC raster interrupt layout: " + prefix);
		AppendLine(out, "; Auto-generated -- do not edit manually.");
		AppendLine(out, ";");
		AppendLine(out, "; ---------------------------------------------------------------------------");
		AppendBlankLine(out);
		AppendLine(out, "; Compatibility note: interrupt-driven layout is currently the default path for all CPC CRTC types.");
		AppendLine(out, "; Layout summary: " + std::to_string(frames.size()) + " frame(s), " + std::to_string((currentSl + 51) / 52) + " interrupt period(s), " + std::to_string(currentSl) + " scan line(s)");
		if (currentSl != 312)
			AppendLine(out, "; [WARNING: total scheduled scanlines = " + std::to_string(currentSl) + ", expected 312 for stable 50 Hz PAL. Delta = " + std::to_string(currentSl - 312) + ".]");
		AppendBlankLine(out);
		//
		// Emit VMA addresses as commented-out stubs so the user can define them
		// in their own source file.  Regenerating this file will not overwrite them.
		//
		AppendLine(out, "; VMA addresses -- define these in your own source, not here.");
		AppendLine(out, "; Regenerating this file will not overwrite your definitions.");
		AppendLine(out, "; ------------------------------------------------------------------");
		for (size_t i = 0; i < frames.size(); i++) {
			if (!(frames[i].state.activeMask & RasterFrameCmd::MASK_R12))
				continue;
			AppendLine(out, "; " + prefix + "_VMA_" + NormalizeAsmLabelToken(GetCommandName(frames[i].cmd)) + "  equ #0000");
		}
		AppendBlankLine(out);
		//
		// Emit SMC patch point header comment
		EmitSmcPatchPoints(out, prefix, m_cpcCommands);
		//
		// Emit padding defines for ABSOLUTE effects (user-configurable)
		//
		{
			std::set<std::string> paddingDefines;
			for (const auto& cmd : m_cpcCommands) {
				if (std::holds_alternative<CpcEffectCommand>(cmd)) {
					const auto& effCmd = std::get<CpcEffectCommand>(cmd);
					if (effCmd.enabled && effCmd.targetMode == EffectTargetMode::Absolute) {
						std::string sanitized = effCmd.name;
						for (char& c : sanitized) {
							if (!std::isalnum(c) && c != '_') c = '_';
						}
						std::string padDefine = prefix + "_" + NormalizeAsmLabelToken(sanitized) + "_Padding";
						paddingDefines.insert(padDefine);
					}
				}
			}
			if (!paddingDefines.empty()) {
				AppendLine(out, "; Padding defines for ABSOLUTE effects -- define these in your own source file.");
				AppendLine(out, "; ------------------------------------------------------------------");
				for (const auto& padDefine : paddingDefines) {
					AppendLine(out, "; " + padDefine + " equ 64-15");
				}
				AppendBlankLine(out);
			}
		}
		AppendLine(out, prefix + "_Start:");
		AppendLine(out, "\t; Initialise constant CRTC registers, then install interrupt handler.");
		EmitStartBoilerplate(out, hoistedRegs, report);
		// Int1 opens with GA_VSyncWaitON so it self-syncs to VSYNC on first fire.
		// No skip phase needed -- the handler absorbs any misaligned first interrupt
		// by waiting for the VSYNC rising edge before acting. (Compendium sec. 26.2)
		AppendLine(out, "\tld hl," + prefix + "_Int1");
		AppendLine(out, "\tcall CPC_InstallIM1Sync");
		AppendLine(out, "\tret");
		AppendBlankLine(out);
		AppendLine(out, prefix + "_Stop:");
		AppendLine(out, "\t; Restore standard CPC CRTC and interrupt state.");
		AppendLine(out, "\t; This function disables interrupts, restores all CRTC registers to their");
		AppendLine(out, "\t; default values for standard CPC operation, restores the standard IM1 handler");
		AppendLine(out, "\t; (ei;ret), enables interrupts, and returns.");
		AppendLine(out, "\tdi");
		AppendLine(out, "\t; Restore the standard CPC CRTC register values");
		const RasterFrameCmd defaultFrame{};
		EmitCrtcWrite(out, 0, defaultFrame.r0);
		EmitCrtcWrite(out, 1, defaultFrame.r1);
		EmitCrtcWrite(out, 2, defaultFrame.r2);
		EmitCrtcWrite(out, 3, defaultFrame.r3);
		EmitCrtcWrite(out, 4, defaultFrame.r4);
		EmitCrtcWrite(out, 5, defaultFrame.r5);
		EmitCrtcWrite(out, 6, defaultFrame.r6);
		EmitCrtcWrite(out, 7, defaultFrame.r7);
		EmitCrtcWrite(out, 9, defaultFrame.r9);
		// Reset R12/R13 to standard CPC screen base &C000 (R12=#30, R13=#00)
		EmitCrtcWordWrite(out, 12, 13, "#3000");
		AppendLine(out, "\tld hl,#C9FB");
		AppendLine(out, "\tld (#0038),hl");
		AppendLine(out, "\tei");
		AppendLine(out, "\tret");
		AppendBlankLine(out);
		// ---------------------------------------------------------------------------
		// Two-pass CRTC frame boundary computation with convergence.
		//
		// Pass 1: Build a schedule using approximate frame lengths to determine
		//   where register writes (especially R4) occur.
		// Pass 2: Simulate CRTC counter progression (C4/C9/C5) through 312 scanlines
		//   with register writes applied at their actual scheduled positions. Use
		//   the simulation to compute real frame boundaries.
		// Pass 3+: Repeat until frame boundaries converge (they typically do in 1-2 iterations).
		// ---------------------------------------------------------------------------
		std::vector<GeneratedFrame> imFrames = frames;

		//
		// ITERATIVE CONVERGENCE: Frame boundary calculation with schedule refinement.
		// Problem: registerWrites positions depend on schedule, which depends on frame boundaries.
		// This circular dependency is resolved by iterating until boundaries stabilize.
		// Loop: Build schedule → Simulate with current registerWrites → Apply boundaries → Rebuild schedule
		// Exit: When boundaries don't change, or after 3 iterations (safeguard).
		//
		std::vector<ScheduledAction> intSchedule;
		std::vector<int> simulatedBoundaries;
		std::vector<int> prevSimulatedBoundaries;
		std::vector<CrtcCounters> lineState(312);  // Declare lineState here for scope visibility
		int convergenceIteration = 0;
		bool boundariesConverged = false;

		std::vector<std::string> intSchedErrors, intSchedWarnings;
		SchedulerConfig intSchedulerConfig;
		intSchedulerConfig.wrapFirstVmaToPreviousScanline = true;

		//
		// Collect patch function information during code emission
		// Key: (frameIdx, reg), Value: {original_reg_value, original_wait_scanlines, label}
		//
		struct PatchFunctionInfo {
			int originalRegValue;
			int originalWaitScanlines;
			std::string smcLabel;      // SMC label for the register write (with override applied)
			std::string label;         // Label for the crossing wait instruction
		};
		std::map<std::pair<int, int>, PatchFunctionInfo> patchFunctionInfoMap;

		while (convergenceIteration < 3 && !boundariesConverged) {
			// Build schedule (approximate boundaries on first pass, refined on later passes)
			intSchedErrors.clear();
			intSchedWarnings.clear();
			intSchedule = BuildRuptureSchedule(imFrames, prefix, m_cpcConfig, intSchedErrors, intSchedWarnings, intSchedulerConfig, &m_cpcCommands);

			// Extract register write positions from the schedule
			std::map<int, std::vector<std::pair<uint8_t, uint8_t>>> registerWrites;
			for (const auto& act : intSchedule) {
				if (!act.isVma) {
					registerWrites[act.scanline].push_back({act.reg, act.value});
				}
			}

			// Simulate CRTC to compute frame boundaries
			CrtcCounters initCtr{};
			initCtr.c9 = m_cpcConfig.initC9;   // From config: VSYNC sync state (default 2)
			initCtr.c4 = m_cpcConfig.initC4;   // From config: VSYNC sync state (default 0)
			initCtr.c0 = m_cpcConfig.initC0;   // From config: VSYNC sync state (default 31)
			initCtr.c5 = m_cpcConfig.initC5;   // From config: VSYNC sync state (default 0)
			RasterFrameCmd defaultCrtcState = frames[0].state;  // Start with BorderTop registers
			simulatedBoundaries = SimulateCrtcWith312Lines(defaultCrtcState, registerWrites, initCtr);

			// Check if boundaries match frame count
			if ((int)simulatedBoundaries.size() != (int)imFrames.size() + 1) {
				if (outReport) {
					outReport->errors.push_back("[Boundary count mismatch] Frame boundary simulation produced " +
						std::to_string(simulatedBoundaries.size()) + " boundaries, expected " +
						std::to_string(imFrames.size() + 1) + " (one per frame plus end). "
						"This may indicate a degenerate R4/R5 combination or register write conflict.");
				}
				break;
			}

			// Check if boundaries converged (same as previous iteration)
			if (convergenceIteration > 0 && simulatedBoundaries == prevSimulatedBoundaries) {
				boundariesConverged = true;
				break;
			}

			// Apply simulated boundaries to imFrames
			for (int fi = 0; fi < (int)imFrames.size(); fi++) {
				imFrames[fi].startSl = simulatedBoundaries[fi];
				imFrames[fi].endSl   = simulatedBoundaries[fi + 1];
			}

			// Save boundaries for convergence check
			prevSimulatedBoundaries = simulatedBoundaries;
			convergenceIteration++;
		}

		if (outReport) {
			outReport->errors.insert(outReport->errors.end(), intSchedErrors.begin(), intSchedErrors.end());
			outReport->warnings.insert(outReport->warnings.end(), intSchedWarnings.begin(), intSchedWarnings.end());
			if (convergenceIteration >= 3 && !boundariesConverged) {
				outReport->warnings.push_back("[Convergence timeout] Interrupt frame boundaries did not converge after 3 iterations. Generated code may have slightly inaccurate frame transitions.");
			}
		}

		//
		// Rebuild lineState by applying register writes at their ACTUAL scheduled positions,
		// not just at frame boundaries. This accounts for mid-frame register changes.
		// (Compendium: frame boundaries determined by CRTC state machine, not register writes)
		//
		// NOTE (C-8 justification): Similar lineState construction exists in BuildRuptureSchedule
		// (line 1477), but these are NOT consolidated because they serve different purposes:
		// - BuildRuptureSchedule: Validates placement DURING scheduling, frame-aware iteration,
		//   uses initCtr from config, no register writes applied.
		// - This version: Generates precise timing AFTER scheduling, full 312-line iteration,
		//   applies scheduled register writes before advancing, tracks absNop for handler
		//   distance calculation, resets on frame boundaries (Compendium sec.12).
		//
		// Extract register write positions from the rebuilt schedule
		std::map<int, std::vector<std::pair<uint8_t, uint8_t>>> registerWritesForLineState;
		for (const auto& act : intSchedule) {
			if (!act.isVma) {
				registerWritesForLineState[act.scanline].push_back({act.reg, act.value});
			}
		}

		//
		// Build lineState by simulating the CRTC through all 312 scanlines with register
		// writes applied at their actual scheduled positions. This reflects the real CRTC
		// state as registers change, not merged state at frame boundaries.
		// Frame boundaries are detected via frameComplete flag; counters reset when frames end.
		//
		{
			RasterFrameCmd state;  // Start with default register values
			CpcRaster::CrtcSimulator sim;
			sim.init(CrtcCounters{}, state);
			int absNopAccumulator = 0;

			for (int sl = 0; sl < 312; sl++) {
				// Apply any register writes scheduled at this scanline BEFORE advancing CRTC
				auto it = registerWritesForLineState.find(sl);
				if (it != registerWritesForLineState.end()) {
					for (const auto& write : it->second) {
						uint8_t reg = write.first;
						uint8_t val = write.second;
						sim.applyRegWrite(reg, val);
					}
				}

				// Record CRTC state at this scanline
				CrtcCounters snapCtr = sim.counters();
				snapCtr.absNop = absNopAccumulator;
				lineState[(size_t)sl] = snapCtr;

				// Advance CRTC for this scanline
				sim.advance(64);
				absNopAccumulator = sim.counters().absScanline * 64;

				// CRITICAL: When frame boundary fires (C4 wraps to 0), reset counters
				// for next frame. This ensures frame-to-frame transitions are correct.
				// (Compendium sec.12: frame boundary when C4 wraps from R4 to 0)
				if (sim.counters().frameComplete) {
					CrtcCounters resetCtr = sim.counters();
					resetCtr.c4 = 0;
					resetCtr.c9 = 0;
					resetCtr.c5 = 0;
					resetCtr.inAdjust = false;
					resetCtr.frameComplete = false;
					sim.init(resetCtr, state);
				}
			}
		}
		static constexpr int kNumSlots = 6;
		std::vector<std::vector<ScheduledAction>> slotActions(kNumSlots);
		for (const ScheduledAction& act : intSchedule) {
			int slot = act.scanline / 52;
			if (slot < 0)
				slot = 0;
			if (slot >= kNumSlots)
				slot = kNumSlots - 1;
			slotActions[(size_t)slot].push_back(act);
		}

		// Pre-compute per-slot timing metrics for the report
		std::vector<int> slotActionNops(kNumSlots, 0);
		std::vector<int> slotWaitNops(kNumSlots, 0);
		std::vector<int> slotElidedNops(kNumSlots, 0);
		for (int slot = 0; slot < kNumSlots; slot++) {
			for (const ScheduledAction& act : slotActions[(size_t)slot]) {
				slotActionNops[slot] += ActionWriteNops(act);
			}
		}
		// Calculate wait NOPs per slot by analyzing gaps between actions
		for (int slot = 0; slot < kNumSlots; slot++) {
			int slotStartSl = slot * 52;
			int effectiveStartSl = (slot == 0) ? 3 : slotStartSl;
			int lastSl = effectiveStartSl;
			for (const ScheduledAction& act : slotActions[(size_t)slot]) {
				int delta = act.scanline - lastSl;
				if (delta > 0) {
					slotWaitNops[slot] += delta * 64;
				}
				lastSl = act.scanline;
			}
		}

		// ---------------------------------------------------------------------------
		// Emit each interrupt handler
		// ---------------------------------------------------------------------------
		//
		// Per sec.2 of the plan: minimum interrupt overhead = 34 NOPs
		// (INT-ack 4 + push af/bc 8 + pop bc/af 8 + ld a,low/high 6 + ld (#0039),a + ld (#003A),a 8 + ei 1 + ret 3).
		//
		static constexpr int kIntOverheadNops = 34;
		static constexpr int kSlotNopBudget = 52 * kLineNopBudget;
		// Calculate dynamic sync wait based on measured C0 position after GA_VSyncWaitON
		// NOPs to reach C0=0 of next scanline: (R0+1) - initC0
		int int1SyncWaitNops = 0;
		if (!imFrames.empty()) {
			int r0 = (int)imFrames[0].state.r0;
			int c0AfterSync = m_cpcConfig.initC0;
			int1SyncWaitNops = (r0 + 1) - c0AfterSync;
			if (int1SyncWaitNops < 0) int1SyncWaitNops = 0;  // safety check
		} else {
			int1SyncWaitNops = 18;  // fallback to hardcoded default
		}
		for (int slot = 0; slot < kNumSlots; slot++) {
			//
			// Build per-slot summary description and NOP tally
			//
			int slotStartSl = slot * 52;
			int slotEndSl   = slotStartSl + 51;
			std::string slotDesc;
			int actionNops = 0;
			for (const ScheduledAction& act : slotActions[(size_t)slot]) {
				if (!slotDesc.empty())
					slotDesc += "\n\t;          ";
				slotDesc += act.comment;
				actionNops += ActionWriteNops(act);
			}
			if (slotDesc.empty())
				slotDesc = "idle";
			// Prepared for future use - for validation or debug output
			int totalNops = kIntOverheadNops + actionNops;
			UNUSED(totalNops);
			// Int1 carries extra overhead: dynamic sync stall (GA_VSyncWaitON + WaitNops N)
			// calculated from initC0 counter position to reach C0=0 of next scanline,
			// plus a 2-scanline VSYNC post-wait (ld b,2 + WaitScanlines_Routine).  The 2
			// scanlines are baked into effectiveStartSl=3; only the sub-scanline sync wait is
			// tracked here for the single-line action-burst budget warning.
			int reportedOverhead = (slot == 0) ? kIntOverheadNops + int1SyncWaitNops : kIntOverheadNops;
			std::string timingNote = "overhead " + std::to_string(reportedOverhead) + " + actions " + std::to_string(actionNops) + " = " + std::to_string(reportedOverhead + actionNops) + " NOPs";
			//
			// Char-row range: each char row = R9+1 scanlines.  Use the merged state
			// of the first frame whose scanlines overlap this slot for R9.
			//
			// Int1 is anchored to scanline 3 after GA_VSyncWaitON + WaitNops (dynamic) + the mandatory
			// 2-scanline VSYNC post-wait (sec.20.3.1 / sec.13.2.1).
			// All other slots start exactly at their GA interrupt boundary.
			int effectiveStartSl = (slot == 0) ? 3 : slotStartSl;
			int slotR9p1 = 8; // default CPC (R9=7)
			for (const auto& fr : imFrames) {
				if (fr.endSl > effectiveStartSl) {
					slotR9p1 = (int)fr.state.r9 + 1;
					break;
				}
			}
			int slotStartRow = effectiveStartSl / slotR9p1;
			int slotEndRow   = slotEndSl        / slotR9p1;

			AppendLine(out, prefix + "_Int" + std::to_string(slot + 1) + ":");
			AppendLine(out, "\t;");
			AppendLine(out, "\t; Interrupt " + std::to_string(slot + 1) + " -- scanlines " + std::to_string(effectiveStartSl) + ".." + std::to_string(slotEndSl) + "  char rows " + std::to_string(slotStartRow) + ".." + std::to_string(slotEndRow));
			AppendLine(out, "\t; Actions: " + slotDesc);
			AppendLine(out, "\t; Timing:  " + timingNote);

			if (reportedOverhead + actionNops > kSlotNopBudget)
				AppendLine(out, "\t; [FATAL]: Int" + std::to_string(slot + 1) + " exceeds maximum slot budget of " + std::to_string(kSlotNopBudget) + " NOPs.");
			else if (reportedOverhead + actionNops > kLineNopBudget)
				AppendLine(out, "\t; [WARNING]: Int" + std::to_string(slot + 1) + " actions burst exceeds one-line budget of " + std::to_string(kLineNopBudget) + " NOPs.");

			AppendLine(out, "\t;");
			AppendLine(out, "\tpush af");
			AppendLine(out, "\tpush bc");
			//
			// Int1 fires at line 0 (VSYNC boundary).  Wait for VSYNC active and align
			// to line 1 character 0 so all downstream scanline waits count from a known
			// horizontal position.  GA_VSyncWaitON spins until PPI Port B bit 0 rises;
			// WaitNops (dynamic) compensates for the C0 position after GA_VSyncWaitON so the next
			// instruction executes exactly at C0=0 of scanline 1. (sec.7.2)
			// A further 2-scanline VSYNC post-wait is mandatory: on CRTC 0 the VMA pipeline
			// initialisation at C4=0 is unsafe to interrupt for 2 scanlines after the rising
			// edge (Compendium sec.20.3.1 / sec.13.2.1).  All scheduled actions start at
			// scanline 3 to clear this window.
			//
			if (slot == 0) {
				AppendLine(out, "\t; Synchronise to the VSYNC rising edge so all downstream WaitScanlines");
				AppendLine(out, "\t; counts are frame-accurate and horizontally aligned across all CRTC types.");
				AppendLine(out, "\t; GA_VSyncWaitON spins on PPI Port B bit 0 until VSYNC goes active (high).");
				AppendLine(out, "\t; WaitNops dynamically calculated from initC0 counter position to reach C0=0");
				AppendLine(out, "\t; of the next scanline. Formula: (R0+1) - initC0 = (" + std::to_string((int)imFrames[0].state.r0 + 1) + " - " + std::to_string(m_cpcConfig.initC0) + ") = " + std::to_string(int1SyncWaitNops));
				AppendLine(out, "\t; This ensures the instruction that follows executes at C0=0 of scanline 1. (Compendium sec.7.2)");
				AppendLine(out, "\tGA_VSyncWaitON");
				AppendLine(out, "\tWaitNops " + std::to_string(int1SyncWaitNops));
				AppendLine(out, "\t; VSYNC post-wait: C4=0 VMA pipeline unsafe for 2 scanlines after sync");
				AppendLine(out, "\t; (Compendium sec.20.3.1 / sec.13.2.1). Wait before issuing register writes.");
				AppendLine(out, "\tld b,2");
				AppendLine(out, "\tcall WaitScanlines_Routine");
			}
			//
			// Emit writes in scanline order.  Track current handler position using
			// lineState emulator to calculate absolute NOP positions precisely.
			//
			// handlerSl = current scanline position inside the handler
			// handlerAbsNop = absolute NOP count from handler start
			// For Int1, GA_VSyncWaitON + WaitNops (dynamic) land us at scanline 1, not 0.
			// Returns the imFrames index whose [startSl, endSl) window contains sl, or -1.
			auto findImFrameAt = [&](int sl) -> int {
				for (int j = 0; j < (int)imFrames.size(); j++)
					if (sl >= imFrames[j].startSl && sl < imFrames[j].endSl)
						return j;
				return -1;
			};
			int handlerSl = effectiveStartSl;
			int handlerAbsNop = lineState[(size_t)handlerSl].absNop;
			const ScheduledAction* prevAct = nullptr;
			for (const ScheduledAction& act : slotActions[(size_t)slot]) {
				int delta = act.scanline - handlerSl;
				if (delta > 0) {
					int hFi = findImFrameAt(handlerSl);
					int aFi = findImFrameAt(act.scanline);
					// "(end of X)" suffix when the wait target is exactly a frame-start scanline.
					std::string waitSuffix;
					if (!act.isVma && aFi > 0 && imFrames[aFi].startSl == act.scanline)
						waitSuffix = " (end of \"" + GetCommandName(imFrames[aFi - 1].cmd) + "\")";
					// For VMA: note that we are in safe territory past C4=0.
					if (act.isVma) {
						std::string cn = (hFi >= 0) ? GetCommandName(imFrames[hFi].cmd) : "?";
						std::string nn = (act.frameIndex >= 0 && act.frameIndex < (int)imFrames.size()) ? GetCommandName(imFrames[act.frameIndex].cmd) : "?";
						AppendLine(out, "\t; Safe space to set VMA for \"" + nn + "\" -- past C4=0 of \"" + cn + "\", deferred to \"" + nn + "\" frame start");
					}
					// Calculate actual wait distance using emulator state
					int targetAbsNop = lineState[(size_t)act.scanline].absNop + act.nopOffset;
					int waitDistance = targetAbsNop - handlerAbsNop;

					// Decompose wait into full scanlines + remainder NOPs
					int fullScanlines = waitDistance / 64;
					int nopsRemainder = waitDistance % 64;

					AppendLine(out, "\t; Wait to reach scanline " + std::to_string(act.scanline) +
						" (" + std::to_string(waitDistance) + " NOPs = " + std::to_string(fullScanlines) + "*64+" +
						std::to_string(nopsRemainder) + ")" + waitSuffix);

					// Add frame context: show which frame we're waiting for and frame boundaries
					if (!act.isVma && aFi >= 0 && aFi < (int)imFrames.size()) {
						std::string targetFrameName = GetCommandName(imFrames[aFi].cmd);
						int targetFrameStart = imFrames[aFi].startSl;
						int targetFrameEnd = imFrames[aFi].endSl;
						AppendLine(out, "\t; Frame \"" + targetFrameName + "\": scanlines " + std::to_string(targetFrameStart) + ".." + std::to_string(targetFrameEnd - 1));
					}

					// Frame-transition note when we are emitting a wait FROM a frame-start scanline.
					if (!act.isVma && hFi > 0 && imFrames[hFi].startSl == handlerSl)
						AppendLine(out, "\t; Frame \"" + GetCommandName(imFrames[hFi - 1].cmd) + "\" ended at " + std::to_string(handlerSl) + "; \"" + GetCommandName(imFrames[hFi].cmd) + "\" starts here (C4=0, C9=0)");

					// Collect SMC labels for crossing waits (emit them right before ld b)
					std::vector<std::string> crossingWaitLabels;
					for (int fi = 0; fi < (int)imFrames.size(); fi++) {
						const auto& frame = imFrames[fi];
						// Check if this frame has R5 patch function enabled
						if (!(std::get_if<CpcFrameCommand>(&frame.cmd)))
							continue;
						const auto* frameCmd = std::get_if<CpcFrameCommand>(&frame.cmd);
						if (!frameCmd || !(frameCmd->frame.smcPatchFunctionMask & RasterFrameCmd::MASK_R5))
							continue;
						// Check if this wait crosses this frame's boundary
						if (handlerSl < frame.endSl && act.frameIndex >= 0 &&
							act.frameIndex < (int)imFrames.size() &&
							imFrames[act.frameIndex].startSl >= frame.endSl &&
							!act.isVma) {
							// Collect label for the ld b,N immediate byte (emit later, right before ld b)
							std::string frameLabel = GetCommandName(frame.cmd);
							std::string label = prefix + "_" + frameLabel + "_R5_WaitB_patch";
							crossingWaitLabels.push_back(label);
							// Record patch function info for later emission
							auto key = std::make_pair(fi, 5);  // 5 = R5 register
							if (patchFunctionInfoMap.find(key) == patchFunctionInfoMap.end()) {
								PatchFunctionInfo info;
								info.originalRegValue = (int)frameCmd->frame.r5;
								info.originalWaitScanlines = fullScanlines;
								info.label = label;  // Label for the crossing wait (ld b,N)
								// Capture the SMC label (with override applied) for the register write
								if (frameCmd->frame.smcLabelOverrides.count(5) > 0) {
									info.smcLabel = frameCmd->frame.smcLabelOverrides.at(5);
								} else {
									info.smcLabel = prefix + "_" + frameLabel + "_R5_patch";
								}
								patchFunctionInfoMap[key] = info;
							}
						}
					}
					// Emit full scanlines first
					if (fullScanlines > 0) {
						AppendLine(out, "\tld b," + std::to_string(fullScanlines));
						// Emit collected equ labels right after ld b, pointing back to immediate byte
						for (const auto& label : crossingWaitLabels) {
							AppendLine(out, label + " equ $-1  ; points back to ld b immediate");
						}
						AppendLine(out, "\tcall WaitScanlines_Routine");
					}
					// Then emit remainder NOPs
					if (nopsRemainder > 0) {
						AppendLine(out, "\tWaitNops " + std::to_string(nopsRemainder) + "\t; reach scanline " + std::to_string(act.scanline));
					}

					handlerSl = act.scanline;
					handlerAbsNop = targetAbsNop;
					prevAct = nullptr;  // Reset tracking when we cross scanline boundary
				}
				//
				// Skip nopOffset for consecutive Effect writes from the same effect on the same scanline.
				// This prevents spurious WaitNops between writes that should execute back-to-back.
				//
				bool isConsecutiveSameEffect = (prevAct != nullptr &&
					prevAct->scanline == act.scanline &&
					prevAct->comment.find("[Effect]") != std::string::npos &&
					act.comment.find("[Effect]") != std::string::npos &&
					prevAct->comment.substr(0, prevAct->comment.find(":")) == act.comment.substr(0, act.comment.find(":")));

				if (!isConsecutiveSameEffect && act.scanline == handlerSl) {
					// Emit sub-scanline NOP wait on same scanline using emulator-based calculation
					int targetAbsNop = lineState[(size_t)act.scanline].absNop + act.nopOffset;
					int waitDistance = targetAbsNop - handlerAbsNop;
					if (waitDistance > 0) {
						// Same scanline, so just emit WaitNops (no full scanlines needed)
						int framIdx = findImFrameAt(act.scanline);
						std::string frameInfo = (framIdx >= 0 && framIdx < (int)imFrames.size()) ?
							(" in frame \"" + GetCommandName(imFrames[framIdx].cmd) + "\"") : "";
						AppendLine(out, "\tWaitNops " + std::to_string(waitDistance) +
							"\t; scanline " + std::to_string(act.scanline) + frameInfo + ", NOP " + std::to_string(act.nopOffset));
						handlerAbsNop = targetAbsNop;
					}
				}
				AppendLine(out, "\t; " + act.comment);

				// Absolute effects (independently scheduled with preceding wait) get user-configurable padding
				// to position in blanking area. Relative effects (following previous) auto-align at same C0.
				// Only emit padding for first write of absolute effects.
				// UNIFIED: Use shared effect padding emitter (same logic in VSync+Loop path)
				EmitEffectPadding(out, act, prefix, &m_cpcCommands);

				// Calculate the cost of this write and update handlerAbsNop after emission
				int writeCostNops;
				if (act.isVma) {
					// VMA: emit SMC labels for R12 and R13 offsets within the macro
					if (!act.smcLabel.empty()) {
						AppendLine(out, act.smcLabel + "_R12 equ $+6  ; VMA high byte (R12 immediate in macro)");
						AppendLine(out, act.smcLabel + "_R13 equ $+16  ; VMA low byte (R13 immediate in macro)");
					}
					writeCostNops = EmitAction(out, act, prefix, knownState, m_cpcCommands);
					knownState.clear(12);
					knownState.clear(13);  // mark as potentially changed
				} else {
					// Use unified EmitAction helper for GA, Subroutine, Variable, and CRTC
					writeCostNops = EmitAction(out, act, prefix, knownState, m_cpcCommands);
				}

				// Update handlerAbsNop after emitting the write
				// absNop is absolute position from start of handler, precise via emulator
				handlerAbsNop += writeCostNops;

				// Update prevAct for next iteration (at end of loop)
				prevAct = &act;
			}
			std::string nextInt = prefix + "_Int" + std::to_string(slot == kNumSlots - 1 ? 1 : slot + 2);
			AppendLine(out, "\tld a,lo(" + nextInt + ")");
			AppendLine(out, "\tld (#0039),a");
			AppendLine(out, "\tld a,hi(" + nextInt + ")");
			AppendLine(out, "\tld (#003A),a");
			AppendLine(out, "\tpop bc");
			AppendLine(out, "\tpop af");
			AppendLine(out, "\tei");
			AppendLine(out, "\tret");
			AppendBlankLine(out);
		}

		//
		// Emit SMC patch functions for boundary-affecting registers (R5 Phase 1)
		//
		if (!patchFunctionInfoMap.empty()) {
			AppendBlankLine(out);
			AppendLine(out, "; SMC Patch Functions");
			AppendLine(out, "; -------------------------------------------------------");
			for (const auto& entry : patchFunctionInfoMap) {
				int frameIdx = entry.first.first;
				const PatchFunctionInfo& info = entry.second;
				if (frameIdx < 0 || frameIdx >= (int)imFrames.size())
					continue;
				const auto& frameCmd = std::get_if<CpcFrameCommand>(&imFrames[frameIdx].cmd);
				if (!frameCmd)
					continue;
				std::string frameName = GetCommandName(imFrames[frameIdx].cmd);
				// For R5: K = originalWait - originalR5
				// new_wait = K + new_R5 = originalWait - originalR5 + new_R5
				int K = info.originalWaitScanlines - info.originalRegValue;
				AppendBlankLine(out);
				AppendLine(out, "; SMC patch function: change R5 for \"" + frameName + "\"");
				AppendLine(out, "; Entry: A = signed delta (0 = no change, +N = grow, -N = shrink)");
				AppendLine(out, "; Safe range: R5 = " + std::to_string(info.originalRegValue) + " +/- delta");
				AppendLine(out, "; Clobbers: A, C");
				AppendLine(out, ";");
				std::string funcName = prefix + "_Patch" + frameName + "_R5";
				AppendLine(out, funcName + ":");
				AppendLine(out, "\tld c,a\t\t\t\t; c = delta");
				AppendLine(out, "\tadd a," + std::to_string(info.originalRegValue) + "\t\t\t; a = original_R5 + delta");
				AppendLine(out, "\tld (" + info.smcLabel + "),a\t; patch CRTC R5 write (uses custom label if provided)");
				AppendLine(out, "\tld a," + std::to_string(K) + "\t\t\t; a = K = " + std::to_string(info.originalWaitScanlines) + " - " + std::to_string(info.originalRegValue));
				AppendLine(out, "\tadd a,c\t\t\t; a = K + delta = new_wait");
				AppendLine(out, "\tld (" + info.label + "),a\t\t; patch ld b,N immediate");
				AppendLine(out, "\tret");
			}
		}

		//
		// Emit variable definitions for both interrupt-driven and VSync+loop modes
		//
		{
			bool hasVariables = false;
			for (const auto& cmd : m_cpcCommands) {
				if (std::holds_alternative<CpcVariableCommand>(cmd)) {
					const auto& varCmd = std::get<CpcVariableCommand>(cmd);
					if (!varCmd.enabled)
						continue;
					if (!hasVariables) {
						AppendBlankLine(out);
						AppendLine(out, "; Variable definitions");
						AppendLine(out, "; These are written by the interrupt handler at their scheduled scanlines.");
						AppendLine(out, "; External code reads and resets them (ld a, (varName); xor a; ld (varName), a).");
						hasVariables = true;
					}
					AppendLine(out, varCmd.variable.varName + ":\tdb\t0\t; " + varCmd.name);
				}
			}
		}

		// Populate slot-based timing report for interrupt-driven mode
		// Create 6 individual reports, one per interrupt slot
		report.slots.clear();
		report.slots.reserve(6);
		int totalWaitNops = 0;
		int totalActiveNops = 0;
		int totalElidedNops = 0;
		for (int slot = 0; slot < kNumSlots; slot++) {
			SlotTimingReport slotReport;
			slotReport.slot = slot;
			// Int1 has extra overhead: dynamic sync wait NOPs + 2-scanline VSYNC post-wait equivalent (34 base)
			slotReport.overheadNops = (slot == 0) ? (34 + int1SyncWaitNops) : 34;
			slotReport.waitNops = slotWaitNops[slot];
			slotReport.activeNops = slotActionNops[slot];
			slotReport.elidedNops = slotElidedNops[slot];
			int slotBudget = 52 * 64;  // 52 scanlines x 64 NOPs per scanline
			slotReport.freeNops = slotBudget - slotReport.overheadNops - slotReport.waitNops - slotReport.activeNops;
			report.slots.push_back(slotReport);
			totalWaitNops += slotWaitNops[slot];
			totalActiveNops += slotActionNops[slot];
			totalElidedNops += slotElidedNops[slot];
		}
		// Store totals (frame report not added to slots list; summary section handles frame totals)
		report.totalWaitNops = totalWaitNops;
		report.totalActiveNops = totalActiveNops;
		report.totalElidedNops = totalElidedNops;
		report.totalFreeNops = (52 * 64 * 6) - (34 * 5 + (34 + int1SyncWaitNops)) - totalWaitNops - totalActiveNops;

		// Populate and return the timing report if requested
		if (outReport) {
			*outReport = report;
			// Emit errors and warnings as ASM comments at the end
			if (!report.errors.empty()) {
				AppendBlankLine(out);
				AppendLine(out, "; ============================================================================");
				AppendLine(out, "; ERRORS (code generation failed)");
				AppendLine(out, "; ============================================================================");
				for (const auto& err : report.errors) {
					AppendLine(out, "; ERROR: " + err);
				}
			}
			if (!report.warnings.empty()) {
				AppendBlankLine(out);
				AppendLine(out, "; ============================================================================");
				AppendLine(out, "; WARNINGS (code generated but may have issues)");
				AppendLine(out, "; ============================================================================");
				for (const auto& warn : report.warnings) {
					AppendLine(out, "; WARNING: " + warn);
				}
			}
		}

		return out;
	}

	// ---------------------------------------------------------------------------
	// CrtcKnownState implementation
	// ---------------------------------------------------------------------------

	void CpcRaster::CrtcKnownState::resetToDefaults() {
		// CPC BIOS default CRTC state
		// Initialize all as unknown first
		for (int i = 0; i < 16; i++) {
			regKnown[i] = false;
			regValues[i] = 0;
		}
		// Set known defaults
		set(0, 63);    // R0 = HTOT
		set(1, 40);    // R1 = HDIS
		set(2, 46);    // R2 = HSYNC pos
		set(3, 0x8E);  // R3 = sync widths
		set(4, 38);    // R4 = VTOT
		set(5, 0);     // R5 = VTADJ
		set(6, 25);    // R6 = VDIS
		set(7, 30);    // R7 = VSYNC pos
		set(9, 7);     // R9 = MAXRAS
		set(12, 0x30); // R12 = VMA high
		set(13, 0x00); // R13 = VMA low
		// R8 not typically set in normal use; left as unknown
	}

	namespace CpcRaster {

	// ---------------------------------------------------------------------------
	// CrtcSimulator implementation
	// ---------------------------------------------------------------------------

	void CrtcSimulator::init(const CrtcCounters& initialCounters, const RasterFrameCmd& initialRegs) {
		m_ctr = initialCounters;
		m_regs = initialRegs;
		m_interruptSlot = 0;
		m_vsyncIcCountdown = 0;
	}

	const CrtcCounters& CrtcSimulator::counters() const {
		return m_ctr;
	}

	const RasterFrameCmd& CrtcSimulator::registers() const {
		return m_regs;
	}

	void CrtcSimulator::applyRegWrite(uint8_t reg, uint8_t value) {
		switch (reg) {
			case 0: m_regs.r0 = value; break;
			case 1: m_regs.r1 = value; break;
			case 2: m_regs.r2 = value; break;
			case 3: m_regs.r3 = value; break;
			case 4: m_regs.r4 = value; break;
			case 5: m_regs.r5 = value; break;
			case 6: m_regs.r6 = value; break;
			case 7: m_regs.r7 = value; break;
			case 9: m_regs.r9 = value; break;
			case 12: m_regs.r12 = value; break;
			case 13: m_regs.r13 = value; break;
		}
	}

	int CrtcSimulator::nopsToEndOfLine() const {
		return (m_regs.r0 + 1) - m_ctr.c0;
	}

	int CrtcSimulator::nopsUntil(int targetC4, int targetC9) const {
		// Simple search forward up to 312 scanlines
		CrtcSimulator sim = *this;
		for (int i = 0; i < 312 * 64; i++) {
			if (sim.m_ctr.c4 == targetC4 && sim.m_ctr.c9 == targetC9)
				return i;
			sim.advance(1);
		}
		return -1;
	}

	int CrtcSimulator::nopsUntilMaReset(CpcCrtcType crtcType) const {
		if (crtcType == CpcCrtcType::Type1) {
			// Type 1: trigger at every C0=0 while C4=0
			if (m_ctr.c4 == 0) {
				if (m_ctr.c0 == 0) return 0;  // At trigger now
				return (m_regs.r0 + 1) - m_ctr.c0;  // NOPs to next C0=0
			}
			// Wait until C4 becomes 0
			CrtcSimulator sim = *this;
			for (int i = 0; i < 312 * 64; i++) {
				sim.advance(1);
				if (sim.m_ctr.c4 == 0) {
					// Found C4=0; now find next C0=0 within it
					if (sim.m_ctr.c0 == 0) return i + 1;
					int nopsTo = (sim.m_regs.r0 + 1) - sim.m_ctr.c0;
					return i + nopsTo + 1;
				}
			}
		} else {
			// Types 0/2/3/4: trigger at C4=0, C9=0, C0=0 simultaneously
			if (m_ctr.c4 == 0 && m_ctr.c9 == 0 && m_ctr.c0 == 0)
				return 0;  // At trigger now
			// Search for next C4=C9=C0=0
			return nopsUntil(0, 0);
		}
		return -1;
	}


	void CrtcSimulator::advanceOneChar() {
		m_ctr.absNop++;
		m_ctr.maOverflow    = false;
		m_ctr.vsyncStart    = false;
		m_ctr.intFired      = false;
		m_ctr.vsyncIntFired = false;
		m_ctr.vsyncIcReset  = false;

		// DisPen: (C4 < R6) && (C0 < R1)
		m_ctr.disPen = (m_ctr.c4 < m_regs.r6) && (m_ctr.c0 < m_regs.r1);

		// MA increment when DisPen active
		if (m_ctr.disPen) {
			int oldPage = (m_ctr.ma >> 12) & 0x3;
			m_ctr.ma = (m_ctr.ma + 1) & 0x3FFF;  // 14-bit wrap
			int newPage = (m_ctr.ma >> 12) & 0x3;
			if (newPage != oldPage)
				m_ctr.maOverflow = true;
		}

		// HSync: active from C0=R2 for R3[3:0] chars
		if (m_ctr.c0 == m_regs.r2 && !m_ctr.hsync) {
			m_ctr.hsync = true;
			m_ctr.c3l = 0;
		}
		if (m_ctr.hsync) {
			m_ctr.c3l++;
			int r3l = m_regs.r3 & 0x0F;
			if (r3l == 0 || m_ctr.c3l >= r3l)
				m_ctr.hsync = false;
		}

		// Advance C0
		m_ctr.c0++;
		if (m_ctr.c0 > m_regs.r0)
			onLineEnd();
	}

	void CrtcSimulator::onLineEnd() {
		m_ctr.c0 = 0;
		m_ctr.absScanline++;
		m_ctr.hsync = false;
		m_ctr.intFired = false;
		m_ctr.vsyncIntFired = false;
		m_ctr.vsyncIcReset = false;
		m_ctr.vsyncStart = false;
		m_ctr.frameComplete = false;

		// IC: GA interrupt counter (one per HSync = one per scanline)
		m_ctr.ic++;
		m_ctr.icLineInSlot = m_ctr.ic;
		if (m_ctr.ic >= 52) {
			m_ctr.ic = 0;
			m_ctr.icLineInSlot = 0;
			m_ctr.intFired = true;
			m_ctr.interruptSlot = (m_ctr.interruptSlot + 1) % 6;
		}

		// VSync IC reset: fires unconditionally 2 HSyncs after VSync started
		if (m_vsyncIcCountdown > 0) {
			m_vsyncIcCountdown--;
			if (m_vsyncIcCountdown == 0) {
				m_ctr.ic = 0;
				m_ctr.icLineInSlot = 0;
				m_ctr.vsyncIcReset = true;
			}
		}

		//
		// VSYNC Pulse Width Counter (Compendium sec.9.3.4.3, sec.16.4)
		//
		// C3h counts down the vertical sync pulse width during VSYNC.
		// VSYNC pulse width controlled by R3[7:4] scanlines.
		// If R3[7:4] == 0, VSYNC width is 16 scanlines (Compendium sec.9.3.4.3).
		// VSYNC ends when C3h reaches R3[7:4] limit.
		//
		if (m_ctr.vsync) {
			int r3h = (m_regs.r3 >> 4) & 0x0F;
			int limit = (r3h == 0) ? 16 : r3h;
			m_ctr.c3h++;
			if (m_ctr.c3h >= limit) {
				m_ctr.vsync = false;
				m_ctr.c3h = 0;
			}
		}

		//
		// R5 vertical-total-adjust phase: C5 increments every individual scanline.
		// Checked before C9 so it fires per-scanline, not per character-row.
		//
		if (m_ctr.inAdjust) {
			m_ctr.c5++;
			if (m_ctr.c5 >= m_regs.r5) {
				m_ctr.c4 = 0; m_ctr.c9 = 0; m_ctr.c5 = 0;
				m_ctr.inAdjust = false; m_ctr.frameComplete = true;
				m_ctr.ma = ((int)(m_regs.r12 & 0x3F) << 8) | m_regs.r13;
				if (!m_regs.disableVSync && m_regs.r7 == 0 && !m_ctr.vsync) {
					m_ctr.vsync = true;
					m_ctr.vsyncStart = true;
					m_ctr.c3h = 0;
					if (m_ctr.ic >= 32)
						m_ctr.vsyncIntFired = true;
					m_vsyncIcCountdown = 2;
				}
			}
			return;
		}

		//
		// C9 -- Raster Line Within Character Row Counter (Compendium sec.10)
		//
		// C9 counts from 0 to R9 per character row, advancing once per scanline.
		// When C9 completes (reaches R9), C4 increments and C9 resets to 0.
		//
		// See Compendium sec.10.3 for CRTC variant rules (overflow handling, etc).
		// This implementation uses the basic logic: C9 increments until R9 is reached.
		//
		if (m_ctr.c9 < m_regs.r9) {
			m_ctr.c9++;
			return;  // Same character row
		}

		// C9 wraps: end of character row
		m_ctr.c9 = 0;

		//
		// C4 -- Character Row Counter (Compendium sec.12)
		//
		// C4 increments once per completed character row (when C9==R9).
		// Counts from 0 to R4 (the "Vertical Total" register).
		// Frame length = (R4+1) * (R9+1) + R5 scanlines.
		//
		// VSync trigger: C4 == R7 at C9=0 (start of character row).
		// See Compendium sec.16.4 for VSync position timing.
		//
		if (m_ctr.c4 < m_regs.r4) {
			m_ctr.c4++;
			// VSync trigger: when C4 == R7 at C9=0, and not disabled
			if (!m_regs.disableVSync && m_regs.r7 != 0) {
				if (m_ctr.c4 == (int)m_regs.r7 && !m_ctr.vsync) {
					m_ctr.vsync = true;
					m_ctr.vsyncStart = true;
					m_ctr.c3h = 0;
					if (m_ctr.ic >= 32)
						m_ctr.vsyncIntFired = true;
					m_vsyncIcCountdown = 2;
				}
			}
			return;
		}

		// C4 == R4: end of main frame rows
		// Check if we enter R5 adjustment phase or complete the frame.
		//
		// R5 Vertical-Total-Adjust Phase (Compendium sec.11)
		//
		// R5 defines additional scanlines AFTER the main frame rows complete.
		// Total frame length = (R4+1)*(R9+1) + R5 scanlines.
		//
		// If R5 == 0: Frame is complete immediately (no adjust phase).
		// If R5 > 0: Enter adjust phase; C5 counts from 0 to R5-1 over individual scanlines.
		//
		// CRTC Variant Timing (see Compendium sec.11.2):
		//   CRTC 0 (sec.11.2.2): [Specific rules]
		//   CRTC 1 (sec.11.2.3/11.2.4): C5 increments every scanline (dissociated from C9)
		//   CRTC 2 (sec.11.2.5): [Similar to CRTC 0]
		//   CRTC 3/4 (sec.11.2.6): [Specific rules]
		//
		// VMA Latching (Compendium sec.20.3):
		//   When frame completes (C4==R4, C5==R5), VMA is loaded with R12/R13.
		//   This is critical for display memory pointer initialization.
		//
		if (m_regs.r5 == 0) {
			m_ctr.c4 = 0; m_ctr.c5 = 0; m_ctr.frameComplete = true;
			m_ctr.ma = ((int)(m_regs.r12 & 0x3F) << 8) | m_regs.r13;
			if (!m_regs.disableVSync && m_regs.r7 == 0 && !m_ctr.vsync) {
				m_ctr.vsync = true;
				m_ctr.vsyncStart = true;
				m_ctr.c3h = 0;
				if (m_ctr.ic >= 32)
					m_ctr.vsyncIntFired = true;
				m_vsyncIcCountdown = 2;
			}
			return;
		}

		// Enter R5 adjust: count additional scanlines until C5 == R5 (frame complete).
		// C5 starts at 0 and increments per scanline until C5 >= R5.
		m_ctr.inAdjust = true;
		m_ctr.c5 = 0;
	}

	void CrtcSimulator::advance(int nops) {
		for (int i = 0; i < nops; i++)
			advanceOneChar();
	}

	std::vector<CpcValidationEntry> CrtcSimulator::checkWrite(uint8_t reg, uint8_t value, CpcCrtcType crtcType) const {
		std::vector<CpcValidationEntry> result;

		// Get the RegisterRule for this register
		RegisterRule rule = GetRegisterRule(reg);

		// Run the rule's checkWrite lambda for validation
		// This uses the register timing constraints already defined in the RegisterRule system
		std::string warning = rule.checkWrite(-1, m_ctr, m_regs, value);
		if (!warning.empty()) {
			result.push_back({
				CpcValidationSeverity::Warning,
				-1,
				"R" + std::to_string(reg) + ": " + warning
			});
		}

		// Additional per-type checks for known CRTC constraints
		// Type 0 (HD6845S): R3 sync width high nibble > 15 may cause issues
		if (crtcType == CpcCrtcType::Type0 && reg == 3) {
			uint8_t vsyncWidth = (value >> 4) & 0x0F;
			if (vsyncWidth > 15) {
				result.push_back({
					CpcValidationSeverity::Warning,
					-1,
					"R3: CRTC Type 0 (HD6845S) has limited VSYNC width (bits 7-4 > 15 may be unsafe)"
				});
			}
		}

		// Check value ranges for all types
		if (reg <= 7 || reg == 9) {
			// Most CRTC registers have 8-bit values; check for obvious issues
			// R1 (HDIS) typically <= R0 (HTOT)
			if (reg == 1 && value > m_regs.r0) {
				result.push_back({
					CpcValidationSeverity::Warning,
					-1,
					"R1 (HDIS) value #" + FormatHexByte(value) + " > R0 (HTOT) #" + FormatHexByte(m_regs.r0) + " may cause display issues"
				});
			}
			// R6 (VDIS) typically <= R4 (VTOT)
			if (reg == 6 && value > m_regs.r4) {
				result.push_back({
					CpcValidationSeverity::Warning,
					-1,
					"R6 (VDIS) value #" + FormatHexByte(value) + " > R4 (VTOT) #" + FormatHexByte(m_regs.r4) + " may cause display issues"
				});
			}
		}

		return result;
	}

	int CrtcSimulator::runFrame() {
		int nopsAtStart = m_ctr.absNop;
		int iterationLimit = 100000;  // Safeguard: ~312 lines * ~320 chars/line = ~100K iterations max
		int iterations = 0;
		//
		// D-2 overflow detection: If frame doesn't complete after iterationLimit iterations,
		// there's likely a degenerate R4/R5 combination that never sets frameComplete.
		// Log error and return what we have rather than looping infinitely.
		//
		while (!m_ctr.frameComplete && iterations < iterationLimit) {
			advanceOneChar();
			iterations++;
		}
		if (iterations >= iterationLimit) {
			RasterDbg("[SIM]", "runFrame overflow: Did not complete after %d iterations. R4=%d R9=%d R5=%d may be degenerate.",
				iterationLimit, (int)m_regs.r4, (int)m_regs.r9, (int)m_regs.r5);
		}
		m_ctr.frameComplete = false;
		return m_ctr.absNop - nopsAtStart;
	}

	int CrtcSimulator::currentScanline() const {
		return m_ctr.absScanline;
	}

	RegisterRule CrtcSimulator::GetRegisterRule(uint8_t reg) const {
		// Call the static GetRegisterRule which has all the constraint rules
		return ::RetrodevLib::GetRegisterRule(reg);
	}

	int CrtcSimulator::TotalScanLines(const RasterFrameCmd& f) {
		return (f.r4 + 1) * (f.r9 + 1) + f.r5;
	}

	std::vector<int> CrtcSimulator::InterruptLines(const RasterFrameCmd& f) {
		std::vector<int> result;
		int total = TotalScanLines(f);
		for (int sl = 52; sl < total; sl += 52) {
			result.push_back(sl);
		}
		return result;
	}

	bool CrtcSimulator::IsDisplayScanLine(const RasterFrameCmd& f, int scanLine) {
		//
		// Display scanlines span from 0 to R6*(R9+1)-1.
		// R6 = vertical displayed (number of displayed character rows)
		// R9 = max raster (scanlines per character row - 1)
		// (Compendium sec.6.1.1 / sec.18)
		//
		int displayEndSl = f.r6 * (f.r9 + 1);
		return scanLine >= 0 && scanLine < displayEndSl;
	}

	bool CrtcSimulator::IsHSyncChar(const RasterFrameCmd& f, int col) {
		return col >= (int)f.r2 && col < (int)f.r2 + (f.r3 & 0x0F);
	}

	bool CrtcSimulator::IsVSyncScanLine(const RasterFrameCmd& f, int scanLine) {
		if (f.r7 == 0) {
			int totalFrameScanLines = (f.r4 + 1) * (f.r9 + 1) + f.r5;
			int vsyncStartSl = totalFrameScanLines - 2 * (f.r9 + 1);
			int vsyncEndSl = totalFrameScanLines;
			return scanLine >= vsyncStartSl && scanLine < vsyncEndSl;
		}
		int vsyncStartSl = f.r7 * (f.r9 + 1);
		int vsyncHeight = ((f.r3 >> 4) & 0x0F);
		if (vsyncHeight == 0) vsyncHeight = 16;
		int vsyncEndSl = vsyncStartSl + vsyncHeight * (f.r9 + 1);
		return scanLine >= vsyncStartSl && scanLine < vsyncEndSl;
	}

	RasterFrameCmd CrtcSimulator::MergeFrame(const RasterFrameCmd& base, const RasterFrameCmd& src) {
		RasterFrameCmd merged = base;
		if (src.activeMask & RasterFrameCmd::MASK_R0)  merged.r0 = src.r0;
		if (src.activeMask & RasterFrameCmd::MASK_R1)  merged.r1 = src.r1;
		if (src.activeMask & RasterFrameCmd::MASK_R2)  merged.r2 = src.r2;
		if (src.activeMask & RasterFrameCmd::MASK_R3)  merged.r3 = src.r3;
		if (src.activeMask & RasterFrameCmd::MASK_R4)  merged.r4 = src.r4;
		if (src.activeMask & RasterFrameCmd::MASK_R5)  merged.r5 = src.r5;
		if (src.activeMask & RasterFrameCmd::MASK_R6)  merged.r6 = src.r6;
		if (src.activeMask & RasterFrameCmd::MASK_R7)  merged.r7 = src.r7;
		if (src.activeMask & RasterFrameCmd::MASK_R9)  merged.r9 = src.r9;
		if (src.activeMask & RasterFrameCmd::MASK_R12) { merged.r12 = src.r12; merged.r13 = src.r13; }
		merged.disableVSync = src.disableVSync;
		//
		// activeMask represents which registers are explicitly set in the CURRENT frame (src).
		// Do NOT accumulate from base.activeMask -- only src registers are scheduled for writing.
		// smcMask must also reflect ONLY src, not accumulate from base.
		// disableVSync is handled in code generation (emits R7=255), not here.
		// Both masks must reflect ONLY what's explicitly defined in the frame command.
		//
		merged.activeMask = src.activeMask;
		merged.smcMask = src.smcMask;
		return merged;
	}

	}  // namespace CpcRaster

	// ---------------------------------------------------------------------------
	// DetectConstantRegisters implementation
	// ---------------------------------------------------------------------------

	CpcRaster::CrtcKnownState CPCRaster::DetectConstantRegisters() {
		using namespace CpcRaster;
		CrtcKnownState result;
		result.resetToDefaults();

		// Scan all commands and collect written values per register
		std::map<uint8_t, std::set<uint8_t>> regValues;

		for (const auto& cmd : m_cpcCommands) {
			if (std::holds_alternative<CpcFrameCommand>(cmd)) {
				const auto& frameCmd = std::get<CpcFrameCommand>(cmd);
				if (!frameCmd.enabled)
					continue;
				// Collect Frame register writes
				if (frameCmd.frame.activeMask & RasterFrameCmd::MASK_R0)
					regValues[0].insert(frameCmd.frame.r0);
				if (frameCmd.frame.activeMask & RasterFrameCmd::MASK_R1)
					regValues[1].insert(frameCmd.frame.r1);
				if (frameCmd.frame.activeMask & RasterFrameCmd::MASK_R2)
					regValues[2].insert(frameCmd.frame.r2);
				if (frameCmd.frame.activeMask & RasterFrameCmd::MASK_R3)
					regValues[3].insert(frameCmd.frame.r3);
				if (frameCmd.frame.activeMask & RasterFrameCmd::MASK_R4)
					regValues[4].insert(frameCmd.frame.r4);
				if (frameCmd.frame.activeMask & RasterFrameCmd::MASK_R5)
					regValues[5].insert(frameCmd.frame.r5);
				if (frameCmd.frame.activeMask & RasterFrameCmd::MASK_R6)
					regValues[6].insert(frameCmd.frame.r6);
				// R7: Always consider it because disableVSync affects the actual value written
				// If MASK_R7 is set, the value depends on disableVSync flag
				// If MASK_R7 is NOT set but disableVSync is true, R7 will be written as 255
				if (frameCmd.frame.activeMask & RasterFrameCmd::MASK_R7) {
					regValues[7].insert(frameCmd.frame.disableVSync ? 255 : frameCmd.frame.r7);
				} else if (frameCmd.frame.disableVSync) {
					// Even if R7 is not being explicitly set, disableVSync causes it to be 255
					regValues[7].insert(255);
				}
				if (frameCmd.frame.activeMask & RasterFrameCmd::MASK_R9)
					regValues[9].insert(frameCmd.frame.r9);
			} else if (std::holds_alternative<CpcEffectCommand>(cmd)) {
				const auto& effectCmd = std::get<CpcEffectCommand>(cmd);
				// Collect Effect register writes
				for (const auto& write : effectCmd.writes) {
					if (write.target == CpcRegTarget::CRTC)
						regValues[write.reg].insert(write.value);
				}
			}
		}

		// Identify constant registers (only one distinct value written)
		for (const auto& entry : regValues) {
			uint8_t reg = entry.first;
			const std::set<uint8_t>& values = entry.second;
			if (values.size() == 1) {
				result.set(reg, *values.begin());
			} else {
				result.clear(reg);  // Multiple values â†’ mark as non-constant (unknown)
			}
		}

		return result;
	}

	// ---------------------------------------------------------------------------
	// CPCRaster - Static serialization helpers for CPC configuration
	// ---------------------------------------------------------------------------

	std::string CPCRaster::SaveCpcConfig(const CpcRasterParams& config) {
		std::string out;
		(void)glz::write_json(config, out);
		return out;
	}

	CpcRasterParams CPCRaster::LoadCpcConfig(const std::string& configJson) {
		CpcRasterParams config;
		if (!configJson.empty()) {
			(void)glz::read_json(config, configJson);
		}
		return config;
	}

	// ---------------------------------------------------------------------------
	// CPCRaster - Instance methods for project serialization and state access
	// ---------------------------------------------------------------------------

	void CPCRaster::LoadProject(const RasterParams& genericParams) {
		m_rasterParams = genericParams;
		m_cpcCommands.clear();
		for (const auto& cmdStr : genericParams.commands) {
			m_cpcCommands.push_back(Load(cmdStr));
		}
		m_cpcConfig = LoadCpcConfig(genericParams.config);
	}

	void CPCRaster::SaveProject(RasterParams& genericParams) {
		genericParams = m_rasterParams;
		genericParams.commands.clear();
		for (const auto& cmd : m_cpcCommands) {
			genericParams.commands.push_back(Save(cmd));
		}
		genericParams.config = SaveCpcConfig(m_cpcConfig);
		// Keep m_rasterParams in sync with serialized live state so it doesn't become stale
		m_rasterParams = genericParams;
	}

	std::vector<CpcRasterCommand>& CPCRaster::GetCommands() {
		return m_cpcCommands;
	}

	const std::vector<CpcRasterCommand>& CPCRaster::GetCommands() const {
		return m_cpcCommands;
	}

	CpcRasterParams& CPCRaster::GetCpcConfig() {
		return m_cpcConfig;
	}

	const CpcRasterParams& CPCRaster::GetCpcConfig() const {
		return m_cpcConfig;
	}

	RasterParams* CPCRaster::GetRasterParams() {
		return &m_rasterParams;
	}

	const RasterParams* CPCRaster::GetRasterParams() const {
		return &m_rasterParams;
	}

	// ---------------------------------------------------------------------------
	// CPCRaster::Monitor -- Pixel-accurate monitor image rendering
	// ---------------------------------------------------------------------------

	bool CPCRaster::Monitor(
		std::vector<uint32_t>& outPixels,
		int& outWidth,
		int& outHeight,
		int& outPhaseLines) {

		using namespace CpcRaster;

		// Project must be loaded
		if (m_cpcCommands.empty())
			return false;

		//
		// Build MonitorSlot list from Frame commands, similar to GUI visualizer logic.
		// Each Frame command contributes a slot with merged CRTC state and scanline range.
		//
		constexpr int kMonitorLines = 312;
		constexpr int kMaxCols = 64;

		// Build slots from frame commands
		std::vector<MonitorSlot> slots;
		RasterFrameCmd current;
		int scanlinePos = 0;

		for (size_t i = 0; i < m_cpcCommands.size(); i++) {
			if (!std::holds_alternative<CpcFrameCommand>(m_cpcCommands[i]))
				continue;

			const auto& frameCmd = std::get<CpcFrameCommand>(m_cpcCommands[i]);
			RasterFrameCmd merged = CrtcSimulator::MergeFrame(current, frameCmd.frame);
			int slotHeight = CrtcSimulator::TotalScanLines(merged);

			MonitorSlot slot;
			slot.state = merged;
			slot.startSl = scanlinePos;
			slot.endSl = scanlinePos + slotHeight;
			slot.slotIndex = (int)slots.size();
			slots.push_back(slot);

			scanlinePos += slotHeight;
			current = merged;

			// Stop if we've exceeded monitor height
			if (scanlinePos >= kMonitorLines)
				break;
		}

		if (slots.empty())
			return false;

		// Compute actual CRTC scanline total
		int crtcTotal = slots.empty() ? kMonitorLines : slots.back().endSl;

		// Update monitor phase: hard re-lock at exact 312, free-run drift otherwise
		if (crtcTotal == kMonitorLines) {
			outPhaseLines = 0;
		} else {
			int deltaPhase = crtcTotal - kMonitorLines;
			outPhaseLines = (outPhaseLines + deltaPhase) % kMonitorLines;
			if (outPhaseLines < 0)
				outPhaseLines += kMonitorLines;
		}

		// Render pixel buffer
		CrtcSimulator::RenderMonitorImage(slots, kMaxCols, crtcTotal, outPhaseLines, outPixels, outWidth, outHeight);

		return true;
	}

	void CPCRaster::Validate(CpcValidationResult& result) {
		using namespace CpcRaster;
		result.ok = true;
		result.entries.clear();

		//
		// Helper lambdas to push errors / warnings
		//
		auto pushError = [&](int idx, const std::string& msg) {
			result.ok = false;
			CpcValidationEntry e;
			e.severity = CpcValidationSeverity::Error;
			e.cmdIndex = idx;
			e.message  = msg;
			result.entries.push_back(e);
		};
		auto pushWarn = [&](int idx, const std::string& msg) {
			CpcValidationEntry e;
			e.severity = CpcValidationSeverity::Warning;
			e.cmdIndex = idx;
			e.message  = msg;
			result.entries.push_back(e);
		};

		//
		// CRITICAL: Verify initC* values match the VSyncPos configuration.
		// initC values (C9=2, C0=31, C4=0) are ONLY valid when VSyncPos (R7) is at frame boundary (0).
		// If VSyncPos has been modified to a mid-frame position (1-254), timing shifts and
		// initC values must be remeasured from actual hardware / emulator.
		// This warning helps users catch timing mismatches.
		//
		// Check all frames for non-standard R7 (VSyncPos) values
		bool foundNonStandardR7 = false;
		for (const auto& cmd : m_cpcCommands) {
			if (std::holds_alternative<CpcFrameCommand>(cmd)) {
				const auto& frameCmd = std::get<CpcFrameCommand>(cmd);
				if (frameCmd.enabled) {
					const auto& f = frameCmd.frame;
					// R7 = 0 is frame boundary (standard), 255 is disabled, anything else is mid-frame
					if ((f.activeMask & RasterFrameCmd::MASK_R7) && f.r7 != 0 && f.r7 != 255) {
						foundNonStandardR7 = true;
						break;
					}
				}
			}
		}
		// Warn if non-standard R7 found AND initC values are still at defaults
		if (foundNonStandardR7 &&
			m_cpcConfig.initC9 == 2 && m_cpcConfig.initC0 == 31 && m_cpcConfig.initC4 == 0) {
			pushWarn(-1,
				"[initC configuration] VSyncPos (R7) is set to a non-standard value (not 0 or 255), "
				"but initC9/initC0/initC4 are at default hardware-measured values (C9=2, C0=31, C4=0). "
				"These initC values are ONLY valid when VSync fires at frame boundary (R7=0). "
				"If you've modified VSyncPos for mid-frame effects, please measure actual initC values "
				"from your emulator/hardware and update the configuration. "
				"(If intentional, update initC values or set R7=0 or R7=255 to suppress this warning.)");
		}

		//
		// Build merged slots exactly as the visualizer does, so validation matches display.
		// Each Frame command is merged onto the cumulative state; the result is the true
		// effective CRTC register set that will be written to hardware.
		//
		RasterFrameCmd current;
		bool hasFrame = false;
		//
		// Screen-group accumulation (CPC VSync semantics):
		// - Frame with disableVSync=true: Starts/continues a group, VSync will NOT fire (disabled)
		// - Frame with disableVSync=false but R7 NOT set: Part of group, VSync still deferred
		// - Frame with disableVSync=false and R7 SET: Closes the group, VSync fires at R7 position
		// Accumulated scanlines from group start (disableVSync=true) to group end (R7 set)
		// must equal 312 for stable 50 Hz PAL timing. groupFirstCmd records the first frame index.
		//
		int groupLines    = 0;
		int groupFirstCmd = -1;
		bool inGroup      = false;

		for (int i = 0; i < (int)m_cpcCommands.size(); i++) {
			const auto& cmd = m_cpcCommands[i];
			if (!std::holds_alternative<CpcFrameCommand>(cmd))
				continue;
			const auto& frameCmd = std::get<CpcFrameCommand>(cmd);
			if (!frameCmd.enabled)
				continue;
			hasFrame = true;
			RasterFrameCmd f = CrtcSimulator::MergeFrame(current, frameCmd.frame);
			current = f;
			std::string tag = "[" + frameCmd.name + "] ";

			//
			// Detect if this frame is a single complete frame or part of a multi-frame group.
			// Multi-frame group structure (CPC VSync semantics):
			//   - Frame with disableVSync=true: Starts a group, VSync disabled for this frame
			//   - Frame with disableVSync=false AND R7 not set (no MASK_R7): Part of group, VSync still deferred
			//   - Frame with disableVSync=false AND R7 set (has MASK_R7): Closes group, VSync fires at R7
			//   - Frame with disableVSync=false and R7 set as FIRST frame: Single complete frame (standalone)
			// Per-frame vertical constraints (R6 > R4+1, R7 > R4, etc.) only apply to complete frames.
			// For multi-frame groups, individual frames are partial and constraints don't apply.
			//
			bool hasR7 = (f.activeMask & RasterFrameCmd::MASK_R7) != 0;
			bool isMultiFrameContext = f.disableVSync || (inGroup && !hasR7);

			//
			// Register hard limits (from the CRTC 6845 datasheet) — always checked
			//
			if (f.r0 == 0)
				pushWarn(i, tag + "R0=0: horizontal total = 1 character. C0 stays at 0 (special CRTC 0 freeze). Intentional?");
			if (f.r1 > f.r0)
				pushError(i, tag + "R1 (" + std::to_string(f.r1) + ") > R0 (" + std::to_string(f.r0) + "): horizontal displayed exceeds horizontal total.");
			if (f.r1 == 0)
				pushWarn(i, tag + "R1=0: no characters displayed, BORDER only.");
			if (f.r2 > f.r0)
				pushError(i, tag + "R2 (" + std::to_string(f.r2) + ") > R0 (" + std::to_string(f.r0) + "): HSync position outside horizontal total.");

			int hsyncWidth = f.r3 & 0x0F;
			if (hsyncWidth == 0)
				pushWarn(i, tag + "R3 HSync width = 0: HSync disabled (CRTC 0 / 1 only -- CRTC 2/3/4 generate 16-char HSync instead).");
			if (hsyncWidth > 0 && (f.r2 + hsyncWidth > f.r0 + 1))
				pushWarn(i, tag + "HSync wraps around line boundary: R2(" + std::to_string(f.r2) + ") + width(" + std::to_string(hsyncWidth) + ") > R0+1(" + std::to_string(f.r0 + 1) + "). Verify this is intentional.");
			if (f.r2 < f.r1)
				pushError(i, tag + "R2 (" + std::to_string(f.r2) + ") < R1 (" + std::to_string(f.r1) + "): HSync starts inside displayed region.");

			int vsyncWidth = (f.r3 >> 4) & 0x0F;
			if (vsyncWidth == 0)
				pushWarn(i, tag + "R3 VSync width = 0: will produce 16-line VSync on CRTC 0/3/4 (CRTC 1/2 always use 16 lines -- no impact there).");
			if (f.r4 > 127)
				pushError(i, tag + "R4 (" + std::to_string(f.r4) + ") > 127: exceeds 7-bit register limit.");
			if (f.r5 > 31)
				pushError(i, tag + "R5 (" + std::to_string(f.r5) + ") > 31: exceeds 5-bit register limit.");
			//
			// Vertical constraints (R5 vs R9, R6 vs R4, R7 vs R4/R6) only apply to complete frames.
			// In multi-frame contexts, individual frames are partial and don't need these checks.
			//
			if (!isMultiFrameContext) {
				if (f.r5 > f.r9)
					pushWarn(i, tag + "R5 (" + std::to_string(f.r5) + ") > R9 (" + std::to_string(f.r9) + "): vertical adjust spans more than one character row. Non-standard -- may work but verify timing.");
				if (f.r6 > f.r4 + 1)
					pushWarn(i, tag + "R6 (" + std::to_string(f.r6) + ") > R4+1 (" + std::to_string(f.r4 + 1) + "): vertical displayed exceeds vertical total -- CRTC will display all rows. May be intentional.");
				if (!f.disableVSync && f.r7 != 0) {
					if (f.r7 > f.r4)
						pushError(i, tag + "R7 (" + std::to_string(f.r7) + ") > R4 (" + std::to_string(f.r4) + "): VSync position outside frame.");
					if (f.r7 < f.r6)
						pushWarn(i, tag + "R7 (" + std::to_string(f.r7) + ") < R6 (" + std::to_string(f.r6) + "): VSync fires inside displayed region -- monitor may lose sync. May be intentional in overscan designs.");
				}
			}
			if (f.r9 > 31)
				pushError(i, tag + "R9 (" + std::to_string(f.r9) + ") > 31: exceeds 5-bit register limit.");

			//
			// Screen-group timing check: accumulated scanlines must total 312 per group.
			// A group is closed when a frame has R7 set (hasR7=true), which signals VSync firing.
			//
			int frameSl = FrameLength(f);
			if (!inGroup) {
				groupLines    = 0;
				groupFirstCmd = i;
				inGroup       = true;
			}
			groupLines += frameSl;
			if (hasR7) {
				// R7 set: this frame closes the group. Check accumulated total = 312 lines.
				int delta = groupLines - 312;
				bool multiFrame = (groupFirstCmd != i);
				std::string groupTag = multiFrame
					? "[frames " + std::to_string(groupFirstCmd) + ".." + std::to_string(i) + "] "
					: tag;
				if (delta != 0) {
					if (delta < -5 || delta > 5)
						pushError(i, groupTag + "Total scan lines = " + std::to_string(groupLines) + " (expected 312 for 50 Hz PAL). Delta = " + std::to_string(delta) + ".");
					else
						pushWarn(i, groupTag + "Total scan lines = " + std::to_string(groupLines) + " (expected 312 for 50 Hz PAL). Delta = " + std::to_string(delta) + " -- minor drift, monitor may tolerate but verify.");
				}
				if (!multiFrame && f.r6 == f.r4 + 1)
					pushWarn(i, tag + "R6 == R4+1: full vertical fill -- no VBlank border, monitor border disabled entirely. May be intentional in overscan designs.");
				groupLines = 0;
				inGroup    = false;
			}

			//
			// HSync width advisories
			//
			if (hsyncWidth > 0 && hsyncWidth < 2)
				pushWarn(i, tag + "HSync width = " + std::to_string(hsyncWidth) + ": less than 2 chars -- Gate Array may not complete mode update. Minimum 2 required for reliable graphic mode switch.");
			if (hsyncWidth > 0 && hsyncWidth < 6)
				pushWarn(i, tag + "HSync width = " + std::to_string(hsyncWidth) + ": C-HSYNC pulse < 4 sec.s. Monitor may lose horizontal sync on some displays.");
		}

		//
		// Unclosed group warning: group started but never closed with R7 set
		//
		if (inGroup && groupLines > 0) {
			int delta = groupLines - 312;
			std::string groupTag = "[frames " + std::to_string(groupFirstCmd) + "..end (no R7 closure)] ";
			if (delta != 0) {
				if (delta < -5 || delta > 5)
					pushError(-1, groupTag + "Accumulated scan lines = " + std::to_string(groupLines) + " with no R7 closure (expected 312). Delta = " + std::to_string(delta) + ".");
				else
					pushWarn(-1, groupTag + "Accumulated scan lines = " + std::to_string(groupLines) + " with no R7 closure (expected 312). Delta = " + std::to_string(delta) + " -- minor drift.");
			} else {
				pushWarn(-1, groupTag + "Accumulated scan lines = 312 but R7 never set -- VSync will not fire at expected position.");
			}
		}

		//
		// SMC Patch Function Balance Warning: Check for frames with patch functions but no compensating frame with SMC
		// A frame with smcPatchFunctionMask set will change its size at runtime via a patch function.
		// To maintain total scanlines (312), another frame in the group must have SMC enabled for any boundary-affecting register (R4, R5, R9).
		// User can compensate with regular SMC (not just patch functions).
		//
		{
			int patchFrameCount = 0;
			int patchFrameIndex = -1;
			std::string patchFrameName;
			for (int i = 0; i < (int)m_cpcCommands.size(); i++) {
				const auto& cmd = m_cpcCommands[i];
				if (!std::holds_alternative<CpcFrameCommand>(cmd))
					continue;
				const auto& frameCmd = std::get<CpcFrameCommand>(cmd);
				if (!frameCmd.enabled)
					continue;
				if ((frameCmd.frame.smcPatchFunctionMask & RasterFrameCmd::MASK_R5) != 0) {
					patchFrameCount++;
					patchFrameIndex = i;
					patchFrameName = frameCmd.name;
				}
			}
			if (patchFrameCount == 1) {
				// Check if any other enabled frame has SMC for boundary-affecting registers (R4, R5, R9)
				bool hasCompensatingFrame = false;
				uint32_t boundaryMask = RasterFrameCmd::MASK_R4 | RasterFrameCmd::MASK_R5 | RasterFrameCmd::MASK_R9;
				for (int i = 0; i < (int)m_cpcCommands.size(); i++) {
					const auto& cmd = m_cpcCommands[i];
					if (!std::holds_alternative<CpcFrameCommand>(cmd))
						continue;
					const auto& frameCmd = std::get<CpcFrameCommand>(cmd);
					if (!frameCmd.enabled || i == patchFrameIndex)
						continue;
					if ((frameCmd.frame.smcMask & boundaryMask) != 0) {
						hasCompensatingFrame = true;
						break;
					}
				}
				if (!hasCompensatingFrame) {
					pushWarn(patchFrameIndex,
						"[" + patchFrameName + "] This frame has R5 patch function enabled but no other enabled frame in the group has SMC enabled for boundary-affecting registers (R4, R5, R9). "
						"Changing R5 at runtime will change the frame height. To maintain the 312-line total, another frame must have SMC enabled to compensate. "
						"(If compensating via a non-SMC mechanism, you can ignore this warning.)");
				}
			}
		}

		//
		// Global checks: no Frame commands, Subroutine in VSync+Loop mode
		//
		if (!hasFrame)
			pushWarn(-1, "No Frame commands defined. Add at least one Frame command to describe the CRTC register set.");

		CpcRasterTimingMode timingMode = ResolveTimingMode(m_cpcConfig.generatorMode);
		if (timingMode == CpcRasterTimingMode::TimedRasterLoop) {
			for (int i = 0; i < (int)m_cpcCommands.size(); i++) {
				const auto& cmd = m_cpcCommands[i];
				if (std::holds_alternative<CpcSubroutineCommand>(cmd)) {
					const auto& subCmd = std::get<CpcSubroutineCommand>(cmd);
					pushError(i, "[" + subCmd.name + "] Subroutine commands are not available in VSync+Loop mode. Use Interrupt-driven mode instead.");
				}
			}
		}
	}

}