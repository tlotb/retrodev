//----------------------------------------------------------------------------------------------------
//
//
//
//
//
//----------------------------------------------------------------------------------------------------

#pragma once

#include <cmath>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <imgui.h>

namespace ImGui {

	//
	// Multi-line source code editor widget with syntax highlighting, multi-cursor editing,
	// undo/redo, and bracket matching. Renders as a scrollable ImGui child window.
	// Based on: https://github.com/santaclose/ImGuiColorTextEdit
	// Based on: https://github/pthom/ImGuiColorTextEdit
	//
	//
	// This is a hard rewrite and modification over the original to
	// -Bugfix for sdl inputs
	// -Functions for search and replace
	// -Removed boost dependency in general (each language can use internally what they want)
	// -Added a codelens / symbol system
	// -Added extra columns for bytecode and timmings
	// -Added some intellisense over the codelens symbols
	// -Fixed bugs regarding colors among others
	// -Remove restrictions or hardcode values (like scrollbar sizes)
	// -Added a new asm.z80 language
	// -Added cursor style
	// -Added right API to interact with the symbols under the cursor
	// -Added document filename
	// -Each language in a separate file (so we can change it to include only what is required)
	//
	//
	class IMGUI_API TextEditor {
	public:
		// ------------- Exposed API ------------- //
		//
		// Constructs the editor with the default C++ language definition and the dark colour palette.
		//
		TextEditor();
		//
		// Destroys the editor and releases all internal resources.
		//
		~TextEditor();

		enum class PaletteId { Dark, Light, Mariana, RetroBlue };
		enum class LanguageDefinitionId { None, Cpp, C, Cs, Python, Lua, Json, Sql, AngelScript, Glsl, Hlsl, Z80Asm };
		enum class SetViewAtLineMode { FirstVisibleLine, Centered, LastVisibleLine };
		enum class CursorStyle { Line, Block, Underline };
		//
		// Selects which timing value to display in the gutter and use in codelens computations.
		// Applies to timing-aware languages (e.g. Z80).
		//
		enum class TimingType {
			Cycles,		 // Standard Z80 T-states
			CyclesM1,	 // Z80 T-states including M1 wait states
			Instructions // Amstrad CPC NOP-equivalent units
		};
		//
		// When enabled, all editing is disabled; cursor movement and selection remain available.
		//
		void SetReadOnlyEnabled(bool aValue);
		bool IsReadOnlyEnabled() const;
		//
		// When disabled, the editor suppresses all keyboard input and SDL input-stealing.
		// Call SetInputEnabled(false) when another UI element (e.g. search panel) should own the keyboard.
		// The editor clears this flag automatically when its area is clicked.
		//
		void SetInputEnabled(bool aValue);
		bool IsInputEnabled() const;
		//
		// When enabled, pressing Enter copies the leading whitespace of the current line to the new line.
		//
		void SetAutoIndentEnabled(bool aValue);
		bool IsAutoIndentEnabled() const;
		//
		// When enabled, space and tab characters are rendered as visible symbols.
		//
		void SetShowWhitespacesEnabled(bool aValue);
		bool IsShowWhitespacesEnabled() const;
		//
		// When enabled, a line-number gutter is displayed on the left side of the editor.
		//
		void SetShowLineNumbersEnabled(bool aValue);
		bool IsShowLineNumbersEnabled() const;
		//
		// When enabled, tabs render at single-character width instead of expanding to the next tab stop.
		//
		void SetShortTabsEnabled(bool aValue);
		bool IsShortTabsEnabled() const;
		//
		// When enabled, an instruction timing gutter is displayed (max 5 chars).
		//
		void SetShowTimingEnabled(bool aValue);
		bool IsShowTimingEnabled() const;
		//
		// When enabled, an instruction bytecode gutter is displayed (max 10 chars).
		//
		void SetShowBytecodeEnabled(bool aValue);
		bool IsShowBytecodeEnabled() const;
		void SetCursorStyle(CursorStyle aValue);
		CursorStyle GetCursorStyle() const;
		//
		// Sets the timing display mode; applies to timing-aware languages only (e.g. Z80).
		// Invalidates the timing gutter cache and schedules a codelens refresh.
		//
		void SetTimingType(TimingType aValue);
		TimingType GetTimingType() const;
		//
		// Returns true when the active language definition provides a timing callback.
		// Use this to conditionally show timing-type configuration UI.
		//
		bool HasTimingSupport() const;
		//
		// Callback type for fetching timing string for a line (returns empty string if not available)
		//
		typedef std::string (*TimingCallback)(int lineNumber, const std::string& lineText, void* userData);
		//
		// Callback type for fetching bytecode string for a line (returns empty string if not available)
		//
		typedef std::string (*BytecodeCallback)(int lineNumber, const std::string& lineText, void* userData);
		//
		// Returns the total number of lines currently in the document.
		//
		int GetLineCount() const;
		//
		// Sets the active colour palette; automatically reapplies the current ImGui style alpha.
		//
		void SetPalette(PaletteId aValue);
		PaletteId GetPalette() const;
		//
		// Sets the language used for syntax highlighting and comment toggling.
		// Pass LanguageDefinitionId::None to disable syntax highlighting entirely.
		//
		void SetLanguageDefinition(LanguageDefinitionId aValue);
		LanguageDefinitionId GetLanguageDefinition() const;
		//
		// Returns the human-readable name of the active language definition, or "None".
		//
		const char* GetLanguageDefinitionName() const;
		//
		// Sets the source path associated with this editor instance for codelens parsing.
		//
		void SetDocumentPath(const std::string& aValue);
		const std::string& GetDocumentPath() const;
		//
		// Sets the tab width in character cells; clamped to [1, 8].
		//
		void SetTabSize(int aValue);
		int GetTabSize() const;
		//
		// Sets the line height multiplier relative to the font height; clamped to [1.0, 2.0].
		//
		void SetLineSpacing(float aValue);
		float GetLineSpacing() const;
		//
		// Default palette applied to all newly constructed TextEditor instances
		//
		static void SetDefaultPalette(PaletteId aValue);
		static PaletteId GetDefaultPalette();
		//
		// Selects the entire document content.
		//
		void SelectAll();
		//
		// Selects all text on the given zero-based line index.
		//
		void SelectLine(int aLine);
		//
		// Selects the region from (aStartLine, aStartChar) to (aEndLine, aEndChar) using byte indices.
		//
		void SelectRegion(int aStartLine, int aStartChar, int aEndLine, int aEndChar);
		//
		// Moves cursor 0's selection to the next occurrence of the given text, wrapping at the document end.
		//
		void SelectNextOccurrenceOf(const char* aText, int aTextSize, bool aCaseSensitive = true);
		//
		// Adds a cursor for every occurrence of the given text in the document.
		//
		void SelectAllOccurrencesOf(const char* aText, int aTextSize, bool aCaseSensitive = true);
		//
		// Returns true if at least one cursor has a non-empty selection.
		//
		bool AnyCursorHasSelection() const;
		//
		// Returns true if every cursor has a non-empty selection.
		//
		bool AllCursorsHaveSelection() const;
		//
		// Removes all extra cursors, leaving only cursor 0.
		//
		void ClearExtraCursors();
		//
		// Collapses every cursor's selection to its end position.
		//
		void ClearSelections();
		//
		// Moves cursor 0 to the character at the given line and byte index.
		//
		void SetCursorPosition(int aLine, int aCharIndex);
		//
		// Retrieves the current line and visual column of cursor 0.
		//
		void GetCursorPosition(int& outLine, int& outColumn) const;
		//
		// Returns the index of the first line currently visible in the scroll viewport.
		//
		int GetFirstVisibleLine();
		//
		// Returns the index of the last line currently visible in the scroll viewport.
		//
		int GetLastVisibleLine();
		//
		// Requests the viewport to scroll to the given line on the next Render call.
		//
		void SetViewAtLine(int aLine, SetViewAtLineMode aMode);
		//
		// Copies selected text to the clipboard; copies the whole current line if nothing is selected.
		//
		void Copy();
		//
		// Cuts selected text to the clipboard; no-op in read-only mode.
		//
		void Cut();
		//
		// Pastes clipboard text at each cursor; distributes individual lines to cursors when counts match.
		//
		void Paste();
		//
		// Steps the undo stack back by aSteps operations.
		//
		void Undo(int aSteps = 1);
		//
		// Steps the redo stack forward by aSteps operations.
		//
		void Redo(int aSteps = 1);
		//
		// Returns true when at least one undo operation is available.
		//
		bool CanUndo() const;
		//
		// Returns true when at least one redo operation is available.
		//
		bool CanRedo() const;
		//
		// Returns the current position in the undo buffer (0 means nothing has been recorded).
		//
		int GetUndoIndex() const;
		//
		// Replaces the entire document with the given text; resets undo history and triggers re-colorization.
		//
		void SetText(const std::string& aText);
		//
		// Returns the full document contents as a single string with '\n' line endings.
		//
		std::string GetText() const;
		//
		// Replaces the document with the given per-line strings; resets undo history.
		//
		void SetTextLines(const std::vector<std::string>& aLines);
		//
		// Returns the document as a vector of line strings without newline characters.
		//
		std::vector<std::string> GetTextLines() const;
		//
		// Renders the editor as a scrollable ImGui child window.
		// Returns true when the child window has keyboard focus.
		//
		bool Render(const char* aTitle, bool aParentIsFocused = false, const ImVec2& aSize = ImVec2(), bool aBorder = false);
		//
		// Opens an ImGui window showing live editor state: cursors, lines, and the undo buffer.
		//
		void ImGuiDebugPanel(const std::string& panelName = "Debug");

		//
		// Additions to @santclose fork below
		//
		//
		// Returns the word under the given absolute screen position, using the origin captured at last Render.
		//
		std::string GetWordAtScreenPos(const ImVec2& aScreenPos) const;
		//
		// Returns the selected text for the given cursor index (-1 = current cursor).
		//
		std::string GetSelectedText(int aCursor = -1) const;
		// TextPosition and SelectionPosition are only used for the public API (private impl uses Coordinates)
		struct TextPosition {
			int line = -1;
			int column = -1;
		};
		struct SelectionPosition {
			TextPosition start;
			TextPosition end;
		};
		struct FindAllResult {
			SelectionPosition occurrence;
			int line = -1;
			std::string lineContent;
		};
		//
		// Global codelens symbol payload stored per file.
		//
		struct CodeLensSymbolData {
			std::string symbolName;
			int lineNumber = -1;
			std::string opcodes;
			std::string timings;
			std::string codelensText;
			std::string externalCode;
		};
		//
		// Global codelens file entry containing all symbols for that file.
		//
		struct CodeLensFileData {
			std::string filePath;
			LanguageDefinitionId language = LanguageDefinitionId::None;
			std::vector<CodeLensSymbolData> symbols;
			std::vector<std::pair<int, std::string>> errors;
		};
		//
		// A single entry in the global codelens parse queue.
		// When lines is empty the file is read from disk when parsing begins.
		//
		struct CodeLensParseTask {
			std::string filePath;
			LanguageDefinitionId language;
			std::vector<std::string> lines;
		};
		typedef void (*FindAllResultsCallback)(const std::vector<FindAllResult>& results, const char* searchedText, void* userData);
		//
		// Sets the selection of cursor 0 from the given public SelectionPosition.
		//
		void SetSelectionPosition(const SelectionPosition& pos);
		//
		// Sets callback invoked by "Find All" with all found ranges and the searched text.
		//
		void SetFindAllResultsCallback(FindAllResultsCallback aValue, void* aUserData = nullptr);
		//
		// Returns the selection bounds for the given cursor (-1 = current cursor) as a public SelectionPosition.
		//
		SelectionPosition GetSelectionPosition(int aCursor = -1) const;
		//
		// Returns the position of cursor 0 as a public TextPosition (line, visual column).
		//
		TextPosition GetCursorPosition() const;
		//
		// Finds all matches of aText and returns line/result details for each match.
		//
		std::vector<FindAllResult> CollectFindAllOccurrences(const char* aText, int aTextSize, bool aCaseSensitive) const;
		//
		// Returns true if aCursor currently selects exactly aText (respecting case sensitivity).
		//
		bool SelectionEqualsText(const char* aText, int aTextSize, bool aCaseSensitive, int aCursor = -1) const;
		//
		// Replaces the current cursor's selection with aReplaceWith, recorded as a single undo operation.
		//
		void ReplaceSelection(const char* aReplaceWith);
		//
		// Returns the word that was under the cursor when the last right-click occurred, then clears it.
		// Returns an empty string if no right-click has occurred since the last call.
		//
		std::string ConsumeRightClickWord();
		//
		// Returns true when a right-click inside the editor was detected and not yet consumed.
		//
		bool IsRightClickPending() const;
		//
		// Returns the word token under the given screen position, or an empty string if none.
		//
		std::string GetWordAt(const ImVec2& aScreenPos) const;
		//
		// Clears all global codelens data across files.
		//
		static void ClearCodeLensData();
		//
		// Adds a file to the global codelens file list if missing and returns its index.
		//
		static int AddCodeLensFile(const std::string& aFilePath);
		//
		// Sets the language on an existing codelens file entry (for synthetic entries not created via the parse pipeline).
		//
		static void SetCodeLensFileLanguage(const std::string& aFilePath, LanguageDefinitionId aLanguage);
		//
		// Returns the global codelens file list.
		//
		static const std::vector<CodeLensFileData>& GetCodeLensFiles();
		//
		// Adds or updates a codelens symbol entry for a file.
		//
		static int AddOrUpdateCodeLensSymbol(const std::string& aFilePath, const CodeLensSymbolData& aSymbolData);
		//
		// Adds a codelens symbol only if no symbol with the same name already exists (any file).
		// Used for function labels which must never overwrite a macro or definition.
		//
		static bool AddCodeLensSymbolIfNew(const std::string& aFilePath, const CodeLensSymbolData& aSymbolData);
		//
		// Deletes codelens symbols from a file by symbol name (trailing ':' is ignored).
		//
		static int DeleteCodeLensSymbol(const std::string& aFilePath, const std::string& aSymbolName);
		//
		// Adds a codelens parse error for a file and line.
		//
		static int AddCodeLensError(const std::string& aFilePath, int aLineNumber, const std::string& aMessage);
		//
		// Parses an in-memory text buffer and populates codelens symbols for a file.
		//
		static bool ParseCodeLensFromText(const std::string& aFilePath, const std::string& aText, LanguageDefinitionId aLanguage);
		//
		// Loads a file from disk, parses its text and populates codelens symbols.
		//
		static bool ParseCodeLensFromFile(const std::string& aFilePath, LanguageDefinitionId aLanguage);
		//
		// Enqueues a high-priority re-parse of the current in-memory document.
		//
		void RefreshCodeLensForCurrentDocument();
		//
		// Cancels any pending or in-progress codelens parse for this editor's file.
		// If symbols were already cleared, enqueues a low-priority disk restore.
		//
		void CancelCodeLensRefresh();
		//
		// Enqueues a file for background codelens parsing.
		// When aLines is non-empty the snapshot is used directly; otherwise the file is read from disk.
		// aHighPriority=true inserts the task at the front of the queue (editor-edit path).
		// Any existing queue entry for the file is replaced, and an active parse for it is cancelled.
		//
		static void EnqueueCodeLensFile(const std::string& aFilePath, LanguageDefinitionId aLanguage, const std::vector<std::string>& aLines = {}, bool aHighPriority = false);
		//
		// Processes up to aMaxLines lines from the global parse queue.
		// Should be called once per frame from the application main loop, independent of any editor.
		// Returns true while work remains (queued or in-progress).
		//
		static bool TickGlobalCodeLensParse(int aMaxLines = 200);
		//
		// Returns true when there is any pending or in-progress work in the global parse queue.
		//
		static bool IsCodeLensParsingPending();

	private:
		// -----------------------------------------------------------------------
		// Internal palette slot index (maps to entries in the Palette array)
		// -----------------------------------------------------------------------
		enum class PaletteIndex {
			Default,
			Keyword,
			Number,
			String,
			CharLiteral,
			Punctuation,
			Preprocessor,
			Identifier,
			KnownIdentifier,
			PreprocIdentifier,
			Comment,
			MultiLineComment,
			Background,
			Cursor,
			Selection,
			ErrorMarker,
			ControlCharacter,
			Breakpoint,
			LineNumber,
			CurrentLineFill,
			CurrentLineFillInactive,
			CurrentLineEdge,
			Max
		};

		// -----------------------------------------------------------------------
		// Character coordinate in screen space: (line index, visual column).
		// Tabs expand to the next tab stop; column is visual, not a byte offset.
		// -----------------------------------------------------------------------
		struct Coordinates {
			int mLine, mColumn; // Zero-based line index and visual column (tabs expand to tab stops).
			Coordinates();
			Coordinates(int aLine, int aColumn);
			static Coordinates Invalid(); // Returns a sentinel value with negative line and column.
			bool operator==(const Coordinates& o) const;
			bool operator!=(const Coordinates& o) const;
			bool operator<(const Coordinates& o) const;
			bool operator>(const Coordinates& o) const;
			bool operator<=(const Coordinates& o) const;
			bool operator>=(const Coordinates& o) const;
			Coordinates operator-(const Coordinates& o);
			Coordinates operator+(const Coordinates& o);
		};

		// -----------------------------------------------------------------------
		// One cursor: start/end span defines the selection. When equal, no selection.
		// -----------------------------------------------------------------------
		struct Cursor {
			Coordinates mInteractiveStart = {0, 0}; // The anchor end of the selection (fixed while shift-extending).
			Coordinates mInteractiveEnd = {0, 0};	// The active end; always equals the visual cursor position.
			Coordinates GetSelectionStart() const;	// Returns min(mInteractiveStart, mInteractiveEnd).
			Coordinates GetSelectionEnd() const;	// Returns max(mInteractiveStart, mInteractiveEnd).
			bool HasSelection() const;				// Returns true when start != end.
		};

		// -----------------------------------------------------------------------
		// Multi-cursor state snapshot, saved and restored by each undo/redo record
		// -----------------------------------------------------------------------
		struct EditorState {
			int mCurrentCursor = 0;					   // Index of the active cursor (0-based; highest index is the last added).
			int mLastAddedCursor = 0;				   // Index of the most recently added cursor (used by occurrence search).
			std::vector<Cursor> mCursors = {{{0, 0}}}; // All active cursors; indices 0..mCurrentCursor are valid.
			void AddCursor();
			int GetLastAddedCursorIndex();
			void SortCursorsFromTopToBottom();
		};

		// -----------------------------------------------------------------------
		// Named identifier entry used for known-identifier and preproc-identifier highlighting
		// -----------------------------------------------------------------------
		struct Identifier {
			Coordinates mLocation;	  // Source location where the identifier was declared.
			std::string mDeclaration; // Human-readable declaration string shown in tooltips.
		};

		typedef std::unordered_map<std::string, Identifier> Identifiers; // Lookup table: name -> Identifier entry.
		typedef std::array<ImU32, (unsigned)PaletteIndex::Max> Palette;	 // Packed ABGR color array indexed by PaletteIndex.

		// -----------------------------------------------------------------------
		// A single rendered character cell carrying the character and its color index
		// -----------------------------------------------------------------------
		struct Glyph {
			char mChar;										  // The character value (single byte in a UTF-8 sequence).
			PaletteIndex mColorIndex = PaletteIndex::Default; // Syntax-highlight color assigned by the tokenizer.
			bool mComment : 1;								  // Set when this glyph falls inside a single-line comment.
			bool mMultiLineComment : 1;						  // Set when this glyph falls inside a block comment.
			bool mPreprocessor : 1;							  // Set when this glyph falls inside a preprocessor directive.
			Glyph(char aChar, PaletteIndex aColorIndex);
		};

		typedef std::vector<Glyph> Line;

		// -----------------------------------------------------------------------
		// Language syntax definition: keyword set, tokenizer callback, comment markers
		// -----------------------------------------------------------------------
		struct LanguageDefinition {
			// Given the range [in_begin, in_end), writes the next recognized token to out_begin/out_end
			// and sets paletteIndex; returns true when a token was found.
			typedef bool (*TokenizeCallback)(const char* in_begin, const char* in_end, const char*& out_begin, const char*& out_end, PaletteIndex& paletteIndex);
			typedef void (*CodeLensParseStartCallback)(const std::string& filePath, void* userData);
			// Called once per source line during ParseCodeLensFromText.
			typedef void (*CodeLensLineParserCallback)(int lineNumber, const std::string& filePath, const std::string& lineText);
			typedef void (*CodeLensParseEndCallback)(const std::string& filePath);

			std::string mName;											// Human-readable language name returned by GetLanguageDefinitionName().
			std::unordered_set<std::string> mKeywords;					// Reserved keywords highlighted with PaletteIndex::Keyword.
			Identifiers mIdentifiers;									// Known type/function names highlighted with KnownIdentifier.
			Identifiers mPreprocIdentifiers;							// Preprocessor symbols highlighted with PreprocIdentifier.
			std::string mCommentStart, mCommentEnd, mSingleLineComment; // Block comment delimiters and line comment prefix.
			char mPreprocChar = '#';									// Character that begins a preprocessor directive.
			TokenizeCallback mTokenize = nullptr;						// Optional custom tokenizer; falls back to identifier scan when null.
			CodeLensParseStartCallback mCodeLensParseStart = nullptr;	// Optional codelens parser start hook called before line iteration.
			CodeLensLineParserCallback mCodeLensLineParser = nullptr;	// Optional per-line codelens parser used by ParseCodeLensFromText.
			CodeLensParseEndCallback mCodeLensParseEnd = nullptr;		// Optional codelens parser end hook called after line iteration.
			bool mCaseSensitive = true;									// When false, keywords are matched case-insensitively.
			TimingCallback mTimingCallback = nullptr;					// Optional callback for instruction timing information.
			BytecodeCallback mBytecodeCallback = nullptr;				// Optional callback for instruction bytecode information.
			mutable TimingType mTimingType = TimingType::Cycles;		// Active timing mode; mutable so it can be changed through the shared const singleton.

			static const LanguageDefinition& Cpp();
			static const LanguageDefinition& Hlsl();
			static const LanguageDefinition& Glsl();
			static const LanguageDefinition& Python();
			static const LanguageDefinition& C();
			static const LanguageDefinition& Sql();
			static const LanguageDefinition& AngelScript();
			static const LanguageDefinition& Lua();
			static const LanguageDefinition& Cs();
			static const LanguageDefinition& Json();
			static const LanguageDefinition& Z80Asm();
			static bool TokenizeZ80Asm(const char* in_begin, const char* in_end, const char*& out_begin, const char*& out_end, PaletteIndex& paletteIndex);
		};

		// -----------------------------------------------------------------------
		// A single atomic text operation (add or delete) stored inside an UndoRecord
		// -----------------------------------------------------------------------
		enum class UndoOperationType { Add, Delete };
		struct UndoOperation {
			std::string mText;				// The text that was added or deleted.
			TextEditor::Coordinates mStart; // Start of the affected region in document coordinates.
			TextEditor::Coordinates mEnd;	// End of the affected region in document coordinates.
			UndoOperationType mType;		// Whether this operation added or deleted text.
		};
		// -----------------------------------------------------------------------
		// A reversible edit: a sequence of atomic operations plus before/after editor states
		// -----------------------------------------------------------------------
		class UndoRecord {
		public:
			UndoRecord() = default;
			~UndoRecord() = default;
			//
			// Constructs the record from a list of operations and the editor state snapshots on either side.
			//
			UndoRecord(const std::vector<UndoOperation>& aOperations, TextEditor::EditorState& aBefore, TextEditor::EditorState& aAfter);
			//
			// Replays all operations in reverse order to undo the edit, then restores mBefore cursor state.
			//
			void Undo(TextEditor* aEditor);
			//
			// Replays all operations in forward order to redo the edit, then restores mAfter cursor state.
			//
			void Redo(TextEditor* aEditor);

			std::vector<UndoOperation> mOperations; // Ordered list of atomic adds/deletes that compose this edit.
			EditorState mBefore;					// Editor state snapshot captured before the edit was applied.
			EditorState mAfter;						// Editor state snapshot captured after the edit was applied.
		};

		//
		// Returns the text between two document coordinates as a string with '\n' line endings.
		//
		std::string GetText(const Coordinates& aStart, const Coordinates& aEnd) const;
		//
		// Builds the clipboard string from all cursors that have a selection, joined by '\n'.
		//
		std::string GetClipboardText() const;
		//
		// Internal overload: moves the given cursor to aPosition; optionally collapses its selection.
		//
		void SetCursorPosition(const Coordinates& aPosition, int aCursor = -1, bool aClearSelection = true);
		//
		// Inserts aValue at aWhere, splitting lines on '\n' and skipping '\r'; returns new-line count added.
		//
		int InsertTextAt(Coordinates& aWhere, const char* aValue);
		//
		// Deletes any selection for the given cursor then inserts aValue at the cursor position.
		//
		void InsertTextAtCursor(const char* aValue, int aCursor = -1);
		// -----------------------------------------------------------------------
		// Primitive movement helpers: operate on raw line/character indices
		// -----------------------------------------------------------------------
		enum class MoveDirection { Right = 0, Left = 1, Up = 2, Down = 3 };
		//
		// Advances or retreats aCharIndex by one UTF-8 codepoint on aLine.
		// Returns false when motion would cross a line boundary (or when aLockLine is true).
		//
		bool Move(int& aLine, int& aCharIndex, bool aLeft = false, bool aLockLine = false) const;
		//
		// Advances aCharIndex and aColumn by one codepoint, expanding tabs to the next tab stop.
		//
		void MoveCharIndexAndColumn(int aLine, int& aCharIndex, int& aColumn) const;
		//
		// Moves aCoords one step in aDirection; aWordMode jumps over whole words, aLineCount is used by Up/Down.
		//
		void MoveCoords(Coordinates& aCoords, MoveDirection aDirection, bool aWordMode = false, int aLineCount = 1) const;

		//
		// Moves all cursors up/down by aAmount lines; aSelect extends the selection anchor.
		//
		void MoveUp(int aAmount = 1, bool aSelect = false);
		void MoveDown(int aAmount = 1, bool aSelect = false);
		//
		// Moves all cursors one position left/right; when a selection exists without aSelect it collapses to its edge.
		//
		void MoveLeft(bool aSelect = false, bool aWordMode = false);
		void MoveRight(bool aSelect = false, bool aWordMode = false);
		//
		// Moves the current cursor to the very first / last position in the document.
		//
		void MoveTop(bool aSelect = false);
		void MoveBottom(bool aSelect = false);
		//
		// Moves all cursors to column 0 / end-of-line of their respective lines.
		//
		void MoveHome(bool aSelect = false);
		void MoveEnd(bool aSelect = false);
		//
		// Inserts aChar at every cursor; handles auto-indent for '\n' and tab-indent for multi-line selections.
		//
		void EnterCharacter(ImWchar aChar, bool aShift);
		//
		// Deletes one codepoint (or one word when aWordMode is true) to the left of each cursor.
		//
		void Backspace(bool aWordMode = false);
		//
		// Deletes the selection or one codepoint/word to the right of each cursor; aEditorState overrides undo snapshot.
		//
		void Delete(bool aWordMode = false, const EditorState* aEditorState = nullptr);

		//
		// Sets the given cursor's selection to [aStart, aEnd], clamped to document bounds.
		//
		void SetSelection(Coordinates aStart, Coordinates aEnd, int aCursor = -1);
		//
		// Overload that accepts raw line/char byte indices; converts them to Coordinates internally.
		//
		void SetSelection(int aStartLine, int aStartChar, int aEndLine, int aEndChar, int aCursor = -1);
		//
		// Returns the normalized (min/max) selection start and end for the given cursor.
		//
		Coordinates GetSelectionStart(int aCursor = -1) const;
		Coordinates GetSelectionEnd(int aCursor = -1) const;

		//
		// Internal overload: finds and selects the next occurrence of aText from the given cursor's position.
		//
		void SelectNextOccurrenceOf(const char* aText, int aTextSize, int aCursor = -1, bool aCaseSensitive = true);
		//
		// Adds a new cursor at the next occurrence of the last-added cursor's selected text.
		//
		void AddCursorForNextOccurrence(bool aCaseSensitive = true);
		//
		// Searches forward from aFrom for aText, wrapping at end-of-document; returns true on match.
		//
		bool FindNextOccurrence(const char* aText, int aTextSize, const Coordinates& aFrom, Coordinates& outStart, Coordinates& outEnd, bool aCaseSensitive = true);
		//
		// Walks the glyph stream to find the bracket matching the one at (aLine, aCharIndex); returns false if none.
		//
		bool FindMatchingBracket(int aLine, int aCharIndex, Coordinates& out);
		//
		// Adds or removes one tab stop of indentation on every line covered by any cursor selection.
		//
		void ChangeCurrentLinesIndentation(bool aIncrease);
		//
		// Swaps each selected line set with the line above; no-op when the topmost affected line is already at row 0.
		//
		void MoveUpCurrentLines();
		//
		// Swaps each selected line set with the line below; no-op when the bottommost line is already the last.
		//
		void MoveDownCurrentLines();
		//
		// Prepends the language's single-line comment token to selected lines, or removes it if all lines are already commented.
		//
		void ToggleLineComment();
		//
		// Deletes every line that contains a cursor or selection, then repositions cursors to the nearest valid line.
		//
		void RemoveCurrentLines();

		//
		// Returns the pixel X offset from the line's left edge to the start of aFrom's column.
		//
		float TextDistanceToLineStart(const Coordinates& aFrom, bool aSanitizeCoords = true) const;
		//
		// Schedules the viewport to scroll so the given cursor is visible on the next Render call.
		//
		void EnsureCursorVisible(int aCursor = -1, bool aStartToo = false);
		//
		// Clamps coordinates to document bounds and snaps a column inside a tab cell to its nearest edge.
		//
		Coordinates SanitizeCoordinates(const Coordinates& aValue) const;
		//
		// Returns sanitized coordinates for the given cursor's interactive end (or start when aStart is true).
		//
		Coordinates GetSanitizedCursorCoordinates(int aCursor = -1, bool aStart = false) const;
		//
		// Converts an ImGui screen position to document Coordinates using ImGui::GetCursorScreenPos() as origin.
		//
		Coordinates ScreenPosToCoordinates(const ImVec2& aPosition, bool* isOverLineNumber = nullptr) const;
		//
		// Scans leftward from aFrom to find the start boundary of the word/token under the cursor.
		//
		Coordinates FindWordStart(const Coordinates& aFrom) const;
		//
		// Scans rightward from aFrom to find the end boundary of the word/token under the cursor.
		//
		Coordinates FindWordEnd(const Coordinates& aFrom) const;
		//
		// Returns the byte index of the glyph whose visual column range contains aCoordinates.mColumn (left edge).
		//
		int GetCharacterIndexL(const Coordinates& aCoordinates) const;
		//
		// Returns the byte index past the glyph at aCoordinates.mColumn (right edge, used as insertion point).
		//
		int GetCharacterIndexR(const Coordinates& aCoordinates) const;
		//
		// Converts a byte index on aLine to its visual column, expanding tab characters.
		//
		int GetCharacterColumn(int aLine, int aIndex) const;
		//
		// Returns the byte index of the first glyph at or past the first visible column (for horizontal scroll).
		//
		int GetFirstVisibleCharacterIndex(int aLine) const;
		//
		// Returns the visual column of the last glyph on aLine; aLimit caps the scan early if provided.
		//
		int GetLineMaxColumn(int aLine, int aLimit = -1) const;

		//
		// Inserts an empty line at aIndex and shifts all cursors below it down by one.
		//
		Line& InsertLine(int aIndex);
		//
		// Removes the line at aIndex and shifts cursors down; skips cursors listed in aHandledCursors.
		//
		void RemoveLine(int aIndex, const std::unordered_set<int>* aHandledCursors = nullptr);
		//
		// Removes lines [aStart, aEnd) and adjusts all cursor positions accordingly.
		//
		void RemoveLines(int aStart, int aEnd);
		//
		// Erases all glyphs in [aStart, aEnd), joining lines together when the range spans a line break.
		//
		void DeleteRange(const Coordinates& aStart, const Coordinates& aEnd);
		//
		// Deletes the selection of the given cursor and moves it to the selection start.
		//
		void DeleteSelection(int aCursor = -1);
		//
		// Erases glyphs [aStartChar, aEndChar) from aLine (-1 = to end of line), firing change notifications.
		//
		void RemoveGlyphsFromLine(int aLine, int aStartChar, int aEndChar = -1);
		//
		// Inserts a range of glyphs at aTargetIndex on aLine, firing change notifications.
		//
		void AddGlyphsToLine(int aLine, int aTargetIndex, Line::iterator aSourceStart, Line::iterator aSourceEnd);
		//
		// Inserts a single glyph at aTargetIndex on aLine, firing change notifications.
		//
		void AddGlyphToLine(int aLine, int aTargetIndex, Glyph aGlyph);
		//
		// Returns the palette color for aGlyph, blending the preprocessor tint when the glyph is inside a directive.
		//
		ImU32 GetGlyphColor(const Glyph& aGlyph) const;

		//
		// Processes keyboard shortcuts and printable character input when the child window is focused.
		//
		void HandleKeyboardInputs(bool aParentIsFocused = false);
		//
		// Handles mouse clicks, double/triple-clicks, drag-selection, and middle-button panning.
		//
		void HandleMouseInputs();
		//
		// Recomputes all visible-line and visible-column bounds from the current scroll offsets and window size.
		//
		void UpdateViewVariables(float aScrollX, float aScrollY);
		//
		// Internal rendering pass: draws selections, cursors, colorized glyphs, line numbers, and handles deferred scroll.
		//
		void Render(bool aParentIsFocused = false);
		//
		// Called when any cursor moves: updates bracket highlighting, sorts cursors, and merges overlapping ones.
		//
		void OnCursorPositionChanged();
		//
		// Adjusts other cursors on the same line when a glyph is inserted or deleted by a different cursor.
		//
		void OnLineChanged(bool aBeforeChange, int aLine, int aColumn, int aCharCount, bool aDeleted);
		//
		// Removes cursors that overlap or share the same position; requires cursors sorted top-to-bottom.
		//
		void MergeCursorsIfPossible();
		//
		// Appends aValue to the undo buffer, truncating any redo history that follows the current index.
		//
		void AddUndo(UndoRecord& aValue);
		//
		// Signals that the document was modified and codelens needs to be re-parsed.
		// The actual parse is deferred and executed incrementally inside Render().
		//
		void ScheduleCodeLensRefresh();
		//
		// Advances the incremental codelens parse by one batch of lines. Called every frame from Render().
		//
		void TickIncrementalCodeLensParse();
		//
		// Marks lines [aFromLine, aFromLine+aCount) as dirty, scheduling them for re-colorization.
		//
		void Colorize(int aFromLine = 0, int aCount = -1);
		//
		// Applies the language tokenizer to each line in [aFromLine, aToLine) to assign palette color indices.
		//
		void ColorizeRange(int aFromLine = 0, int aToLine = 0);
		//
		// Runs the full comment/preprocessor scan when needed, then colorizes a batch of dirty lines incrementally.
		//
		void ColorizeInternal();

		// -----------------------------------------------------------------------
		// Document state
		// -----------------------------------------------------------------------
		std::vector<Line> mLines;			 // Document storage: one Line (vector<Glyph>) per text line.
		EditorState mState;					 // Current multi-cursor state (positions and selections).
		std::vector<UndoRecord> mUndoBuffer; // Undo/redo history; entries past mUndoIndex are redo records.
		int mUndoIndex = 0;					 // Points to the next free slot in mUndoBuffer.
		// -----------------------------------------------------------------------
		// Editor settings
		// -----------------------------------------------------------------------
		int mTabSize = 4;										  // Visual width of a tab stop in columns; clamped to [1, 8].
		float mLineSpacing = 1.0f;								  // Line height multiplier applied to the font height.
		bool mReadOnly = false;									  // When true, all text mutations are blocked.
		bool mInputEnabled = true;								  // When false, keyboard input and SDL input-stealing are suppressed.
		bool mAutoIndent = true;								  // When true, Enter copies leading whitespace to the new line.
		bool mShowWhitespaces = true;							  // When true, spaces and tabs are rendered as visible symbols.
		bool mShowLineNumbers = true;							  // When true, a line-number gutter is shown on the left.
		bool mShortTabs = false;								  // When true, tabs render at single-character width.
		bool mShowTiming = false;								  // When true, an instruction timing gutter is shown.
		bool mShowBytecode = false;								  // When true, an instruction bytecode gutter is shown.
		CursorStyle mCursorStyle = CursorStyle::Line;			  // Visual cursor style used when rendering focused cursors.
		FindAllResultsCallback mFindAllResultsCallback = nullptr; // Optional callback invoked by Find All.
		void* mFindAllResultsUserData = nullptr;				  // User data forwarded to mFindAllResultsCallback.
		TimingCallback mTimingCallback = nullptr;				  // Callback to fetch timing string for a line.
		BytecodeCallback mBytecodeCallback = nullptr;			  // Callback to fetch bytecode string for a line.
		std::vector<std::string> mTimingTextCache;				  // Per-line cached timing text.
		std::vector<unsigned char> mTimingTextCacheValid;		  // Per-line timing cache validity flags.
		std::vector<std::string> mBytecodeTextCache;			  // Per-line cached bytecode text.
		std::vector<unsigned char> mBytecodeTextCacheValid;		  // Per-line bytecode cache validity flags.
		//
		// Cached total codelens lane height for all document lines (sum of codeLensLaneHeight
		// for every line that has a codelens annotation). Used to correctly size the
		// virtual scroll area and to compute cursor-scroll targets.
		//
		mutable float mTotalCodeLensHeight = 0.0f;    // Sum of codelens lane heights across all lines.
		mutable int mTotalCodeLensVersion = -1;       // sCodeLensDataVersion at the time of last recompute.
		mutable bool mTotalCodeLensDirty = true;      // Set when document content or path changes.
		// -----------------------------------------------------------------------
		// Deferred scroll and cursor-visibility requests applied on the next Render call
		// -----------------------------------------------------------------------
		int mSetViewAtLine = -1;				   // Target line for a programmatic scroll, or -1 if inactive.
		SetViewAtLineMode mSetViewAtLineMode;	   // How the target line should be positioned in the viewport.
		int mEnsureCursorVisible = -1;			   // Index of the cursor to scroll into view, or -1 if inactive.
		bool mEnsureCursorVisibleStartToo = false; // When true, also ensure the selection start is visible.
		bool mScrollToTop = false;				   // When true, jump to the very top on the next Render call.
		// -----------------------------------------------------------------------
		// Per-frame rendering metrics, recomputed inside Render()
		// -----------------------------------------------------------------------
		float mTextStart = 20.0f;		   // Pixel X offset where code text begins (accounts for gutter width).
		int mLeftMargin = 10;			   // Fixed left margin in pixels before the line-number gutter.
		ImVec2 mCharAdvance;			   // Width/height of one character cell in pixels.
		float mCurrentSpaceHeight = 20.0f; // Total pixel height of the virtual scroll area.
		float mCurrentSpaceWidth = 20.0f;  // Total pixel width of the virtual scroll area.
		float mLastClickTime = -1.0f;	   // ImGui time of the previous left-button click (triple-click detection).
		ImVec2 mLastClickPos;			   // Screen position of the previous left-button click.
		// -----------------------------------------------------------------------
		// Viewport bounds, updated by UpdateViewVariables()
		// -----------------------------------------------------------------------
		int mFirstVisibleLine = 0;	 // Index of the first line currently visible in the scroll viewport.
		int mLastVisibleLine = 0;	 // Index of the last line currently visible in the scroll viewport.
		int mVisibleLineCount = 0;	 // Number of lines that fit in the viewport.
		float mFirstVisibleLineYOffset = 0.0f; // Accumulated codelens lane height above mFirstVisibleLine (for render loop init).
		//
		// Per-line cumulative Y cache: mLineTopYCache[i] = document-space Y of the top of line i.
		// mLineTopYCache[lineCount] = total document height. Rebuilt only when dirty.
		//
		std::vector<float> mLineTopYCache;
		int mLineTopYCacheVersion = -1;       // sCodeLensDataVersion at last rebuild (-1 = never built).
		int mLineTopYCacheLineCount = 0;      // mLines.size() at last rebuild.
		float mLineTopYCacheCharAdvanceY = 0.0f; // mCharAdvance.y at last rebuild; invalidates on font-size change.
		int mFirstVisibleColumn = 0; // Visual column of the leftmost visible character.
		int mLastVisibleColumn = 0;	 // Visual column of the rightmost visible character.
		int mVisibleColumnCount = 0; // Number of columns that fit in the viewport.
		float mContentWidth = 0.0f;	 // Visible content area width in pixels (window minus scrollbar).
		float mContentHeight = 0.0f; // Visible content area height in pixels (window minus scrollbar).
		float mScrollX = 0.0f;		 // Current horizontal scroll offset in pixels.
		float mScrollY = 0.0f;		 // Current vertical scroll offset in pixels.
		//
		// Screen-space origin of the child window captured during Render,
		// used for coordinate conversion outside the child window scope
		//
		ImVec2 mLastRenderOrigin = ImVec2(0.0f, 0.0f);
		//
		// Word token resolved from the hovered mouse position, updated every frame inside HandleMouseInputs.
		// Valid to read from outside the child window scope (e.g. context menus).
		//
		std::string mLastHoveredWord;
		//
		// Word under the cursor at the moment of the last right-click, set inside HandleMouseInputs.
		// Read and clear with ConsumeRightClickWord() after Render() returns.
		//
		std::string mRightClickWord;
		//
		// Set to true when a right-click is detected inside the child window. Cleared by ConsumeRightClickWord().
		//
		bool mRightClickPending = false;
		// -----------------------------------------------------------------------
		// Mouse input state
		// -----------------------------------------------------------------------
		bool mPanning = false;			 // True while the middle mouse button is held for panning.
		bool mDraggingSelection = false; // True while the left mouse button extends a drag-selection.
		ImVec2 mLastMousePos;			 // Mouse drag delta captured when middle-button panning began.
		// -----------------------------------------------------------------------
		// Cursor and bracket-match state
		// -----------------------------------------------------------------------
		bool mCursorPositionChanged = false; // Set when a cursor moves; triggers OnCursorPositionChanged on next frame.
		bool mCursorOnBracket = false;		 // True when cursor 0 is adjacent to a matchable bracket.
		Coordinates mMatchingBracketCoords;	 // Document coordinates of the bracket matching the one under cursor 0.
		// -----------------------------------------------------------------------
		// Colorization state
		// -----------------------------------------------------------------------
		int mColorRangeMin = 0;									 // Start of the pending dirty line range.
		int mColorRangeMax = 0;									 // End of the pending dirty line range.
		bool mCheckComments = true;								 // When true, rerun the full comment/preprocessor scan before tokenizing.
		PaletteId mPaletteId;									 // Currently active palette identifier.
		Palette mPalette;										 // Resolved ABGR color values for every PaletteIndex slot.
		LanguageDefinitionId mLanguageDefinitionId;				 // Active language definition identifier.
		const LanguageDefinition* mLanguageDefinition = nullptr; // Pointer to the active language definition, or nullptr for plain text.
		std::string mDocumentPath;								 // Source path bound to this editor instance for codelens ownership.
		// -----------------------------------------------------------------------
		// Incremental codelens parse state (per-editor; actual parsing runs in the global queue)
		// -----------------------------------------------------------------------
		bool mCodeLensPendingRefresh = false; // True when the document was modified and a re-parse is needed.
		double mCodeLensLastEditTime = -1.0;  // ImGui time of the last document mutation.
		// -----------------------------------------------------------------------
		// Symbol-table autocomplete state
		// -----------------------------------------------------------------------
		bool mAutocompleteActive = false;				   // True when the suggestion popup is currently visible.
		bool mAutocompleteEnabled = true;				   // True when autocomplete is allowed to trigger (reset to true on cursor move/edit).
		std::vector<std::string> mAutocompleteSuggestions; // Current symbol-name completions.
		ImVec2 mAutocompletePopupPos;					   // Screen-space position for the popup window.
		std::string mAutocompleteCurrentWord;			   // The partial word that seeded the current suggestions.

		//
		// Returns true when the total content width exceeds the visible area, making the horizontal scrollbar visible.
		//
		bool IsHorizontalScrollbarVisible() const;
		//
		// Returns true when the total content height exceeds the visible area, making the vertical scrollbar visible.
		//
		bool IsVerticalScrollbarVisible() const;
		//
		// Returns the number of columns a tab at aColumn must advance to reach the next tab stop.
		//
		int TabSizeAtColumn(int aColumn) const;
		//
		// Ensures cache vectors have one slot per line.
		//
		void EnsureLineMetadataCacheSize();
		//
		// Invalidates cached timing/bytecode data for a single line.
		//
		void InvalidateLineMetadataCache(int aLine);
		//
		// Invalidates cached timing/bytecode data from aLine to the end.
		//
		void InvalidateLineMetadataCacheFromLine(int aLine);
		//
		// Returns cached timing text for a line, computing it on cache miss.
		//
		const std::string& GetCachedTimingText(int aLine);
		//
		// Returns cached bytecode text for a line, computing it on cache miss.
		//
		const std::string& GetCachedBytecodeText(int aLine);

		//
		// Scans the codelens symbol table for prefix matches of the word at the cursor; updates popup state.
		//
		void UpdateAutocomplete();
		//
		// Replaces the partial word at the cursor with the given suggestion and closes the popup.
		//
		void ApplyAutocompletion(const std::string& suggestion);
		// -----------------------------------------------------------------------
		// Palette factory functions and static bracket pair lookup tables
		// -----------------------------------------------------------------------
		static const Palette& GetDarkPalette();
		static const Palette& GetMarianaPalette();
		static const Palette& GetLightPalette();
		static const Palette& GetRetroBluePalette();
		static const std::unordered_map<char, char> OPEN_TO_CLOSE_CHAR;
		static const std::unordered_map<char, char> CLOSE_TO_OPEN_CHAR;
		static PaletteId defaultPalette;
		static std::vector<CodeLensFileData> sCodeLensFiles;
		// -----------------------------------------------------------------------
		// Global codelens parse queue and active parse state (shared across all editors)
		// -----------------------------------------------------------------------
		static std::vector<CodeLensParseTask> sCodeLensParseQueue;
		static std::string sCodeLensActiveFilePath;
		static LanguageDefinitionId sCodeLensActiveLanguage;
		static const LanguageDefinition* sCodeLensActiveLanguageDef;
		static std::vector<std::string> sCodeLensActiveLines;
		static int sCodeLensActiveNextLine;
		static bool sCodeLensActiveParseInProgress;
		static bool sCodeLensActiveSymbolsCleared;
		//
		// Incremented whenever the global codelens symbol table changes (parse complete,
		// symbol deleted, data cleared). Each editor compares against its cached version
		// to detect when mTotalCodeLensHeight must be recomputed.
		//
		static int sCodeLensDataVersion;
		//
		// Returns the language definition singleton for the given id, or nullptr for None.
		//
		static const LanguageDefinition* GetLanguageDefinitionForId(LanguageDefinitionId aId);
		//
		// Returns true when line aLine has a codelens annotation in the current codelens data.
		// aCurrentFilePath must be the normalized document path (NormalizePath applied).
		//
		bool ComputeLineHasCodeLens(int aLine, const std::string& aCurrentFilePath) const;
	};

} // namespace ImGui
