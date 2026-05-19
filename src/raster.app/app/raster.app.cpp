// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Raster
//
// Standalone raster editor application entry point and loop.
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#include "raster.app.h"
#include <app/app.icons.mdi.h>
#include <convert/amstrad.cpc/amstrad.cpc.h>
#include <convert/converters.h>
#include <project/metadata/meta.raster.h>
#include <glaze/glaze.hpp>
#include <system/version.h>
#include <filesystem>
#include <utility>

namespace RetrodevGui {

	struct RasterFileJson {
		std::string format;
		int version = 0;
		RetrodevLib::RasterParams settings;
	};

}

template <> struct glz::meta<RetrodevGui::RasterFileJson> {
	using T = RetrodevGui::RasterFileJson;
	static constexpr auto value = glz::object("format", &T::format, "version", &T::version, "settings", &T::settings);
};

template <> struct glz::meta<RetrodevLib::ExportParams> {
	using T = RetrodevLib::ExportParams;
	static constexpr auto value = glz::object("scriptPath", &T::scriptPath, "outputName", &T::outputName, "scriptParams", &T::scriptParams);
};

namespace RetrodevGui {

	namespace {
		constexpr const char* kRasterFormat = "retrodev.raster";
		constexpr int kRasterVersion = 1;

		void NormalizeParams(RetrodevLib::RasterParams& params) {
			if (params.targetSystem.empty())
				params.targetSystem = RetrodevLib::SupportedSystems::AmstradCPC;
			if (params.targetMode.empty())
				params.targetMode = RetrodevLib::ConverterAmstradCPC::CPCModes::Mode0;
			if (params.targetPaletteType.empty())
				params.targetPaletteType = RetrodevLib::ConverterAmstradCPC::CPCPaletteTypes::Hardware;
			if (params.selectedCommand < -1)
				params.selectedCommand = -1;
		}

		bool SaveRasterFile(const std::string& filePath, const RetrodevLib::RasterParams& params, std::string& outError) {
			RasterFileJson doc;
			doc.format = kRasterFormat;
			doc.version = kRasterVersion;
			doc.settings = params;

			std::string buffer;
			auto ec = glz::write_json(doc, buffer);
			if (ec) {
				outError = "Failed to serialize raster JSON.";
				return false;
			}

			std::string pretty = glz::prettify_json(buffer);
			auto wec = glz::buffer_to_file(pretty, filePath);
			if (wec != glz::error_code::none) {
				outError = "Could not write raster JSON file.";
				return false;
			}

			return true;
		}

		bool LoadRasterFile(const std::string& filePath, RetrodevLib::RasterParams& outParams, std::string& outError) {
			RasterFileJson doc;
			std::string buffer;
			auto ec = glz::read_file_json<glz::opts{.error_on_missing_keys = false}>(doc, filePath, buffer);
			if (ec) {
				outError = "Could not parse raster JSON file.";
				return false;
			}

			if (doc.format != kRasterFormat) {
				outError = "Unsupported raster JSON format.";
				return false;
			}
			if (doc.version != kRasterVersion) {
				outError = "Unsupported raster JSON version.";
				return false;
			}

			NormalizeParams(doc.settings);
			outParams = std::move(doc.settings);
			return true;
		}
	}

	void RasterApplication::SetupImGuiStyle(float scale) {
		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		style.ScaleAllSizes(scale);
		style.FontScaleDpi = scale;
		style.FontSizeBase = 14.0f;
		style.WindowRounding = 4.0f;
		style.FrameRounding = 3.0f;
		style.TabRounding = 3.0f;
	}

	RetrodevLib::RasterParams RasterApplication::CreateDefaultParams() {
		RetrodevLib::RasterParams params;
		params.targetSystem = RetrodevLib::SupportedSystems::AmstradCPC;
		params.targetMode = RetrodevLib::ConverterAmstradCPC::CPCModes::Mode0;
		params.targetPaletteType = RetrodevLib::ConverterAmstradCPC::CPCPaletteTypes::Hardware;
		params.selectedCommand = -1;
		return params;
	}

	void RasterApplication::SetStatus(const std::string& message, const ImVec4& color) {
		s_statusMessage = message;
		s_statusColor = color;
	}

	void RasterApplication::InitializeDefaultDocument() {
		s_params = CreateDefaultParams();
		s_currentFilePath.clear();
		if (s_currentFolder.empty()) {
			std::error_code ec;
			s_currentFolder = std::filesystem::current_path(ec).string();
		}
		s_panel.Reset();
		s_panel.SetParent(nullptr);
		s_panel.SetOnModified([]() { RasterApplication::s_modified = true; });
		s_panel.SetRenderer(s_renderer);
		s_panel.SetProjectFolder(s_currentFolder);
		s_modified = false;
		SetStatus("Created new raster document.", ImVec4(0.6f, 0.85f, 1.0f, 1.0f));
	}

	void RasterApplication::RequestAction(PendingAction action) {
		if (action == PendingAction::None)
			return;
		if (s_modified) {
			s_requestedAction = action;
			s_openUnsavedPopup = true;
			return;
		}
		ExecuteAction(action);
	}

	void RasterApplication::ExecuteAction(PendingAction action) {
		s_requestedAction = PendingAction::None;
		s_pendingActionAfterSave = PendingAction::None;
		s_openUnsavedPopup = false;
		auto& fileDialog = ImGui::FileDialog::Instance();
		switch (action) {
			case PendingAction::NewDocument:
				InitializeDefaultDocument();
				break;
			case PendingAction::OpenDocument:
				fileDialog.Open(kOpenDialogKey, "Open Raster", "Raster Document (*.raster){.raster}", false, s_currentFolder);
				break;
			case PendingAction::QuitApplication:
				s_done = true;
				break;
			default:
				break;
		}
	}

	bool RasterApplication::SaveCurrentDocument(bool allowSaveAs) {
		if (s_currentFilePath.empty()) {
			if (!allowSaveAs)
				return false;
			ImGui::FileDialog::Instance().Save(kSaveDialogKey, "Save Raster", "Raster Document (*.raster){.raster}", s_currentFolder);
			return false;
		}
		return SaveDocumentToPath(s_currentFilePath);
	}

	bool RasterApplication::SaveDocumentToPath(const std::string& filePath) {
		s_panel.SaveProject(&s_params);
		std::string error;
		if (!SaveRasterFile(filePath, s_params, error)) {
			SetStatus(error, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
			return false;
		}

		std::error_code ec;
		std::filesystem::path absPath = std::filesystem::absolute(std::filesystem::path(filePath), ec);
		s_currentFilePath = ec ? filePath : absPath.string();
		s_currentFolder = std::filesystem::path(s_currentFilePath).parent_path().string();
		s_panel.SetProjectFolder(s_currentFolder);
		s_modified = false;
		SetStatus("Raster file saved.", ImVec4(0.2f, 0.9f, 0.2f, 1.0f));
		return true;
	}

	bool RasterApplication::LoadDocumentFromPath(const std::string& filePath) {
		RetrodevLib::RasterParams loaded;
		std::string error;
		if (!LoadRasterFile(filePath, loaded, error)) {
			SetStatus(error, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
			return false;
		}

		std::error_code ec;
		std::filesystem::path absPath = std::filesystem::absolute(std::filesystem::path(filePath), ec);
		s_currentFilePath = ec ? filePath : absPath.string();
		s_currentFolder = std::filesystem::path(s_currentFilePath).parent_path().string();
		s_params = std::move(loaded);
		s_panel.Reset();
		s_panel.SetParent(nullptr);
		s_panel.SetOnModified([]() { RasterApplication::s_modified = true; });
		s_panel.SetRenderer(s_renderer);
		s_panel.SetProjectFolder(s_currentFolder);
		s_modified = false;
		SetStatus("Raster file loaded.", ImVec4(0.2f, 0.9f, 0.2f, 1.0f));
		return true;
	}

	void RasterApplication::RenderMainMenu() {
		if (ImGui::BeginMainMenuBar()) {
			if (ImGui::BeginMenu("File")) {
				if (ImGui::MenuItem(ICON_FILE " New", "Ctrl+N"))
					RequestAction(PendingAction::NewDocument);
				if (ImGui::MenuItem(ICON_FOLDER_OPEN " Open...", "Ctrl+O"))
					RequestAction(PendingAction::OpenDocument);
				if (ImGui::MenuItem(ICON_CONTENT_SAVE " Save", "Ctrl+S"))
					(void)SaveCurrentDocument(true);
				if (ImGui::MenuItem(ICON_CONTENT_SAVE_EDIT " Save As..."))
					ImGui::FileDialog::Instance().Save(kSaveDialogKey, "Save Raster", "Raster Document (*.raster){.raster}", s_currentFolder);
				ImGui::Separator();
				if (ImGui::MenuItem(ICON_POWER " Quit"))
					RequestAction(PendingAction::QuitApplication);
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}

		ImGuiIO& io = ImGui::GetIO();
		bool isShortcut = (io.ConfigMacOSXBehaviors ? (io.KeySuper && !io.KeyCtrl) : (io.KeyCtrl && !io.KeySuper)) && !io.KeyAlt;
		if (isShortcut && ImGui::IsKeyPressed(ImGuiKey_N, false))
			RequestAction(PendingAction::NewDocument);
		if (isShortcut && ImGui::IsKeyPressed(ImGuiKey_O, false))
			RequestAction(PendingAction::OpenDocument);
		if (isShortcut && ImGui::IsKeyPressed(ImGuiKey_S, false))
			(void)SaveCurrentDocument(true);
	}

	void RasterApplication::RenderMainWindow() {
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
		if (ImGui::Begin("RasterMainWindow", nullptr, flags)) {
			std::string titleName = s_currentFilePath.empty() ? "Untitled" : std::filesystem::path(s_currentFilePath).filename().string();
			std::string title = "RetroDev Raster - " + titleName + (s_modified ? " *" : "");
			SDL_SetWindowTitle(s_window, title.c_str());

			if (s_currentFilePath.empty())
				ImGui::Text("File: (unsaved)");
			else
				ImGui::Text("File: %s", s_currentFilePath.c_str());
			ImGui::SameLine();
			ImGui::TextColored(s_statusColor, "%s", s_statusMessage.c_str());
			ImGui::Separator();

			s_panel.SetProjectFolder(s_currentFolder);
			s_panel.RenderPanel(&s_params);
		}
		ImGui::End();
	}

	void RasterApplication::RenderDialogs() {
		auto& fileDialog = ImGui::FileDialog::Instance();

		if (fileDialog.IsDone(kOpenDialogKey)) {
			if (fileDialog.HasResult())
				(void)LoadDocumentFromPath(fileDialog.GetResult().string());
			fileDialog.Close();
		}

		if (fileDialog.IsDone(kSaveDialogKey)) {
			bool saved = false;
			if (fileDialog.HasResult())
				saved = SaveDocumentToPath(fileDialog.GetResult().string());
			fileDialog.Close();

			if (saved && s_pendingActionAfterSave != PendingAction::None) {
				PendingAction action = s_pendingActionAfterSave;
				s_pendingActionAfterSave = PendingAction::None;
				ExecuteAction(action);
			} else {
				s_pendingActionAfterSave = PendingAction::None;
			}
		}

		if (s_openUnsavedPopup) {
			ImGui::OpenPopup(kUnsavedPopupKey);
			s_openUnsavedPopup = false;
		}

		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		if (ImGui::BeginPopupModal(kUnsavedPopupKey, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Current raster has unsaved changes.");
			ImGui::Text("Save before continuing?");
			ImGui::Separator();

			if (ImGui::Button("Save", ImVec2(120.0f, 0.0f))) {
				PendingAction action = s_requestedAction;
				s_requestedAction = PendingAction::None;
				if (s_currentFilePath.empty()) {
					s_pendingActionAfterSave = action;
					fileDialog.Save(kSaveDialogKey, "Save Raster", "Raster Document (*.raster){.raster}", s_currentFolder);
				} else if (SaveDocumentToPath(s_currentFilePath)) {
					ExecuteAction(action);
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Discard", ImVec2(120.0f, 0.0f))) {
				PendingAction action = s_requestedAction;
				s_requestedAction = PendingAction::None;
				ExecuteAction(action);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
				s_requestedAction = PendingAction::None;
				s_pendingActionAfterSave = PendingAction::None;
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}

	bool RasterApplication::Initialize() {
		if (!RetrodevLib::RetroDevInit())
			return false;

		s_screenScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
		SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
		s_window = SDL_CreateWindow("RetroDev Raster", static_cast<int>(1280 * s_screenScale), static_cast<int>(820 * s_screenScale), windowFlags);
		if (s_window == nullptr)
			return false;

		s_renderer = SDL_CreateRenderer(s_window, nullptr);
		if (s_renderer == nullptr)
			return false;

		SDL_SetRenderVSync(s_renderer, 1);
		SDL_SetWindowPosition(s_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
		SDL_ShowWindow(s_window);

		IMGUI_CHECKVERSION();
		s_imguiContext = ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		const char* basePath = SDL_GetBasePath();
		s_iniFilePath = (basePath != nullptr ? basePath : "") + std::string("rasterdev.ini");
		io.IniFilename = s_iniFilePath.c_str();

		SetupImGuiStyle(s_screenScale);
		ImGui_ImplSDL3_InitForSDLRenderer(s_window, s_renderer);
		ImGui_ImplSDLRenderer3_Init(s_renderer);

		io.Fonts->AddFontDefault();

		// Load Fonts
		std::filesystem::path iconFontPath;
		const std::filesystem::path exeDir = (basePath != nullptr) ? std::filesystem::path(basePath) : std::filesystem::path();
		const std::filesystem::path candidates[] = {
			std::filesystem::path("src/gui/res/fonts/materialdesign.ttf"),
			exeDir / "materialdesign.ttf",
			exeDir / "../../../../../src/gui/res/fonts/materialdesign.ttf"
		};
		for (const auto& candidate : candidates) {
			std::error_code existsEc;
			if (std::filesystem::exists(candidate, existsEc) && !existsEc) {
				iconFontPath = candidate;
				break;
			}
		}
		if (!iconFontPath.empty()) {
			ImFontConfig fontConfig;
			fontConfig.MergeMode = true;
			io.Fonts->AddFontFromFileTTF(iconFontPath.string().c_str(), 14.0f, &fontConfig, nullptr);
		}

		auto& fileDialog = ImGui::FileDialog::Instance();
		fileDialog.DefaultFileIcon = ICON_FILE;
		fileDialog.FolderIcon = ICON_FOLDER;
		fileDialog.FolderOpenIcon = ICON_FOLDER_OPEN;
		fileDialog.FileTypeIcons[".raster"] = ICON_FILE_DOCUMENT;

		std::error_code ec;
		s_currentFolder = std::filesystem::current_path(ec).string();
		InitializeDefaultDocument();
		SetStatus("Ready.", ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
		return true;
	}

	void RasterApplication::Run() {
		s_done = false;
		while (!s_done) {
			SDL_Event event;
			while (SDL_PollEvent(&event)) {
				ImGui_ImplSDL3_ProcessEvent(&event);
				if (event.type == SDL_EVENT_QUIT)
					RequestAction(PendingAction::QuitApplication);
				if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(s_window))
					RequestAction(PendingAction::QuitApplication);
			}

			if (SDL_GetWindowFlags(s_window) & SDL_WINDOW_MINIMIZED) {
				SDL_Delay(10);
				continue;
			}

			ImGuiIO& io = ImGui::GetIO();
			ImGui_ImplSDLRenderer3_NewFrame();
			ImGui_ImplSDL3_NewFrame();
			ImGui::NewFrame();

			RenderMainMenu();
			RenderMainWindow();
			RenderDialogs();

			ImGui::Render();
			SDL_SetRenderScale(s_renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
			SDL_RenderClear(s_renderer);
			ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), s_renderer);
			SDL_RenderPresent(s_renderer);
		}
	}

	void RasterApplication::Shutdown() {
		s_panel.Reset();
		ImGui_ImplSDLRenderer3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
		s_imguiContext = nullptr;

		if (s_renderer) {
			SDL_DestroyRenderer(s_renderer);
			s_renderer = nullptr;
		}
		if (s_window) {
			SDL_DestroyWindow(s_window);
			s_window = nullptr;
		}

		RetrodevLib::RetroDevShutdown();
	}

}
