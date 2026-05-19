// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Gui
//
// Application entry point and main SDL/ImGui loop.
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#include "app.h"
#include <retrodev.gui.h>
#include <imgui_internal.h>
#include <views/main.view.h>
#include <views/main.view.menu.h>
#include <views/build/document.build.settings.h>
#include <app/app.resources.h>
#include <app/app.icons.mdi.h>
#include <app/version.check/version.check.h>
#include <app/version.check/version.check.ui.h>
#include <system/version.h>

namespace RetrodevGui {

	//
	// Setup ImGui custom Amstrad CPC-inspired color scheme and style
	//
	void Application::SetupImGuiStyle(float scale) {
		ImGuiStyle& style = ImGui::GetStyle();
		ImVec4* colors = style.Colors;
		// CPC green color palette
		const ImVec4 cpcGreenDark = ImVec4(0.0f, 0.3f, 0.0f, 1.0f);
		const ImVec4 cpcGreen = ImVec4(0.0f, 0.5f, 0.0f, 1.0f);
		const ImVec4 cpcGreenBright = ImVec4(0.0f, 0.8f, 0.0f, 1.0f);
		const ImVec4 cpcGreenVeryBright = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
		const ImVec4 darkBg = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);
		const ImVec4 darkerBg = ImVec4(0.05f, 0.05f, 0.05f, 1.0f);
		// Base colors
		colors[ImGuiCol_Text] = ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
		colors[ImGuiCol_TextDisabled] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
		colors[ImGuiCol_WindowBg] = darkBg;
		colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.94f);
		colors[ImGuiCol_Border] = ImVec4(0.0f, 0.4f, 0.0f, 0.5f);
		colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		// Frame backgrounds (comboboxes, input fields - darker green)
		colors[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.2f, 0.0f, 0.54f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.0f, 0.35f, 0.0f, 0.4f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.0f, 0.4f, 0.0f, 0.67f);
		// Title bar
		colors[ImGuiCol_TitleBg] = darkerBg;
		colors[ImGuiCol_TitleBgActive] = cpcGreenDark;
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.51f);
		// Menu bar
		colors[ImGuiCol_MenuBarBg] = ImVec4(0.1f, 0.1f, 0.1f, 1.0f);
		// Scrollbar
		colors[ImGuiCol_ScrollbarBg] = darkerBg;
		colors[ImGuiCol_ScrollbarGrab] = cpcGreenDark;
		colors[ImGuiCol_ScrollbarGrabHovered] = cpcGreen;
		colors[ImGuiCol_ScrollbarGrabActive] = cpcGreenBright;
		// Checkboxes and radio buttons
		colors[ImGuiCol_CheckMark] = cpcGreenVeryBright;
		// Sliders
		colors[ImGuiCol_SliderGrab] = cpcGreenBright;
		colors[ImGuiCol_SliderGrabActive] = cpcGreenVeryBright;
		// Buttons (darker green)
		colors[ImGuiCol_Button] = ImVec4(0.0f, 0.3f, 0.0f, 0.4f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.0f, 0.5f, 0.0f, 1.0f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.0f, 0.6f, 0.0f, 1.0f);
		// Headers (collapsible frames - brighter green)
		colors[ImGuiCol_Header] = ImVec4(0.0f, 0.5f, 0.0f, 0.5f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.0f, 0.7f, 0.0f, 0.8f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.0f, 0.8f, 0.0f, 1.0f);
		// Separators
		colors[ImGuiCol_Separator] = cpcGreenDark;
		colors[ImGuiCol_SeparatorHovered] = cpcGreen;
		colors[ImGuiCol_SeparatorActive] = cpcGreenBright;
		// Resize grip
		colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.4f, 0.0f, 0.2f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.0f, 0.6f, 0.0f, 0.67f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(0.0f, 0.8f, 0.0f, 0.95f);
		// Tabs
		colors[ImGuiCol_Tab] = ImVec4(0.0f, 0.25f, 0.0f, 0.86f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.0f, 0.6f, 0.0f, 0.8f);
		colors[ImGuiCol_TabActive] = ImVec4(0.0f, 0.5f, 0.0f, 1.0f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.0f, 0.15f, 0.0f, 0.97f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.0f, 0.35f, 0.0f, 1.0f);
		// Plot
		colors[ImGuiCol_PlotLines] = cpcGreenBright;
		colors[ImGuiCol_PlotLinesHovered] = cpcGreenVeryBright;
		colors[ImGuiCol_PlotHistogram] = cpcGreenBright;
		colors[ImGuiCol_PlotHistogramHovered] = cpcGreenVeryBright;
		// Table
		colors[ImGuiCol_TableHeaderBg] = ImVec4(0.0f, 0.3f, 0.0f, 1.0f);
		colors[ImGuiCol_TableBorderStrong] = ImVec4(0.0f, 0.4f, 0.0f, 1.0f);
		colors[ImGuiCol_TableBorderLight] = ImVec4(0.0f, 0.3f, 0.0f, 1.0f);
		colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
		colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
		// Text selection
		colors[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.5f, 0.0f, 0.35f);
		// Drag and drop target
		colors[ImGuiCol_DragDropTarget] = cpcGreenVeryBright;
		// Navigation highlight
		colors[ImGuiCol_NavHighlight] = cpcGreenBright;
		colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
		colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);
		// Modal dim background
		colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.6f);
		// Style configuration
		style.ScaleAllSizes(scale);
		style.FontScaleDpi = scale;
		style.FontSizeBase = 14.0f;
		style.FrameBorderSize = 0.0f;
		style.WindowBorderSize = 0.0f;
		style.WindowPadding = ImVec2(8.0f, 8.0f);
		style.FramePadding = ImVec2(8.0f, 4.0f);
		style.ItemSpacing = ImVec2(8.0f, 4.0f);
		style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
		style.CellPadding = ImVec2(4.0f, 2.0f);
		// Apply rounding to controls for a smoother look
		style.WindowRounding = 4.0f;
		style.ChildRounding = 2.0f;
		style.FrameRounding = 3.0f;
		style.PopupRounding = 3.0f;
		style.ScrollbarRounding = 4.0f;
		style.GrabRounding = 3.0f;
		style.TabRounding = 3.0f;
	}

	//
	// Initialize the application window
	//
	bool Application::Initialize() {
		// Initialize retrodev library
		title = "RetroDev " + RetrodevLib::GetVersion();
		if (!RetrodevLib::RetroDevInit()) {
			return false;
		}
		//
		// Wire the library logger to AppConsole so every RetrodevLib log event
		// appears in the application console routed to the matching channel and severity
		//
		RetrodevLib::Log::SetCallback([](RetrodevLib::LogLevel level, RetrodevLib::LogChannel channel, const char* message) {
			AppConsole::Channel ch = AppConsole::Channel::Output;
			if (channel == RetrodevLib::LogChannel::Build)
				ch = AppConsole::Channel::Build;
			else if (channel == RetrodevLib::LogChannel::Script)
				ch = AppConsole::Channel::Script;
			AppConsole::AddLog(ch, static_cast<AppConsole::LogLevel>(level), message);
		});
		//
		// SDL Window Initialization (SDL is already initialized in the lib)
		// Create window with SDL_Renderer graphics context
		// ------------------------------------------------------------------
		screenScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
		SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
		window = SDL_CreateWindow(title.c_str(), (int)(width * screenScale), (int)(height * screenScale), window_flags);
		if (window == nullptr) {
			return 1;
		}
		renderer = SDL_CreateRenderer(window, nullptr);
		SDL_SetRenderVSync(renderer, 1);
		if (renderer == nullptr) {
			return 1;
		}
		SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
		SDL_ShowWindow(window);
		#if SDL_VERSION_ATLEAST(3, 3, 0)
		SDL_SetDefaultTextureScaleMode(renderer, SDL_SCALEMODE_NEAREST);
		#else
		(void)renderer;
		#endif
		//
		// ImGui initialization
		// ------------------------------
		IMGUI_CHECKVERSION();
		imguiContext = ImGui::CreateContext();
		//
		// Register custom INI handlers before the first frame so they participate in ini load
		//
		MainView::RegisterSettingsHandler();
		MainViewMenu::RegisterSettingsHandler();
		EmulatorSettings::RegisterSettingsHandler();
		//
		// Disable ImGui's built-in Ctrl+Tab window-switching; document switching is handled by DocumentsView
		//
		imguiContext->ConfigNavWindowingKeyNext = ImGuiKey_None;
		imguiContext->ConfigNavWindowingKeyPrev = ImGuiKey_None;
		ImGuiIO& io = ImGui::GetIO();
		// Build an absolute path for retrodev.ini next to the executable so that
		// the file is always found regardless of the working directory at launch time
		// (e.g. when the application is started via a shortcut).
		const char* basePath = SDL_GetBasePath();
		iniFilePath = (basePath != nullptr ? basePath : "") + std::string("retrodev.ini");
		io.IniFilename = iniFilePath.c_str();
		// Apply custom Amstrad CPC-inspired theme
		SetupImGuiStyle(screenScale);
		// Init the backend
		ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
		ImGui_ImplSDLRenderer3_Init(renderer);
		// Load Fonts
		//
		// Ubuntu Medium -- default UI font
		Resource fontText = Resources::GetResource("gui.res.fonts.ubuntu-medium.ttf");
		if (fontText._ptr != nullptr) {
			ImFontConfig fontConfig;
			// We will manage the memory of the font data, so we set this to false to prevent ImGui from trying to free it.
			fontConfig.FontDataOwnedByAtlas = false;
			AppFont = io.Fonts->AddFontFromMemoryTTF((void*)fontText._ptr, fontText._size, 14.0f, &fontConfig, nullptr);
		}
		Resource fontIcon = Resources::GetResource("gui.res.fonts.materialdesign.ttf");
		if (fontIcon._ptr != nullptr) {
			ImFontConfig fontConfig;
			fontConfig.FontDataOwnedByAtlas = false;
			fontConfig.MergeMode = true;
			io.Fonts->AddFontFromMemoryTTF((void*)fontIcon._ptr, fontIcon._size, 14.0f, &fontConfig, nullptr);
		}
		// Fixedsys Excelsior Mono -- monospaced font for the text editor
		Resource fontEditor = Resources::GetResource("gui.res.fonts.fixedsys-excelsior-mono.ttf");
		if (fontEditor._ptr != nullptr) {
			ImFontConfig fontConfig;
			fontConfig.FontDataOwnedByAtlas = false;
			EditorFont = io.Fonts->AddFontFromMemoryTTF((void*)fontEditor._ptr, fontEditor._size, 14.0f, &fontConfig, nullptr);
		}

		//
		// File Dialog Configuration
		//
		auto& fd = ImGui::FileDialog::Instance();
		fd.DefaultFileIcon = ICON_FILE;
		fd.FolderIcon = ICON_FOLDER;
		fd.FolderOpenIcon = ICON_FOLDER_OPEN;

		// Image files
		fd.FileTypeIcons[".png"] = ICON_FILE_IMAGE;
		fd.FileTypeIcons[".jpg"] = ICON_FILE_IMAGE;
		fd.FileTypeIcons[".jpeg"] = ICON_FILE_IMAGE;
		fd.FileTypeIcons[".bmp"] = ICON_FILE_IMAGE;
		fd.FileTypeIcons[".tga"] = ICON_FILE_IMAGE;
		fd.FileTypeIcons[".gif"] = ICON_FILE_IMAGE;
		fd.FileTypeIcons[".svg"] = ICON_FILE_IMAGE;
		fd.FileTypeIcons[".ico"] = ICON_FILE_IMAGE;

		// Audio files
		fd.FileTypeIcons[".mp3"] = ICON_FILE_MUSIC;
		fd.FileTypeIcons[".wav"] = ICON_FILE_MUSIC;
		fd.FileTypeIcons[".ogg"] = ICON_FILE_MUSIC;
		fd.FileTypeIcons[".flac"] = ICON_FILE_MUSIC;
		fd.FileTypeIcons[".aac"] = ICON_FILE_MUSIC;

		// Video files
		fd.FileTypeIcons[".mp4"] = ICON_FILE_VIDEO;
		fd.FileTypeIcons[".avi"] = ICON_FILE_VIDEO;
		fd.FileTypeIcons[".mkv"] = ICON_FILE_VIDEO;
		fd.FileTypeIcons[".mov"] = ICON_FILE_VIDEO;
		fd.FileTypeIcons[".wmv"] = ICON_FILE_VIDEO;

		// Document / text files
		fd.FileTypeIcons[".txt"] = ICON_FILE_DOCUMENT;
		fd.FileTypeIcons[".md"] = ICON_FILE_DOCUMENT;
		fd.FileTypeIcons[".log"] = ICON_FILE_DOCUMENT;
		fd.FileTypeIcons[".csv"] = ICON_FILE_DOCUMENT;
		fd.FileTypeIcons[".ini"] = ICON_FILE_DOCUMENT;
		fd.FileTypeIcons[".cfg"] = ICON_FILE_DOCUMENT;
		fd.FileTypeIcons[".json"] = ICON_FILE_DOCUMENT;
		fd.FileTypeIcons[".xml"] = ICON_FILE_DOCUMENT;
		fd.FileTypeIcons[".yaml"] = ICON_FILE_DOCUMENT;
		fd.FileTypeIcons[".yml"] = ICON_FILE_DOCUMENT;

		// Code / source files
		fd.FileTypeIcons[".c"] = ICON_FILE_CODE;
		fd.FileTypeIcons[".cpp"] = ICON_FILE_CODE;
		fd.FileTypeIcons[".h"] = ICON_FILE_CODE;
		fd.FileTypeIcons[".hpp"] = ICON_FILE_CODE;
		fd.FileTypeIcons[".cs"] = ICON_FILE_CODE;
		fd.FileTypeIcons[".java"] = ICON_FILE_CODE;
		fd.FileTypeIcons[".py"] = ICON_FILE_CODE;
		fd.FileTypeIcons[".js"] = ICON_FILE_CODE;
		fd.FileTypeIcons[".ts"] = ICON_FILE_CODE;
		fd.FileTypeIcons[".html"] = ICON_FILE_CODE;
		fd.FileTypeIcons[".css"] = ICON_FILE_CODE;
		fd.FileTypeIcons[".lua"] = ICON_FILE_CODE;
		fd.FileTypeIcons[".asm"] = ICON_FILE_CODE;
		fd.FileTypeIcons[".s"] = ICON_FILE_CODE;

		// PDF
		fd.FileTypeIcons[".pdf"] = ICON_FILE_PDF_BOX;

		// Office documents
		fd.FileTypeIcons[".doc"] = ICON_FILE_WORD;
		fd.FileTypeIcons[".docx"] = ICON_FILE_WORD;
		fd.FileTypeIcons[".xls"] = ICON_FILE_EXCEL;
		fd.FileTypeIcons[".xlsx"] = ICON_FILE_EXCEL;
		fd.FileTypeIcons[".ppt"] = ICON_FILE_POWERPOINT;
		fd.FileTypeIcons[".pptx"] = ICON_FILE_POWERPOINT;

		// Archive files
		fd.FileTypeIcons[".zip"] = ICON_ZIP_BOX;
		fd.FileTypeIcons[".rar"] = ICON_ZIP_BOX;
		fd.FileTypeIcons[".7z"] = ICON_ZIP_BOX;
		fd.FileTypeIcons[".tar"] = ICON_ZIP_BOX;
		fd.FileTypeIcons[".gz"] = ICON_ZIP_BOX;

		return true;
	}

	//
	//
	//
	void Application::Run() {
		bool done = false;
		while (!done) {
			SDL_Event event;
			while (SDL_PollEvent(&event)) {
				ImGui_ImplSDL3_ProcessEvent(&event);
				if (event.type == SDL_EVENT_QUIT)
					MainViewMenu::RequestQuit();
				if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
					MainViewMenu::RequestQuit();
			}
			//
			// Check after each event batch so a confirmed quit exits cleanly
			//
			if (MainViewMenu::ShouldQuit())
				done = true;
			// If Window is minized just sleep
			if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
				SDL_Delay(10);
				continue;
			}
			// Get a reference to the ImGuiIO structure, which we will use to communicate with ImGui
			ImGuiIO& io = ImGui::GetIO();
			// Start new frame
			ImGui_ImplSDLRenderer3_NewFrame();
			ImGui_ImplSDL3_NewFrame();
			ImGui::NewFrame();

			// Drive the version check state machine
			VersionCheckUi::Tick();
			// Perform the main view
			//
			MainView::Perform();
			// Render the update-available popup (overlays the main view)
			VersionCheckUi::RenderPopup();
			// Paint UI
			// ImGui::ShowDemoWindow();

			// Rendering
			ImGui::Render();
			SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
			// SDL_SetRenderDrawColorFloat(renderer,20,20,20,2);
			SDL_RenderClear(renderer);
			ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
			SDL_RenderPresent(renderer);
		}
	}

	//
	//
	//
	void Application::Shutdown() {
		VersionCheck::Shutdown();
		ImGui_ImplSDLRenderer3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();

		SDL_DestroyRenderer(renderer);
		SDL_DestroyWindow(window);
		SDL_Quit();
	}

}
