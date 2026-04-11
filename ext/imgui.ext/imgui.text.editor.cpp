//----------------------------------------------------------------------------------------------------
//
//
//
//
//
//----------------------------------------------------------------------------------------------------

#include <algorithm>
#include <string>
#include <set>
#include <cctype>
#include <cstring>
#include <sstream>
#include <fstream>
#include <cstdio>

#include <imgui.h>
#include <imgui_internal.h>
#include "imgui.text.editor.h"

//
// Fractional column bias applied when converting a screen X position to a character column.
// A click landing in the left third of a glyph cell (< 0.33 of its width) snaps to that glyph;
// a click in the right two-thirds snaps to the next one.  Without this bias every click would
// round down, making it impossible to place the caret after the last character on a line without
// clicking well past it.  0.33 is intentional UX tuning — not derivable from any ImGui metric.
//
#define POS_TO_COORDS_COLUMN_OFFSET 0.33f

namespace ImGui {

	//
	// char_isspace: isspace wrapper which is safe to use with char
	// (isspace is UB if its input is not representable as unsigned char)
	//
	static bool char_isspace(char ch) {
		return std::isspace(static_cast<unsigned char>(ch));
	}

	// --------------------------------------- //
	// ----------- Static helpers ------------ //

	//
	// Converts a packed ImU32 ABGR color to a normalized ImVec4 RGBA color
	//
	static ImVec4 U32ColorToVec4(ImU32 in) {
		float s = 1.0f / 255.0f;
		return ImVec4(((in >> IM_COL32_A_SHIFT) & 0xFF) * s, ((in >> IM_COL32_B_SHIFT) & 0xFF) * s, ((in >> IM_COL32_G_SHIFT) & 0xFF) * s, ((in >> IM_COL32_R_SHIFT) & 0xFF) * s);
	}

	//
	// Returns true when the byte is a UTF-8 continuation byte (10xxxxxx)
	//
	static bool IsUTFSequence(char c) {
		return (c & 0xC0) == 0x80;
	}

	//
	// Euclidean distance between two 2D points
	//
	static float Distance(const ImVec2& a, const ImVec2& b) {
		float x = a.x - b.x;
		float y = a.y - b.y;
		return sqrt(x * x + y * y);
	}
	//
	// Returns true when a byte can be considered part of an identifier word.
	//
	static bool IsIdentifierWordByte(char ch) {
		return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
	}
	//
	// Returns true when word appears in line as a whole identifier token.
	// When a match is found, outMatchStart is set to the byte index of the first character of the match.
	//
	static bool LineContainsWholeWord(const std::string& lineText, const std::string& word, int* outMatchStart = nullptr) {
		if (word.empty() || lineText.empty())
			return false;
		const int lineSize = (int)lineText.size();
		const int wordSize = (int)word.size();
		for (int i = 0; i + wordSize <= lineSize; i++) {
			bool matched = true;
			for (int j = 0; j < wordSize; j++)
				if ((char)std::toupper((unsigned char)lineText[i + j]) != (char)std::toupper((unsigned char)word[j])) {
					matched = false;
					break;
				}
			if (!matched)
				continue;
			const bool leftBoundary = (i == 0) || !IsIdentifierWordByte(lineText[i - 1]);
			const bool rightBoundary = (i + wordSize >= lineSize) || !IsIdentifierWordByte(lineText[i + wordSize]);
			if (leftBoundary && rightBoundary) {
				if (outMatchStart != nullptr)
					*outMatchStart = i;
				return true;
			}
		}
		return false;
	}

	//
	// Extract first line from a codelens payload for compact rendering.
	//
	static std::string GetSingleLineCodeLensText(const std::string& text) {
		size_t end = text.find('\n');
		if (end == std::string::npos)
			end = text.find('\r');
		return end == std::string::npos ? text : text.substr(0, end);
	}
	//
	// Expands tab characters to spaces using tab stops so tooltip text aligns with editor tab width.
	//
	static std::string ExpandTabsForTooltip(const std::string& text, int tabSize) {
		if (tabSize <= 0)
			tabSize = 4;
		std::string expanded;
		expanded.reserve(text.size());
		int column = 0;
		for (size_t i = 0; i < text.size(); i++) {
			char ch = text[i];
			if (ch == '\t') {
				int spaces = tabSize - (column % tabSize);
				expanded.append((size_t)spaces, ' ');
				column += spaces;
			} else {
				expanded.push_back(ch);
				if (ch == '\n' || ch == '\r')
					column = 0;
				else
					column++;
			}
		}
		return expanded;
	}
	//
	// Normalizes path separators to forward slashes for consistent cross-platform comparison.
	//
	static std::string NormalizePath(const std::string& path) {
		std::string result = path;
		for (size_t i = 0; i < result.size(); i++)
			if (result[i] == '\\')
				result[i] = '/';
		return result;
	}
	//
	// Builds repeat-block synthetic codelens symbol key for a specific file/line.
	//
	static std::string BuildRepeatCodeLensSymbolName(const std::string& filePath, int lineNumber) {
		return filePath + ":" + std::to_string(lineNumber) + ":REPEAT";
	}

	// --------------------------------------- //
	// ------------- Exposed API ------------- //

	TextEditor::TextEditor() {
		SetPalette(defaultPalette);
		mLines.push_back(Line());
		SetLanguageDefinition(LanguageDefinitionId::Cpp);
	}

	TextEditor::~TextEditor() {}

	// --------------------------------------- //
	// ----------- Simple accessors ---------- //

	void TextEditor::SetReadOnlyEnabled(bool aValue) {
		mReadOnly = aValue;
	}
	bool TextEditor::IsReadOnlyEnabled() const {
		return mReadOnly;
	}
	void TextEditor::SetInputEnabled(bool aValue) {
		mInputEnabled = aValue;
	}
	bool TextEditor::IsInputEnabled() const {
		return mInputEnabled;
	}
	void TextEditor::SetAutoIndentEnabled(bool aValue) {
		mAutoIndent = aValue;
	}
	bool TextEditor::IsAutoIndentEnabled() const {
		return mAutoIndent;
	}
	void TextEditor::SetShowWhitespacesEnabled(bool aValue) {
		mShowWhitespaces = aValue;
	}
	bool TextEditor::IsShowWhitespacesEnabled() const {
		return mShowWhitespaces;
	}
	void TextEditor::SetShowLineNumbersEnabled(bool aValue) {
		mShowLineNumbers = aValue;
	}
	bool TextEditor::IsShowLineNumbersEnabled() const {
		return mShowLineNumbers;
	}
	void TextEditor::SetShortTabsEnabled(bool aValue) {
		mShortTabs = aValue;
	}
	bool TextEditor::IsShortTabsEnabled() const {
		return mShortTabs;
	}
	void TextEditor::SetShowTimingEnabled(bool aValue) {
		mShowTiming = aValue;
	}
	bool TextEditor::IsShowTimingEnabled() const {
		return mShowTiming;
	}
	void TextEditor::SetShowBytecodeEnabled(bool aValue) {
		mShowBytecode = aValue;
	}
	bool TextEditor::IsShowBytecodeEnabled() const {
		return mShowBytecode;
	}
	void TextEditor::SetCursorStyle(CursorStyle aValue) {
		mCursorStyle = aValue;
	}
	TextEditor::CursorStyle TextEditor::GetCursorStyle() const {
		return mCursorStyle;
	}
	void TextEditor::SetTimingType(TimingType aValue) {
		if (mLanguageDefinition == nullptr)
			return;
		mLanguageDefinition->mTimingType = aValue;
		if (mTimingCallback == nullptr)
			return;
		InvalidateLineMetadataCacheFromLine(0);
		ScheduleCodeLensRefresh();
	}
	TextEditor::TimingType TextEditor::GetTimingType() const {
		if (mLanguageDefinition == nullptr)
			return TimingType::Cycles;
		return mLanguageDefinition->mTimingType;
	}
	bool TextEditor::HasTimingSupport() const {
		return mTimingCallback != nullptr;
	}
	void TextEditor::SetFindAllResultsCallback(FindAllResultsCallback aValue, void* aUserData) {
		mFindAllResultsCallback = aValue;
		mFindAllResultsUserData = aUserData;
	}
	int TextEditor::GetLineCount() const {
		return (int)mLines.size();
	}
	TextEditor::PaletteId TextEditor::GetPalette() const {
		return mPaletteId;
	}
	TextEditor::LanguageDefinitionId TextEditor::GetLanguageDefinition() const {
		return mLanguageDefinitionId;
	}
	void TextEditor::SetDocumentPath(const std::string& aValue) {
		mDocumentPath = aValue;
		mTotalCodeLensDirty = true;
	}
	const std::string& TextEditor::GetDocumentPath() const {
		return mDocumentPath;
	}
	int TextEditor::GetTabSize() const {
		return mTabSize;
	}
	float TextEditor::GetLineSpacing() const {
		return mLineSpacing;
	}
	void TextEditor::SetDefaultPalette(TextEditor::PaletteId aValue) {
		defaultPalette = aValue;
	}
	TextEditor::PaletteId TextEditor::GetDefaultPalette() {
		return defaultPalette;
	}
	bool TextEditor::CanUndo() const {
		return !mReadOnly && mUndoIndex > 0;
	}
	bool TextEditor::CanRedo() const {
		return !mReadOnly && mUndoIndex < (int)mUndoBuffer.size();
	}
	int TextEditor::GetUndoIndex() const {
		return mUndoIndex;
	}
	bool TextEditor::IsHorizontalScrollbarVisible() const {
		return mCurrentSpaceWidth > mContentWidth;
	}
	bool TextEditor::IsVerticalScrollbarVisible() const {
		return mCurrentSpaceHeight > mContentHeight;
	}
	int TextEditor::TabSizeAtColumn(int aColumn) const {
		return mTabSize - (aColumn % mTabSize);
	}
	void TextEditor::EnsureLineMetadataCacheSize() {
		size_t lineCount = mLines.size();
		if (mTimingTextCache.size() != lineCount)
			mTimingTextCache.resize(lineCount);
		if (mTimingTextCacheValid.size() != lineCount)
			mTimingTextCacheValid.resize(lineCount, 0);
		if (mBytecodeTextCache.size() != lineCount)
			mBytecodeTextCache.resize(lineCount);
		if (mBytecodeTextCacheValid.size() != lineCount)
			mBytecodeTextCacheValid.resize(lineCount, 0);
	}
	void TextEditor::InvalidateLineMetadataCache(int aLine) {
		if (aLine < 0)
			return;
		EnsureLineMetadataCacheSize();
		if ((size_t)aLine >= mTimingTextCacheValid.size())
			return;
		mTimingTextCacheValid[aLine] = 0;
		mBytecodeTextCacheValid[aLine] = 0;
	}
	void TextEditor::InvalidateLineMetadataCacheFromLine(int aLine) {
		if (aLine < 0)
			aLine = 0;
		EnsureLineMetadataCacheSize();
		for (size_t i = (size_t)aLine; i < mTimingTextCacheValid.size(); ++i)
			mTimingTextCacheValid[i] = 0;
		for (size_t i = (size_t)aLine; i < mBytecodeTextCacheValid.size(); ++i)
			mBytecodeTextCacheValid[i] = 0;
	}
	const std::string& TextEditor::GetCachedTimingText(int aLine) {
		static const std::string empty;
		if (mTimingCallback == nullptr || aLine < 0 || aLine >= (int)mLines.size())
			return empty;
		EnsureLineMetadataCacheSize();
		if (!mTimingTextCacheValid[aLine]) {
			std::string lineText;
			lineText.reserve(mLines[aLine].size());
			for (const auto& glyph : mLines[aLine])
				lineText += glyph.mChar;
			mTimingTextCache[aLine] = mTimingCallback(aLine, lineText, (void*)&mLanguageDefinition->mTimingType);
			mTimingTextCacheValid[aLine] = 1;
		}
		return mTimingTextCache[aLine];
	}
	const std::string& TextEditor::GetCachedBytecodeText(int aLine) {
		static const std::string empty;
		if (mBytecodeCallback == nullptr || aLine < 0 || aLine >= (int)mLines.size())
			return empty;
		EnsureLineMetadataCacheSize();
		if (!mBytecodeTextCacheValid[aLine]) {
			std::string lineText;
			lineText.reserve(mLines[aLine].size());
			for (const auto& glyph : mLines[aLine])
				lineText += glyph.mChar;
			mBytecodeTextCache[aLine] = mBytecodeCallback(aLine, lineText, nullptr);
			mBytecodeTextCacheValid[aLine] = 1;
		}
		return mBytecodeTextCache[aLine];
	}

	void TextEditor::UpdateAutocomplete() {
		mAutocompleteActive = false;
		mAutocompleteSuggestions.clear();
		if (!mAutocompleteEnabled)
			return;
		if (mReadOnly || !mInputEnabled)
			return;
		if (mState.mCursors[mState.mCurrentCursor].HasSelection())
			return;
		if (sCodeLensFiles.empty())
			return;
		Coordinates cursorPos = GetSanitizedCursorCoordinates();
		Coordinates wordStart = FindWordStart(cursorPos);
		int wordStartCharIndex = GetCharacterIndexL(wordStart);
		if (wordStartCharIndex >= 0 && wordStartCharIndex < (int)mLines[wordStart.mLine].size()) {
			const Glyph& g = mLines[wordStart.mLine][wordStartCharIndex];
			if (g.mComment || g.mMultiLineComment)
				return;
		}
		std::string currentWord = GetText(wordStart, cursorPos);
		if (currentWord.size() < 2)
			return;
		for (size_t ci = 0; ci < currentWord.size(); ci++)
			if (!IsIdentifierWordByte(currentWord[ci]))
				return;
		std::string upperPrefix;
		upperPrefix.reserve(currentWord.size());
		for (size_t ci = 0; ci < currentWord.size(); ci++)
			upperPrefix += (char)std::toupper((unsigned char)currentWord[ci]);
		std::set<std::string> seen;
		const int kMaxSuggestions = 8;
		bool done = false;
		for (size_t fi = 0; fi < sCodeLensFiles.size() && !done; fi++) {
			// Only suggest symbols from files parsed under the same language as this editor instance.
			if (sCodeLensFiles[fi].language != mLanguageDefinitionId)
				continue;
			for (const auto& sym : sCodeLensFiles[fi].symbols) {
				if (sym.symbolName.size() <= currentWord.size())
					continue;
				if (seen.count(sym.symbolName))
					continue;
				// Pass 1: exact case-sensitive prefix match.
				bool match = (sym.symbolName.compare(0, currentWord.size(), currentWord) == 0);
				// Pass 2: if no exact match and language is case-insensitive, try uppercase prefix.
				if (!match && mLanguageDefinition != nullptr && !mLanguageDefinition->mCaseSensitive) {
					match = true;
					for (size_t ci = 0; ci < upperPrefix.size(); ci++) {
						if ((char)std::toupper((unsigned char)sym.symbolName[ci]) != upperPrefix[ci]) {
							match = false;
							break;
						}
					}
				}
				if (!match)
					continue;
				seen.insert(sym.symbolName);
				mAutocompleteSuggestions.push_back(sym.symbolName);
				if ((int)mAutocompleteSuggestions.size() >= kMaxSuggestions) {
					done = true;
					break;
				}
			}
		}
		if (mAutocompleteSuggestions.empty())
			return;
		mAutocompleteActive = true;
		mAutocompleteCurrentWord = currentWord;
	}
	void TextEditor::ApplyAutocompletion(const std::string& suggestion) {
		if (mReadOnly || suggestion.empty())
			return;
		Coordinates cursorPos = GetSanitizedCursorCoordinates();
		Coordinates wordStart = FindWordStart(cursorPos);
		SetSelection(wordStart, cursorPos, mState.mCurrentCursor);
		ReplaceSelection(suggestion.c_str());
		mAutocompleteActive = false;
		mAutocompleteEnabled = false;
		mAutocompleteSuggestions.clear();
		mAutocompleteCurrentWord.clear();
	}
	void TextEditor::GetCursorPosition(int& outLine, int& outColumn) const {
		auto coords = GetSanitizedCursorCoordinates();
		outLine = coords.mLine;
		outColumn = coords.mColumn;
	}

	void TextEditor::SetPalette(PaletteId aValue) {
		mPaletteId = aValue;

		// [Bundle:DelayedInit]
		// This is a workaround to allow the TextEditor to be constructed
		// even if the ImGui context is not yet initialized
		// (We will call SetPalette at each call to Render)
		if (ImGui::GetCurrentContext() == nullptr)
			return;

		const Palette* palletteBase;
		switch (mPaletteId) {
			case PaletteId::Dark:
				palletteBase = &(GetDarkPalette());
				break;
			case PaletteId::Light:
				palletteBase = &(GetLightPalette());
				break;
			case PaletteId::Mariana:
				palletteBase = &(GetMarianaPalette());
				break;
			case PaletteId::RetroBlue:
				palletteBase = &(GetRetroBluePalette());
				break;
		}
		/* Update palette with the current alpha from ImGui style */
		// Each base color is decoded to float, alpha-multiplied, then repacked as ABGR.
		for (int i = 0; i < (int)PaletteIndex::Max; ++i) {
			ImVec4 color = U32ColorToVec4((*palletteBase)[i]);
			color.w *= ImGui::GetStyle().Alpha;
			mPalette[i] = ImGui::ColorConvertFloat4ToU32(color);
		}
	}

	void TextEditor::SetLanguageDefinition(LanguageDefinitionId aValue) {
		mLanguageDefinitionId = aValue;
		mTotalCodeLensDirty = true;
		switch (mLanguageDefinitionId) {
			case LanguageDefinitionId::None:
				mLanguageDefinition = nullptr;
				mTimingCallback = nullptr;
				mBytecodeCallback = nullptr;
				return;
			case LanguageDefinitionId::Cpp:
				mLanguageDefinition = &(LanguageDefinition::Cpp());
				break;
			case LanguageDefinitionId::C:
				mLanguageDefinition = &(LanguageDefinition::C());
				break;
			case LanguageDefinitionId::Cs:
				mLanguageDefinition = &(LanguageDefinition::Cs());
				break;
			case LanguageDefinitionId::Python:
				mLanguageDefinition = &(LanguageDefinition::Python());
				break;
			case LanguageDefinitionId::Lua:
				mLanguageDefinition = &(LanguageDefinition::Lua());
				break;
			case LanguageDefinitionId::Json:
				mLanguageDefinition = &(LanguageDefinition::Json());
				break;
			case LanguageDefinitionId::Sql:
				mLanguageDefinition = &(LanguageDefinition::Sql());
				break;
			case LanguageDefinitionId::AngelScript:
				mLanguageDefinition = &(LanguageDefinition::AngelScript());
				break;
			case LanguageDefinitionId::Glsl:
				mLanguageDefinition = &(LanguageDefinition::Glsl());
				break;
			case LanguageDefinitionId::Hlsl:
				mLanguageDefinition = &(LanguageDefinition::Hlsl());
				break;
			case LanguageDefinitionId::Z80Asm:
				mLanguageDefinition = &(LanguageDefinition::Z80Asm());
				break;
		}
		if (mLanguageDefinition) {
			mTimingCallback = mLanguageDefinition->mTimingCallback;
			mBytecodeCallback = mLanguageDefinition->mBytecodeCallback;
		} else {
			mTimingCallback = nullptr;
			mBytecodeCallback = nullptr;
		}

		InvalidateLineMetadataCacheFromLine(0);

		Colorize();
		ScheduleCodeLensRefresh();
	}

	const char* TextEditor::GetLanguageDefinitionName() const {
		return mLanguageDefinition != nullptr ? mLanguageDefinition->mName.c_str() : "None";
	}

	void TextEditor::SetTabSize(int aValue) {
		mTabSize = std::max(1, std::min(8, aValue));
	}

	void TextEditor::SetLineSpacing(float aValue) {
		mLineSpacing = std::max(1.0f, std::min(2.0f, aValue));
	}

	void TextEditor::SelectAll() {
		ClearSelections();
		ClearExtraCursors();
		MoveTop();
		MoveBottom(true);
	}

	void TextEditor::SelectLine(int aLine) {
		ClearSelections();
		ClearExtraCursors();
		SetSelection({aLine, 0}, {aLine, GetLineMaxColumn(aLine)});
	}

	void TextEditor::SelectRegion(int aStartLine, int aStartChar, int aEndLine, int aEndChar) {
		ClearSelections();
		ClearExtraCursors();
		SetSelection(aStartLine, aStartChar, aEndLine, aEndChar);
	}

	void TextEditor::SelectNextOccurrenceOf(const char* aText, int aTextSize, bool aCaseSensitive) {
		ClearSelections();
		ClearExtraCursors();
		SelectNextOccurrenceOf(aText, aTextSize, -1, aCaseSensitive);
	}

	void TextEditor::SelectAllOccurrencesOf(const char* aText, int aTextSize, bool aCaseSensitive) {
		ClearSelections();
		ClearExtraCursors();
		SelectNextOccurrenceOf(aText, aTextSize, -1, aCaseSensitive);
		Coordinates startPos = mState.mCursors[mState.GetLastAddedCursorIndex()].mInteractiveEnd;
		while (true) {
			AddCursorForNextOccurrence(aCaseSensitive);
			Coordinates lastAddedPos = mState.mCursors[mState.GetLastAddedCursorIndex()].mInteractiveEnd;
			if (lastAddedPos == startPos)
				break;
		}
	}

	bool TextEditor::AnyCursorHasSelection() const {
		for (int c = 0; c <= mState.mCurrentCursor; c++)
			if (mState.mCursors[c].HasSelection())
				return true;
		return false;
	}

	bool TextEditor::AllCursorsHaveSelection() const {
		for (int c = 0; c <= mState.mCurrentCursor; c++)
			if (!mState.mCursors[c].HasSelection())
				return false;
		return true;
	}

	void TextEditor::ClearExtraCursors() {
		mState.mCurrentCursor = 0;
	}

	void TextEditor::ClearSelections() {
		for (int c = mState.mCurrentCursor; c > -1; c--)
			mState.mCursors[c].mInteractiveEnd = mState.mCursors[c].mInteractiveStart = mState.mCursors[c].GetSelectionEnd();
	}

	void TextEditor::SetCursorPosition(int aLine, int aCharIndex) {
		SetCursorPosition({aLine, GetCharacterColumn(aLine, aCharIndex)}, -1, true);
	}

	int TextEditor::GetFirstVisibleLine() {
		return mFirstVisibleLine;
	}

	int TextEditor::GetLastVisibleLine() {
		return mLastVisibleLine;
	}

	void TextEditor::SetViewAtLine(int aLine, SetViewAtLineMode aMode) {
		mSetViewAtLine = aLine;
		mSetViewAtLineMode = aMode;
	}

	void TextEditor::Copy() {
		if (AnyCursorHasSelection()) {
			std::string clipboardText = GetClipboardText();
			ImGui::SetClipboardText(clipboardText.c_str());
		} else {
			if (!mLines.empty()) {
				std::string str;
				auto& line = mLines[GetSanitizedCursorCoordinates().mLine];
				for (auto& g : line)
					str.push_back(g.mChar);
				ImGui::SetClipboardText(str.c_str());
			}
		}
	}

	void TextEditor::Cut() {
		if (mReadOnly) {
			Copy();
		} else {
			if (AnyCursorHasSelection()) {
				UndoRecord u;
				u.mBefore = mState;

				Copy();
				for (int c = mState.mCurrentCursor; c > -1; c--) {
					u.mOperations.push_back({GetSelectedText(c), mState.mCursors[c].GetSelectionStart(), mState.mCursors[c].GetSelectionEnd(), UndoOperationType::Delete});
					DeleteSelection(c);
				}

				u.mAfter = mState;
				AddUndo(u);
			}
		}
	}

	void TextEditor::Paste() {
		if (mReadOnly)
			return;

		if (ImGui::GetClipboardText() == nullptr)
			return; // something other than text in the clipboard

		// Check if the clipboard was produced by a multi-cursor Copy/Cut: if the number of lines
		// in the clipboard exactly matches the number of active cursors we distribute one line per cursor.
		std::string clipText = ImGui::GetClipboardText();
		bool canPasteToMultipleCursors = false;
		std::vector<std::pair<int, int>> clipTextLines;
		if (mState.mCurrentCursor > 0) {
			clipTextLines.push_back({0, 0});
			for (int i = 0; i < (int)clipText.length(); i++) {
				if (clipText[i] == '\n') {
					clipTextLines.back().second = i;
					clipTextLines.push_back({i + 1, 0});
				}
			}
			clipTextLines.back().second = (int)clipText.length();
			canPasteToMultipleCursors = (int)clipTextLines.size() == mState.mCurrentCursor + 1;
		}

		if (clipText.length() > 0) {
			UndoRecord u;
			u.mBefore = mState;

			if (AnyCursorHasSelection()) {
				for (int c = mState.mCurrentCursor; c > -1; c--) {
					u.mOperations.push_back({GetSelectedText(c), mState.mCursors[c].GetSelectionStart(), mState.mCursors[c].GetSelectionEnd(), UndoOperationType::Delete});
					DeleteSelection(c);
				}
			}

			for (int c = mState.mCurrentCursor; c > -1; c--) {
				Coordinates start = GetSanitizedCursorCoordinates(c);
				if (canPasteToMultipleCursors) {
					std::string clipSubText = clipText.substr(clipTextLines[c].first, clipTextLines[c].second - clipTextLines[c].first);
					InsertTextAtCursor(clipSubText.c_str(), c);
					u.mOperations.push_back({clipSubText, start, GetSanitizedCursorCoordinates(c), UndoOperationType::Add});
				} else {
					InsertTextAtCursor(clipText.c_str(), c);
					u.mOperations.push_back({clipText, start, GetSanitizedCursorCoordinates(c), UndoOperationType::Add});
				}
			}

			u.mAfter = mState;
			AddUndo(u);
		}
	}

	void TextEditor::Undo(int aSteps) {
		while (CanUndo() && aSteps-- > 0)
			mUndoBuffer[--mUndoIndex].Undo(this);
	}

	void TextEditor::Redo(int aSteps) {
		while (CanRedo() && aSteps-- > 0)
			mUndoBuffer[mUndoIndex++].Redo(this);
	}

	void TextEditor::SetText(const std::string& aText) {
		mLines.clear();
		mLines.emplace_back(Line());
		for (auto chr : aText) {
			if (chr == '\r')
				continue;

			if (chr == '\n')
				mLines.emplace_back(Line());
			else {
				mLines.back().emplace_back(Glyph(chr, PaletteIndex::Default));
			}
		}

		mScrollToTop = true;

		mUndoBuffer.clear();
		mUndoIndex = 0;
		InvalidateLineMetadataCacheFromLine(0);

		Colorize();
		ScheduleCodeLensRefresh();
	}

	std::string TextEditor::GetText() const {
		auto lastLine = (int)mLines.size() - 1;
		auto lastLineLength = GetLineMaxColumn(lastLine);
		Coordinates startCoords = Coordinates();
		Coordinates endCoords = Coordinates(lastLine, lastLineLength);
		return startCoords < endCoords ? GetText(startCoords, endCoords) : "";
	}

	void TextEditor::SetTextLines(const std::vector<std::string>& aLines) {
		mLines.clear();

		if (aLines.empty())
			mLines.emplace_back(Line());
		else {
			mLines.resize(aLines.size());

			for (size_t i = 0; i < aLines.size(); ++i) {
				const std::string& aLine = aLines[i];

				mLines[i].reserve(aLine.size());
				for (size_t j = 0; j < aLine.size(); ++j)
					mLines[i].emplace_back(Glyph(aLine[j], PaletteIndex::Default));
			}
		}

		mScrollToTop = true;

		mUndoBuffer.clear();
		mUndoIndex = 0;
		InvalidateLineMetadataCacheFromLine(0);
		mTotalCodeLensDirty = true;

		Colorize();
	}

	std::vector<std::string> TextEditor::GetTextLines() const {		std::vector<std::string> result;

		result.reserve(mLines.size());

		for (auto& line : mLines) {
			std::string text;

			text.resize(line.size());

			for (size_t i = 0; i < line.size(); ++i)
				text[i] = line[i].mChar;

			result.emplace_back(std::move(text));
		}

		return result;
	}

	bool TextEditor::Render(const char* aTitle, bool aParentIsFocused, const ImVec2& aSize, bool aBorder) {
		if (mCursorPositionChanged)
			OnCursorPositionChanged();
		mCursorPositionChanged = false;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(mPalette[(int)PaletteIndex::Background]));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
		ImGui::BeginChild(aTitle, aSize, aBorder, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavInputs);

		bool isFocused = ImGui::IsWindowFocused();
		//
		// This is a bit messy. SDL requires explicit code to trigger input events so in order to achieve them
		// from here, using only ImGui, we need to mimic the TextInput behavior...
		//
		if ((isFocused || aParentIsFocused) && mInputEnabled) {
			ImGuiContext* g = ImGui::GetCurrentContext();
			g->WantCaptureKeyboardNextFrame = true;
			ImGuiIO& io = ImGui::GetIO();
			io.WantCaptureKeyboard = true;
			io.WantTextInput = true;
			ImGuiPlatformImeData* ime_data = &g->PlatformImeData; // (this is a public struct, passed to io.Platform_SetImeDataFn() handler)
			ime_data->WantVisible = true;
			ime_data->WantTextInput = true;
			ime_data->InputPos = ImVec2(g->CurrentWindow->DC.CursorPos.x - 1.0f, g->CurrentWindow->DC.CursorPos.y - g->FontSize);
			ime_data->InputLineHeight = g->FontSize;
			ime_data->ViewportId = g->CurrentWindow->Viewport->ID;
		}
		if (mInputEnabled)
			HandleKeyboardInputs(aParentIsFocused);
		HandleMouseInputs();
		ColorizeInternal();
		TickIncrementalCodeLensParse();
		UpdateAutocomplete();
		Render(aParentIsFocused);
		ImGui::EndChild();
		// Autocomplete popup — rendered outside the child window so it is never clipped by its scroll region
		if (mAutocompleteActive && !mAutocompleteSuggestions.empty()) {
			// Escape can be checked here too since ImGui key state is global
			if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
				mAutocompleteActive = false;
				mAutocompleteEnabled = false;
			} else {
				ImGui::SetNextWindowPos(ImVec2(mAutocompletePopupPos.x - mCharAdvance.x * 0.5f, mAutocompletePopupPos.y - mCharAdvance.y * 0.5f), ImGuiCond_Always);
				ImGui::SetNextWindowBgAlpha(0.98f);
				ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
				ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
				// Darken the background relative to the editor background so the popup is visually distinct
				ImVec4 editorBg = ImGui::ColorConvertU32ToFloat4(mPalette[(int)PaletteIndex::Background]);
				ImVec4 popupBg = ImVec4(editorBg.x * 0.5f, editorBg.y * 0.5f, editorBg.z * 0.5f, 1.0f);
				ImGui::PushStyleColor(ImGuiCol_WindowBg, popupBg);
				ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(mPalette[(int)PaletteIndex::Selection]));
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(mPalette[(int)PaletteIndex::LineNumber]));
				std::string autocompleteWindowId = std::string("##Autocomplete_") + aTitle;
				ImGui::Begin(autocompleteWindowId.c_str(), nullptr,
							 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
								 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav);
				// Force this window above all sibling editor child windows in the draw order.
				ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
				ImGui::SetWindowFontScale(0.80f);
				for (int i = 0; i < (int)mAutocompleteSuggestions.size(); i++) {
					bool isFirst = (i == 0);
					if (isFirst)
						ImGui::PushStyleColor(ImGuiCol_Header, ImGui::ColorConvertU32ToFloat4(mPalette[(int)PaletteIndex::Selection]));
					if (ImGui::Selectable(mAutocompleteSuggestions[i].c_str(), isFirst))
						ApplyAutocompletion(mAutocompleteSuggestions[i]);
					if (isFirst)
						ImGui::PopStyleColor();
				}
				ImGui::SetWindowFontScale(1.0f);
				ImGui::End();
				ImGui::PopStyleColor(3);
				ImGui::PopStyleVar(3);
			}
		}
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();
		return isFocused;
	}

	// ------------------------------------ //
	// ---------- Generic utils ----------- //

	// https://en.wikipedia.org/wiki/UTF-8
	// We assume that the char is a standalone character (<128) or a leading byte of an UTF-8 code sequence (non-10xxxxxx code)
	static int UTF8CharLength(char c) {
		if ((c & 0xFE) == 0xFC)
			return 6;
		if ((c & 0xFC) == 0xF8)
			return 5;
		if ((c & 0xF8) == 0xF0)
			return 4;
		else if ((c & 0xF0) == 0xE0)
			return 3;
		else if ((c & 0xE0) == 0xC0)
			return 2;
		return 1;
	}

	// "Borrowed" from ImGui source
	static inline int ImTextCharToUtf8(char* buf, int buf_size, unsigned int c) {
		if (c < 0x80) {
			buf[0] = (char)c;
			return 1;
		}
		if (c < 0x800) {
			if (buf_size < 2)
				return 0;
			buf[0] = (char)(0xc0 + (c >> 6));
			buf[1] = (char)(0x80 + (c & 0x3f));
			return 2;
		}
		if (c >= 0xdc00 && c < 0xe000) {
			return 0;
		}
		if (c >= 0xd800 && c < 0xdc00) {
			if (buf_size < 4)
				return 0;
			buf[0] = (char)(0xf0 + (c >> 18));
			buf[1] = (char)(0x80 + ((c >> 12) & 0x3f));
			buf[2] = (char)(0x80 + ((c >> 6) & 0x3f));
			buf[3] = (char)(0x80 + ((c) & 0x3f));
			return 4;
		}
		// else if (c < 0x10000)
		{
			if (buf_size < 3)
				return 0;
			buf[0] = (char)(0xe0 + (c >> 12));
			buf[1] = (char)(0x80 + ((c >> 6) & 0x3f));
			buf[2] = (char)(0x80 + ((c) & 0x3f));
			return 3;
		}
	}

	static inline bool CharIsWordChar(char ch) {
		int sizeInBytes = UTF8CharLength(ch);
		return sizeInBytes > 1 || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
	}

	// ------------------------------------ //
	// ------------- Internal ------------- //

	// --------- Coordinates --------- //

	TextEditor::Coordinates::Coordinates() : mLine(0), mColumn(0) {}
	TextEditor::Coordinates::Coordinates(int aLine, int aColumn) : mLine(aLine), mColumn(aColumn) {
		assert(aLine >= 0);
		assert(aColumn >= 0);
	}
	TextEditor::Coordinates TextEditor::Coordinates::Invalid() {
		static Coordinates invalid(-1, -1);
		return invalid;
	}
	bool TextEditor::Coordinates::operator==(const Coordinates& o) const {
		return mLine == o.mLine && mColumn == o.mColumn;
	}
	bool TextEditor::Coordinates::operator!=(const Coordinates& o) const {
		return mLine != o.mLine || mColumn != o.mColumn;
	}
	bool TextEditor::Coordinates::operator<(const Coordinates& o) const {
		return mLine != o.mLine ? mLine < o.mLine : mColumn < o.mColumn;
	}
	bool TextEditor::Coordinates::operator>(const Coordinates& o) const {
		return mLine != o.mLine ? mLine > o.mLine : mColumn > o.mColumn;
	}
	bool TextEditor::Coordinates::operator<=(const Coordinates& o) const {
		return mLine != o.mLine ? mLine < o.mLine : mColumn <= o.mColumn;
	}
	bool TextEditor::Coordinates::operator>=(const Coordinates& o) const {
		return mLine != o.mLine ? mLine > o.mLine : mColumn >= o.mColumn;
	}
	TextEditor::Coordinates TextEditor::Coordinates::operator-(const Coordinates& o) {
		return Coordinates(mLine - o.mLine, mColumn - o.mColumn);
	}
	TextEditor::Coordinates TextEditor::Coordinates::operator+(const Coordinates& o) {
		return Coordinates(mLine + o.mLine, mColumn + o.mColumn);
	}

	// --------- Cursor --------- //

	TextEditor::Coordinates TextEditor::Cursor::GetSelectionStart() const {
		return mInteractiveStart < mInteractiveEnd ? mInteractiveStart : mInteractiveEnd;
	}
	TextEditor::Coordinates TextEditor::Cursor::GetSelectionEnd() const {
		return mInteractiveStart > mInteractiveEnd ? mInteractiveStart : mInteractiveEnd;
	}
	bool TextEditor::Cursor::HasSelection() const {
		return mInteractiveStart != mInteractiveEnd;
	}

	// --------- Glyph --------- //

	TextEditor::Glyph::Glyph(char aChar, PaletteIndex aColorIndex) : mChar(aChar), mColorIndex(aColorIndex), mComment(false), mMultiLineComment(false), mPreprocessor(false) {}

	// ---------- Editor state functions --------- //

	void TextEditor::EditorState::AddCursor() {
		// The vector is never shrunk; mCurrentCursor is the index of the last valid cursor.
		// Growing it here ensures the slot exists before any code reads from it.
		mCurrentCursor++;
		mCursors.resize(mCurrentCursor + 1);
		mLastAddedCursor = mCurrentCursor;
	}

	int TextEditor::EditorState::GetLastAddedCursorIndex() {
		// If mLastAddedCursor exceeds the valid range (e.g. after ClearExtraCursors), fall back to cursor 0.
		return mLastAddedCursor > mCurrentCursor ? 0 : mLastAddedCursor;
	}

	void TextEditor::EditorState::SortCursorsFromTopToBottom() {
		// Save the interactive-end position of the last-added cursor so we can re-locate it
		// after the sort and keep mLastAddedCursor pointing at the correct index.
		Coordinates lastAddedCursorPos = mCursors[GetLastAddedCursorIndex()].mInteractiveEnd;
		std::sort(mCursors.begin(), mCursors.begin() + (mCurrentCursor + 1),
				  [](const Cursor& a, const Cursor& b) -> bool { return a.GetSelectionStart() < b.GetSelectionStart(); });
		// update last added cursor index to be valid after sort
		for (int c = mCurrentCursor; c > -1; c--)
			if (mCursors[c].mInteractiveEnd == lastAddedCursorPos)
				mLastAddedCursor = c;
	}

	// ---------- Undo record functions --------- //

	TextEditor::UndoRecord::UndoRecord(const std::vector<UndoOperation>& aOperations, TextEditor::EditorState& aBefore, TextEditor::EditorState& aAfter) {
		mOperations = aOperations;
		mBefore = aBefore;
		mAfter = aAfter;
		for (const UndoOperation& o : mOperations) {
			assert(o.mStart <= o.mEnd);
			(void)o;
		}
	}

	void TextEditor::UndoRecord::Undo(TextEditor* aEditor) {
		// Operations are replayed in reverse order so that later operations
		// (which may depend on earlier ones) are rolled back first.
		for (int i = mOperations.size() - 1; i > -1; i--) {
			const UndoOperation& operation = mOperations[i];
			if (!operation.mText.empty()) {
				switch (operation.mType) {
					case UndoOperationType::Delete: {
						auto start = operation.mStart;
						aEditor->InsertTextAt(start, operation.mText.c_str());
						aEditor->Colorize(operation.mStart.mLine - 1, operation.mEnd.mLine - operation.mStart.mLine + 2);
						break;
					}
					case UndoOperationType::Add: {
						aEditor->DeleteRange(operation.mStart, operation.mEnd);
						aEditor->Colorize(operation.mStart.mLine - 1, operation.mEnd.mLine - operation.mStart.mLine + 2);
						break;
					}
				}
			}
		}

		aEditor->mState = mBefore;
		aEditor->EnsureCursorVisible();
	}

	void TextEditor::UndoRecord::Redo(TextEditor* aEditor) {
		for (int i = 0; i < (int)mOperations.size(); i++) {
			const UndoOperation& operation = mOperations[i];
			if (!operation.mText.empty()) {
				switch (operation.mType) {
					case UndoOperationType::Delete: {
						aEditor->DeleteRange(operation.mStart, operation.mEnd);
						aEditor->Colorize(operation.mStart.mLine - 1, operation.mEnd.mLine - operation.mStart.mLine + 1);
						break;
					}
					case UndoOperationType::Add: {
						auto start = operation.mStart;
						aEditor->InsertTextAt(start, operation.mText.c_str());
						aEditor->Colorize(operation.mStart.mLine - 1, operation.mEnd.mLine - operation.mStart.mLine + 1);
						break;
					}
				}
			}
		}

		aEditor->mState = mAfter;
		aEditor->EnsureCursorVisible();
	}

	// ---------- Text editor internal functions --------- //

	std::string TextEditor::GetText(const Coordinates& aStart, const Coordinates& aEnd) const {
		assert(aStart <= aEnd);
		if (aStart == aEnd)
			return std::string();

		std::string result;
		auto lstart = aStart.mLine;
		auto lend = aEnd.mLine;
		auto istart = GetCharacterIndexR(aStart);
		auto iend = GetCharacterIndexR(aEnd);
		size_t s = 0;

		for (int i = lstart; i < lend; i++)
			s += mLines[i].size();

		result.reserve(s + s / 8);

		while (istart < iend || lstart < lend) {
			if (lstart >= (int)mLines.size())
				break;

			auto& line = mLines[lstart];
			if (istart < (int)line.size()) {
				result += line[istart].mChar;
				istart++;
			} else {
				istart = 0;
				++lstart;
				result += '\n';
			}
		}

		return result;
	}

	std::string TextEditor::GetClipboardText() const {
		std::string result;
		for (int c = 0; c <= mState.mCurrentCursor; c++) {
			if (mState.mCursors[c].GetSelectionStart() < mState.mCursors[c].GetSelectionEnd()) {
				if (result.length() != 0)
					result += '\n';
				result += GetText(mState.mCursors[c].GetSelectionStart(), mState.mCursors[c].GetSelectionEnd());
			}
		}
		return result;
	}

	std::string TextEditor::GetSelectedText(int aCursor) const {
		if (aCursor == -1)
			aCursor = mState.mCurrentCursor;

		return GetText(mState.mCursors[aCursor].GetSelectionStart(), mState.mCursors[aCursor].GetSelectionEnd());
	}

	void TextEditor::SetSelectionPosition(const TextEditor::SelectionPosition& pos) {
		SetSelection(pos.start.line, pos.start.column, pos.end.line, pos.end.column);
	}
	TextEditor::TextPosition TextEditor::GetCursorPosition() const {
		Coordinates pos = GetSanitizedCursorCoordinates();
		TextPosition result;
		result.line = pos.mLine;
		result.column = pos.mColumn;
		return result;
	}

	TextEditor::SelectionPosition TextEditor::GetSelectionPosition(int aCursor) const {
		SelectionPosition pos;
		pos.start.line = GetSelectionStart(aCursor).mLine;
		pos.start.column = GetSelectionStart(aCursor).mColumn;
		pos.end.line = GetSelectionEnd(aCursor).mLine;
		pos.end.column = GetSelectionEnd(aCursor).mColumn;
		return pos;
	}

	std::string TextEditor::GetWordAtScreenPos(const ImVec2& aScreenPos) const {
		Coordinates coords = ScreenPosToCoordinates(aScreenPos);
		auto start = FindWordStart(coords);
		auto end = FindWordEnd(coords);
		return GetText(start, end);
	}

	void TextEditor::SetCursorPosition(const Coordinates& aPosition, int aCursor, bool aClearSelection) {
		if (aCursor == -1)
			aCursor = mState.mCurrentCursor;

		mCursorPositionChanged = true;
		if (aClearSelection)
			mState.mCursors[aCursor].mInteractiveStart = aPosition;
		if (mState.mCursors[aCursor].mInteractiveEnd != aPosition) {
			mState.mCursors[aCursor].mInteractiveEnd = aPosition;
			EnsureCursorVisible();
		}
	}

	int TextEditor::InsertTextAt(Coordinates& /* inout */ aWhere, const char* aValue) {
		assert(!mReadOnly);

		int cindex = GetCharacterIndexR(aWhere);
		int totalLines = 0;
		while (*aValue != '\0') {
			assert(!mLines.empty());

			if (*aValue == '\r') {
				// Skip carriage returns; only '\n' advances to the next line.
				++aValue;
			} else if (*aValue == '\n') {
				if (cindex < (int)mLines[aWhere.mLine].size()) {
					InsertLine(aWhere.mLine + 1);
					auto& line = mLines[aWhere.mLine];
					AddGlyphsToLine(aWhere.mLine + 1, 0, line.begin() + cindex, line.end());
					RemoveGlyphsFromLine(aWhere.mLine, cindex);
				} else {
					InsertLine(aWhere.mLine + 1);
				}
				++aWhere.mLine;
				aWhere.mColumn = 0;
				cindex = 0;
				++totalLines;
				++aValue;
			} else {
				auto d = UTF8CharLength(*aValue);
				// Insert all bytes of the UTF-8 codepoint as individual glyphs.
				while (d-- > 0 && *aValue != '\0')
					AddGlyphToLine(aWhere.mLine, cindex++, Glyph(*aValue++, PaletteIndex::Default));
				aWhere.mColumn = GetCharacterColumn(aWhere.mLine, cindex);
			}
		}

		return totalLines;
	}

	void TextEditor::InsertTextAtCursor(const char* aValue, int aCursor) {
		if (aValue == nullptr)
			return;
		if (aCursor == -1)
			aCursor = mState.mCurrentCursor;

		auto pos = GetSanitizedCursorCoordinates(aCursor);
		auto start = std::min(pos, mState.mCursors[aCursor].GetSelectionStart());
		int totalLines = pos.mLine - start.mLine;

		totalLines += InsertTextAt(pos, aValue);

		SetCursorPosition(pos, aCursor);
		Colorize(start.mLine - 1, totalLines + 2);
	}

	bool TextEditor::Move(int& aLine, int& aCharIndex, bool aLeft, bool aLockLine) const {
		// Assumes the given char index is not in the middle of a UTF-8 sequence;
		// char index may equal line.length() (one past the end).

		// invalid line
		if (aLine >= (int)mLines.size())
			return false;

		if (aLeft) {
			if (aCharIndex == 0) {
				if (aLockLine || aLine == 0)
					return false;
				aLine--;
				aCharIndex = (int)mLines[aLine].size();
			} else {
				aCharIndex--;
				// Skip over any UTF-8 continuation bytes (10xxxxxx) to land on the lead byte.
				while (aCharIndex > 0 && IsUTFSequence(mLines[aLine][aCharIndex].mChar))
					aCharIndex--;
			}
		} else // right
		{
			if (aCharIndex == (int)mLines[aLine].size()) {
				if (aLockLine || aLine == (int)mLines.size() - 1)
					return false;
				aLine++;
				aCharIndex = 0;
			} else {
				int seqLength = UTF8CharLength(mLines[aLine][aCharIndex].mChar);
				aCharIndex = std::min(aCharIndex + seqLength, (int)mLines[aLine].size());
			}
		}
		return true;
	}

	void TextEditor::MoveCharIndexAndColumn(int aLine, int& aCharIndex, int& aColumn) const {
		assert(aLine < (int)mLines.size());
		assert(aCharIndex < (int)mLines[aLine].size());
		char c = mLines[aLine][aCharIndex].mChar;
		aCharIndex += UTF8CharLength(c);
		if (c == '\t')
			aColumn = (aColumn / mTabSize) * mTabSize + mTabSize;
		else
			aColumn++;
	}

	void TextEditor::MoveCoords(Coordinates& aCoords, MoveDirection aDirection, bool aWordMode, int aLineCount) const {
		int charIndex = GetCharacterIndexR(aCoords);
		int lineIndex = aCoords.mLine;
		switch (aDirection) {
			case MoveDirection::Right:
				if (charIndex >= (int)mLines[lineIndex].size()) {
					if (lineIndex < (int)mLines.size() - 1) {
						aCoords.mLine = std::max(0, std::min((int)mLines.size() - 1, lineIndex + 1));
						aCoords.mColumn = 0;
					}
				} else {
					Move(lineIndex, charIndex);
					int oneStepRightColumn = GetCharacterColumn(lineIndex, charIndex);
					if (aWordMode) {
						aCoords.mLine = lineIndex;
						aCoords.mColumn = oneStepRightColumn;
						aCoords = FindWordEnd(aCoords);
						aCoords.mColumn = std::max(aCoords.mColumn, oneStepRightColumn);
					} else
						aCoords.mColumn = oneStepRightColumn;
				}
				break;
			case MoveDirection::Left:
				if (charIndex == 0) {
					if (lineIndex > 0) {
						aCoords.mLine = lineIndex - 1;
						aCoords.mColumn = GetLineMaxColumn(aCoords.mLine);
					}
				} else {
					Move(lineIndex, charIndex, true);
					aCoords.mLine = lineIndex;
					aCoords.mColumn = GetCharacterColumn(lineIndex, charIndex);
					if (aWordMode)
						aCoords = FindWordStart(aCoords);
				}
				break;
			case MoveDirection::Up:
				aCoords.mLine = std::max(0, lineIndex - aLineCount);
				break;
			case MoveDirection::Down:
				aCoords.mLine = std::max(0, std::min((int)mLines.size() - 1, lineIndex + aLineCount));
				break;
		}
	}

	void TextEditor::MoveUp(int aAmount, bool aSelect) {
		for (int c = 0; c <= mState.mCurrentCursor; c++) {
			Coordinates newCoords = mState.mCursors[c].mInteractiveEnd;
			MoveCoords(newCoords, MoveDirection::Up, false, aAmount);
			SetCursorPosition(newCoords, c, !aSelect);
		}
		EnsureCursorVisible();
	}

	void TextEditor::MoveDown(int aAmount, bool aSelect) {
		for (int c = 0; c <= mState.mCurrentCursor; c++) {
			assert(mState.mCursors[c].mInteractiveEnd.mColumn >= 0);
			Coordinates newCoords = mState.mCursors[c].mInteractiveEnd;
			MoveCoords(newCoords, MoveDirection::Down, false, aAmount);
			SetCursorPosition(newCoords, c, !aSelect);
		}
		EnsureCursorVisible();
	}

	void TextEditor::MoveLeft(bool aSelect, bool aWordMode) {
		if (mLines.empty())
			return;

		if (AnyCursorHasSelection() && !aSelect && !aWordMode) {
			for (int c = 0; c <= mState.mCurrentCursor; c++)
				SetCursorPosition(mState.mCursors[c].GetSelectionStart(), c);
		} else {
			for (int c = 0; c <= mState.mCurrentCursor; c++) {
				Coordinates newCoords = mState.mCursors[c].mInteractiveEnd;
				MoveCoords(newCoords, MoveDirection::Left, aWordMode);
				SetCursorPosition(newCoords, c, !aSelect);
			}
		}
		EnsureCursorVisible();
	}

	void TextEditor::MoveRight(bool aSelect, bool aWordMode) {
		if (mLines.empty())
			return;

		if (AnyCursorHasSelection() && !aSelect && !aWordMode) {
			for (int c = 0; c <= mState.mCurrentCursor; c++)
				SetCursorPosition(mState.mCursors[c].GetSelectionEnd(), c);
		} else {
			for (int c = 0; c <= mState.mCurrentCursor; c++) {
				Coordinates newCoords = mState.mCursors[c].mInteractiveEnd;
				MoveCoords(newCoords, MoveDirection::Right, aWordMode);
				SetCursorPosition(newCoords, c, !aSelect);
			}
		}
		EnsureCursorVisible();
	}

	void TextEditor::MoveTop(bool aSelect) {
		SetCursorPosition(Coordinates(0, 0), mState.mCurrentCursor, !aSelect);
	}

	void TextEditor::MoveBottom(bool aSelect) {
		int maxLine = (int)mLines.size() - 1;
		Coordinates newPos = Coordinates(maxLine, GetLineMaxColumn(maxLine));
		SetCursorPosition(newPos, mState.mCurrentCursor, !aSelect);
	}

	void TextEditor::MoveHome(bool aSelect) {
		for (int c = 0; c <= mState.mCurrentCursor; c++)
			SetCursorPosition(Coordinates(mState.mCursors[c].mInteractiveEnd.mLine, 0), c, !aSelect);
	}

	void TextEditor::MoveEnd(bool aSelect) {
		for (int c = 0; c <= mState.mCurrentCursor; c++) {
			int lindex = mState.mCursors[c].mInteractiveEnd.mLine;
			SetCursorPosition(Coordinates(lindex, GetLineMaxColumn(lindex)), c, !aSelect);
		}
	}

	void TextEditor::EnterCharacter(ImWchar aChar, bool aShift) {
		assert(!mReadOnly);

		bool hasSelection = AnyCursorHasSelection();
		bool anyCursorHasMultilineSelection = false;
		for (int c = mState.mCurrentCursor; c > -1; c--)
			if (mState.mCursors[c].GetSelectionStart().mLine != mState.mCursors[c].GetSelectionEnd().mLine) {
				anyCursorHasMultilineSelection = true;
				break;
			}
		// A Tab key press on a multi-line selection is treated as an indent/unindent operation,
		// not a character insertion; Shift+Tab decreases indentation.
		bool isIndentOperation = hasSelection && anyCursorHasMultilineSelection && aChar == '\t';
		if (isIndentOperation) {
			ChangeCurrentLinesIndentation(!aShift);
			return;
		}

		UndoRecord u;
		u.mBefore = mState;

		if (hasSelection) {
			for (int c = mState.mCurrentCursor; c > -1; c--) {
				u.mOperations.push_back({GetSelectedText(c), mState.mCursors[c].GetSelectionStart(), mState.mCursors[c].GetSelectionEnd(), UndoOperationType::Delete});
				DeleteSelection(c);
			}
		}

		// Cursors are processed newest-to-oldest (highest index first) so that inserting '\n'
		// on the same line from two cursors does not invalidate earlier cursor coordinates.
		std::vector<Coordinates> coords;
		for (int c = mState.mCurrentCursor; c > -1; c--) // order important here for typing \n in the same line at the same time
		{
			auto coord = GetSanitizedCursorCoordinates(c);
			coords.push_back(coord);
			UndoOperation added;
			added.mType = UndoOperationType::Add;
			added.mStart = coord;

			assert(!mLines.empty());

			if (aChar == '\n') {
				InsertLine(coord.mLine + 1);
				auto& line = mLines[coord.mLine];
				auto& newLine = mLines[coord.mLine + 1];

				added.mText = "";
				added.mText += (char)aChar;
				if (mAutoIndent)
					// Copy every leading ASCII whitespace character from the current line to the new line.
					for (int i = 0; i < (int)line.size() && isascii(line[i].mChar) && isblank(line[i].mChar); ++i) {
						newLine.push_back(line[i]);
						added.mText += line[i].mChar;
					}

				const size_t whitespaceSize = newLine.size();
				auto cindex = GetCharacterIndexR(coord);
				AddGlyphsToLine(coord.mLine + 1, newLine.size(), line.begin() + cindex, line.end());
				RemoveGlyphsFromLine(coord.mLine, cindex);
				SetCursorPosition(Coordinates(coord.mLine + 1, GetCharacterColumn(coord.mLine + 1, (int)whitespaceSize)), c);
			} else {
				char buf[7];
				int e = ImTextCharToUtf8(buf, 7, aChar);
				if (e > 0) {
					buf[e] = '\0';
					auto cindex = GetCharacterIndexR(coord);

					for (auto p = buf; *p != '\0'; p++, ++cindex)
						AddGlyphToLine(coord.mLine, cindex, Glyph(*p, PaletteIndex::Default));
					added.mText = buf;

					SetCursorPosition(Coordinates(coord.mLine, GetCharacterColumn(coord.mLine, cindex)), c);
				} else
					continue;
			}

			added.mEnd = GetSanitizedCursorCoordinates(c);
			u.mOperations.push_back(added);
		}

		u.mAfter = mState;
		AddUndo(u);

		for (const auto& coord : coords)
			Colorize(coord.mLine - 1, 3);
		EnsureCursorVisible();
	}

	void TextEditor::Backspace(bool aWordMode) {
		assert(!mReadOnly);

		if (mLines.empty())
			return;

		if (AnyCursorHasSelection())
			Delete(aWordMode);
		else {
			EditorState stateBeforeDeleting = mState;
			// Extend each cursor's selection one step to the left (or one word left) to create the range to delete.
			MoveLeft(true, aWordMode);
			// Guard: if any cursor could not move (e.g. already at {0,0}), abort the whole operation
			// so we never partially delete across cursors.
			if (!AllCursorsHaveSelection()) // can't do backspace if any cursor at {0,0}
			{
				if (AnyCursorHasSelection())
					MoveRight();
				return;
			}

			OnCursorPositionChanged(); // might combine cursors
			Delete(aWordMode, &stateBeforeDeleting);
		}
	}

	void TextEditor::Delete(bool aWordMode, const EditorState* aEditorState) {
		assert(!mReadOnly);

		if (mLines.empty())
			return;

		if (AnyCursorHasSelection()) {
			UndoRecord u;
			u.mBefore = aEditorState == nullptr ? mState : *aEditorState;
			for (int c = mState.mCurrentCursor; c > -1; c--) {
				if (!mState.mCursors[c].HasSelection())
					continue;
				u.mOperations.push_back({GetSelectedText(c), mState.mCursors[c].GetSelectionStart(), mState.mCursors[c].GetSelectionEnd(), UndoOperationType::Delete});
				DeleteSelection(c);
			}
			u.mAfter = mState;
			AddUndo(u);
		} else {
			EditorState stateBeforeDeleting = mState;
			// Extend each cursor one step to the right (or one word right) to create the range to delete.
			MoveRight(true, aWordMode);
			if (!AllCursorsHaveSelection()) // can't do delete if any cursor at end of last line
			{
				if (AnyCursorHasSelection())
					MoveLeft();
				return;
			}

			OnCursorPositionChanged(); // might combine cursors
			Delete(aWordMode, &stateBeforeDeleting);
		}
	}

	void TextEditor::SetSelection(Coordinates aStart, Coordinates aEnd, int aCursor) {
		if (aCursor == -1)
			aCursor = mState.mCurrentCursor;

		Coordinates minCoords = Coordinates(0, 0);
		int maxLine = (int)mLines.size() - 1;
		Coordinates maxCoords = Coordinates(maxLine, GetLineMaxColumn(maxLine));
		if (aStart < minCoords)
			aStart = minCoords;
		else if (aStart > maxCoords)
			aStart = maxCoords;
		if (aEnd < minCoords)
			aEnd = minCoords;
		else if (aEnd > maxCoords)
			aEnd = maxCoords;

		mState.mCursors[aCursor].mInteractiveStart = aStart;
		SetCursorPosition(aEnd, aCursor, false);
	}

	void TextEditor::SetSelection(int aStartLine, int aStartChar, int aEndLine, int aEndChar, int aCursor) {
		Coordinates startCoords = {aStartLine, GetCharacterColumn(aStartLine, aStartChar)};
		Coordinates endCoords = {aEndLine, GetCharacterColumn(aEndLine, aEndChar)};
		SetSelection(startCoords, endCoords, aCursor);
	}

	TextEditor::Coordinates TextEditor::GetSelectionStart(int aCursor) const {
		if (aCursor == -1)
			aCursor = mState.mCurrentCursor;
		return mState.mCursors[aCursor].GetSelectionStart();
	}

	TextEditor::Coordinates TextEditor::GetSelectionEnd(int aCursor) const

	{
		if (aCursor == -1)
			aCursor = mState.mCurrentCursor;
		return mState.mCursors[aCursor].GetSelectionEnd();
	}

	void TextEditor::SelectNextOccurrenceOf(const char* aText, int aTextSize, int aCursor, bool aCaseSensitive) {
		if (aCursor == -1)
			aCursor = mState.mCurrentCursor;
		Coordinates nextStart, nextEnd;
		FindNextOccurrence(aText, aTextSize, mState.mCursors[aCursor].mInteractiveEnd, nextStart, nextEnd, aCaseSensitive);
		SetSelection(nextStart, nextEnd, aCursor);
		EnsureCursorVisible(aCursor, true);
	}

	void TextEditor::AddCursorForNextOccurrence(bool aCaseSensitive) {
		const Cursor& currentCursor = mState.mCursors[mState.GetLastAddedCursorIndex()];
		if (currentCursor.GetSelectionStart() == currentCursor.GetSelectionEnd())
			return;

		std::string selectionText = GetText(currentCursor.GetSelectionStart(), currentCursor.GetSelectionEnd());
		Coordinates nextStart, nextEnd;
		if (!FindNextOccurrence(selectionText.c_str(), selectionText.length(), currentCursor.GetSelectionEnd(), nextStart, nextEnd, aCaseSensitive))
			return;

		mState.AddCursor();
		SetSelection(nextStart, nextEnd, mState.mCurrentCursor);
		mState.SortCursorsFromTopToBottom();
		MergeCursorsIfPossible();
		EnsureCursorVisible(-1, true);
	}

	bool TextEditor::FindNextOccurrence(const char* aText, int aTextSize, const Coordinates& aFrom, Coordinates& outStart, Coordinates& outEnd, bool aCaseSensitive) {
		assert(aTextSize > 0);
		int fline, ifline;
		int findex, ifindex;

		// ifline/ifindex record the starting position so we can detect a full wrap-around
		// and avoid looping forever when the text does not exist in the document.
		ifline = fline = aFrom.mLine;
		ifindex = findex = GetCharacterIndexR(aFrom);

		while (true) {
			bool matches;
			{ // match function
				int lineOffset = 0;
				int currentCharIndex = findex;
				int i = 0;
				for (; i < aTextSize; i++) {
					if (currentCharIndex == (int)mLines[fline + lineOffset].size()) {
						if (aText[i] == '\n' && fline + lineOffset + 1 < (int)mLines.size()) {
							currentCharIndex = 0;
							lineOffset++;
						} else
							break;
					} else {
						char toCompareA = mLines[fline + lineOffset][currentCharIndex].mChar;
						char toCompareB = aText[i];
						toCompareA = (!aCaseSensitive && toCompareA >= 'A' && toCompareA <= 'Z') ? toCompareA - 'A' + 'a' : toCompareA;
						toCompareB = (!aCaseSensitive && toCompareB >= 'A' && toCompareB <= 'Z') ? toCompareB - 'A' + 'a' : toCompareB;
						if (toCompareA != toCompareB)
							break;
						else
							currentCharIndex++;
					}
				}
				matches = i == aTextSize;
				if (matches) {
					outStart = {fline, GetCharacterColumn(fline, findex)};
					outEnd = {fline + lineOffset, GetCharacterColumn(fline + lineOffset, currentCharIndex)};
					return true;
				}
			}

			// move forward one character at a time; wrap at end of document back to the beginning
			if (findex == (int)mLines[fline].size()) // need to consider line breaks
			{
				if (fline == (int)mLines.size() - 1) {
					fline = 0;
					findex = 0;
				} else {
					fline++;
					findex = 0;
				}
			} else
				findex++;

			// detect complete scan: if we are back where we started the search found nothing
			if (findex == ifindex && fline == ifline)
				return false;
		}
	}

	bool TextEditor::FindMatchingBracket(int aLine, int aCharIndex, Coordinates& out) {
		if (aLine > (int)mLines.size() - 1)
			return false;
		int maxCharIndex = mLines[aLine].size() - 1;
		if (aCharIndex > maxCharIndex)
			return false;

		int currentLine = aLine;
		int currentCharIndex = aCharIndex;
		// counter tracks nesting depth; starts at 1 (for the bracket under the cursor) and
		// reaches 0 when the matching bracket is found.
		int counter = 1;
		if (CLOSE_TO_OPEN_CHAR.find(mLines[aLine][aCharIndex].mChar) != CLOSE_TO_OPEN_CHAR.end()) {
			// Cursor is on a closing bracket: scan leftward for the matching opener.
			char closeChar = mLines[aLine][aCharIndex].mChar;
			char openChar = CLOSE_TO_OPEN_CHAR.at(closeChar);
			while (Move(currentLine, currentCharIndex, true)) {
				if (currentCharIndex < (int)mLines[currentLine].size()) {
					char currentChar = mLines[currentLine][currentCharIndex].mChar;
					if (currentChar == openChar) {
						counter--;
						if (counter == 0) {
							out = {currentLine, GetCharacterColumn(currentLine, currentCharIndex)};
							return true;
						}
					} else if (currentChar == closeChar)
						counter++;
				}
			}
		} else if (OPEN_TO_CLOSE_CHAR.find(mLines[aLine][aCharIndex].mChar) != OPEN_TO_CLOSE_CHAR.end()) {
			// Cursor is on an opening bracket: scan rightward for the matching closer.
			char openChar = mLines[aLine][aCharIndex].mChar;
			char closeChar = OPEN_TO_CLOSE_CHAR.at(openChar);
			while (Move(currentLine, currentCharIndex)) {
				if (currentCharIndex < (int)mLines[currentLine].size()) {
					char currentChar = mLines[currentLine][currentCharIndex].mChar;
					if (currentChar == closeChar) {
						counter--;
						if (counter == 0) {
							out = {currentLine, GetCharacterColumn(currentLine, currentCharIndex)};
							return true;
						}
					} else if (currentChar == openChar)
						counter++;
				}
			}
		}
		return false;
	}

	void TextEditor::ChangeCurrentLinesIndentation(bool aIncrease) {
		assert(!mReadOnly);

		UndoRecord u;
		u.mBefore = mState;

		for (int c = mState.mCurrentCursor; c > -1; c--) {
			for (int currentLine = mState.mCursors[c].GetSelectionEnd().mLine; currentLine >= mState.mCursors[c].GetSelectionStart().mLine; currentLine--) {
				if (Coordinates{currentLine, 0} == mState.mCursors[c].GetSelectionEnd() &&
					mState.mCursors[c].GetSelectionEnd() != mState.mCursors[c].GetSelectionStart()) // when selection ends at line start
					continue;

				if (aIncrease) {
					if (mLines[currentLine].size() > 0) {
						Coordinates lineStart = {currentLine, 0};
						Coordinates insertionEnd = lineStart;
						InsertTextAt(insertionEnd, "\t"); // sets insertion end
						u.mOperations.push_back({"\t", lineStart, insertionEnd, UndoOperationType::Add});
						Colorize(lineStart.mLine, 1);
					}
				} else {
					Coordinates start = {currentLine, 0};
					Coordinates end = {currentLine, mTabSize};
					int charIndex = GetCharacterIndexL(end) - 1;
					while (charIndex > -1 && (mLines[currentLine][charIndex].mChar == ' ' || mLines[currentLine][charIndex].mChar == '\t'))
						charIndex--;
					bool onlySpaceCharactersFound = charIndex == -1;
					if (onlySpaceCharactersFound) {
						u.mOperations.push_back({GetText(start, end), start, end, UndoOperationType::Delete});
						DeleteRange(start, end);
						Colorize(currentLine, 1);
					}
				}
			}
		}

		if (u.mOperations.size() > 0)
			AddUndo(u);
	}

	void TextEditor::MoveUpCurrentLines() {
		assert(!mReadOnly);

		UndoRecord u;
		u.mBefore = mState;

		std::set<int> affectedLines;
		int minLine = -1;
		int maxLine = -1;
		for (int c = mState.mCurrentCursor; c > -1; c--) // cursors are expected to be sorted from top to bottom
		{
			for (int currentLine = mState.mCursors[c].GetSelectionEnd().mLine; currentLine >= mState.mCursors[c].GetSelectionStart().mLine; currentLine--) {
				if (Coordinates{currentLine, 0} == mState.mCursors[c].GetSelectionEnd() &&
					mState.mCursors[c].GetSelectionEnd() != mState.mCursors[c].GetSelectionStart()) // when selection ends at line start
					continue;
				affectedLines.insert(currentLine);
				minLine = minLine == -1 ? currentLine : (currentLine < minLine ? currentLine : minLine);
				maxLine = maxLine == -1 ? currentLine : (currentLine > maxLine ? currentLine : maxLine);
			}
		}
		if (minLine == 0) // can't move up anymore
			return;

		// Record old text before swapping so undo can reconstruct the original order.
		Coordinates start = {minLine - 1, 0};
		Coordinates end = {maxLine, GetLineMaxColumn(maxLine)};
		u.mOperations.push_back({GetText(start, end), start, end, UndoOperationType::Delete});

		// Shift each affected line one position up by swapping it with its predecessor.
		for (int line : affectedLines) // lines should be sorted here
			std::swap(mLines[line - 1], mLines[line]);
		for (int c = mState.mCurrentCursor; c > -1; c--) {
			mState.mCursors[c].mInteractiveStart.mLine -= 1;
			mState.mCursors[c].mInteractiveEnd.mLine -= 1;
			// no need to set mCursorPositionChanged as cursors will remain sorted
		}

		end = {maxLine, GetLineMaxColumn(maxLine)}; // this line is now swapped with the line above, recompute max column
		u.mOperations.push_back({GetText(start, end), start, end, UndoOperationType::Add});
		u.mAfter = mState;
		AddUndo(u);
	}

	void TextEditor::MoveDownCurrentLines() {
		assert(!mReadOnly);

		UndoRecord u;
		u.mBefore = mState;

		std::set<int> affectedLines;
		int minLine = -1;
		int maxLine = -1;
		for (int c = 0; c <= mState.mCurrentCursor; c++) // cursors are expected to be sorted from top to bottom
		{
			for (int currentLine = mState.mCursors[c].GetSelectionEnd().mLine; currentLine >= mState.mCursors[c].GetSelectionStart().mLine; currentLine--) {
				if (Coordinates{currentLine, 0} == mState.mCursors[c].GetSelectionEnd() &&
					mState.mCursors[c].GetSelectionEnd() != mState.mCursors[c].GetSelectionStart()) // when selection ends at line start
					continue;
				affectedLines.insert(currentLine);
				minLine = minLine == -1 ? currentLine : (currentLine < minLine ? currentLine : minLine);
				maxLine = maxLine == -1 ? currentLine : (currentLine > maxLine ? currentLine : maxLine);
			}
		}
		if (maxLine == (int)mLines.size() - 1) // can't move down anymore
			return;

		// Record old text before swapping so undo can reconstruct the original order.
		Coordinates start = {minLine, 0};
		Coordinates end = {maxLine + 1, GetLineMaxColumn(maxLine + 1)};
		u.mOperations.push_back({GetText(start, end), start, end, UndoOperationType::Delete});

		// Shift each affected line one position down by swapping it with its successor (reverse order).
		std::set<int>::reverse_iterator rit;
		for (rit = affectedLines.rbegin(); rit != affectedLines.rend(); rit++) // lines should be sorted here
			std::swap(mLines[*rit + 1], mLines[*rit]);
		for (int c = mState.mCurrentCursor; c > -1; c--) {
			mState.mCursors[c].mInteractiveStart.mLine += 1;
			mState.mCursors[c].mInteractiveEnd.mLine += 1;
			// no need to set mCursorPositionChanged as cursors will remain sorted
		}

		end = {maxLine + 1, GetLineMaxColumn(maxLine + 1)}; // this line is now swapped with the line below, recompute max column
		u.mOperations.push_back({GetText(start, end), start, end, UndoOperationType::Add});
		u.mAfter = mState;
		AddUndo(u);
	}

	void TextEditor::ToggleLineComment() {
		assert(!mReadOnly);
		if (mLanguageDefinition == nullptr)
			return;
		const std::string& commentString = mLanguageDefinition->mSingleLineComment;

		UndoRecord u;
		u.mBefore = mState;

		// First pass: determine whether at least one affected line lacks a comment prefix.
		// If so, we add comments to all lines; otherwise we remove them from all lines.
		bool shouldAddComment = false;
		std::unordered_set<int> affectedLines;
		for (int c = mState.mCurrentCursor; c > -1; c--) {
			for (int currentLine = mState.mCursors[c].GetSelectionEnd().mLine; currentLine >= mState.mCursors[c].GetSelectionStart().mLine; currentLine--) {
				if (Coordinates{currentLine, 0} == mState.mCursors[c].GetSelectionEnd() &&
					mState.mCursors[c].GetSelectionEnd() != mState.mCursors[c].GetSelectionStart()) // when selection ends at line start
					continue;
				affectedLines.insert(currentLine);
				int currentIndex = 0;
				while (currentIndex < (int)mLines[currentLine].size() && (mLines[currentLine][currentIndex].mChar == ' ' || mLines[currentLine][currentIndex].mChar == '\t'))
					currentIndex++;
				if (currentIndex == (int)mLines[currentLine].size())
					continue;
				int i = 0;
				while (i < (int)commentString.length() && currentIndex + i < (int)mLines[currentLine].size() && mLines[currentLine][currentIndex + i].mChar == commentString[i])
					i++;
				bool matched = i == (int)commentString.length();
				shouldAddComment |= !matched;
			}
		}

		if (shouldAddComment) {
			// Second pass (add): prepend "commentString " at the start of each affected line.
			for (int currentLine : affectedLines) // order doesn't matter as changes are not multiline
			{
				Coordinates lineStart = {currentLine, 0};
				Coordinates insertionEnd = lineStart;
				InsertTextAt(insertionEnd, (commentString + ' ').c_str()); // sets insertion end
				u.mOperations.push_back({(commentString + ' '), lineStart, insertionEnd, UndoOperationType::Add});
				Colorize(lineStart.mLine, 1);
			}
		} else {
			// Second pass (remove): strip the comment token (and optional trailing space) from each line.
			for (int currentLine : affectedLines) // order doesn't matter as changes are not multiline
			{
				int currentIndex = 0;
				while (currentIndex < (int)mLines[currentLine].size() && (mLines[currentLine][currentIndex].mChar == ' ' || mLines[currentLine][currentIndex].mChar == '\t'))
					currentIndex++;
				if (currentIndex == (int)mLines[currentLine].size())
					continue;
				int i = 0;
				while (i < (int)commentString.length() && currentIndex + i < (int)mLines[currentLine].size() && mLines[currentLine][currentIndex + i].mChar == commentString[i])
					i++;
				[[maybe_unused]] bool matched = i == (int)commentString.length();
				assert(matched);
				if (currentIndex + i < (int)mLines[currentLine].size() && mLines[currentLine][currentIndex + i].mChar == ' ')
					i++;

				Coordinates start = {currentLine, GetCharacterColumn(currentLine, currentIndex)};
				Coordinates end = {currentLine, GetCharacterColumn(currentLine, currentIndex + i)};
				u.mOperations.push_back({GetText(start, end), start, end, UndoOperationType::Delete});
				DeleteRange(start, end);
				Colorize(currentLine, 1);
			}
		}

		u.mAfter = mState;
		AddUndo(u);
	}

	void TextEditor::RemoveCurrentLines() {
		UndoRecord u;
		u.mBefore = mState;

		// First, delete any existing selections so cursors collapse to a single position per line.
		if (AnyCursorHasSelection()) {
			for (int c = mState.mCurrentCursor; c > -1; c--) {
				if (!mState.mCursors[c].HasSelection())
					continue;
				u.mOperations.push_back({GetSelectedText(c), mState.mCursors[c].GetSelectionStart(), mState.mCursors[c].GetSelectionEnd(), UndoOperationType::Delete});
				DeleteSelection(c);
			}
		}
		MoveHome();
		OnCursorPositionChanged(); // might combine cursors

		// Second, delete the full line for each remaining cursor, preferring to remove the line break
		// toward the next line so the cursor stays at the same visual row.
		for (int c = mState.mCurrentCursor; c > -1; c--) {
			int currentLine = mState.mCursors[c].mInteractiveEnd.mLine;
			int nextLine = currentLine + 1;
			int prevLine = currentLine - 1;

			Coordinates toDeleteStart, toDeleteEnd;
			if ((int)mLines.size() > nextLine) // next line exists
			{
				toDeleteStart = Coordinates(currentLine, 0);
				toDeleteEnd = Coordinates(nextLine, 0);
				SetCursorPosition({mState.mCursors[c].mInteractiveEnd.mLine, 0}, c);
			} else if (prevLine > -1) // previous line exists
			{
				toDeleteStart = Coordinates(prevLine, GetLineMaxColumn(prevLine));
				toDeleteEnd = Coordinates(currentLine, GetLineMaxColumn(currentLine));
				SetCursorPosition({prevLine, 0}, c);
			} else {
				toDeleteStart = Coordinates(currentLine, 0);
				toDeleteEnd = Coordinates(currentLine, GetLineMaxColumn(currentLine));
				SetCursorPosition({currentLine, 0}, c);
			}

			u.mOperations.push_back({GetText(toDeleteStart, toDeleteEnd), toDeleteStart, toDeleteEnd, UndoOperationType::Delete});

			std::unordered_set<int> handledCursors = {c};
			if (toDeleteStart.mLine != toDeleteEnd.mLine)
				RemoveLine(currentLine, &handledCursors);
			else
				DeleteRange(toDeleteStart, toDeleteEnd);
		}

		u.mAfter = mState;
		AddUndo(u);
	}

	float TextEditor::TextDistanceToLineStart(const Coordinates& aFrom, bool aSanitizeCoords) const {
		if (aSanitizeCoords)
			return SanitizeCoordinates(aFrom).mColumn * mCharAdvance.x;
		else
			return aFrom.mColumn * mCharAdvance.x;
	}

	void TextEditor::EnsureCursorVisible(int aCursor, bool aStartToo) {
		if (aCursor == -1)
			aCursor = mState.GetLastAddedCursorIndex();

		mEnsureCursorVisible = aCursor;
		mEnsureCursorVisibleStartToo = aStartToo;
		return;
	}

	TextEditor::Coordinates TextEditor::SanitizeCoordinates(const Coordinates& aValue) const {
		// Clamp line and column into valid document bounds.
		auto line = std::max(aValue.mLine, 0);
		auto column = std::max(aValue.mColumn, 0);
		Coordinates out;
		if (line >= (int)mLines.size()) {
			if (mLines.empty()) {
				line = 0;
				column = 0;
			} else {
				line = (int)mLines.size() - 1;
				column = GetLineMaxColumn(line);
			}
			out = Coordinates(line, column);
		} else {
			column = mLines.empty() ? 0 : GetLineMaxColumn(line, column);
			out = Coordinates(line, column);
		}

		// Snap columns that land inside a tab cell to the nearest tab edge (left or right).
		int charIndex = GetCharacterIndexL(out);
		if (charIndex > -1 && charIndex < (int)mLines[out.mLine].size() && mLines[out.mLine][charIndex].mChar == '\t') {
			int columnToLeft = GetCharacterColumn(out.mLine, charIndex);
			int columnToRight = GetCharacterColumn(out.mLine, GetCharacterIndexR(out));
			if (out.mColumn - columnToLeft <= columnToRight - out.mColumn)
				out.mColumn = columnToLeft;
			else
				out.mColumn = columnToRight;
		}
		return out;
	}

	TextEditor::Coordinates TextEditor::GetSanitizedCursorCoordinates(int aCursor, bool aStart) const {
		aCursor = aCursor == -1 ? mState.mCurrentCursor : aCursor;
		return SanitizeCoordinates(aStart ? mState.mCursors[aCursor].mInteractiveStart : mState.mCursors[aCursor].mInteractiveEnd);
	}

	TextEditor::Coordinates TextEditor::ScreenPosToCoordinates(const ImVec2& aPosition, bool* isOverLineNumber) const {
		// mLastRenderOrigin is the authoritative render-time origin (captured in Render(bool)).
		// Using GetCursorScreenPos() here would give the wrong origin between frames.
		ImVec2 local(aPosition.x - mLastRenderOrigin.x + 3.0f, aPosition.y - mLastRenderOrigin.y);

		if (isOverLineNumber != nullptr)
			*isOverLineNumber = local.x < mTextStart;

		// Convert the click Y (in document scroll-space: local.y + mScrollY) to a line number
		// using mLineTopYCache, which is the same per-line Y table built by Render().
		// This correctly accounts for codelens lane heights inserted above each line.
		int line = mFirstVisibleLine;
		float localScrollY = 0.0f;
		if (!mLines.empty() && !mLineTopYCache.empty()) {
			// localY is the position relative to the document top (scroll-space).
			localScrollY = local.y + mScrollY;
			// upper_bound gives the first entry strictly greater than localScrollY;
			// stepping back one gives the last line whose top is at or before the click Y.
			const int lineCount = (int)mLines.size();
			auto it = std::upper_bound(mLineTopYCache.begin(), mLineTopYCache.begin() + lineCount, localScrollY);
			if (it != mLineTopYCache.begin())
				--it;
			line = (int)(it - mLineTopYCache.begin());
			line = std::max(0, std::min(line, lineCount - 1));
		}
		Coordinates out;
		out.mLine = line;
		out.mColumn = std::max(0, (int)floor((local.x - mTextStart + POS_TO_COORDS_COLUMN_OFFSET * mCharAdvance.x) / mCharAdvance.x));

		return SanitizeCoordinates(out);
	}

	TextEditor::Coordinates TextEditor::FindWordStart(const Coordinates& aFrom) const {
		if (aFrom.mLine >= (int)mLines.size())
			return aFrom;

		int lineIndex = aFrom.mLine;
		auto& line = mLines[lineIndex];
		int charIndex = GetCharacterIndexL(aFrom);

		if (charIndex > (int)line.size() || line.size() == 0)
			return aFrom;
		if (charIndex == (int)line.size())
			charIndex--;

		bool initialIsWordChar = CharIsWordChar(line[charIndex].mChar);
		bool initialIsSpace = char_isspace(line[charIndex].mChar);
		char initialChar = line[charIndex].mChar;
		while (Move(lineIndex, charIndex, true, true)) {
			bool isWordChar = CharIsWordChar(line[charIndex].mChar);
			bool isSpace = char_isspace(line[charIndex].mChar);
			if ((initialIsSpace && !isSpace) || (initialIsWordChar && !isWordChar) || (!initialIsWordChar && !initialIsSpace && initialChar != line[charIndex].mChar)) {
				Move(lineIndex, charIndex, false, true); // one step to the right
				break;
			}
		}
		return {aFrom.mLine, GetCharacterColumn(aFrom.mLine, charIndex)};
	}

	TextEditor::Coordinates TextEditor::FindWordEnd(const Coordinates& aFrom) const {
		if (aFrom.mLine >= (int)mLines.size())
			return aFrom;

		int lineIndex = aFrom.mLine;
		auto& line = mLines[lineIndex];
		auto charIndex = GetCharacterIndexL(aFrom);

		if (charIndex >= (int)line.size())
			return aFrom;

		bool initialIsWordChar = CharIsWordChar(line[charIndex].mChar);
		bool initialIsSpace = char_isspace(line[charIndex].mChar);
		char initialChar = line[charIndex].mChar;
		while (Move(lineIndex, charIndex, false, true)) {
			if (charIndex == (int)line.size())
				break;
			bool isWordChar = CharIsWordChar(line[charIndex].mChar);
			bool isSpace = char_isspace(line[charIndex].mChar);
			if ((initialIsSpace && !isSpace) || (initialIsWordChar && !isWordChar) || (!initialIsWordChar && !initialIsSpace && initialChar != line[charIndex].mChar))
				break;
		}
		return {lineIndex, GetCharacterColumn(aFrom.mLine, charIndex)};
	}

	int TextEditor::GetCharacterIndexL(const Coordinates& aCoords) const {
		// Returns the byte index of the glyph whose visual column range CONTAINS aCoords.mColumn.
		// When the column falls inside a tab, this returns the index of the tab glyph itself (left edge).
		if (aCoords.mLine >= (int)mLines.size())
			return -1;

		auto& line = mLines[aCoords.mLine];
		int c = 0;
		int i = 0;
		int tabCoordsLeft = 0;

		for (; i < (int)line.size() && c < aCoords.mColumn;) {
			if (line[i].mChar == '\t') {
				if (tabCoordsLeft == 0)
					tabCoordsLeft = TabSizeAtColumn(c);
				if (tabCoordsLeft > 0)
					tabCoordsLeft--;
				c++;
			} else
				++c;
			if (tabCoordsLeft == 0)
				i += UTF8CharLength(line[i].mChar);
		}
		return i;
	}

	int TextEditor::GetCharacterIndexR(const Coordinates& aCoords) const {
		// Returns the byte index ONE PAST the glyph at aCoords.mColumn (the right edge / insertion point).
		// Unlike GetCharacterIndexL, when the column falls inside a tab this returns the index after the tab.
		if (aCoords.mLine >= (int)mLines.size())
			return -1;
		int c = 0;
		int i = 0;
		for (; i < (int)mLines[aCoords.mLine].size() && c < aCoords.mColumn;)
			MoveCharIndexAndColumn(aCoords.mLine, i, c);
		return i;
	}

	int TextEditor::GetCharacterColumn(int aLine, int aIndex) const {
		if (aLine >= (int)mLines.size())
			return 0;
		int c = 0;
		int i = 0;
		while (i < aIndex && i < (int)mLines[aLine].size())
			MoveCharIndexAndColumn(aLine, i, c);
		return c;
	}

	int TextEditor::GetFirstVisibleCharacterIndex(int aLine) const {
		if (aLine >= (int)mLines.size())
			return 0;
		int c = 0;
		int i = 0;
		while (c < mFirstVisibleColumn && i < (int)mLines[aLine].size())
			MoveCharIndexAndColumn(aLine, i, c);
		if (c > mFirstVisibleColumn)
			i--;
		return i;
	}

	int TextEditor::GetLineMaxColumn(int aLine, int aLimit) const {
		if (aLine >= (int)mLines.size())
			return 0;
		int c = 0;
		if (aLimit == -1) {
			for (int i = 0; i < (int)mLines[aLine].size();)
				MoveCharIndexAndColumn(aLine, i, c);
		} else {
			for (int i = 0; i < (int)mLines[aLine].size();) {
				MoveCharIndexAndColumn(aLine, i, c);
				if (c > aLimit)
					return aLimit;
			}
		}
		return c;
	}

	TextEditor::Line& TextEditor::InsertLine(int aIndex) {
		assert(!mReadOnly);
		auto& result = *mLines.insert(mLines.begin() + aIndex, Line());
		InvalidateLineMetadataCacheFromLine(aIndex);

		for (int c = 0; c <= mState.mCurrentCursor; c++) // handle multiple cursors
		{
			if (mState.mCursors[c].mInteractiveEnd.mLine >= aIndex)
				SetCursorPosition({mState.mCursors[c].mInteractiveEnd.mLine + 1, mState.mCursors[c].mInteractiveEnd.mColumn}, c);
		}

		return result;
	}

	void TextEditor::RemoveLine(int aIndex, const std::unordered_set<int>* aHandledCursors) {
		assert(!mReadOnly);
		assert(mLines.size() > 1);

		mLines.erase(mLines.begin() + aIndex);
		InvalidateLineMetadataCacheFromLine(aIndex);
		assert(!mLines.empty());

		// handle multiple cursors
		for (int c = 0; c <= mState.mCurrentCursor; c++) {
			if (mState.mCursors[c].mInteractiveEnd.mLine >= aIndex) {
				if (aHandledCursors == nullptr || aHandledCursors->find(c) == aHandledCursors->end()) // move up if has not been handled already
					SetCursorPosition({mState.mCursors[c].mInteractiveEnd.mLine - 1, mState.mCursors[c].mInteractiveEnd.mColumn}, c);
			}
		}
	}

	void TextEditor::RemoveLines(int aStart, int aEnd) {
		assert(!mReadOnly);
		assert(aEnd >= aStart);
		assert(mLines.size() > (size_t)(aEnd - aStart));

		mLines.erase(mLines.begin() + aStart, mLines.begin() + aEnd);
		InvalidateLineMetadataCacheFromLine(aStart);
		assert(!mLines.empty());

		// handle multiple cursors
		for (int c = 0; c <= mState.mCurrentCursor; c++) {
			if (mState.mCursors[c].mInteractiveEnd.mLine >= aStart) {
				int targetLine = mState.mCursors[c].mInteractiveEnd.mLine - (aEnd - aStart);
				targetLine = targetLine < 0 ? 0 : targetLine;
				mState.mCursors[c].mInteractiveEnd.mLine = targetLine;
			}
			if (mState.mCursors[c].mInteractiveStart.mLine >= aStart) {
				int targetLine = mState.mCursors[c].mInteractiveStart.mLine - (aEnd - aStart);
				targetLine = targetLine < 0 ? 0 : targetLine;
				mState.mCursors[c].mInteractiveStart.mLine = targetLine;
			}
		}
	}

	void TextEditor::DeleteRange(const Coordinates& aStart, const Coordinates& aEnd) {
		assert(aEnd >= aStart);
		assert(!mReadOnly);

		if (aEnd == aStart)
			return;

		auto start = GetCharacterIndexL(aStart);
		auto end = GetCharacterIndexR(aEnd);

		if (aStart.mLine == aEnd.mLine) {
			// Same-line deletion: simply erase the glyph range.
			auto n = GetLineMaxColumn(aStart.mLine);
			if (aEnd.mColumn >= n)
				RemoveGlyphsFromLine(aStart.mLine, start); // from start to end of line
			else
				RemoveGlyphsFromLine(aStart.mLine, start, end);
		} else {
			// Multi-line deletion: strip the tail of the first line and the head of the last line,
			// then append the remainder of the last line onto the first line, and remove intermediate lines.
			RemoveGlyphsFromLine(aStart.mLine, start); // from start to end of line
			RemoveGlyphsFromLine(aEnd.mLine, 0, end);
			auto& firstLine = mLines[aStart.mLine];
			auto& lastLine = mLines[aEnd.mLine];

			if (aStart.mLine < aEnd.mLine) {
				AddGlyphsToLine(aStart.mLine, firstLine.size(), lastLine.begin() + 0, lastLine.end());
				for (int c = 0; c <= mState.mCurrentCursor; c++) // move up cursors in line that is being moved up
				{
					// if cursor is selecting the same range we are deleting, it's because this is being called from
					// DeleteSelection which already sets the cursor position after the range is deleted
					if (mState.mCursors[c].GetSelectionStart() == aStart && mState.mCursors[c].GetSelectionEnd() == aEnd)
						continue;
					if (mState.mCursors[c].mInteractiveEnd.mLine > aEnd.mLine)
						break;
					else if (mState.mCursors[c].mInteractiveEnd.mLine != aEnd.mLine)
						continue;
					int otherCursorEndCharIndex = GetCharacterIndexR(mState.mCursors[c].mInteractiveEnd);
					int otherCursorStartCharIndex = GetCharacterIndexR(mState.mCursors[c].mInteractiveStart);
					int otherCursorNewEndCharIndex = GetCharacterIndexR(aStart) + otherCursorEndCharIndex;
					int otherCursorNewStartCharIndex = GetCharacterIndexR(aStart) + otherCursorStartCharIndex;
					auto targetEndCoords = Coordinates(aStart.mLine, GetCharacterColumn(aStart.mLine, otherCursorNewEndCharIndex));
					auto targetStartCoords = Coordinates(aStart.mLine, GetCharacterColumn(aStart.mLine, otherCursorNewStartCharIndex));
					SetCursorPosition(targetStartCoords, c, true);
					SetCursorPosition(targetEndCoords, c, false);
				}
				RemoveLines(aStart.mLine + 1, aEnd.mLine + 1);
			}
		}
	}

	void TextEditor::DeleteSelection(int aCursor) {
		if (aCursor == -1)
			aCursor = mState.mCurrentCursor;

		if (mState.mCursors[aCursor].GetSelectionEnd() == mState.mCursors[aCursor].GetSelectionStart())
			return;

		Coordinates newCursorPos = mState.mCursors[aCursor].GetSelectionStart();
		DeleteRange(newCursorPos, mState.mCursors[aCursor].GetSelectionEnd());
		SetCursorPosition(newCursorPos, aCursor);
		Colorize(newCursorPos.mLine, 1);
	}

	void TextEditor::RemoveGlyphsFromLine(int aLine, int aStartChar, int aEndChar) {
		int column = GetCharacterColumn(aLine, aStartChar);
		auto& line = mLines[aLine];
		OnLineChanged(true, aLine, column, aEndChar - aStartChar, true);
		line.erase(line.begin() + aStartChar, aEndChar == -1 ? line.end() : line.begin() + aEndChar);
		OnLineChanged(false, aLine, column, aEndChar - aStartChar, true);
	}

	void TextEditor::AddGlyphsToLine(int aLine, int aTargetIndex, Line::iterator aSourceStart, Line::iterator aSourceEnd) {
		int targetColumn = GetCharacterColumn(aLine, aTargetIndex);
		int charsInserted = std::distance(aSourceStart, aSourceEnd);
		auto& line = mLines[aLine];
		OnLineChanged(true, aLine, targetColumn, charsInserted, false);
		line.insert(line.begin() + aTargetIndex, aSourceStart, aSourceEnd);
		OnLineChanged(false, aLine, targetColumn, charsInserted, false);
	}

	void TextEditor::AddGlyphToLine(int aLine, int aTargetIndex, Glyph aGlyph) {
		int targetColumn = GetCharacterColumn(aLine, aTargetIndex);
		auto& line = mLines[aLine];
		OnLineChanged(true, aLine, targetColumn, 1, false);
		line.insert(line.begin() + aTargetIndex, aGlyph);
		OnLineChanged(false, aLine, targetColumn, 1, false);
	}

	ImU32 TextEditor::GetGlyphColor(const Glyph& aGlyph) const {
		if (mLanguageDefinition == nullptr)
			return mPalette[(int)PaletteIndex::Default];
		if (aGlyph.mComment)
			return mPalette[(int)PaletteIndex::Comment];
		if (aGlyph.mMultiLineComment)
			return mPalette[(int)PaletteIndex::MultiLineComment];
		auto const color = mPalette[(int)aGlyph.mColorIndex];
		if (aGlyph.mPreprocessor) {
			const auto ppcolor = mPalette[(int)PaletteIndex::Preprocessor];
			const int c0 = ((ppcolor & 0xff) + (color & 0xff)) / 2;
			const int c1 = (((ppcolor >> 8) & 0xff) + ((color >> 8) & 0xff)) / 2;
			const int c2 = (((ppcolor >> 16) & 0xff) + ((color >> 16) & 0xff)) / 2;
			const int c3 = (((ppcolor >> 24) & 0xff) + ((color >> 24) & 0xff)) / 2;
			return ImU32(c0 | (c1 << 8) | (c2 << 16) | (c3 << 24));
		}
		return color;
	}

	void TextEditor::HandleKeyboardInputs(bool aParentIsFocused) {
		if (!ImGui::IsWindowFocused() && !aParentIsFocused)
			return;
		// Do not steal keyboard input when any popup (modal, context menu, input dialog) is open
		// on top of this window. Those popups own keyboard focus and must receive input undisturbed.
		if (GImGui->OpenPopupStack.Size > 0)
			return;
		if (ImGui::IsWindowHovered()) {
			ImVec2 mousePos = ImGui::GetMousePos();
			ImVec2 contentMin = ImGui::GetWindowPos();
			ImVec2 contentMax = ImVec2(contentMin.x + ImGui::GetWindowWidth() - (IsVerticalScrollbarVisible() ? ImGui::GetStyle().ScrollbarSize : 0.0f),
									   contentMin.y + ImGui::GetWindowHeight() - (IsHorizontalScrollbarVisible() ? ImGui::GetStyle().ScrollbarSize : 0.0f));
			if (mousePos.x >= contentMin.x && mousePos.x < contentMax.x && mousePos.y >= contentMin.y && mousePos.y < contentMax.y)
				ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
			else
				ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
		}
		ImGuiIO& io = ImGui::GetIO();
		auto isOSX = io.ConfigMacOSXBehaviors;
		auto alt = io.KeyAlt;
		auto ctrl = io.KeyCtrl;
		auto shift = io.KeyShift;
		auto super = io.KeySuper;
		auto isShortcut = (isOSX ? (super && !ctrl) : (ctrl && !super)) && !alt && !shift;
		auto isShiftShortcut = (isOSX ? (super && !ctrl) : (ctrl && !super)) && shift && !alt;
		auto isWordmoveKey = isOSX ? alt : ctrl;
		auto isAltOnly = alt && !ctrl && !shift && !super;
		auto isCtrlOnly = ctrl && !alt && !shift && !super;
		auto isShiftOnly = shift && !alt && !ctrl && !super;
		//
		// Undo / Redo
		//
		if (!mReadOnly && isShortcut && ImGui::IsKeyPressed(ImGuiKey_Z))
			Undo();
		else if (!mReadOnly && isAltOnly && ImGui::IsKeyPressed(ImGuiKey_Backspace))
			Undo();
		else if (!mReadOnly && isShortcut && ImGui::IsKeyPressed(ImGuiKey_Y))
			Redo();
		else if (!mReadOnly && isShiftShortcut && ImGui::IsKeyPressed(ImGuiKey_Z))
			Redo();
		//
		// Cursor movement
		//
		else if (!alt && !ctrl && !super && (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_Keypad8))) {
			mAutocompleteEnabled = true;
			MoveUp(1, shift);
		} else if (!alt && !ctrl && !super && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_Keypad2))) {
			mAutocompleteEnabled = true;
			MoveDown(1, shift);
		} else if ((isOSX ? !ctrl : !alt) && !super && (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_Keypad4))) {
			mAutocompleteEnabled = true;
			MoveLeft(shift, isWordmoveKey);
		} else if ((isOSX ? !ctrl : !alt) && !super && (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_Keypad6))) {
			mAutocompleteEnabled = true;
			MoveRight(shift, isWordmoveKey);
		} else if (!alt && !ctrl && !super && (ImGui::IsKeyPressed(ImGuiKey_PageUp) || ImGui::IsKeyPressed(ImGuiKey_Keypad9))) {
			int pageAmount = std::max(1, mVisibleLineCount - 2);
			mAutocompleteEnabled = true;
			ImGui::SetScrollY(mScrollY = std::max(0.0f, mScrollY - pageAmount * mCharAdvance.y));
			MoveUp(pageAmount, shift);
		} else if (!alt && !ctrl && !super && (ImGui::IsKeyPressed(ImGuiKey_PageDown) || ImGui::IsKeyPressed(ImGuiKey_Keypad3))) {
			int pageAmount = std::max(1, mVisibleLineCount - 2);
			mAutocompleteEnabled = true;
			ImGui::SetScrollY(mScrollY = mScrollY + pageAmount * mCharAdvance.y);
			MoveDown(pageAmount, shift);
		} else if (ctrl && !alt && !super && (ImGui::IsKeyPressed(ImGuiKey_Home) || ImGui::IsKeyPressed(ImGuiKey_Keypad7))) {
			mAutocompleteEnabled = true;
			MoveTop(shift);
		} else if (ctrl && !alt && !super && (ImGui::IsKeyPressed(ImGuiKey_End) || ImGui::IsKeyPressed(ImGuiKey_Keypad1))) {
			mAutocompleteEnabled = true;
			MoveBottom(shift);
		} else if (!alt && !ctrl && !super && (ImGui::IsKeyPressed(ImGuiKey_Home) || ImGui::IsKeyPressed(ImGuiKey_Keypad7))) {
			mAutocompleteEnabled = true;
			MoveHome(shift);
		} else if (!alt && !ctrl && !super && (ImGui::IsKeyPressed(ImGuiKey_End) || ImGui::IsKeyPressed(ImGuiKey_Keypad1))) {
			mAutocompleteEnabled = true;
			MoveEnd(shift);
			//
			// Delete / Backspace
			//
		} else if (!mReadOnly && !alt && !shift && !super && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_KeypadDecimal))) {
			mAutocompleteEnabled = true;
			Delete(ctrl);
		} else if (!mReadOnly && !alt && !shift && !super && ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
			mAutocompleteEnabled = true;
			Backspace(ctrl);
			//
			// Line operations
			//
		} else if (!mReadOnly && !alt && ctrl && shift && !super && ImGui::IsKeyPressed(ImGuiKey_K))
			RemoveCurrentLines();
		else if (!mReadOnly && !alt && ctrl && !shift && !super && ImGui::IsKeyPressed(ImGuiKey_LeftBracket))
			ChangeCurrentLinesIndentation(false);
		else if (!mReadOnly && !alt && ctrl && !shift && !super && ImGui::IsKeyPressed(ImGuiKey_RightBracket))
			ChangeCurrentLinesIndentation(true);
		else if (!alt && ctrl && shift && !super && ImGui::IsKeyPressed(ImGuiKey_UpArrow))
			MoveUpCurrentLines();
		else if (!alt && ctrl && shift && !super && ImGui::IsKeyPressed(ImGuiKey_DownArrow))
			MoveDownCurrentLines();
		else if (!mReadOnly && !alt && ctrl && !shift && !super && ImGui::IsKeyPressed(ImGuiKey_Slash))
			ToggleLineComment();
		//
		// Clipboard operations
		//
		else if (isCtrlOnly && ImGui::IsKeyPressed(ImGuiKey_Insert))
			Copy();
		else if (isShortcut && ImGui::IsKeyPressed(ImGuiKey_C))
			Copy();
		else if (!mReadOnly && isShiftOnly && ImGui::IsKeyPressed(ImGuiKey_Insert))
			Paste();
		else if (!mReadOnly && isShortcut && ImGui::IsKeyPressed(ImGuiKey_V))
			Paste();
		else if (isShortcut && ImGui::IsKeyPressed(ImGuiKey_X))
			Cut();
		else if (isShiftOnly && ImGui::IsKeyPressed(ImGuiKey_Delete))
			Cut();
		//
		// Selection
		//
		else if (isShortcut && ImGui::IsKeyPressed(ImGuiKey_A))
			SelectAll();
		else if (isShortcut && ImGui::IsKeyPressed(ImGuiKey_D))
			AddCursorForNextOccurrence();
		//
		// Character input (Enter, Tab, and regular characters)
		//
		else if (!mReadOnly && !alt && !ctrl && !shift && !super && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) {
			mAutocompleteEnabled = true;
			EnterCharacter('\n', false);
		} else if (!mReadOnly && !alt && !ctrl && !super && ImGui::IsKeyPressed(ImGuiKey_Tab)) {
			if (shift) {
				ChangeCurrentLinesIndentation(false);
			} else if (mAutocompleteActive && !mAutocompleteSuggestions.empty()) {
				ApplyAutocompletion(mAutocompleteSuggestions[0]);
			} else {
				EnterCharacter('\t', false);
			}
		} else if (!mReadOnly && !super && io.InputQueueCharacters.Size > 0) {
			mAutocompleteEnabled = true;
			for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
				auto c = io.InputQueueCharacters[i];
				if (c != 0 && (c == '\n' || c >= 32))
					EnterCharacter(c, shift);
			}
			io.InputQueueCharacters.resize(0);
		}
	}
	void TextEditor::ReplaceSelection(const char* aReplaceWith) {
		if (mReadOnly || aReplaceWith == nullptr)
			return;
		UndoRecord u;
		u.mBefore = mState;
		std::string selectedText = GetSelectedText();
		Coordinates selectionStart = mState.mCursors[mState.mCurrentCursor].GetSelectionStart();
		Coordinates selectionEnd = mState.mCursors[mState.mCurrentCursor].GetSelectionEnd();
		u.mOperations.push_back({selectedText, selectionStart, selectionEnd, UndoOperationType::Delete});
		DeleteSelection();
		Coordinates insertStart = GetSanitizedCursorCoordinates();
		InsertTextAtCursor(aReplaceWith);
		Coordinates insertEnd = GetSanitizedCursorCoordinates();
		u.mOperations.push_back({std::string(aReplaceWith), insertStart, insertEnd, UndoOperationType::Add});
		u.mAfter = mState;
		AddUndo(u);
	}
	bool TextEditor::SelectionEqualsText(const char* aText, int aTextSize, bool aCaseSensitive, int aCursor) const {
		if (aCursor == -1)
			aCursor = mState.mCurrentCursor;
		if (aText == nullptr || aTextSize <= 0)
			return false;
		if (aCursor < 0 || aCursor > mState.mCurrentCursor)
			return false;
		if (!mState.mCursors[aCursor].HasSelection())
			return false;
		std::string selectedText = GetSelectedText(aCursor);
		if ((int)selectedText.size() != aTextSize)
			return false;
		if (aCaseSensitive)
			return selectedText.compare(0, selectedText.size(), aText, (size_t)aTextSize) == 0;
		for (int i = 0; i < aTextSize; i++) {
			unsigned char lhs = (unsigned char)selectedText[i];
			unsigned char rhs = (unsigned char)aText[i];
			if ((char)std::tolower(lhs) != (char)std::tolower(rhs))
				return false;
		}
		return true;
	}
	std::vector<TextEditor::FindAllResult> TextEditor::CollectFindAllOccurrences(const char* aText, int aTextSize, bool aCaseSensitive) const {
		std::vector<FindAllResult> result;
		if (aText == nullptr || aTextSize <= 0)
			return result;
		std::string haystack = GetText();
		if (haystack.empty())
			return result;
		std::string needle(aText, aText + aTextSize);
		if (!aCaseSensitive) {
			std::transform(haystack.begin(), haystack.end(), haystack.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		}
		std::vector<size_t> lineOffsets;
		lineOffsets.reserve(mLines.size());
		size_t offset = 0;
		for (size_t i = 0; i < mLines.size(); i++) {
			lineOffsets.push_back(offset);
			offset += mLines[i].size();
			if (i + 1 < mLines.size())
				offset += 1;
		}
		auto indexToCoordinates = [&](size_t absoluteIndex) -> Coordinates {
			size_t line = 0;
			while (line + 1 < lineOffsets.size() && lineOffsets[line + 1] <= absoluteIndex)
				line++;
			size_t charIndex = absoluteIndex - lineOffsets[line];
			if (charIndex > mLines[line].size())
				charIndex = mLines[line].size();
			return Coordinates((int)line, GetCharacterColumn((int)line, (int)charIndex));
		};
		size_t pos = 0;
		while (pos < haystack.size()) {
			size_t found = haystack.find(needle, pos);
			if (found == std::string::npos)
				break;
			size_t foundEnd = found + needle.size();
			Coordinates start = indexToCoordinates(found);
			Coordinates end = indexToCoordinates(foundEnd);
			FindAllResult occurrence;
			occurrence.occurrence.start.line = start.mLine;
			occurrence.occurrence.start.column = start.mColumn;
			occurrence.occurrence.end.line = end.mLine;
			occurrence.occurrence.end.column = end.mColumn;
			occurrence.line = start.mLine;
			const Line& line = mLines[start.mLine];
			occurrence.lineContent.resize(line.size());
			for (size_t i = 0; i < line.size(); ++i)
				occurrence.lineContent[i] = line[i].mChar;
			result.push_back(std::move(occurrence));
			pos = found + 1;
		}
		return result;
	}

	void TextEditor::HandleMouseInputs() {
		ImGuiIO& io = ImGui::GetIO();
		auto shift = io.KeyShift;
		auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
		auto alt = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt;

		/*
		Pan with middle mouse button
		*/
		mPanning &= ImGui::IsMouseDown(2);
		if (mPanning && ImGui::IsMouseDragging(2)) {
			ImVec2 scroll = {ImGui::GetFont()->Scale * ImGui::GetStyle().ScrollbarSize, ImGui::GetFont()->Scale * ImGui::GetStyle().ScrollbarSize};
			ImVec2 currentMousePos = ImGui::GetMouseDragDelta(2);
			ImVec2 mouseDelta = {currentMousePos.x - mLastMousePos.x, currentMousePos.y - mLastMousePos.y};
			ImGui::SetScrollY(scroll.y - mouseDelta.y);
			ImGui::SetScrollX(scroll.x - mouseDelta.x);
			mLastMousePos = currentMousePos;
		}

		// Mouse left button dragging (=> update selection)
		mDraggingSelection &= ImGui::IsMouseDown(0);
		if (mDraggingSelection && ImGui::IsMouseDragging(0)) {
			io.WantCaptureMouse = true;
			Coordinates cursorCoords = ScreenPosToCoordinates(ImGui::GetMousePos());
			SetCursorPosition(cursorCoords, mState.GetLastAddedCursorIndex(), false);
		}

		if (ImGui::IsWindowHovered()) {
			if (ImGui::IsMouseClicked(0)) {
				mInputEnabled = true;
				mAutocompleteEnabled = true;
			}
			// mLastHoveredWord is kept accurate by the render path, which has access to the
			// precise per-line Y positions needed to handle codelens lane offsets correctly.
			// Capture the word at right-click time so ConsumeRightClickWord() works
			// correctly outside the child window scope.
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
				mRightClickWord = mLastHoveredWord;
				mRightClickPending = true;
			}
			auto click = ImGui::IsMouseClicked(0);
			if (!shift && !alt) {
				auto doubleClick = ImGui::IsMouseDoubleClicked(0);
				auto t = ImGui::GetTime();
				auto tripleClick =
					click && !doubleClick && (mLastClickTime != -1.0f && (t - mLastClickTime) < io.MouseDoubleClickTime && Distance(io.MousePos, mLastClickPos) < 0.01f);

				if (click)
					mDraggingSelection = true;

				/*
				Pan with middle mouse button
				*/

				if (ImGui::IsMouseClicked(2)) {
					mPanning = true;
					mLastMousePos = ImGui::GetMouseDragDelta(2);
				}

				/*
				Left mouse button triple click
				*/

				if (tripleClick) {
					if (ctrl)
						mState.AddCursor();
					else
						mState.mCurrentCursor = 0;

					Coordinates cursorCoords = ScreenPosToCoordinates(ImGui::GetMousePos());
					Coordinates targetCursorPos = cursorCoords.mLine < (int)mLines.size() - 1 ? Coordinates{cursorCoords.mLine + 1, 0}
																							  : Coordinates{cursorCoords.mLine, GetLineMaxColumn(cursorCoords.mLine)};
					SetSelection({cursorCoords.mLine, 0}, targetCursorPos, mState.mCurrentCursor);

					mLastClickTime = -1.0f;
				}

				/*
				Left mouse button double click
				*/

				else if (doubleClick) {
					if (ctrl)
						mState.AddCursor();
					else
						mState.mCurrentCursor = 0;

					Coordinates cursorCoords = ScreenPosToCoordinates(ImGui::GetMousePos());
					SetSelection(FindWordStart(cursorCoords), FindWordEnd(cursorCoords), mState.mCurrentCursor);

					mLastClickTime = (float)ImGui::GetTime();
					mLastClickPos = io.MousePos;
				}

				/*
				Left mouse button click
				*/
				else if (click) {
					if (ctrl)
						mState.AddCursor();
					else
						mState.mCurrentCursor = 0;

					bool isOverLineNumber;
					Coordinates cursorCoords = ScreenPosToCoordinates(ImGui::GetMousePos(), &isOverLineNumber);
					if (isOverLineNumber) {
						Coordinates targetCursorPos = cursorCoords.mLine < (int)mLines.size() - 1 ? Coordinates{cursorCoords.mLine + 1, 0}
																								  : Coordinates{cursorCoords.mLine, GetLineMaxColumn(cursorCoords.mLine)};
						SetSelection({cursorCoords.mLine, 0}, targetCursorPos, mState.mCurrentCursor);
					} else
						SetCursorPosition(cursorCoords, mState.GetLastAddedCursorIndex());

					mLastClickTime = (float)ImGui::GetTime();
					mLastClickPos = io.MousePos;
				} else if (ImGui::IsMouseReleased(0)) {
					mState.SortCursorsFromTopToBottom();
					MergeCursorsIfPossible();
				}
			} else if (shift) {
				if (click) {
					Coordinates newSelection = ScreenPosToCoordinates(ImGui::GetMousePos());
					SetCursorPosition(newSelection, mState.mCurrentCursor, false);
				}
			}
		} else {
			mLastHoveredWord.clear();
		}
	}

	void TextEditor::UpdateViewVariables(float aScrollX, float aScrollY) {
		mContentHeight = ImGui::GetWindowHeight() - (IsHorizontalScrollbarVisible() ? ImGui::GetStyle().ScrollbarSize : 0.0f);
		mContentWidth = ImGui::GetWindowWidth() - (IsVerticalScrollbarVisible() ? ImGui::GetStyle().ScrollbarSize : 0.0f);

		mVisibleLineCount = std::max((int)ceil(mContentHeight / mCharAdvance.y), 0);
		const int lineCount = (int)mLines.size();
		// Raw estimates (ignore codelens lane heights). The Render() path corrects these
		// with a codelens-aware walk once currentFilePath and codeLensLaneHeight are known.
		mFirstVisibleLine = std::min(std::max((int)(aScrollY / mCharAdvance.y), 0), std::max(lineCount - 1, 0));
		mLastVisibleLine = std::min(std::max((int)((mContentHeight + aScrollY) / mCharAdvance.y), 0), std::max(lineCount - 1, 0));
		mFirstVisibleLineYOffset = 0.0f;

		mVisibleColumnCount = std::max((int)ceil((mContentWidth - std::max(mTextStart - aScrollX, 0.0f)) / mCharAdvance.x), 0);
		mFirstVisibleColumn = std::max((int)(std::max(aScrollX - mTextStart, 0.0f) / mCharAdvance.x), 0);
		mLastVisibleColumn = std::max((int)((mContentWidth + aScrollX - mTextStart) / mCharAdvance.x), 0);
	}

	void TextEditor::Render(bool aParentIsFocused) {
		// [Bundle:DelayedInit]
		SetPalette(mPaletteId);

		/* Compute mCharAdvance regarding to scaled font size (Ctrl + mouse wheel)*/
		const float fontWidth = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, "#", nullptr, nullptr).x;
		const float fontHeight = ImGui::GetTextLineHeightWithSpacing();
		mCharAdvance = ImVec2(fontWidth, fontHeight * mLineSpacing);
		const float codeLensLaneHeight = ImGui::GetFontSize() * 0.95f + 2.0f;

		// Deduce mTextStart by evaluating mLines size (global lineMax) plus two spaces as text width
		mTextStart = mLeftMargin;
		static char lineNumberBuffer[16];
		static char timingBuffer[8];
		static char bytecodeBuffer[13];
		if (mShowLineNumbers) {
			snprintf(lineNumberBuffer, 16, " %zu ", mLines.size());
			mTextStart += ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, lineNumberBuffer, nullptr, nullptr).x;
		}
		if (mShowTiming && mTimingCallback != nullptr) {
			snprintf(timingBuffer, 8, " 99999 ");
			mTextStart += ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, timingBuffer, nullptr, nullptr).x;
		}
		if (mShowBytecode && mBytecodeCallback != nullptr) {
			snprintf(bytecodeBuffer, 13, " 9999999999 ");
			mTextStart += ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, bytecodeBuffer, nullptr, nullptr).x;
		}

		ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
		mLastRenderOrigin = ImGui::GetWindowPos();
		mScrollX = ImGui::GetScrollX();
		mScrollY = ImGui::GetScrollY();
		UpdateViewVariables(mScrollX, mScrollY);

		int maxColumnLimited = 0;
		// Compute normalized file path once; used by total-height cache, EnsureCursorVisible, and the render loop.
		const std::string currentFilePath = NormalizePath(mDocumentPath.empty() ? "<active-document>" : mDocumentPath);
		// Rebuild the per-line Y cache when document content, codelens data, or font size changed.
		// ComputeLineHasCodeLens is expensive (walks all symbol tables), so it runs here once on
		// dirty rather than every frame in the visible-line walk.
		const int lineCount = (int)mLines.size();
		if (mTotalCodeLensDirty || mTotalCodeLensVersion != sCodeLensDataVersion
			|| mLineTopYCacheVersion != sCodeLensDataVersion || mLineTopYCacheLineCount != lineCount
			|| mLineTopYCacheCharAdvanceY != mCharAdvance.y) {
			mTotalCodeLensHeight = 0.0f;
			mLineTopYCache.resize((size_t)lineCount + 1);
			mLineTopYCache[0] = 0.0f;
			for (int ln = 0; ln < lineCount; ln++) {
				float lineH = mCharAdvance.y;
				if (ComputeLineHasCodeLens(ln, currentFilePath)) {
					lineH += codeLensLaneHeight;
					mTotalCodeLensHeight += codeLensLaneHeight;
				}
				mLineTopYCache[ln + 1] = mLineTopYCache[ln] + lineH;
			}
			mTotalCodeLensDirty = false;
			mTotalCodeLensVersion = sCodeLensDataVersion;
			mLineTopYCacheVersion = sCodeLensDataVersion;
			mLineTopYCacheLineCount = lineCount;
			mLineTopYCacheCharAdvanceY = mCharAdvance.y;
		}
		// Compute mFirstVisibleLine/mLastVisibleLine from the cache in O(log N) + O(viewport lines).
		if (lineCount > 0 && !mLineTopYCache.empty()) {
			// upper_bound returns iterator to first entry strictly greater than mScrollY;
			// stepping back one gives the last line whose top is at or before the scroll position.
			auto it = std::upper_bound(mLineTopYCache.begin(), mLineTopYCache.begin() + lineCount, mScrollY);
			if (it != mLineTopYCache.begin())
				--it;
			mFirstVisibleLine = (int)(it - mLineTopYCache.begin());
			mFirstVisibleLineYOffset = mLineTopYCache[mFirstVisibleLine] - mScrollY;
			mLastVisibleLine = mFirstVisibleLine;
			for (int ln = mFirstVisibleLine + 1; ln < lineCount; ln++) {
				if (mLineTopYCache[ln] - mScrollY >= mContentHeight)
					break;
				mLastVisibleLine = ln;
			}
		}
		const int visibleLineCount = mLastVisibleLine - mFirstVisibleLine + 1;
		std::vector<float> visibleLineTextY(visibleLineCount > 0 ? visibleLineCount : 0, 0.0f);
		std::vector<unsigned char> hasVisibleLineTextY(visibleLineCount > 0 ? visibleLineCount : 0, 0);
		if (!mLines.empty()) {
			auto drawList = ImGui::GetWindowDrawList();
			float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, " ", nullptr, nullptr).x;
			float codeLensStartX = cursorScreenPos.x + mTextStart + mCharAdvance.x * 0.5f;
			std::unordered_map<int, std::string> errorTextByLine;
			for (size_t fileIndex = 0; fileIndex < sCodeLensFiles.size(); fileIndex++) {
				if (sCodeLensFiles[fileIndex].filePath != currentFilePath)
					continue;
				const auto& errors = sCodeLensFiles[fileIndex].errors;
				for (size_t errorIndex = 0; errorIndex < errors.size(); errorIndex++) {
					int errorLine = errors[errorIndex].first;
					if (errorLine < 0 || errorLine >= (int)mLines.size())
						continue;
					auto it = errorTextByLine.find(errorLine);
					if (it == errorTextByLine.end())
						errorTextByLine[errorLine] = errors[errorIndex].second;
					else
						it->second += "\n" + errors[errorIndex].second;
				}
				break;
			}
			// Initialize dynamicCodeLensYOffset: document-space Y of mFirstVisibleLine minus its
			// raw line-index contribution, yielding the accumulated codelens lane height above it.
			float dynamicCodeLensYOffset = !mLineTopYCache.empty()
				? mLineTopYCache[mFirstVisibleLine] - mFirstVisibleLine * mCharAdvance.y
				: 0.0f;
			std::vector<std::pair<ImVec2, std::string>> pendingCodeLensTexts;
			bool hasHoveredLineErrorTooltip = false;
			const std::vector<CodeLensSymbolData>* currentFileSymbols = nullptr;
			for (size_t fileIndex = 0; fileIndex < sCodeLensFiles.size(); fileIndex++)
				if (sCodeLensFiles[fileIndex].filePath == currentFilePath) {
					currentFileSymbols = &sCodeLensFiles[fileIndex].symbols;
					break;
				}

			for (int lineNo = mFirstVisibleLine; lineNo <= mLastVisibleLine && lineNo < (int)mLines.size(); lineNo++) {
				auto& line = mLines[lineNo];
				std::string lineTextForCodeLens(line.size(), '\0');
				for (size_t glyphIndex = 0; glyphIndex < line.size(); glyphIndex++)
					lineTextForCodeLens[glyphIndex] = line[glyphIndex].mChar;
				std::string codeLensText;
				if (currentFileSymbols != nullptr) {
					const std::string repeatSymbolName = BuildRepeatCodeLensSymbolName(currentFilePath, lineNo);
					for (size_t symbolIndex = 0; symbolIndex < currentFileSymbols->size(); symbolIndex++) {
						const CodeLensSymbolData& symbol = (*currentFileSymbols)[symbolIndex];
						if (symbol.codelensText.empty())
							continue;
						if (symbol.symbolName != repeatSymbolName)
							continue;
						codeLensText = GetSingleLineCodeLensText(symbol.codelensText);
						break;
					}
				}
				if (codeLensText.empty() && !sCodeLensFiles.empty()) {
					const CodeLensSymbolData* matchedCodeLensSymbol = nullptr;
					for (size_t fileIndex = 0; fileIndex < sCodeLensFiles.size() && matchedCodeLensSymbol == nullptr; fileIndex++) {
						const auto& symbols = sCodeLensFiles[fileIndex].symbols;
						for (size_t symbolIndex = 0; symbolIndex < symbols.size(); symbolIndex++) {
							const CodeLensSymbolData& symbol = symbols[symbolIndex];
							if (symbol.codelensText.empty() || symbol.symbolName.empty())
								continue;
							int matchStart = 0;
							if (!LineContainsWholeWord(lineTextForCodeLens, symbol.symbolName, &matchStart))
								continue;
							{
								bool glyphInComment = false;
								int matchWordSize = (int)symbol.symbolName.size();
								for (int ci = matchStart; ci < matchStart + matchWordSize && ci < (int)line.size(); ci++)
									if (line[ci].mComment || line[ci].mMultiLineComment) {
										glyphInComment = true;
										break;
									}
								if (glyphInComment)
									continue;
							}
							matchedCodeLensSymbol = &symbol;
							break;
						}
					}
					if (matchedCodeLensSymbol != nullptr)
						codeLensText = GetSingleLineCodeLensText(matchedCodeLensSymbol->codelensText);
				}
				bool hasCodeLens = !codeLensText.empty();
				float lineTopY = cursorScreenPos.y + lineNo * mCharAdvance.y + dynamicCodeLensYOffset;
				ImVec2 lineStartScreenPos = ImVec2(cursorScreenPos.x, lineTopY + (hasCodeLens ? codeLensLaneHeight : 0.0f));
				ImVec2 textScreenPos = ImVec2(lineStartScreenPos.x + mTextStart, lineStartScreenPos.y);
				visibleLineTextY[lineNo - mFirstVisibleLine] = lineStartScreenPos.y;
				hasVisibleLineTextY[lineNo - mFirstVisibleLine] = 1;
				maxColumnLimited = std::max(GetLineMaxColumn(lineNo, mLastVisibleColumn), maxColumnLimited);

				Coordinates lineStartCoord(lineNo, 0);
				Coordinates lineEndCoord(lineNo, maxColumnLimited);
				auto lineErrorIt = errorTextByLine.find(lineNo);
				if (lineErrorIt != errorTextByLine.end()) {
					ImVec2 lineErrorStart(lineStartScreenPos.x + mTextStart, lineStartScreenPos.y);
					ImVec2 lineErrorEnd(cursorScreenPos.x + mScrollX + mContentWidth, lineStartScreenPos.y + mCharAdvance.y);
					drawList->AddRectFilled(lineErrorStart, lineErrorEnd, mPalette[(int)PaletteIndex::ErrorMarker]);
					if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(lineErrorStart, lineErrorEnd)) {
						hasHoveredLineErrorTooltip = true;
						ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
						ImGui::BeginTooltip();
						ImGui::TextUnformatted(lineErrorIt->second.c_str());
						ImGui::EndTooltip();
						ImGui::PopStyleVar();
					}
				}

				// Draw selection for the current line
				for (int c = 0; c <= mState.mCurrentCursor; c++) {
					float rectStart = -1.0f;
					float rectEnd = -1.0f;
					Coordinates cursorSelectionStart = mState.mCursors[c].GetSelectionStart();
					Coordinates cursorSelectionEnd = mState.mCursors[c].GetSelectionEnd();
					assert(cursorSelectionStart <= cursorSelectionEnd);

					if (cursorSelectionStart <= lineEndCoord)
						rectStart = cursorSelectionStart > lineStartCoord ? TextDistanceToLineStart(cursorSelectionStart) : 0.0f;
					if (cursorSelectionEnd > lineStartCoord)
						rectEnd = TextDistanceToLineStart(cursorSelectionEnd < lineEndCoord ? cursorSelectionEnd : lineEndCoord);
					if (cursorSelectionEnd.mLine > lineNo || (cursorSelectionEnd.mLine == lineNo && cursorSelectionEnd > lineEndCoord))
						rectEnd += mCharAdvance.x;

					if (rectStart != -1 && rectEnd != -1 && rectStart < rectEnd)
						drawList->AddRectFilled(ImVec2{lineStartScreenPos.x + mTextStart + rectStart, lineStartScreenPos.y},
												ImVec2{lineStartScreenPos.x + mTextStart + rectEnd, lineStartScreenPos.y + mCharAdvance.y}, mPalette[(int)PaletteIndex::Selection]);
				}
				if (hasCodeLens)
					pendingCodeLensTexts.push_back(std::make_pair(ImVec2(codeLensStartX, lineTopY + 1.0f), codeLensText));

				// Line numbers are drawn after the main loop as a fixed gutter overlay (see below)
				{
					bool focused = ImGui::IsWindowFocused() || aParentIsFocused;
					if (focused) {
						for (int c = 0; c <= mState.mCurrentCursor; c++) {
							if (mState.mCursors[c].mInteractiveEnd.mLine != lineNo)
								continue;
							const Coordinates& cursorCoords = mState.mCursors[c].mInteractiveEnd;
							auto cindex = GetCharacterIndexR(cursorCoords);
							float cx = TextDistanceToLineStart(cursorCoords);
							float cursorCellWidth = mCharAdvance.x;
							if (cindex < (int)line.size() && line[cindex].mChar == '\t' && !mShortTabs)
								cursorCellWidth = TabSizeAtColumn(cursorCoords.mColumn) * mCharAdvance.x;
							ImVec2 cstart(textScreenPos.x + cx, lineStartScreenPos.y);
							ImVec2 cend(cstart.x + 1.0f, lineStartScreenPos.y + mCharAdvance.y);
							mAutocompletePopupPos = ImVec2(cstart.x + mCharAdvance.x * 2.0f, cend.y);
							switch (mCursorStyle) {
								case CursorStyle::Block:
									cend.x = cstart.x + cursorCellWidth;
									drawList->AddRectFilled(cstart, cend, mPalette[(int)PaletteIndex::Cursor]);
									break;
								case CursorStyle::Underline:
									cend = ImVec2(cstart.x + cursorCellWidth, lineStartScreenPos.y + mCharAdvance.y);
									cstart.y = cend.y - 2.0f;
									drawList->AddRectFilled(cstart, cend, mPalette[(int)PaletteIndex::Cursor]);
									break;
								case CursorStyle::Line:
								default:
									drawList->AddRectFilled(cstart, cend, mPalette[(int)PaletteIndex::Cursor]);
									break;
							}
							if (mCursorOnBracket) {
								ImVec2 topLeft = {cstart.x, lineStartScreenPos.y + fontHeight + 1.0f};
								ImVec2 bottomRight = {topLeft.x + mCharAdvance.x, topLeft.y + 1.0f};
								drawList->AddRectFilled(topLeft, bottomRight, mPalette[(int)PaletteIndex::Cursor]);
							}
						}
					}
				}

				// Render colorized text
				static std::string glyphBuffer;
				int charIndex = GetFirstVisibleCharacterIndex(lineNo);
				int column = mFirstVisibleColumn; // can be in the middle of tab character
				while (charIndex < (int)mLines[lineNo].size() && column <= mLastVisibleColumn) {
					auto& glyph = line[charIndex];
					auto color = GetGlyphColor(glyph);
					ImVec2 targetGlyphPos = {lineStartScreenPos.x + mTextStart + TextDistanceToLineStart({lineNo, column}, false), lineStartScreenPos.y};

					if (glyph.mChar == '\t') {
						if (mShowWhitespaces) {
							ImVec2 p1, p2, p3, p4;

							const auto s = ImGui::GetFontSize();
							const auto x1 = targetGlyphPos.x + mCharAdvance.x * 0.3f;
							const auto y = targetGlyphPos.y + fontHeight * 0.5f;

							if (mShortTabs) {
								const auto x2 = targetGlyphPos.x + mCharAdvance.x;
								p1 = ImVec2(x1, y);
								p2 = ImVec2(x2, y);
								p3 = ImVec2(x2 - s * 0.16f, y - s * 0.16f);
								p4 = ImVec2(x2 - s * 0.16f, y + s * 0.16f);
							} else {
								const auto x2 = targetGlyphPos.x + TabSizeAtColumn(column) * mCharAdvance.x - mCharAdvance.x * 0.3f;
								p1 = ImVec2(x1, y);
								p2 = ImVec2(x2, y);
								p3 = ImVec2(x2 - s * 0.2f, y - s * 0.2f);
								p4 = ImVec2(x2 - s * 0.2f, y + s * 0.2f);
							}

							drawList->AddLine(p1, p2, mPalette[(int)PaletteIndex::ControlCharacter]);
							drawList->AddLine(p2, p3, mPalette[(int)PaletteIndex::ControlCharacter]);
							drawList->AddLine(p2, p4, mPalette[(int)PaletteIndex::ControlCharacter]);
						}
					} else if (glyph.mChar == ' ') {
						if (mShowWhitespaces) {
							const auto s = ImGui::GetFontSize();
							const auto x = targetGlyphPos.x + spaceSize * 0.5f;
							const auto y = targetGlyphPos.y + s * 0.5f;
							drawList->AddCircleFilled(ImVec2(x, y), 1.5f, mPalette[(int)PaletteIndex::ControlCharacter], 4);
						}
					} else {
						int seqLength = UTF8CharLength(glyph.mChar);
						if (mCursorOnBracket && seqLength == 1 && mMatchingBracketCoords == Coordinates{lineNo, column}) {
							ImVec2 topLeft = {targetGlyphPos.x, targetGlyphPos.y + fontHeight + 1.0f};
							ImVec2 bottomRight = {topLeft.x + mCharAdvance.x, topLeft.y + 1.0f};
							drawList->AddRectFilled(topLeft, bottomRight, mPalette[(int)PaletteIndex::Cursor]);
						}
						glyphBuffer.clear();
						for (int i = 0; i < seqLength; i++)
							glyphBuffer.push_back(line[charIndex + i].mChar);
						drawList->AddText(targetGlyphPos, color, glyphBuffer.c_str());
					}

					MoveCharIndexAndColumn(lineNo, charIndex, column);
				}
				if (hasCodeLens)
					dynamicCodeLensYOffset += codeLensLaneHeight;
			}
			if (!hasHoveredLineErrorTooltip && ImGui::IsWindowHovered()) {
				ImVec2 mousePos = ImGui::GetMousePos();
				int hoveredLine = -1;
				for (int lineNo = mFirstVisibleLine; lineNo <= mLastVisibleLine && lineNo < (int)mLines.size(); lineNo++) {
					if (!hasVisibleLineTextY[lineNo - mFirstVisibleLine])
						continue;
					float lineY = visibleLineTextY[lineNo - mFirstVisibleLine];
					if (mousePos.y >= lineY && mousePos.y < lineY + mCharAdvance.y) {
						hoveredLine = lineNo;
						break;
					}
				}
				// Resolve the word under the mouse using accurate per-line Y positions.
				// This is the authoritative source for mLastHoveredWord (used by right-click).
				// It is independent of the tooltip block below so disabling tooltips does not break it.
				mLastHoveredWord.clear();
				if (hoveredLine != -1) {
					float lineTextStartX = cursorScreenPos.x + mTextStart;
					if (mousePos.x >= lineTextStartX) {
						int hoveredColumn = std::max(0, (int)floor((mousePos.x - lineTextStartX + POS_TO_COORDS_COLUMN_OFFSET * mCharAdvance.x) / mCharAdvance.x));
						Coordinates hoveredCoords(hoveredLine, hoveredColumn);
						int hoveredCharIndex = GetCharacterIndexL(hoveredCoords);
						if (hoveredCharIndex >= 0 && hoveredCharIndex < (int)mLines[hoveredLine].size() && IsIdentifierWordByte(mLines[hoveredLine][hoveredCharIndex].mChar)) {
							const Glyph& hoveredGlyph = mLines[hoveredLine][hoveredCharIndex];
							if (!hoveredGlyph.mComment && !hoveredGlyph.mMultiLineComment) {
								std::string hoveredWord = GetText(FindWordStart(hoveredCoords), FindWordEnd(hoveredCoords));
								const bool caseInsensitive = (mLanguageDefinition != nullptr && !mLanguageDefinition->mCaseSensitive);
								std::string hoveredWordUpper;
								if (caseInsensitive) {
									hoveredWordUpper.resize(hoveredWord.size());
									for (size_t c = 0; c < hoveredWord.size(); c++)
										hoveredWordUpper[c] = (char)std::toupper((unsigned char)hoveredWord[c]);
								}
								for (size_t fileIndex = 0; fileIndex < sCodeLensFiles.size(); fileIndex++) {
									if (sCodeLensFiles[fileIndex].language != mLanguageDefinitionId)
										continue;
									const auto& symbols = sCodeLensFiles[fileIndex].symbols;
									bool found = false;
									for (size_t symbolIndex = 0; symbolIndex < symbols.size(); symbolIndex++) {
										const CodeLensSymbolData& symbol = symbols[symbolIndex];
										if (symbol.symbolName.empty() || symbol.symbolName.size() != hoveredWord.size())
											continue;
										// Pass 1: exact case-sensitive match.
										if (symbol.symbolName == hoveredWord) {
											mLastHoveredWord = hoveredWord;
											found = true;
											break;
										}
										// Pass 2: uppercase fallback for case-insensitive languages.
										if (!caseInsensitive)
											continue;
										bool sameWord = true;
										for (size_t c = 0; c < hoveredWordUpper.size(); c++)
											if ((char)std::toupper((unsigned char)symbol.symbolName[c]) != hoveredWordUpper[c]) {
												sameWord = false;
												break;
											}
										if (!sameWord)
											continue;
										mLastHoveredWord = hoveredWord;
										found = true;
										break;
									}
									if (found)
										break;
								}
							}
						}
					}
				}
				// Show tooltip for symbols that have external code, independently of the word resolver above.
				if (hoveredLine != -1) {
					int hoveredLineMaxColumn = GetLineMaxColumn(hoveredLine);
					float lineTextStartX = cursorScreenPos.x + mTextStart;
					float lineTextEndX = lineTextStartX + TextDistanceToLineStart({hoveredLine, hoveredLineMaxColumn}, false);
					if (mousePos.x >= lineTextStartX && mousePos.x < lineTextEndX) {
						int hoveredColumn = std::max(0, (int)floor((mousePos.x - lineTextStartX + POS_TO_COORDS_COLUMN_OFFSET * mCharAdvance.x) / mCharAdvance.x));
						Coordinates hoveredCoords(hoveredLine, hoveredColumn);
						int hoveredCharIndex = GetCharacterIndexL(hoveredCoords);
						if (hoveredCharIndex >= 0 && hoveredCharIndex < (int)mLines[hoveredLine].size()) {
							char hoveredChar = mLines[hoveredLine][hoveredCharIndex].mChar;
							const Glyph& tooltipGlyph = mLines[hoveredLine][hoveredCharIndex];
							if (IsIdentifierWordByte(hoveredChar) && !tooltipGlyph.mComment && !tooltipGlyph.mMultiLineComment) {
								std::string hoveredWord = GetText(FindWordStart(hoveredCoords), FindWordEnd(hoveredCoords));
								const bool caseInsensitive = (mLanguageDefinition != nullptr && !mLanguageDefinition->mCaseSensitive);
								std::string hoveredWordUpper;
								if (caseInsensitive) {
									hoveredWordUpper.resize(hoveredWord.size());
									for (size_t c = 0; c < hoveredWord.size(); c++)
										hoveredWordUpper[c] = (char)std::toupper((unsigned char)hoveredWord[c]);
								}
								const CodeLensSymbolData* hoveredSymbol = nullptr;
									std::string hoveredSymbolFilePath;
									for (size_t fileIndex = 0; fileIndex < sCodeLensFiles.size() && hoveredSymbol == nullptr; fileIndex++) {
										if (sCodeLensFiles[fileIndex].language != mLanguageDefinitionId)
											continue;
										const auto& symbols = sCodeLensFiles[fileIndex].symbols;
										for (size_t symbolIndex = 0; symbolIndex < symbols.size(); symbolIndex++) {
											const CodeLensSymbolData& symbol = symbols[symbolIndex];
											if (symbol.externalCode.empty() || symbol.symbolName.empty())
												continue;
											if (symbol.symbolName.size() != hoveredWord.size())
												continue;
											// Pass 1: exact case-sensitive match.
											if (symbol.symbolName == hoveredWord) {
												hoveredSymbol = &symbol;
												hoveredSymbolFilePath = sCodeLensFiles[fileIndex].filePath;
												break;
											}
											// Pass 2: uppercase fallback for case-insensitive languages.
											if (!caseInsensitive)
												continue;
											bool sameWord = true;
											for (size_t c = 0; c < hoveredWordUpper.size(); c++)
												if ((char)std::toupper((unsigned char)symbol.symbolName[c]) != hoveredWordUpper[c]) {
													sameWord = false;
													break;
												}
											if (!sameWord)
												continue;
											hoveredSymbol = &symbol;
											hoveredSymbolFilePath = sCodeLensFiles[fileIndex].filePath;
											break;
										}
									}
									// Suppress the tooltip when hovering the exact file and line where the symbol is defined.
									const bool isOnDefinitionLine = (hoveredSymbol != nullptr) &&
										(hoveredSymbol->lineNumber == hoveredLine) &&
										(hoveredSymbolFilePath == currentFilePath);
									if (hoveredSymbol != nullptr && !isOnDefinitionLine) {
									ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
									ImGui::BeginTooltip();
									std::string tooltipText = ExpandTabsForTooltip(hoveredSymbol->externalCode, mTabSize);
									ImGui::TextUnformatted(tooltipText.c_str());
									ImGui::EndTooltip();
									ImGui::PopStyleVar();
								}
							}
						}
					}
				}
			}
			for (size_t i = 0; i < pendingCodeLensTexts.size(); i++)
				drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.9f, pendingCodeLensTexts[i].first, mPalette[(int)PaletteIndex::LineNumber],
								  pendingCodeLensTexts[i].second.c_str());

			// Draw gutters at fixed screen position - immune to horizontal scroll
			float gutterX = cursorScreenPos.x + mScrollX;
			float currentGutterRight = gutterX + mLeftMargin;
			// Background rect covers any code text that scrolled into the gutter area
			float topY = cursorScreenPos.y + mFirstVisibleLine * mCharAdvance.y;
			float bottomY = cursorScreenPos.y + std::min(mLastVisibleLine + 1, (int)mLines.size()) * mCharAdvance.y + dynamicCodeLensYOffset;
			if (mShowLineNumbers || (mShowTiming && mTimingCallback != nullptr) || (mShowBytecode && mBytecodeCallback != nullptr)) {
				drawList->AddRectFilled(ImVec2(gutterX, topY), ImVec2(gutterX + mTextStart, bottomY), mPalette[(int)PaletteIndex::Background]);
			}
			// Draw line numbers gutter
			if (mShowLineNumbers) {
				for (int lineNo = mFirstVisibleLine; lineNo <= mLastVisibleLine && lineNo < (int)mLines.size(); lineNo++) {
					float lineY = hasVisibleLineTextY[lineNo - mFirstVisibleLine] ? visibleLineTextY[lineNo - mFirstVisibleLine] : cursorScreenPos.y + lineNo * mCharAdvance.y;
					snprintf(lineNumberBuffer, 16, "%d  ", lineNo + 1);
					drawList->AddText(ImVec2(currentGutterRight, lineY), mPalette[(int)PaletteIndex::LineNumber], lineNumberBuffer);
				}
				snprintf(lineNumberBuffer, 16, " %zu ", mLines.size());
				currentGutterRight += ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, lineNumberBuffer, nullptr, nullptr).x;
			}
			// Draw timing gutter
			if (mShowTiming && mTimingCallback != nullptr) {
				for (int lineNo = mFirstVisibleLine; lineNo <= mLastVisibleLine && lineNo < (int)mLines.size(); lineNo++) {
					float lineY = hasVisibleLineTextY[lineNo - mFirstVisibleLine] ? visibleLineTextY[lineNo - mFirstVisibleLine] : cursorScreenPos.y + lineNo * mCharAdvance.y;
					const std::string& timingText = GetCachedTimingText(lineNo);
					if (!timingText.empty() && timingText.length() <= 5) {
						snprintf(timingBuffer, 8, " %s ", timingText.c_str());
						drawList->AddText(ImVec2(currentGutterRight, lineY), mPalette[(int)PaletteIndex::LineNumber], timingBuffer);
					}
				}
				snprintf(timingBuffer, 8, " 99999 ");
				currentGutterRight += ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, timingBuffer, nullptr, nullptr).x;
			}
			// Draw bytecode gutter
			if (mShowBytecode && mBytecodeCallback != nullptr) {
				for (int lineNo = mFirstVisibleLine; lineNo <= mLastVisibleLine && lineNo < (int)mLines.size(); lineNo++) {
					float lineY = hasVisibleLineTextY[lineNo - mFirstVisibleLine] ? visibleLineTextY[lineNo - mFirstVisibleLine] : cursorScreenPos.y + lineNo * mCharAdvance.y;
					const std::string& bytecodeText = GetCachedBytecodeText(lineNo);
					if (!bytecodeText.empty() && bytecodeText.length() <= 10) {
						snprintf(bytecodeBuffer, 12, " %s ", bytecodeText.c_str());
						drawList->AddText(ImVec2(currentGutterRight, lineY), mPalette[(int)PaletteIndex::LineNumber], bytecodeBuffer);
					}
				}
			}
		}
		// Total scrollable height includes codelens lane pixels so the scrollbar range is accurate.
		mCurrentSpaceHeight = (mLines.size() + 1) * mCharAdvance.y + mTotalCodeLensHeight;
		mCurrentSpaceWidth = (maxColumnLimited + std::min(mVisibleColumnCount - 1, maxColumnLimited)) * mCharAdvance.x;

		ImGui::SetCursorPos(ImVec2(0, 0));
		ImGui::Dummy(ImVec2(mCurrentSpaceWidth, mCurrentSpaceHeight));

		if (mEnsureCursorVisible > -1) {
			// The child window is always created with ImGuiWindowFlags_HorizontalScrollbar,
			// so the horizontal scrollbar space is ALWAYS reserved regardless of content width.
			// effectiveH = windowH - sbSize always.
			// The vertical scrollbar appears when content is taller than effectiveH.
			const float sbSize = ImGui::GetStyle().ScrollbarSize;
			const float windowH = ImGui::GetWindowHeight();
			const float windowW = ImGui::GetWindowWidth();
			const float effectiveH = windowH - sbSize;
			const bool vscrollVisible = mCurrentSpaceHeight > effectiveH;
			const float effectiveW = windowW - (vscrollVisible ? sbSize : 0.0f);
			const float codeLensLaneH = ImGui::GetFontSize() * 0.95f + 2.0f;
			for (int i = 0; i < (mEnsureCursorVisibleStartToo ? 2 : 1); i++) {
				Coordinates targetCoords = GetSanitizedCursorCoordinates(mEnsureCursorVisible, i);
				int cursorLine = targetCoords.mLine;
				// Count codelens lane pixels above the cursor line so the scroll target
				// places the cursor's actual rendered Y inside the viewport.
				float codeLensAbove = 0.0f;
				for (int ln = 0; ln < cursorLine; ln++)
					if (ComputeLineHasCodeLens(ln, currentFilePath))
						codeLensAbove += codeLensLaneH;
				// cursorScrollBase: scroll-space Y of the top of the cursor line.
				// Cursor occupies [cursorScrollBase, cursorScrollBase + mCharAdvance.y].
				// Viewport occupies [mScrollY, mScrollY + effectiveH].
				// Scroll up when cursor top is above viewport top.
				// Scroll down when cursor bottom is below viewport bottom.
				float cursorScrollBase = cursorLine * mCharAdvance.y + codeLensAbove;
				if (cursorScrollBase < mScrollY)
					ImGui::SetScrollY(mScrollY = std::max(0.0f, cursorScrollBase));
				else if (cursorScrollBase + mCharAdvance.y > mScrollY + effectiveH)
					ImGui::SetScrollY(mScrollY = std::max(0.0f, cursorScrollBase + mCharAdvance.y - effectiveH));
				// Horizontal scroll: ensure cursor column is inside the visible column range.
				// Recompute visible columns using the fresh effective width.
				const float freshFirstVisibleColumn = std::max(0.0f, mScrollX - mTextStart) / mCharAdvance.x;
				const float freshLastVisibleColumn = (effectiveW + mScrollX - mTextStart) / mCharAdvance.x;
				if ((float)targetCoords.mColumn <= freshFirstVisibleColumn) {
					float targetScroll = std::max(0.0f, mTextStart + (targetCoords.mColumn - 0.5f) * mCharAdvance.x);
					if (targetScroll < mScrollX)
						ImGui::SetScrollX(mScrollX = targetScroll);
				}
				if ((float)targetCoords.mColumn >= freshLastVisibleColumn) {
					float targetScroll = std::max(0.0f, mTextStart + (targetCoords.mColumn + 0.5f) * mCharAdvance.x - effectiveW);
					if (targetScroll > mScrollX)
						ImGui::SetScrollX(mScrollX = targetScroll);
				}
			}
			mEnsureCursorVisible = -1;
		}
		if (mScrollToTop) {
			ImGui::SetScrollY(0.0f);
			mScrollToTop = false;
		}
		if (mSetViewAtLine > -1) {
			float targetScroll;
			switch (mSetViewAtLineMode) {
				default:
				case SetViewAtLineMode::FirstVisibleLine:
					targetScroll = std::max(0.0f, ((float)mSetViewAtLine - 0.5f) * mCharAdvance.y);
					break;
				case SetViewAtLineMode::LastVisibleLine:
					targetScroll = std::max(0.0f, ((float)mSetViewAtLine + 1.5f) * mCharAdvance.y - mContentHeight);
					break;
				case SetViewAtLineMode::Centered:
					targetScroll = std::max(0.0f, ((float)mSetViewAtLine - (float)(mLastVisibleLine - mFirstVisibleLine) * 0.5f) * mCharAdvance.y);
					break;
			}
			ImGui::SetScrollY(targetScroll);
			mSetViewAtLine = -1;
		}
	}

	void TextEditor::OnCursorPositionChanged() {
		// When there is exactly one cursor and no selection, try to find a matching bracket
		// under the cursor and highlight it.
		if (mState.mCurrentCursor == 0 && !mState.mCursors[0].HasSelection()) // only one cursor without selection
			mCursorOnBracket = FindMatchingBracket(mState.mCursors[0].mInteractiveEnd.mLine, GetCharacterIndexR(mState.mCursors[0].mInteractiveEnd), mMatchingBracketCoords);
		else
			mCursorOnBracket = false;

		if (!mDraggingSelection) {
			mState.SortCursorsFromTopToBottom();
			MergeCursorsIfPossible();
		}
	}

	void TextEditor::OnLineChanged(bool aBeforeChange, int aLine, int aColumn, int aCharCount,
								   bool aDeleted) // adjusts cursor position when other cursor writes/deletes in the same line
	{
		// Uses a static map so the before-pass can record indices and the after-pass can apply them
		// without needing to pass state through the caller.
		static std::unordered_map<int, int> cursorCharIndices;
		if (aBeforeChange) {
			cursorCharIndices.clear();
			for (int c = 0; c <= mState.mCurrentCursor; c++) {
				if (mState.mCursors[c].mInteractiveEnd.mLine == aLine &&							// cursor is at the line
					mState.mCursors[c].mInteractiveEnd.mColumn > aColumn &&							// cursor is to the right of changing part
					mState.mCursors[c].GetSelectionEnd() == mState.mCursors[c].GetSelectionStart()) // cursor does not have a selection
				{
					cursorCharIndices[c] = GetCharacterIndexR({aLine, mState.mCursors[c].mInteractiveEnd.mColumn});
					cursorCharIndices[c] += aDeleted ? -aCharCount : aCharCount;
				}
			}
		} else {
			InvalidateLineMetadataCache(aLine);
			for (auto& item : cursorCharIndices)
				SetCursorPosition({aLine, GetCharacterColumn(aLine, item.second)}, item.first);
		}
	}

	void TextEditor::MergeCursorsIfPossible() {
		// Requires the cursors to be sorted from top to bottom.
		// Two strategies: merge overlapping selections, or merge cursors at identical positions.
		std::unordered_set<int> cursorsToDelete;
		if (AnyCursorHasSelection()) {
			// Merge cursors if their selections overlap or one contains the other.
			for (int c = mState.mCurrentCursor; c > 0; c--) // iterate backwards through pairs
			{
				int pc = c - 1; // pc for previous cursor

				bool pcContainsC = mState.mCursors[pc].GetSelectionEnd() >= mState.mCursors[c].GetSelectionEnd();
				bool pcContainsStartOfC = mState.mCursors[pc].GetSelectionEnd() > mState.mCursors[c].GetSelectionStart();

				if (pcContainsC) {
					cursorsToDelete.insert(c);
				} else if (pcContainsStartOfC) {
					Coordinates pcStart = mState.mCursors[pc].GetSelectionStart();
					Coordinates cEnd = mState.mCursors[c].GetSelectionEnd();
					mState.mCursors[pc].mInteractiveEnd = cEnd;
					mState.mCursors[pc].mInteractiveStart = pcStart;
					cursorsToDelete.insert(c);
				}
			}
		} else {
			// No selections: merge cursors that occupy the exact same position.
			for (int c = mState.mCurrentCursor; c > 0; c--) // iterate backwards through pairs
			{
				int pc = c - 1;
				if (mState.mCursors[pc].mInteractiveEnd == mState.mCursors[c].mInteractiveEnd)
					cursorsToDelete.insert(c);
			}
		}
		for (int c = mState.mCurrentCursor; c > -1; c--) // iterate backwards through each of them
		{
			if (cursorsToDelete.find(c) != cursorsToDelete.end())
				mState.mCursors.erase(mState.mCursors.begin() + c);
		}
		mState.mCurrentCursor -= cursorsToDelete.size();
	}

	void TextEditor::AddUndo(UndoRecord& aValue) {
		assert(!mReadOnly);
		// Truncate any redo history beyond the current position before appending the new record.
		mUndoBuffer.resize((size_t)(mUndoIndex + 1));
		mUndoBuffer.back() = aValue;
		++mUndoIndex;
		ScheduleCodeLensRefresh();
	}

	void TextEditor::RefreshCodeLensForCurrentDocument() {
		if (mLanguageDefinitionId == LanguageDefinitionId::None)
			return;
		const std::string filePath = NormalizePath(mDocumentPath.empty() ? "<active-document>" : mDocumentPath);
		// Snapshot current lines and enqueue with high priority.
		const int lineCount = (int)mLines.size();
		std::vector<std::string> snapshot(lineCount);
		for (int i = 0; i < lineCount; i++) {
			const auto& glyphs = mLines[i];
			snapshot[i].resize(glyphs.size());
			for (size_t j = 0; j < glyphs.size(); j++)
				snapshot[i][j] = glyphs[j].mChar;
		}
		EnqueueCodeLensFile(filePath, mLanguageDefinitionId, snapshot, true);
	}

	void TextEditor::ScheduleCodeLensRefresh() {
		if (mLanguageDefinitionId == LanguageDefinitionId::None)
			return;
		mCodeLensPendingRefresh = true;
		mCodeLensLastEditTime = ImGui::GetTime();
		mTotalCodeLensDirty = true;
		// Cancel any in-progress or queued parse for this file; document state is now stale.
		const std::string filePath = NormalizePath(mDocumentPath.empty() ? "<active-document>" : mDocumentPath);
		if (sCodeLensActiveParseInProgress && sCodeLensActiveFilePath == filePath) {
			if (sCodeLensActiveLanguageDef != nullptr && sCodeLensActiveLanguageDef->mCodeLensParseEnd != nullptr)
				sCodeLensActiveLanguageDef->mCodeLensParseEnd(filePath);
			sCodeLensActiveParseInProgress = false;
			sCodeLensActiveSymbolsCleared = false;
			sCodeLensActiveLines.clear();
		}
		for (int i = (int)sCodeLensParseQueue.size() - 1; i >= 0; i--)
			if (sCodeLensParseQueue[i].filePath == filePath)
				sCodeLensParseQueue.erase(sCodeLensParseQueue.begin() + i);
	}

	void TextEditor::CancelCodeLensRefresh() {
		const std::string filePath = NormalizePath(mDocumentPath.empty() ? "<active-document>" : mDocumentPath);
		// Cancel the active global parse if it belongs to this editor's file.
		bool needsRestore = false;
		if (sCodeLensActiveParseInProgress && sCodeLensActiveFilePath == filePath) {
			if (sCodeLensActiveLanguageDef != nullptr && sCodeLensActiveLanguageDef->mCodeLensParseEnd != nullptr)
				sCodeLensActiveLanguageDef->mCodeLensParseEnd(filePath);
			needsRestore = sCodeLensActiveSymbolsCleared;
			sCodeLensActiveParseInProgress = false;
			sCodeLensActiveSymbolsCleared = false;
			sCodeLensActiveLines.clear();
		}
		// Remove any queued entry (symbols not yet cleared, no restore needed for those).
		for (int i = (int)sCodeLensParseQueue.size() - 1; i >= 0; i--)
			if (sCodeLensParseQueue[i].filePath == filePath)
				sCodeLensParseQueue.erase(sCodeLensParseQueue.begin() + i);
		mCodeLensPendingRefresh = false;
		if (!needsRestore)
			return;
		if (mLanguageDefinitionId == LanguageDefinitionId::None)
			return;
		// Symbols were cleared mid-parse; schedule a disk restore so the codelens table is not left empty.
		if (filePath == "<active-document>")
			return;
		EnqueueCodeLensFile(filePath, mLanguageDefinitionId);
	}

	void TextEditor::TickIncrementalCodeLensParse() {
		if (mLanguageDefinitionId == LanguageDefinitionId::None)
			return;
		if (mLanguageDefinition == nullptr || mLanguageDefinition->mCodeLensLineParser == nullptr)
			return;
		if (!mCodeLensPendingRefresh)
			return;
		const double kCodeLensRefreshDelay = 2.0;
		// Wait for the user to stop typing before enqueuing the parse.
		if (ImGui::GetTime() - mCodeLensLastEditTime < kCodeLensRefreshDelay)
			return;
		// Snapshot current document lines and enqueue with high priority.
		const std::string filePath = NormalizePath(mDocumentPath.empty() ? "<active-document>" : mDocumentPath);
		const int lineCount = (int)mLines.size();
		std::vector<std::string> snapshot(lineCount);
		for (int i = 0; i < lineCount; i++) {
			const auto& glyphs = mLines[i];
			snapshot[i].resize(glyphs.size());
			for (size_t j = 0; j < glyphs.size(); j++)
				snapshot[i][j] = glyphs[j].mChar;
		}
		EnqueueCodeLensFile(filePath, mLanguageDefinitionId, snapshot, true);
		mCodeLensPendingRefresh = false;
	}

	// TODO
	// - multiline comments vs single-line: latter is blocking start of a ML
	void TextEditor::Colorize(int aFromLine, int aLines) {
		// Expand the pending dirty range to include [aFromLine, aFromLine+aLines).
		int toLine = aLines == -1 ? (int)mLines.size() : std::min((int)mLines.size(), aFromLine + aLines);
		mColorRangeMin = std::min(mColorRangeMin, aFromLine);
		mColorRangeMax = std::max(mColorRangeMax, toLine);
		mColorRangeMin = std::max(0, mColorRangeMin);
		mColorRangeMax = std::max(mColorRangeMin, mColorRangeMax);
		mCheckComments = true;
	}

	void TextEditor::ColorizeRange(int aFromLine, int aToLine) {
		if (mLines.empty() || aFromLine >= aToLine || mLanguageDefinition == nullptr)
			return;

		std::string buffer;
		std::string id;

		int endLine = std::max(0, std::min((int)mLines.size(), aToLine));
		for (int i = aFromLine; i < endLine; ++i) {
			auto& line = mLines[i];

			if (line.empty())
				continue;

			buffer.resize(line.size());
			for (size_t j = 0; j < line.size(); ++j) {
				auto& col = line[j];
				buffer[j] = col.mChar;
				col.mColorIndex = PaletteIndex::Default;
			}

			const char* bufferBegin = &buffer.front();
			const char* bufferEnd = bufferBegin + buffer.size();

			auto last = bufferEnd;

			for (auto first = bufferBegin; first != last;) {
				const char* token_begin = nullptr;
				const char* token_end = nullptr;
				PaletteIndex token_color = PaletteIndex::Default;

				bool hasTokenizeResult = false;

				if (mLanguageDefinition->mTokenize != nullptr) {
					if (mLanguageDefinition->mTokenize(first, last, token_begin, token_end, token_color))
						hasTokenizeResult = true;
				}

				if (hasTokenizeResult == false) {
					first++;
				} else {
					const size_t token_length = token_end - token_begin;

					if (token_color == PaletteIndex::Identifier) {
						id.assign(token_begin, token_end);

						// todo : allmost all language definitions use lower case to specify keywords, so shouldn't this use ::tolower ?
						if (!mLanguageDefinition->mCaseSensitive)
							std::transform(id.begin(), id.end(), id.begin(), ::toupper);

						if (!line[first - bufferBegin].mPreprocessor) {
							if (mLanguageDefinition->mKeywords.count(id) != 0)
								token_color = PaletteIndex::Keyword;
							else if (mLanguageDefinition->mIdentifiers.count(id) != 0)
								token_color = PaletteIndex::KnownIdentifier;
							else if (mLanguageDefinition->mPreprocIdentifiers.count(id) != 0)
								token_color = PaletteIndex::PreprocIdentifier;
						} else {
							if (mLanguageDefinition->mPreprocIdentifiers.count(id) != 0)
								token_color = PaletteIndex::PreprocIdentifier;
						}
					}

					for (size_t j = 0; j < token_length; ++j)
						line[(token_begin - bufferBegin) + j].mColorIndex = token_color;

					first = token_end;
				}
			}
		}
	}

	template <class InputIt1, class InputIt2, class BinaryPredicate> bool ColorizerEquals(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2, BinaryPredicate p) {
		for (; first1 != last1 && first2 != last2; ++first1, ++first2) {
			if (!p(*first1, *first2))
				return false;
		}
		return first1 == last1 && first2 == last2;
	}
	void TextEditor::ColorizeInternal() {
		if (mLines.empty() || mLanguageDefinition == nullptr)
			return;

		if (mCheckComments) {
			// Full document scan: walk every character once to tag glyphs as belonging to
			// single-line comments, block comments, strings, or preprocessor directives.
			// This must run before ColorizeRange which only handles per-line tokenization.
			int endLine = (int)mLines.size();
			auto endIndex = 0;
			auto commentStartLine = endLine;
			auto commentStartIndex = endIndex;
			auto withinString = false;
			auto withinSingleLineComment = false;
			auto withinPreproc = false;
			auto firstChar = true;	  // there is no other non-whitespace characters in the line before
			auto concatenate = false; // '\' on the very end of the line
			auto currentLine = 0;
			auto currentIndex = 0;
			while (currentLine < endLine || currentIndex < endIndex) {
				auto& line = mLines[currentLine];

				if (currentIndex == 0 && !concatenate) {
					withinSingleLineComment = false;
					withinPreproc = false;
					firstChar = true;
				}

				concatenate = false;

				if (!line.empty()) {
					auto& g = line[currentIndex];
					auto c = g.mChar;

					if (c != mLanguageDefinition->mPreprocChar && !char_isspace(c))
						firstChar = false;

					if (currentIndex == (int)line.size() - 1 && line[line.size() - 1].mChar == '\\')
						concatenate = true;

					bool inComment = (commentStartLine < currentLine || (commentStartLine == currentLine && commentStartIndex <= currentIndex));

					if (withinString) {
						line[currentIndex].mMultiLineComment = inComment;

						if (c == '\"') {
							if (currentIndex + 1 < (int)line.size() && line[currentIndex + 1].mChar == '\"') {
								currentIndex += 1;
								if (currentIndex < (int)line.size())
									line[currentIndex].mMultiLineComment = inComment;
							} else
								withinString = false;
						} else if (c == '\\') {
							currentIndex += 1;
							if (currentIndex < (int)line.size())
								line[currentIndex].mMultiLineComment = inComment;
						}
					} else {
						if (firstChar && c == mLanguageDefinition->mPreprocChar)
							withinPreproc = true;

						if (c == '\"') {
							withinString = true;
							line[currentIndex].mMultiLineComment = inComment;
						} else {
							auto pred = [](const char& a, const Glyph& b) { return a == b.mChar; };
							auto from = line.begin() + currentIndex;
							auto& startStr = mLanguageDefinition->mCommentStart;
							auto& singleStartStr = mLanguageDefinition->mSingleLineComment;

							if (!withinSingleLineComment && currentIndex + startStr.size() <= line.size() &&
								ColorizerEquals(startStr.begin(), startStr.end(), from, from + startStr.size(), pred)) {
								commentStartLine = currentLine;
								commentStartIndex = currentIndex;
							} else if (singleStartStr.size() > 0 && currentIndex + singleStartStr.size() <= line.size() &&
									   ColorizerEquals(singleStartStr.begin(), singleStartStr.end(), from, from + singleStartStr.size(), pred)) {
								withinSingleLineComment = true;
							}

							inComment = (commentStartLine < currentLine || (commentStartLine == currentLine && commentStartIndex <= currentIndex));

							line[currentIndex].mMultiLineComment = inComment;
							line[currentIndex].mComment = withinSingleLineComment;

							auto& endStr = mLanguageDefinition->mCommentEnd;
							if (currentIndex + 1 >= (int)endStr.size() && ColorizerEquals(endStr.begin(), endStr.end(), from + 1 - endStr.size(), from + 1, pred)) {
								commentStartIndex = endIndex;
								commentStartLine = endLine;
							}
						}
					}
					if (currentIndex < (int)line.size())
						line[currentIndex].mPreprocessor = withinPreproc;
					currentIndex += UTF8CharLength(c);
					if (currentIndex >= (int)line.size()) {
						currentIndex = 0;
						++currentLine;
					}
				} else {
					currentIndex = 0;
					++currentLine;
				}
			}
			mCheckComments = false;
		}

		if (mColorRangeMin < mColorRangeMax) {
			// Tokenize at most `increment` lines per frame to spread the cost across multiple frames.
			// When there is no custom tokenizer the increment is small because the fallback is cheaper.
			const int increment = (mLanguageDefinition->mTokenize == nullptr) ? 10 : 10000;
			const int to = std::min(mColorRangeMin + increment, mColorRangeMax);
			ColorizeRange(mColorRangeMin, to);
			mColorRangeMin = to;

			if (mColorRangeMax == mColorRangeMin) {
				mColorRangeMin = std::numeric_limits<int>::max();
				mColorRangeMax = 0;
			}
			return;
		}
	}

	const TextEditor::Palette& TextEditor::GetDarkPalette() {
		const static Palette p = {{
			0xdcdfe4ff, // Default
			0xe06c75ff, // Keyword
			0xe5c07bff, // Number
			0x98c379ff, // String
			0xe0a070ff, // Char literal
			0x6a7384ff, // Punctuation
			0x808040ff, // Preprocessor
			0xdcdfe4ff, // Identifier
			0x61afefff, // Known identifier
			0xc678ddff, // Preproc identifier
			0x3696a2ff, // Comment (single line)
			0x3696a2ff, // Comment (multi line)
			0x282c34ff, // Background
			0xe0e0e0ff, // Cursor
			0x3875c8ff, // Selection
			0xff200080, // ErrorMarker
			0xffffff15, // ControlCharacter
			0x0080f040, // Breakpoint
			0xff8394ff, // Line number
			0x00000040, // Current line fill
			0x80808040, // Current line fill (inactive)
			0xa0a0a040, // Current line edge
		}};
		return p;
	}

	const TextEditor::Palette& TextEditor::GetMarianaPalette() {
		const static Palette p = {{
			0xffffffff, // Default
			0xc695c6ff, // Keyword
			0xf9ae58ff, // Number
			0x99c794ff, // String
			0xe0a070ff, // Char literal
			0x5fb4b4ff, // Punctuation
			0x808040ff, // Preprocessor
			0xffffffff, // Identifier
			0x4dc69bff, // Known identifier
			0xe0a0ffff, // Preproc identifier
			0xa6acb9ff, // Comment (single line)
			0xa6acb9ff, // Comment (multi line)
			0x303841ff, // Background
			0xe0e0e0ff, // Cursor
			0x6e7a8580, // Selection
			0xec5f6680, // ErrorMarker
			0xffffff30, // ControlCharacter
			0x0080f040, // Breakpoint
			0xffffffb0, // Line number
			0x4e5a6580, // Current line fill
			0x4e5a6530, // Current line fill (inactive)
			0x4e5a65b0, // Current line edge
		}};
		return p;
	}

	const TextEditor::Palette& TextEditor::GetLightPalette() {
		const static Palette p = {{
			0x404040ff, // None
			0x060cffff, // Keyword
			0xffff00ff, // Number
			0xa02020ff, // String
			0x704030ff, // Char literal
			0x000000ff, // Punctuation
			0x606040ff, // Preprocessor
			0x404040ff, // Identifier
			0x106060ff, // Known identifier
			0xa040c0ff, // Preproc identifier
			0x205020ff, // Comment (single line)
			0x205040ff, // Comment (multi line)
			0xffffffff, // Background
			0x000000ff, // Cursor
			0x00006040, // Selection
			0xff1000a0, // ErrorMarker
			0x90909090, // ControlCharacter
			0x0080f080, // Breakpoint
			0xff5050ff, // Line number
			0x00000040, // Current line fill
			0x80808040, // Current line fill (inactive)
			0x00000040, // Current line edge
		}};
		return p;
	}

	const TextEditor::Palette& TextEditor::GetRetroBluePalette() {
		const static Palette p = {{
			0xFF0000ff, // None
			0x00ffffff, // Keyword
			0xffff00ff, // Number
			0xFF80FFff, // String
			0xFF80B0ff, // Char literal
			0xffffffff, // Punctuation
			0x008000ff, // Preprocessor
			0x1fff00ff, // Identifier
			0xffffffff, // Known identifier
			0xF05070ff, // Preproc identifier
			0xFF00FFff, // Comment (single line)
			0xFF00FFff, // Comment (multi line)
			0x000080ff, // Background
			0xff8000ff, // Cursor
			0x00ffff80, // Selection
			0xff0000a0, // ErrorMarker,
			0x702070a0, // Control Character
			0x0080ff80, // Breakpoint
			0xa0a0a0f0, // Line number
			0x00000040, // Current line fill
			0x80808040, // Current line fill (inactive)
			0x00000040, // Current line edge
		}};
		return p;
	}

	const std::unordered_map<char, char> TextEditor::OPEN_TO_CLOSE_CHAR = {{'{', '}'}, {'(', ')'}, {'[', ']'}};
	const std::unordered_map<char, char> TextEditor::CLOSE_TO_OPEN_CHAR = {{'}', '{'}, {')', '('}, {']', '['}};

	TextEditor::PaletteId TextEditor::defaultPalette = TextEditor::PaletteId::Dark;
	std::vector<TextEditor::CodeLensFileData> TextEditor::sCodeLensFiles;
	std::vector<TextEditor::CodeLensParseTask> TextEditor::sCodeLensParseQueue;
	std::string TextEditor::sCodeLensActiveFilePath;
	TextEditor::LanguageDefinitionId TextEditor::sCodeLensActiveLanguage = TextEditor::LanguageDefinitionId::None;
	const TextEditor::LanguageDefinition* TextEditor::sCodeLensActiveLanguageDef = nullptr;
	std::vector<std::string> TextEditor::sCodeLensActiveLines;
	int TextEditor::sCodeLensActiveNextLine = 0;
	bool TextEditor::sCodeLensActiveParseInProgress = false;
	bool TextEditor::sCodeLensActiveSymbolsCleared = false;
	int TextEditor::sCodeLensDataVersion = 0;

	bool TextEditor::ComputeLineHasCodeLens(int aLine, const std::string& aCurrentFilePath) const {
		if (sCodeLensFiles.empty() || aLine < 0 || aLine >= (int)mLines.size())
			return false;
		const auto& lineGlyphs = mLines[aLine];
		// Check the repeat-block synthetic symbol first (file + line number key).
		const std::string repeatKey = BuildRepeatCodeLensSymbolName(aCurrentFilePath, aLine);
		for (size_t fi = 0; fi < sCodeLensFiles.size(); fi++) {
			const auto& symbols = sCodeLensFiles[fi].symbols;
			for (size_t si = 0; si < symbols.size(); si++) {
				if (symbols[si].codelensText.empty())
					continue;
				if (symbols[si].symbolName == repeatKey)
					return true;
			}
		}
		// Check word-match symbols across all files.
		const int lineSize = (int)lineGlyphs.size();
		for (size_t fi = 0; fi < sCodeLensFiles.size(); fi++) {
			const auto& symbols = sCodeLensFiles[fi].symbols;
			for (size_t si = 0; si < symbols.size(); si++) {
				const CodeLensSymbolData& sym = symbols[si];
				if (sym.codelensText.empty() || sym.symbolName.empty())
					continue;
				const int wordSize = (int)sym.symbolName.size();
				for (int ci = 0; ci + wordSize <= lineSize; ci++) {
					bool matched = true;
					for (int j = 0; j < wordSize; j++)
						if ((char)std::toupper((unsigned char)lineGlyphs[ci + j].mChar) != (char)std::toupper((unsigned char)sym.symbolName[j])) {
							matched = false;
							break;
						}
					if (!matched)
						continue;
					const bool leftBoundary = (ci == 0) || !IsIdentifierWordByte(lineGlyphs[ci - 1].mChar);
					const bool rightBoundary = (ci + wordSize >= lineSize) || !IsIdentifierWordByte(lineGlyphs[ci + wordSize].mChar);
					if (!leftBoundary || !rightBoundary)
						continue;
					bool inComment = false;
					for (int ki = ci; ki < ci + wordSize && ki < lineSize; ki++)
						if (lineGlyphs[ki].mComment || lineGlyphs[ki].mMultiLineComment) {
							inComment = true;
							break;
						}
					if (!inComment)
						return true;
				}
			}
		}
		return false;
	}

	void TextEditor::ClearCodeLensData() {
		sCodeLensFiles.clear();
		sCodeLensDataVersion++;
	}

	int TextEditor::AddCodeLensFile(const std::string& aFilePath) {
		const std::string normalizedPath = NormalizePath(aFilePath);
		for (int i = 0; i < (int)sCodeLensFiles.size(); i++)
			if (sCodeLensFiles[i].filePath == normalizedPath)
				return i;
		CodeLensFileData fileData;
		fileData.filePath = normalizedPath;
		sCodeLensFiles.push_back(std::move(fileData));
		return (int)sCodeLensFiles.size() - 1;
	}

	void TextEditor::SetCodeLensFileLanguage(const std::string& aFilePath, LanguageDefinitionId aLanguage) {
		const std::string normalizedPath = NormalizePath(aFilePath);
		for (int i = 0; i < (int)sCodeLensFiles.size(); i++)
			if (sCodeLensFiles[i].filePath == normalizedPath) {
				sCodeLensFiles[i].language = aLanguage;
				return;
			}
	}

	const std::vector<TextEditor::CodeLensFileData>& TextEditor::GetCodeLensFiles() {
		return sCodeLensFiles;
	}

	int TextEditor::AddOrUpdateCodeLensSymbol(const std::string& aFilePath, const CodeLensSymbolData& aSymbolData) {
		int fileIndex = AddCodeLensFile(aFilePath);
		auto& symbols = sCodeLensFiles[fileIndex].symbols;
		for (int i = 0; i < (int)symbols.size(); i++)
			if (symbols[i].lineNumber == aSymbolData.lineNumber && symbols[i].symbolName == aSymbolData.symbolName) {
				symbols[i] = aSymbolData;
				return i;
			}
		symbols.push_back(aSymbolData);
		return (int)symbols.size() - 1;
	}

	std::string TextEditor::ConsumeRightClickWord() {
		std::string word = std::move(mRightClickWord);
		mRightClickWord.clear();
		mRightClickPending = false;
		return word;
	}

	bool TextEditor::IsRightClickPending() const {
		return mRightClickPending;
	}

	std::string TextEditor::GetWordAt(const ImVec2& aScreenPos) const {
		(void)aScreenPos;
		return mLastHoveredWord;
	}

	bool TextEditor::AddCodeLensSymbolIfNew(const std::string& aFilePath, const CodeLensSymbolData& aSymbolData) {
		// Check all files: if any file already has a symbol with this name, do not insert.
		for (size_t fileIndex = 0; fileIndex < sCodeLensFiles.size(); fileIndex++) {
			const auto& symbols = sCodeLensFiles[fileIndex].symbols;
			for (size_t i = 0; i < symbols.size(); i++)
				if (symbols[i].symbolName == aSymbolData.symbolName)
					return false;
		}
		AddOrUpdateCodeLensSymbol(aFilePath, aSymbolData);
		return true;
	}

	int TextEditor::DeleteCodeLensSymbol(const std::string& aFilePath, const std::string& aSymbolName) {
		if (aSymbolName.empty())
			return 0;
		const std::string normalizedPath = NormalizePath(aFilePath);
		int removed = 0;
		for (size_t fileIndex = 0; fileIndex < sCodeLensFiles.size(); fileIndex++) {
			if (aFilePath != "*" && sCodeLensFiles[fileIndex].filePath != normalizedPath)
				continue;
			auto& symbols = sCodeLensFiles[fileIndex].symbols;
			for (int i = (int)symbols.size() - 1; i >= 0; i--)
				if (symbols[i].symbolName == aSymbolName) {
					symbols.erase(symbols.begin() + i);
					removed++;
				}
		}
		if (removed > 0)
			sCodeLensDataVersion++;
		return removed;
	}

	int TextEditor::AddCodeLensError(const std::string& aFilePath, int aLineNumber, const std::string& aMessage) {
		int fileIndex = AddCodeLensFile(aFilePath);
		auto& errors = sCodeLensFiles[fileIndex].errors;
		for (int i = 0; i < (int)errors.size(); i++)
			if (errors[i].first == aLineNumber && errors[i].second == aMessage)
				return i;
		errors.push_back(std::make_pair(aLineNumber, aMessage));
		return (int)errors.size() - 1;
	}

	bool TextEditor::ParseCodeLensFromText(const std::string& aFilePath, const std::string& aText, LanguageDefinitionId aLanguage) {
		int fileIndex = AddCodeLensFile(aFilePath);
		sCodeLensFiles[fileIndex].language = aLanguage;
		sCodeLensFiles[fileIndex].symbols.clear();
		sCodeLensFiles[fileIndex].errors.clear();
		const LanguageDefinition* languageDefinition = GetLanguageDefinitionForId(aLanguage);
		if (languageDefinition == nullptr || languageDefinition->mCodeLensLineParser == nullptr)
			return true;
		if (languageDefinition->mCodeLensParseStart != nullptr)
			languageDefinition->mCodeLensParseStart(aFilePath, (void*)&languageDefinition->mTimingType);
		std::istringstream stream(aText);
		std::string lineText;
		int lineNumber = 0;
		while (std::getline(stream, lineText)) {
			if (!lineText.empty() && lineText.back() == '\r')
				lineText.pop_back();
			languageDefinition->mCodeLensLineParser(lineNumber, aFilePath, lineText);
			lineNumber++;
		}
		if (languageDefinition->mCodeLensParseEnd != nullptr)
			languageDefinition->mCodeLensParseEnd(aFilePath);
		sCodeLensDataVersion++;
		return true;
	}

	bool TextEditor::ParseCodeLensFromFile(const std::string& aFilePath, LanguageDefinitionId aLanguage) {
		std::ifstream file(aFilePath, std::ios::binary);
		if (!file.good())
			return false;
		std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		return ParseCodeLensFromText(aFilePath, text, aLanguage);
	}

	const TextEditor::LanguageDefinition* TextEditor::GetLanguageDefinitionForId(LanguageDefinitionId aId) {
		switch (aId) {
			case LanguageDefinitionId::Cpp:
				return &LanguageDefinition::Cpp();
			case LanguageDefinitionId::C:
				return &LanguageDefinition::C();
			case LanguageDefinitionId::Cs:
				return &LanguageDefinition::Cs();
			case LanguageDefinitionId::Python:
				return &LanguageDefinition::Python();
			case LanguageDefinitionId::Lua:
				return &LanguageDefinition::Lua();
			case LanguageDefinitionId::Json:
				return &LanguageDefinition::Json();
			case LanguageDefinitionId::Sql:
				return &LanguageDefinition::Sql();
			case LanguageDefinitionId::AngelScript:
				return &LanguageDefinition::AngelScript();
			case LanguageDefinitionId::Glsl:
				return &LanguageDefinition::Glsl();
			case LanguageDefinitionId::Hlsl:
				return &LanguageDefinition::Hlsl();
			case LanguageDefinitionId::Z80Asm:
				return &LanguageDefinition::Z80Asm();
			default:
				return nullptr;
		}
	}

	void TextEditor::EnqueueCodeLensFile(const std::string& aFilePath, LanguageDefinitionId aLanguage, const std::vector<std::string>& aLines, bool aHighPriority) {
		const std::string normalizedPath = NormalizePath(aFilePath);
		// If this file is currently being parsed, abort that run; the new snapshot supersedes it.
		if (sCodeLensActiveParseInProgress && sCodeLensActiveFilePath == normalizedPath) {
			if (sCodeLensActiveLanguageDef != nullptr && sCodeLensActiveLanguageDef->mCodeLensParseEnd != nullptr)
				sCodeLensActiveLanguageDef->mCodeLensParseEnd(aFilePath);
			sCodeLensActiveParseInProgress = false;
			sCodeLensActiveSymbolsCleared = false;
			sCodeLensActiveLines.clear();
		}
		// Remove any existing queued entry for this file to avoid duplicate work.
		for (int i = (int)sCodeLensParseQueue.size() - 1; i >= 0; i--)
			if (sCodeLensParseQueue[i].filePath == normalizedPath)
				sCodeLensParseQueue.erase(sCodeLensParseQueue.begin() + i);
		// Build the task; empty lines means "load from disk when the task is dequeued".
		CodeLensParseTask task;
		task.filePath = normalizedPath;
		task.language = aLanguage;
		task.lines = aLines;
		if (aHighPriority)
			sCodeLensParseQueue.insert(sCodeLensParseQueue.begin(), std::move(task));
		else
			sCodeLensParseQueue.push_back(std::move(task));
	}

	bool TextEditor::TickGlobalCodeLensParse(int aMaxLines) {
		if (!sCodeLensActiveParseInProgress) {
			if (sCodeLensParseQueue.empty())
				return false;
			// Dequeue the next file to parse.
			CodeLensParseTask task = std::move(sCodeLensParseQueue.front());
			sCodeLensParseQueue.erase(sCodeLensParseQueue.begin());
			const LanguageDefinition* langDef = GetLanguageDefinitionForId(task.language);
			if (langDef == nullptr || langDef->mCodeLensLineParser == nullptr)
				return !sCodeLensParseQueue.empty();
			// No snapshot provided: load file from disk now.
			if (task.lines.empty()) {
				std::ifstream file(task.filePath, std::ios::binary);
				if (!file.good())
					return !sCodeLensParseQueue.empty();
				std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
				std::istringstream stream(content);
				std::string lineText;
				while (std::getline(stream, lineText)) {
					if (!lineText.empty() && lineText.back() == '\r')
						lineText.pop_back();
					task.lines.push_back(std::move(lineText));
				}
			}
			// Clear old symbols and start the parse.
			int fileIndex = AddCodeLensFile(task.filePath);
			sCodeLensFiles[fileIndex].language = task.language;
			sCodeLensFiles[fileIndex].symbols.clear();
			sCodeLensFiles[fileIndex].errors.clear();
			sCodeLensActiveFilePath = std::move(task.filePath);
			sCodeLensActiveLanguage = task.language;
			sCodeLensActiveLanguageDef = langDef;
			sCodeLensActiveLines = std::move(task.lines);
			sCodeLensActiveNextLine = 0;
			sCodeLensActiveSymbolsCleared = true;
			if (langDef->mCodeLensParseStart != nullptr)
				langDef->mCodeLensParseStart(sCodeLensActiveFilePath, (void*)&langDef->mTimingType);
			sCodeLensActiveParseInProgress = true;
		}
		// Feed up to aMaxLines lines to the active parser.
		const int totalLines = (int)sCodeLensActiveLines.size();
		const int endLine = std::min(sCodeLensActiveNextLine + aMaxLines, totalLines);
		for (int i = sCodeLensActiveNextLine; i < endLine; i++)
			sCodeLensActiveLanguageDef->mCodeLensLineParser(i, sCodeLensActiveFilePath, sCodeLensActiveLines[i]);
		sCodeLensActiveNextLine = endLine;
		if (sCodeLensActiveNextLine >= totalLines) {
			// Parse complete: notify the language and release active state.
			if (sCodeLensActiveLanguageDef->mCodeLensParseEnd != nullptr)
				sCodeLensActiveLanguageDef->mCodeLensParseEnd(sCodeLensActiveFilePath);
			sCodeLensActiveParseInProgress = false;
			sCodeLensActiveSymbolsCleared = false;
			sCodeLensActiveLines.clear();
			sCodeLensDataVersion++;
		}
		return sCodeLensActiveParseInProgress || !sCodeLensParseQueue.empty();
	}

	bool TextEditor::IsCodeLensParsingPending() {
		return sCodeLensActiveParseInProgress || !sCodeLensParseQueue.empty();
	}

	void TextEditor::ImGuiDebugPanel(const std::string& panelName) {
		ImGui::Begin(panelName.c_str());
		if (ImGui::CollapsingHeader("Editor state info")) {
			ImGui::Checkbox("Panning", &mPanning);
			ImGui::Checkbox("Dragging selection", &mDraggingSelection);
			ImGui::DragInt("Cursor count", &mState.mCurrentCursor);
			for (int i = 0; i <= mState.mCurrentCursor; i++) {
				Coordinates sanitizedStart = SanitizeCoordinates(mState.mCursors[i].mInteractiveStart);
				Coordinates sanitizedEnd = SanitizeCoordinates(mState.mCursors[i].mInteractiveEnd);
				ImGui::DragInt2("Interactive start", &mState.mCursors[i].mInteractiveStart.mLine);
				ImGui::DragInt2("Interactive end", &mState.mCursors[i].mInteractiveEnd.mLine);
				ImGui::Text("Sanitized start: %d, %d", sanitizedStart.mLine, sanitizedStart.mColumn);
				ImGui::Text("Sanitized end:   %d, %d", sanitizedEnd.mLine, sanitizedEnd.mColumn);
			}
		}
		if (ImGui::CollapsingHeader("Lines")) {
			for (int i = 0; i < (int)mLines.size(); i++) {
				ImGui::Text("%zu", mLines[i].size());
			}
		}
		if (ImGui::CollapsingHeader("Undo")) {
			ImGui::Text("Number of records: %zu", mUndoBuffer.size());
			ImGui::DragInt("Undo index", &mUndoIndex);
			for (int i = 0; i < (int)mUndoBuffer.size(); i++) {
				if (ImGui::CollapsingHeader(std::to_string(i).c_str())) {
					ImGui::Text("Operations");
					for (int j = 0; j < (int)mUndoBuffer[i].mOperations.size(); j++) {
						ImGui::Text("%s", mUndoBuffer[i].mOperations[j].mText.c_str());
						ImGui::Text(mUndoBuffer[i].mOperations[j].mType == UndoOperationType::Add ? "Add" : "Delete");
						ImGui::DragInt2("Start", &mUndoBuffer[i].mOperations[j].mStart.mLine);
						ImGui::DragInt2("End", &mUndoBuffer[i].mOperations[j].mEnd.mLine);
						ImGui::Separator();
					}
				}
			}
		}
		ImGui::End();
	}
} // namespace ImGui
