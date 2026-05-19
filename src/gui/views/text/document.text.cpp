// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Gui
//
// Text/code editor document -- Z80 assembly and AngelScript editing.
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#include "document.text.h"
#include "../main.view.documents.h"
#include "langs/lang.asm.z80.h"
#include "langs/lang.angelscript.h"
#include <retrodev.gui.h>
#include <app/app.h>
#include <app/app.icons.mdi.h>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <cstring>

namespace RetrodevGui {
	//
	// Singleton language instances — function-local statics to avoid static initialization order issues.
	// All TextEditor instances using the same pointer share a codelens symbol space.
	//
	static Z80AsmLanguage& Z80LangSingleton() { static Z80AsmLanguage s; return s; }
	static AngelScriptLanguage& AngelScriptLangSingleton() { static AngelScriptLanguage s; return s; }

	const ImGui::ILanguageDefinition* GetZ80AsmLanguage() { return &Z80LangSingleton(); }
	const ImGui::ILanguageDefinition* GetAngelScriptLanguage() { return &AngelScriptLangSingleton(); }
	//
	// Map file extension to the matching language definition for syntax highlighting
	//
	static const ImGui::ILanguageDefinition* GetLanguageForPath(const std::string& filepath) {
		std::string ext = std::filesystem::path(filepath).extension().string();
		if (ext == ".asm")
			return &Z80LangSingleton();
		if (ext == ".as")
			return &AngelScriptLangSingleton();
		return nullptr;
	}
	//
	// Receive "Find All" results from the text editor and push them to the Find channel
	//
	static void OnEditorFindAllResults(const std::vector<ImGui::TextEditor::FindAllResult>& occurrences, const char* searchedText, void* userData) {
		DocumentText* document = static_cast<DocumentText*>(userData);
		if (document == nullptr)
			return;
		const char* queryText = searchedText == nullptr ? "" : searchedText;
		// Use project-relative path when available; fall back to absolute path
		const std::string& logPath = document->GetProjectRelativePath().empty() ? document->GetFilePath() : document->GetProjectRelativePath();
		// Clear previous results then output the new ones to the Find channel
		AppConsole::Clear(AppConsole::Channel::Find);
		AppConsole::AddLogF(AppConsole::Channel::Find, AppConsole::LogLevel::Info, "Find all \"%s\": %zu match(es) in %s", queryText, occurrences.size(), logPath.c_str());
		for (const auto& occurrence : occurrences)
			AppConsole::AddLogF(AppConsole::Channel::Find, AppConsole::LogLevel::Info, "[Find] [%s:%d] %s", logPath.c_str(), occurrence.line + 1, occurrence.lineContent.c_str());
	}
	//
	// Converts leading indentation spaces to tabs based on a fixed tab size.
	//
	static std::string TabbifyText(const std::string& text, int tabSize) {
		if (tabSize <= 0)
			tabSize = 4;
		std::string result;
		result.reserve(text.size());
		size_t i = 0;
		while (i < text.size()) {
			size_t lineStart = i;
			while (i < text.size() && text[i] != '\n' && text[i] != '\r')
				i++;
			size_t lineEnd = i;
			size_t indentEnd = lineStart;
			int column = 0;
			while (indentEnd < lineEnd && (text[indentEnd] == ' ' || text[indentEnd] == '\t')) {
				if (text[indentEnd] == ' ')
					column++;
				else
					column += tabSize - (column % tabSize);
				indentEnd++;
			}
			result.append((size_t)((column + tabSize - 1) / tabSize), '\t');
			size_t contentEnd = lineEnd;
			while (contentEnd > indentEnd && (text[contentEnd - 1] == ' ' || text[contentEnd - 1] == '\t'))
				contentEnd--;
			result.append(text, indentEnd, contentEnd - indentEnd);
			if (i < text.size() && text[i] == '\r') {
				result.push_back(text[i++]);
				if (i < text.size() && text[i] == '\n')
					result.push_back(text[i++]);
			} else if (i < text.size() && text[i] == '\n') {
				result.push_back(text[i++]);
			}
		}
		return result;
	}
	//
	// Converts leading indentation tabs to spaces based on a fixed tab size.
	//
	static std::string UntabiffyText(const std::string& text, int tabSize) {
		if (tabSize <= 0)
			tabSize = 4;
		std::string result;
		result.reserve(text.size());
		size_t i = 0;
		while (i < text.size()) {
			size_t lineStart = i;
			while (i < text.size() && text[i] != '\n' && text[i] != '\r')
				i++;
			size_t lineEnd = i;
			size_t indentEnd = lineStart;
			int column = 0;
			while (indentEnd < lineEnd && (text[indentEnd] == ' ' || text[indentEnd] == '\t')) {
				if (text[indentEnd] == ' ') {
					result.push_back(' ');
					column++;
				} else {
					int spaces = tabSize - (column % tabSize);
					result.append((size_t)spaces, ' ');
					column += spaces;
				}
				indentEnd++;
			}
			size_t contentEnd = lineEnd;
			while (contentEnd > indentEnd && (text[contentEnd - 1] == ' ' || text[contentEnd - 1] == '\t'))
				contentEnd--;
			result.append(text, indentEnd, contentEnd - indentEnd);
			if (i < text.size() && text[i] == '\r') {
				result.push_back(text[i++]);
				if (i < text.size() && text[i] == '\n')
					result.push_back(text[i++]);
			} else if (i < text.size() && text[i] == '\n') {
				result.push_back(text[i++]);
			}
		}
		return result;
	}
	//
	// Renders the toolbar row: status text, action buttons and their popups.
	//
	void DocumentText::RenderToolbar() {
		auto cursorPos = m_editor.GetCursorPosition();
		ImGui::Text("%6d/%-6d %6d lines  | %s | %s | %s | %s", cursorPos.line + 1, cursorPos.column + 1, m_editor.GetLineCount(), "Ins", m_editor.CanUndo() ? "*" : " ",
					m_editor.GetLanguageDefinitionName(), m_name.c_str());
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 580.0f);
		if (ImGui::Button(ICON_CONTENT_SAVE "##SaveDocument"))
			SaveDocument();
		ImGui::SameLine();
		if (ImGui::Button("Edit"))
			ImGui::OpenPopup("EditorEditMenu");
		ImGui::SameLine();
		if (ImGui::Button("Format"))
			ImGui::OpenPopup("EditorFormatMenu");
		ImGui::SameLine();
		if (ImGui::Button("Options"))
			ImGui::OpenPopup("EditorOptions");
		ImGui::SameLine();
		if (ImGui::Button(ICON_MAGNIFY " Find")) {
			m_panelMode = (m_panelMode == PanelMode::Find) ? PanelMode::None : PanelMode::Find;
			m_focusSearchInput = (m_panelMode == PanelMode::Find);
			if (m_panelMode == PanelMode::None)
				m_editor.SetInputEnabled(true);
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_FIND_REPLACE " Replace")) {
			m_panelMode = (m_panelMode == PanelMode::Replace) ? PanelMode::None : PanelMode::Replace;
			m_focusSearchInput = (m_panelMode == PanelMode::Replace);
			if (m_panelMode == PanelMode::None)
				m_editor.SetInputEnabled(true);
		}
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
		if (ImGui::BeginPopup("EditorEditMenu")) {
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_editor.CanUndo()))
				m_editor.Undo();
			if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_editor.CanRedo()))
				m_editor.Redo();
			ImGui::Separator();
			if (ImGui::MenuItem("Cut", "Ctrl+X", false, !m_editor.IsReadOnlyEnabled()))
				m_editor.Cut();
			if (ImGui::MenuItem("Copy", "Ctrl+C"))
				m_editor.Copy();
			if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_editor.IsReadOnlyEnabled()))
				m_editor.Paste();
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopup("EditorFormatMenu")) {
			if (ImGui::MenuItem("Tabbify", nullptr, false, !m_editor.IsReadOnlyEnabled())) {
				std::string before = m_editor.GetText();
				std::string after = TabbifyText(before, m_editor.GetTabSize());
				if (after != before)
					m_editor.SetText(after);
			}
			if (ImGui::MenuItem("Untabiffy", nullptr, false, !m_editor.IsReadOnlyEnabled())) {
				std::string before = m_editor.GetText();
				std::string after = UntabiffyText(before, m_editor.GetTabSize());
				if (after != before)
					m_editor.SetText(after);
			}
			ImGui::EndPopup();
		}
		if (ImGui::BeginPopup("EditorOptions")) {
			bool showLineNumbers = m_editor.IsShowLineNumbersEnabled();
			if (ImGui::MenuItem("Line Numbers", nullptr, showLineNumbers))
				m_editor.SetShowLineNumbersEnabled(!showLineNumbers);
			bool showTiming = m_editor.IsShowTimingEnabled();
			if (ImGui::MenuItem("Timing", nullptr, showTiming))
				m_editor.SetShowTimingEnabled(!showTiming);
			if (m_editor.HasTimingSupport()) {
				if (ImGui::BeginMenu("Timing Type")) {
					Z80TimingType currentTiming = Z80LangSingleton().mTimingType;
					if (ImGui::MenuItem("Cycles", nullptr, currentTiming == Z80TimingType::Cycles)) {
						Z80LangSingleton().mTimingType = Z80TimingType::Cycles;
						m_editor.SetLanguageDefinition(&Z80LangSingleton());
						RefreshAllCodeLens();
					}
					if (ImGui::MenuItem("Cycles+M1", nullptr, currentTiming == Z80TimingType::CyclesM1)) {
						Z80LangSingleton().mTimingType = Z80TimingType::CyclesM1;
						m_editor.SetLanguageDefinition(&Z80LangSingleton());
						RefreshAllCodeLens();
					}
					if (ImGui::MenuItem("Instructions", nullptr, currentTiming == Z80TimingType::Instructions)) {
						Z80LangSingleton().mTimingType = Z80TimingType::Instructions;
						m_editor.SetLanguageDefinition(&Z80LangSingleton());
						RefreshAllCodeLens();
					}
					ImGui::EndMenu();
				}
			}
			bool showBytecode = m_editor.IsShowBytecodeEnabled();
			if (ImGui::MenuItem("Bytecode", nullptr, showBytecode))
				m_editor.SetShowBytecodeEnabled(!showBytecode);
			if (ImGui::BeginMenu("Palette")) {
				ImGui::TextEditor::PaletteId currentPalette = m_editor.GetPalette();
				if (ImGui::MenuItem("Dark", nullptr, currentPalette == ImGui::TextEditor::PaletteId::Dark))
					m_editor.SetPalette(ImGui::TextEditor::PaletteId::Dark);
				if (ImGui::MenuItem("Light", nullptr, currentPalette == ImGui::TextEditor::PaletteId::Light))
					m_editor.SetPalette(ImGui::TextEditor::PaletteId::Light);
				if (ImGui::MenuItem("Mariana", nullptr, currentPalette == ImGui::TextEditor::PaletteId::Mariana))
					m_editor.SetPalette(ImGui::TextEditor::PaletteId::Mariana);
				if (ImGui::MenuItem("RetroBlue", nullptr, currentPalette == ImGui::TextEditor::PaletteId::RetroBlue))
					m_editor.SetPalette(ImGui::TextEditor::PaletteId::RetroBlue);
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar(2);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		ImGui::SliderFloat("##FontScale", &m_fontScale, 0.5f, 3.0f, "%.1fx");
	}
	//
	// Renders the inline bottom search/replace panel.
	//
	void DocumentText::RenderSearchPanel() {
		if (m_panelMode == PanelMode::None)
			return;
		ImGui::Separator();
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
		if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			m_panelMode = PanelMode::None;
			m_editor.SetInputEnabled(true);
			ImGui::PopStyleVar();
			return;
		}
		// Panel icon indicating mode
		ImGui::AlignTextToFramePadding();
		if (m_panelMode == PanelMode::Find)
			ImGui::TextUnformatted(ICON_MAGNIFY);
		else
			ImGui::TextUnformatted(ICON_FIND_REPLACE);
		ImGui::SameLine();
		// Find input
		if (m_focusSearchInput) {
			ImGui::SetKeyboardFocusHere();
			m_editor.SetInputEnabled(false);
			m_focusSearchInput = false;
		}
		const int findSize = (int)std::strlen(m_searchFindBuffer);
		ImGui::SetNextItemWidth(220.0f);
		bool enterPressed = ImGui::InputText("##SearchFind", m_searchFindBuffer, sizeof(m_searchFindBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
		if (enterPressed && findSize > 0)
			m_editor.SelectNextOccurrenceOf(m_searchFindBuffer, findSize, m_searchCaseSensitive);
		ImGui::SameLine();
		// Prev / Next navigation
		if (ImGui::Button(ICON_CHEVRON_LEFT "##Prev") && findSize > 0)
			m_editor.SelectNextOccurrenceOf(m_searchFindBuffer, findSize, m_searchCaseSensitive);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Previous match");
		ImGui::SameLine();
		if (ImGui::Button(ICON_CHEVRON_RIGHT "##Next") && findSize > 0)
			m_editor.SelectNextOccurrenceOf(m_searchFindBuffer, findSize, m_searchCaseSensitive);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Next match");
		ImGui::SameLine();
		// Find All
		if (ImGui::Button(ICON_TEXT_SEARCH "##All") && findSize > 0) {
			std::vector<ImGui::TextEditor::FindAllResult> occurrences = m_editor.CollectFindAllOccurrences(m_searchFindBuffer, findSize, m_searchCaseSensitive);
			OnEditorFindAllResults(occurrences, m_searchFindBuffer, this);
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Find all");
		ImGui::SameLine();
		// Case sensitivity toggle -- highlighted when active
		const bool caseActive = m_searchCaseSensitive;
		if (caseActive)
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		if (ImGui::Button(ICON_FORMAT_LETTER_CASE "##CaseSensitive"))
			m_searchCaseSensitive = !m_searchCaseSensitive;
		if (caseActive)
			ImGui::PopStyleColor();
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip(caseActive ? "Case sensitive (on)" : "Case sensitive (off)");
		// Replace row
		if (m_panelMode == PanelMode::Replace) {
			ImGui::SameLine();
			ImGui::TextUnformatted(ICON_SWAP_HORIZONTAL);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(220.0f);
			ImGui::InputText("##SearchReplace", m_searchReplaceBuffer, sizeof(m_searchReplaceBuffer));
			ImGui::SameLine();
			if (ImGui::Button(ICON_SWAP_HORIZONTAL "##Replace") && findSize > 0) {
				if (!m_editor.SelectionEqualsText(m_searchFindBuffer, findSize, m_searchCaseSensitive) || m_editor.IsReadOnlyEnabled())
					m_editor.SelectNextOccurrenceOf(m_searchFindBuffer, findSize, m_searchCaseSensitive);
				else {
					m_editor.ReplaceSelection(m_searchReplaceBuffer);
					m_editor.SelectNextOccurrenceOf(m_searchFindBuffer, findSize, m_searchCaseSensitive);
				}
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Replace");
			ImGui::SameLine();
			if (ImGui::Button(ICON_CHECK_BOLD "##ReplaceAll") && findSize > 0 && !m_editor.IsReadOnlyEnabled()) {
				// Snapshot all occurrences before any replacement so line numbers and original content are accurate
				std::vector<ImGui::TextEditor::FindAllResult> replaceOccurrences = m_editor.CollectFindAllOccurrences(m_searchFindBuffer, findSize, m_searchCaseSensitive);
				// Replace exactly as many times as there are original occurrences.
				// A counted loop is required: if the replacement text contains the search text,
				// SelectNextOccurrenceOf would keep finding newly introduced occurrences and
				// the previous position-based cycle detection would loop forever.
				// After each ReplaceSelection the cursor sits at the end of the inserted text,
				// so introduced occurrences that begin inside the replacement are naturally skipped.
				const int replaceCount = (int)replaceOccurrences.size();
				for (int replaceIdx = 0; replaceIdx < replaceCount; replaceIdx++) {
					m_editor.SelectNextOccurrenceOf(m_searchFindBuffer, findSize, m_searchCaseSensitive);
					if (!m_editor.SelectionEqualsText(m_searchFindBuffer, findSize, m_searchCaseSensitive))
						break;
					m_editor.ReplaceSelection(m_searchReplaceBuffer);
				}
				// Log results to the Find channel -- one navigable entry per replaced occurrence,
				// showing the original line content so the user can spot unintended replacements
				const std::string& logPath = m_projectRelativePath.empty() ? m_filePath : m_projectRelativePath;
				AppConsole::Clear(AppConsole::Channel::Find);
				AppConsole::AddLogF(AppConsole::Channel::Find, AppConsole::LogLevel::Info, "Replace all \"%s\" -> \"%s\": %zu replacement(s) in %s", m_searchFindBuffer,
									m_searchReplaceBuffer, replaceOccurrences.size(), logPath.c_str());
				for (const auto& occurrence : replaceOccurrences)
					AppConsole::AddLogF(AppConsole::Channel::Find, AppConsole::LogLevel::Info, "[Replace] [%s:%d] %s", logPath.c_str(), occurrence.line + 1,
										occurrence.lineContent.c_str());
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Replace all");
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_CLOSE_CIRCLE "##ClosePanel")) {
			m_panelMode = PanelMode::None;
			m_editor.SetInputEnabled(true);
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Close");
		// Scope label -- right-aligned, cosmetic only
		const char* scopeLabel = "Scope: Whole document";
		float scopeLabelWidth = ImGui::CalcTextSize(scopeLabel).x;
		float rightEdge = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
		ImGui::SameLine(rightEdge - scopeLabelWidth - ImGui::GetStyle().ItemSpacing.x);
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled("%s", scopeLabel);
		ImGui::PopStyleVar();
		// Advance cursor to next line so editor doesn't overlap with search panel
		ImGui::NewLine();
	}
	//
	// Renders the text editor with the current font scale.
	// Returns true when the editor child window is focused.
	//
	bool DocumentText::RenderEditor() {
		ImFont* editorFont = Application::EditorFont != nullptr ? Application::EditorFont : ImGui::GetFont();
		editorFont->Scale = m_fontScale;
		ImGui::PushFont(editorFont);
		// Calculate exact remaining space for the editor, accounting for any accumulated cursor position offsets
		ImVec2 editorSize = ImGui::GetContentRegionAvail();
		bool isEditorFocused = m_editor.Render(m_name.c_str(), true, editorSize, false);
		ImGui::PopFont();
		editorFont->Scale = 1.0f;
		// Right-click is detected inside HandleMouseInputs (child window scope) where IsWindowHovered()
		// is reliable. ConsumeRightClickWord captures the word and clears the pending flag.
		if (m_editor.IsRightClickPending()) {
			m_contextMenuWord = m_editor.ConsumeRightClickWord();
			// Reject whitespace-only words before doing any symbol lookup
			bool wordIsBlank = true;
			for (size_t i = 0; i < m_contextMenuWord.size(); i++)
				if (m_contextMenuWord[i] != ' ' && m_contextMenuWord[i] != '\t') {
					wordIsBlank = false;
					break;
				}
			if (wordIsBlank)
				m_contextMenuWord.clear();
			// Resolve the symbol once here, not every frame inside BeginPopup
			m_contextMenuSymbolFile.clear();
			m_contextMenuSymbolLine = -1;
			if (!m_contextMenuWord.empty()) {
				std::string upperWord;
				for (size_t i = 0; i < m_contextMenuWord.size(); i++)
					upperWord += (char)std::toupper((unsigned char)m_contextMenuWord[i]);
				const std::vector<ImGui::TextEditor::CodeLensFileData>& files = ImGui::TextEditor::AllCodeLensFiles();
				for (size_t fi = 0; fi < files.size() && m_contextMenuSymbolLine < 0; fi++) {
					for (size_t si = 0; si < files[fi].symbols.size() && m_contextMenuSymbolLine < 0; si++) {
						const ImGui::TextEditor::CodeLensSymbolData& sym = files[fi].symbols[si];
						if (sym.symbolName.size() != upperWord.size())
							continue;
						std::string upperSym;
						for (size_t i = 0; i < sym.symbolName.size(); i++)
							upperSym += (char)std::toupper((unsigned char)sym.symbolName[i]);
						if (upperWord == upperSym) {
							m_contextMenuSymbolFile = files[fi].filePath;
							m_contextMenuSymbolLine = sym.lineNumber;
						}
					}
				}
			}
			ImGui::OpenPopup("##EditorContextMenu");
		}
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
		if (ImGui::BeginPopup("##EditorContextMenu")) {
			const bool hasSelection = m_editor.AnyCursorHasSelection();
			const bool isReadOnly = m_editor.IsReadOnlyEnabled();
			// Go to Definition -- only shown when the right-clicked word resolved to a known codelens symbol
			if (m_contextMenuSymbolLine >= 0) {
				if (ImGui::MenuItem(ICON_MAGNIFY_SCAN "  Go to Definition"))
					DocumentText::OpenAtLine(m_contextMenuSymbolFile, m_contextMenuSymbolLine);
				ImGui::Separator();
			}
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !isReadOnly && m_editor.CanUndo()))
				m_editor.Undo();
			if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !isReadOnly && m_editor.CanRedo()))
				m_editor.Redo();
			ImGui::Separator();
			if (ImGui::MenuItem("Cut", "Ctrl+X", false, hasSelection && !isReadOnly))
				m_editor.Cut();
			if (ImGui::MenuItem("Copy", "Ctrl+C", false, hasSelection))
				m_editor.Copy();
			if (ImGui::MenuItem("Paste", "Ctrl+V", false, !isReadOnly))
				m_editor.Paste();
			ImGui::Separator();
			if (ImGui::MenuItem("Select All", "Ctrl+A"))
				m_editor.SelectAll();
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar(2);
		return isEditorFocused;
	}
	//
	// Pending scroll requests: filePath -> line, applied on the target document's next Perform().
	//
	static std::unordered_map<std::string, int> s_pendingScrollLines;
	//
	// Re-enqueue all files known to the global codelens registry for background re-parsing.
	// Used to propagate a setting change to every file without tracking instances.
	//
	void DocumentText::RefreshAllCodeLens() {
		for (const auto& file : ImGui::TextEditor::AllCodeLensFiles()) {
			const ImGui::ILanguageDefinition* lang = GetLanguageForPath(file.filePath);
			if (lang != nullptr)
				ImGui::TextEditor::EnqueueCodeLensFileStatic(file.filePath, lang);
		}
	}
	//
	// Opens or activates a text document and schedules a scroll to the given line.
	//
	void DocumentText::OpenAtLine(const std::string& filePath, int line) {
		std::filesystem::path path(filePath);
		std::string name = path.filename().string();
		//
		// If the document is already open, activate it and scroll directly on the instance.
		// For newly opened documents, queue the scroll via s_pendingScrollLines so it is
		// applied once the editor has been initialised and rendered for the first time.
		//
		if (!DocumentsView::ActivateDocumentAtLine(name, filePath, line)) {
			auto doc = std::make_shared<DocumentText>(name, filePath);
			//
			// Queue the scroll keyed on the project-relative path (computed inside ScrollToLine)
			// before OpenDocument so the pending entry is ready for the first Perform() call.
			//
			doc->SetProjectRelativePath(DocumentsView::ComputeProjectRelativePath(filePath));
			doc->ScrollToLine(line);
			DocumentsView::OpenDocument(doc);
		}
	}
	DocumentText::DocumentText(const std::string& name, const std::string& filepath) : DocumentView(name, filepath) {
		m_editor.SetDocumentPath(filepath);
		m_editor.SetPalette(ImGui::TextEditor::PaletteId::RetroBlue);
		m_editor.SetLanguageDefinition(GetLanguageForPath(filepath));
		m_editor.SetShowLineNumbersEnabled(true);
		m_editor.SetTabSize(4);
		m_editor.SetReadOnlyEnabled(false);
		m_editor.SetShowTimingEnabled(true);
		m_editor.SetShowBytecodeEnabled(true);
		m_editor.SetCursorStyle(ImGui::TextEditor::CursorStyle::Block);
		m_editor.SetFindAllResultsCallback(OnEditorFindAllResults, this);
		std::ifstream file(filepath);
		if (file.good()) {
			std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			m_editor.SetText(content);
			m_originalText = content;
		}
		std::error_code ec;
		m_lastWriteTime = std::filesystem::last_write_time(filepath, ec);
	}

	DocumentText::~DocumentText() {
		m_editor.SetFindAllResultsCallback(nullptr, nullptr);
		// Cancel any pending or in-progress incremental codelens parse. If the parse had already
		// cleared the symbol table for this file, restore the data from disk so the global
		// codelens table is not left empty. This avoids calling ParseCodeLensFromText which
		// would reset the shared Z80 parser global state and corrupt any other running parse.
		m_editor.CancelCodeLensRefresh();
	}
	//
	// Saves current editor content to the document file and resets modified tracking.
	//
	bool DocumentText::Save() {
		return SaveDocument();
	}
	//
	// Scrolls the editor to the given line and places the cursor there.
	// Queues via s_pendingScrollLines so the scroll is applied at the start of the next
	// Perform() call, before RenderEditor() -- the same path used for newly opened documents.
	//
	void DocumentText::ScrollToLine(int line) {
		//
		// Key on the project-relative path when available so it matches the lookup in Perform().
		// Fall back to the absolute path for documents opened outside a project context.
		//
		const std::string& key = m_projectRelativePath.empty() ? m_filePath : m_projectRelativePath;
		s_pendingScrollLines[key] = line;
	}
	//
	// Saves current editor content to the document file and resets modified tracking.
	//
	bool DocumentText::SaveDocument() {
		const std::string content = m_editor.GetText();
		// Scope the ofstream so the file descriptor is closed and the OS commits
		// the final write timestamp before we read it below. If we read last_write_time
		// while the file is still open the OS may not have finalised the timestamp yet,
		// causing the external-change guard to fire on the very next Perform() frame.
		{
			std::ofstream file(m_filePath, std::ios::binary | std::ios::trunc);
			if (!file.good()) {
				AppConsole::AddLogF(AppConsole::LogLevel::Error, "Failed to save file: %s", m_filePath.c_str());
				return false;
			}
			file.write(content.data(), (std::streamsize)content.size());
			if (!file.good()) {
				AppConsole::AddLogF(AppConsole::LogLevel::Error, "Failed to write file: %s", m_filePath.c_str());
				return false;
			}
		}
		m_originalText = content;
		SetModified(false);
		std::error_code ec;
		m_lastWriteTime = std::filesystem::last_write_time(m_filePath, ec);
		m_savedWriteTime = m_lastWriteTime;
		m_justSaved = true;
		return true;
	}
	//
	// Render the editor filling the full available document area.
	// Modified flag is driven by comparing current text to the text at load time.
	//
	void DocumentText::Perform() {
		SetModified(m_editor.GetText() != m_originalText);
		//
		// Poll for external file modification once per frame
		//
		if (!m_externalChangeDetected) {
			std::error_code ec;
			std::filesystem::file_time_type diskTime = std::filesystem::last_write_time(m_filePath, ec);
			if (!ec && diskTime != m_lastWriteTime) {
				if (m_justSaved) {
					//
					// Delayed timestamp update from our own save -- absorb it only if the
					// disk time matches what we recorded right after writing. If the disk time
					// is newer than our saved baseline it means an external tool also wrote the
					// file after us, so fall through to the reload/dialog logic below.
					//
					if (diskTime <= m_savedWriteTime) {
						m_lastWriteTime = diskTime;
						m_justSaved = false;
					} else {
						//
						// External write happened after our save -- treat as external change
						//
						m_justSaved = false;
						m_lastWriteTime = diskTime;
						if (!IsModified()) {
							std::ifstream reloadFile(m_filePath);
							if (reloadFile.good()) {
								std::string content((std::istreambuf_iterator<char>(reloadFile)), std::istreambuf_iterator<char>());
								auto savedCursor = m_editor.GetCursorPosition();
								m_editor.SetText(content);
								m_originalText = content;
								SetModified(false);
								int lastLine = m_editor.GetLineCount() - 1;
								int restoreLine = savedCursor.line > lastLine ? lastLine : savedCursor.line;
								m_editor.SetCursorPosition(restoreLine, savedCursor.column);
							}
						} else {
							m_externalChangeDetected = true;
							ImGui::OpenPopup("File Changed Externally##ExternalChangeModal");
						}
					}
				} else {
					m_lastWriteTime = diskTime;
					if (!IsModified()) {
						//
						// No unsaved changes -- reload silently, keeping the cursor on the same line
						//
						std::ifstream reloadFile(m_filePath);
						if (reloadFile.good()) {
							std::string content((std::istreambuf_iterator<char>(reloadFile)), std::istreambuf_iterator<char>());
							auto savedCursor = m_editor.GetCursorPosition();
							m_editor.SetText(content);
							m_originalText = content;
							SetModified(false);
							int lastLine = m_editor.GetLineCount() - 1;
							int restoreLine = savedCursor.line > lastLine ? lastLine : savedCursor.line;
							m_editor.SetCursorPosition(restoreLine, savedCursor.column);
						}
					} else {
						//
						// Unsaved changes present -- ask the user
						//
						m_externalChangeDetected = true;
						ImGui::OpenPopup("File Changed Externally##ExternalChangeModal");
					}
				}
			}
		}
		//
		// External change modal -- only shown when unsaved changes conflict with a disk change
		//
		ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
		if (ImGui::BeginPopupModal("File Changed Externally##ExternalChangeModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), ICON_ALERT_CIRCLE);
			ImGui::SameLine();
			ImGui::Text("The file has been modified on disk:");
			ImGui::Spacing();
			//
			// Show project-relative path when available, otherwise show filename only
			//
			const std::string displayPath = m_projectRelativePath.empty() ? std::filesystem::path(m_filePath).filename().string() : m_projectRelativePath;
			ImGui::Indent(ImGui::GetFrameHeight());
			ImGui::TextDisabled("%s", displayPath.c_str());
			ImGui::Unindent(ImGui::GetFrameHeight());
			ImGui::Spacing();
			ImGui::Text("You have unsaved changes. Reload from disk?");
			ImGui::Spacing();
			if (ImGui::Button("Reload from disk", ImVec2(160.0f, 0.0f))) {
				std::ifstream reloadFile(m_filePath);
				if (reloadFile.good()) {
					std::string content((std::istreambuf_iterator<char>(reloadFile)), std::istreambuf_iterator<char>());
					m_editor.SetText(content);
					m_originalText = content;
					SetModified(false);
				}
				m_externalChangeDetected = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Keep my changes", ImVec2(160.0f, 0.0f))) {
				m_externalChangeDetected = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
		//
		// Apply any pending scroll request targeting this document.
		// Look up by project-relative path first (the canonical key); fall back to absolute path.
		//
		const std::string& scrollKey = m_projectRelativePath.empty() ? m_filePath : m_projectRelativePath;
		auto pendingIt = s_pendingScrollLines.find(scrollKey);
		if (pendingIt != s_pendingScrollLines.end()) {
			m_editor.SetViewAtLine(pendingIt->second, ImGui::TextEditor::SetViewAtLineMode::Centered);
			m_editor.SetCursorPosition(pendingIt->second, 0);
			s_pendingScrollLines.erase(pendingIt);
		}
		RenderToolbar();
		RenderSearchPanel();
		bool isEditorFocused = RenderEditor();
		ImGuiIO& io = ImGui::GetIO();
		bool isShortcut = (io.ConfigMacOSXBehaviors ? (io.KeySuper && !io.KeyCtrl) : (io.KeyCtrl && !io.KeySuper)) && !io.KeyAlt && !io.KeyShift;
		if (isEditorFocused && isShortcut && ImGui::IsKeyPressed(ImGuiKey_S))
			SaveDocument();
		if (isEditorFocused && isShortcut && ImGui::IsKeyPressed(ImGuiKey_F)) {
			m_panelMode = PanelMode::Find;
			m_focusSearchInput = true;
		}
		if (isEditorFocused && isShortcut && ImGui::IsKeyPressed(ImGuiKey_H)) {
			m_panelMode = PanelMode::Replace;
			m_focusSearchInput = true;
		}
	}

}
