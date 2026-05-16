// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Raster
//
// Standalone raster editor application entry point and loop.
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#pragma once

#include <retrodev.gui.h>
#include <views/raster/document.raster.cpc.h>
#include <string>

namespace RetrodevGui {

	class RasterApplication {
	public:
		static bool Initialize();
		static void Run();
		static void Shutdown();

	private:
		enum class PendingAction {
			None,
			NewDocument,
			OpenDocument,
			QuitApplication
		};

		static void SetupImGuiStyle(float scale);
		static RetrodevLib::RasterParams CreateDefaultParams();
		static void InitializeDefaultDocument();
		static void RequestAction(PendingAction action);
		static void ExecuteAction(PendingAction action);
		static bool SaveCurrentDocument(bool allowSaveAs);
		static bool SaveDocumentToPath(const std::string& filePath);
		static bool LoadDocumentFromPath(const std::string& filePath);
		static void RenderMainMenu();
		static void RenderMainWindow();
		static void RenderDialogs();
		static void SetStatus(const std::string& message, const ImVec4& color);

		inline static SDL_Window* s_window = nullptr;
		inline static SDL_Renderer* s_renderer = nullptr;
		inline static ImGuiContext* s_imguiContext = nullptr;
		inline static float s_screenScale = 1.0f;
		inline static bool s_done = false;
		inline static bool s_modified = false;
		inline static std::string s_iniFilePath;
		inline static std::string s_currentFilePath;
		inline static std::string s_currentFolder;
		inline static RetrodevLib::RasterParams s_params;
		inline static DocumentRasterCpc s_panel;
		inline static std::string s_statusMessage = "Ready.";
		inline static ImVec4 s_statusColor = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
		inline static PendingAction s_requestedAction = PendingAction::None;
		inline static PendingAction s_pendingActionAfterSave = PendingAction::None;
		inline static bool s_openUnsavedPopup = false;

		static constexpr const char* kOpenDialogKey = "RasterOpenDialog";
		static constexpr const char* kSaveDialogKey = "RasterSaveDialog";
		static constexpr const char* kUnsavedPopupKey = "UnsavedRasterChanges";
	};

}
