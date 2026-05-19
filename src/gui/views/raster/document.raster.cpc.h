// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Gui
//
// Raster document -- Amstrad CPC CRTC raster panels.
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#pragma once

#include <retrodev.gui.h>
#include <retrodev.lib.h>
#include <generators/raster/raster.params.h>
#include <generators/raster/amstrad.cpc/cpc.raster.h>
#include <widgets/palette.widget.h>
#include <SDL3/SDL.h>
#include <functional>

namespace RetrodevGui {
	//
	// Amstrad CPC-specific UI for the raster document.
	// Owns no project state -- operates exclusively on the RasterParams pointer
	// supplied by DocumentRaster on each Perform() call.
	// Splits horizontally: left = CRTC frame visualizer, right = command list + register editor.
	//
	class DocumentRasterCpc {
	public:
		~DocumentRasterCpc() {
			if (m_monitorTex)
				SDL_DestroyTexture(m_monitorTex);
		}
		//
		// Reset panel UI/cache state so a new raster file can be loaded cleanly.
		//
		void Reset();
		//
		// Set the parent document for modification notifications
		//
		void SetParent(class DocumentRaster* parent);
		//
		// Optional callback invoked whenever the panel data is modified.
		//
		void SetOnModified(std::function<void()> onModified);
		//
		// Set base folder used to resolve relative output paths on ASM export.
		//
		void SetProjectFolder(const std::string& projectFolder);
		//
		// Set SDL renderer used by monitor-mode texture creation.
		//
		void SetRenderer(SDL_Renderer* renderer);
		//
		// Initialize the panel with raster parameters (called once when document loads).
		// Transfers ownership of the raster state to the library instance.
		//
		void Initialize(RetrodevLib::RasterParams* params);
		//
		// Render the full CPC CRTC raster panel.
		// Called once per frame from DocumentRaster::Perform() when the target system is Amstrad CPC.
		//
		void RenderPanel(RetrodevLib::RasterParams* params);
		//
		// Save the raster state back to parameters (called when document is saved).
		//
		void SaveProject(RetrodevLib::RasterParams* params);
		//
		// Get mutable reference to commands for direct modification by UI
		//
		std::vector<RetrodevLib::CpcRasterCommand>& GetCommands() { return m_cpcCrtc.GetCommands(); }
		//
		// Get mutable reference to CPC config for direct modification by UI
		//
		RetrodevLib::CpcRasterParams& GetCpcConfig() { return m_cpcCrtc.GetCpcConfig(); }
	private:
		class DocumentRaster* m_parent = nullptr;
		//
		// Horizontal splitter state: left visualizer | right command editor
		//
		float m_hSizeLeft = 0.0f;
		float m_hSizeRight = 0.0f;
		bool m_sizesInitialized = false;
		//
		// Vertical splitter state: top main area | bottom violations panel
		//
		float m_vSizeTop = 0.0f;
		float m_vSizeBottom = 120.0f;
		//
		// Generator text buffers and one-time initialisation guard
		//
		char m_ruptureNameBuf[64] = {};
		char m_outputPathBuf[256] = {};
		bool m_generatorFieldsInit = false;
		char m_generatorStatusBuf[256] = {};
		ImVec4 m_generatorStatusColor = ImVec4(0.75f, 0.75f, 0.75f, 1.0f);
		bool m_generatorStatusVisible = false;
		//
		// Warnings extracted from the last code generation pass (e.g. slot NOP budget overruns).
		// Populated by scanning the generated output for "[WARNING:" tokens.
		//
		std::vector<std::string> m_generatorWarnings;
		//
		// Last validation result -- recomputed every frame from the current command set
		//
		RetrodevLib::CpcValidationResult m_validationResult;
		//
		// Auto-generation caching: generated assembly, timing report, validation result
		// These are regenerated on parameter change with debounce
		//
		std::string m_generatedAsm;
		RetrodevLib::CpcRaster::RasterTimingReport m_timingReport;
		//
		// Dirty flag and debounce timer for auto-generate on parameter change
		// When a parameter changes, m_dirty is set and m_debounceTime starts
		// Generation happens when debounce timer expires (200 ms default)
		//
		bool m_dirty = false;
		double m_debounceTime = 0.0;  // elapsed time since last parameter change
		static constexpr double m_debounceInterval = 0.2;  // 200 ms
		//
		// Monitor mode: when true the visualizer shows a pixel-accurate raster image
		// instead of the abstract design-mode CRTC grid.
		//
		bool m_monitorMode = false;
		SDL_Texture* m_monitorTex = nullptr;
		int m_monitorTexW = 0;
		int m_monitorTexH = 0;
		int m_monitorPhaseLines = 0;
		//
		// UI State: Command Selection
		//
		int m_selectedCommandIndex = -1;  // Currently selected command in the list
		//
		// UI State: Frame Command Checkboxes and SMC Labels
		//
		std::map<std::pair<int,int>, bool> m_frameSmcCheckboxStates;
		std::map<std::pair<int,int>, bool> m_framePatchFunctionCheckboxStates;
		std::map<std::pair<int,int>, std::array<char, 128>> m_frameSmcLabelBuffers;
		std::map<int, bool> m_vmaEnableStates;
		std::map<std::pair<int,int>, bool> m_vmaSmcCheckboxStates;
		std::map<int, std::array<char, 128>> m_vmaSmcLabelBuffers;
		//
		// UI State: Effect Command Selections
		//
		std::map<int, int> m_effectModeStates;
		std::map<int, int> m_effectOffsetStates;
		std::map<std::pair<int,size_t>, bool> m_effectSmcCheckboxStates;
		std::map<int, std::array<char, 128>> m_effectSmcLabelBuffers;
		//
		// UI State: Variable Command Options
		//
		std::map<int, bool> m_varUnrestrainedStates;
		//
		// Sync all UI state maps from command data (called each frame)
		//
		void SyncUIStateFromCommands();
		//
		// Left panel: draw the CRTC frame grid using ImDrawList
		//
		void RenderVisualizer();
		//
		// Right panel: command list (two-tab view: Commands | Source code) + register editor
		//
		void RenderCommandEditor(RetrodevLib::RasterParams* params);
		//
		// Effect editor UI (modal or inline) - prepared for future use
		//
		[[maybe_unused]] int m_selectedEffectIndex = -1;  // -1 = no selection
		[[maybe_unused]] int m_effectRegIndex = 0;  // selected register in effect editor
		[[maybe_unused]] bool m_showEffectEditor = false;
		//
		// Variable editor UI (modal or inline) - prepared for future use
		//
		[[maybe_unused]] int m_selectedVariableIndex = -1;  // -1 = no selection
		[[maybe_unused]] bool m_showVariableEditor = false;
		[[maybe_unused]] char m_varNameBuf[64] = {};  // variable name buffer
		[[maybe_unused]] int m_varValueBuf = 0;  // variable value
		//
		// Palette converter and color pickers for GA command editing
		//
		std::shared_ptr<RetrodevLib::IPaletteConverter> m_cpcPalette;
		SystemColorPicker m_inkColorPicker;
		SystemColorPicker m_borderColorPicker;
		//
		// Bottom panel: always-on validation violations list
		//
		void RenderViolationsPanel();
		//
		// Source code viewer (TextEditor for read-only ASM display)
		//
		ImGui::TextEditor m_sourceEditor;
		//
		// Auto-generation helpers
		//
		// Mark params as dirty and start debounce timer (called on any param change)
		void MarkDirty();
		//
		// Notify host that the document state changed (used for Save enablement and dirty flags).
		//
		void NotifyHostModified();
		//
		// Check debounce timer and regenerate if interval expired (called every frame)
		void UpdateAutoGenerate(double deltaTime);
		//
		// Timing gauges panel: shows per-slot NOP budget and usage metrics
		//
		void RenderTimingPanel();

		// Project folder path to resolve relative output paths
		std::string m_projectFolder = {};

		// Optional host callback invoked on any user-visible modification.
		std::function<void()> m_onModified;

		// Optional renderer override used by standalone hosts.
		SDL_Renderer* m_renderer = nullptr;

		// CPC CRTC library instance: one per raster document
		// Handles all serialization/deserialization and code generation
		RetrodevLib::CPCRaster m_cpcCrtc;

		// Initialization flag: LoadProject called once, library owns state from then on
		bool m_initialized = false;
	};

}
