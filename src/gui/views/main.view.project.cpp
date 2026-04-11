// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Gui
//
// Main view -- project panel (SDK, Project, Files trees).
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#include "main.view.project.h"
#include "main.view.documents.h"
#include <views/image/document.image.h>
#include <views/bitmaps/document.bitmap.h>
#include <views/tiles/document.tiles.h>
#include <views/sprites/document.sprite.h>
#include <views/maps/document.map.h>
#include <views/text/document.text.h>
#include <views/build/document.build.h>
#include <views/data/document.data.h>
#include <views/palette/document.palette.h>
#include <app/app.icons.mdi.h>
#include <app/app.console.h>
#include <dialogs/dialog.confirm.h>
#include <imgui.text.editor.h>
#include <assets/image/image.h>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include <fstream>

namespace RetrodevGui {
	//
	// Global flag to force tree rebuild
	//
	bool g_forceProjectTreeRebuild = false;
	//
	// Pending scroll-to build item (set when a new build item is added)
	//
	static std::string g_pendingScrollToBuildItem;
	//
	// Map source extension to text-editor language id used by codelens parsing
	//
	static ImGui::TextEditor::LanguageDefinitionId GetCodeLensLanguageForPath(const std::filesystem::path& path) {
		static const std::unordered_map<std::string, ImGui::TextEditor::LanguageDefinitionId> extMap = {
			{".c", ImGui::TextEditor::LanguageDefinitionId::C},
			{".h", ImGui::TextEditor::LanguageDefinitionId::C},
			{".cpp", ImGui::TextEditor::LanguageDefinitionId::Cpp},
			{".hpp", ImGui::TextEditor::LanguageDefinitionId::Cpp},
			{".cxx", ImGui::TextEditor::LanguageDefinitionId::Cpp},
			{".asm", ImGui::TextEditor::LanguageDefinitionId::Z80Asm},
			{".as", ImGui::TextEditor::LanguageDefinitionId::AngelScript},
		};
		std::string ext = path.extension().string();
		auto it = extMap.find(ext);
		if (it != extMap.end())
			return it->second;
		return ImGui::TextEditor::LanguageDefinitionId::None;
	}
	//
	// Rebuild global codelens cache from project files
	//
	static void RefreshProjectCodeLens(const std::string& projectRootPath) {
		ImGui::TextEditor::ClearCodeLensData();
		if (projectRootPath.empty())
			return;
		// Iterate the registered file lists directly instead of scanning the filesystem.
		// A filesystem scan starting at projectRootPath would miss SDK files whose stored
		// paths begin with $(sdk) and resolve to a directory outside the project root.
		// Project::ExpandPath() resolves both $(sdk)/... and regular relative paths to
		// absolute paths that can be directly passed to EnqueueCodeLensFile.
		auto enqueueStoredPaths = [](const std::vector<std::string>& storedPaths) {
			for (const auto& storedPath : storedPaths) {
				std::string absPath = RetrodevLib::Project::ExpandPath(storedPath);
				ImGui::TextEditor::LanguageDefinitionId language = GetCodeLensLanguageForPath(std::filesystem::path(absPath));
				if (language == ImGui::TextEditor::LanguageDefinitionId::None)
					continue;
				ImGui::TextEditor::EnqueueCodeLensFile(absPath, language);
			}
		};
		enqueueStoredPaths(RetrodevLib::Project::GetSourceFiles());
		enqueueStoredPaths(RetrodevLib::Project::GetScriptFiles());
	}
	//
	// New Map dialog state
	//
	static bool g_showNewMapDialog = false;
	static char g_newMapNameBuf[256] = "";
	//
	// New Build dialog state
	//
	static bool g_showNewBuildDialog = false;
	static char g_newBuildNameBuf[256] = "";
	//
	// New Palette dialog state
	//
	static bool g_showNewPaletteDialog = false;
	static char g_newPaletteNameBuf[256] = "";
	//
	// New Virtual Folder dialog state
	//
	static bool g_showNewVirtualFolderDialog = false;
	static std::string g_newVirtualFolderParentPath;
	static char g_newVirtualFolderNameBuf[256] = "";
	//
	// Rename Virtual Folder dialog state
	//
	static bool g_showRenameVirtualFolderDialog = false;
	static std::string g_renameVirtualFolderOldPath;
	static char g_renameVirtualFolderNameBuf[256] = "";
	//
	// Rename Folder (filesystem) dialog state
	//
	static bool g_showRenameFolderDialog = false;
	static std::filesystem::path g_renameFolderOldPath;
	static char g_renameFolderNameBuf[256] = "";
	//
	// New Folder dialog state
	//
	static bool g_showNewFolderDialog = false;
	static std::string g_newFolderParentPath;
	static char g_newFolderNameBuf[256] = "";
	//
	// New File (text/source) dialog state
	//
	static bool g_showNewFileDialog = false;
	static std::string g_newFileParentPath;
	static char g_newFileNameBuf[256] = "";
	//
	// New Image dialog state
	//
	static bool g_showNewImageDialog = false;
	static std::string g_newImageParentPath;
	static char g_newImageNameBuf[256] = "";
	static int g_newImageWidth = 320;
	static int g_newImageHeight = 200;
	//
	// Tracks whether a modal was just opened so we can autofocus its first InputText
	//
	static bool g_popupJustOpened = false;
	//
	// Drag and drop payload for build items
	//
	struct BuildItemDragPayload {
		char buildItemPath[512];
		RetrodevLib::ProjectBuildType buildItemType;
	};
	//
	// Drag and drop payload for files
	//
	struct FileDragPayload {
		char filePath[512];
	};
	//
	// Return the absolute path to the sdk folder next to the executable, or empty if it does not exist.
	// Calls SDL_GetBasePath() once and caches the result.
	//
	static const std::string& GetSdkFolderPath() {
		static std::string cachedPath;
		static bool checked = false;
		if (!checked) {
			checked = true;
			const char* base = SDL_GetBasePath();
			if (base) {
				std::filesystem::path sdkPath = std::filesystem::path(base) / "sdk";
				std::error_code ec;
				if (std::filesystem::is_directory(sdkPath, ec)) {
					cachedPath = sdkPath.string();
					//
					// Register the SDK path with the project library so it can store
					// SDK-rooted file paths as $(sdk)/... instead of fragile relative paths
					//
					RetrodevLib::Project::SetSdkFolder(cachedPath);
				}
			}
		}
		return cachedPath;
	}
	//
	// Return the folder prefix to pre-fill when creating a new map from the given node
	// For a directory node: buildItemPath + "/"
	// For a leaf build item: directory part of buildItemPath + "/"
	// For the Build root (empty buildItemPath): ""
	//
	static std::string GetBuildPathPrefix(const FileTreeNode& node) {
		if (node.isBuildItem) {
			size_t slash = node.buildItemPath.rfind('/');
			if (slash != std::string::npos)
				return node.buildItemPath.substr(0, slash + 1);
			return "";
		}
		if (!node.buildItemPath.empty())
			return node.buildItemPath + "/";
		return "";
	}
	//
	// Notify the project view that the project has changed
	// Sets a flag to trigger tree rebuild on next frame
	//
	void ProjectView::NotifyProjectChanged() {
		g_forceProjectTreeRebuild = true;
	}
	//
	// Get the appropriate icon for a file based on its extension
	//
	const char* ProjectView::GetFileIcon(const std::filesystem::path& path) {
		static const std::unordered_map<std::string, const char*> iconMap = {// Image files
																			 {".png", ICON_FILE_IMAGE},
																			 {".jpg", ICON_FILE_IMAGE},
																			 {".jpeg", ICON_FILE_IMAGE},
																			 {".bmp", ICON_FILE_IMAGE},
																			 {".tga", ICON_FILE_IMAGE},
																			 {".gif", ICON_FILE_IMAGE},
																			 {".svg", ICON_FILE_IMAGE},
																			 {".ico", ICON_FILE_IMAGE},
																			 // Audio files
																			 {".mp3", ICON_FILE_MUSIC},
																			 {".wav", ICON_FILE_MUSIC},
																			 {".ogg", ICON_FILE_MUSIC},
																			 {".flac", ICON_FILE_MUSIC},
																			 {".aac", ICON_FILE_MUSIC},
																			 // Video files
																			 {".mp4", ICON_FILE_VIDEO},
																			 {".avi", ICON_FILE_VIDEO},
																			 {".mkv", ICON_FILE_VIDEO},
																			 {".mov", ICON_FILE_VIDEO},
																			 {".wmv", ICON_FILE_VIDEO},
																			 // Document / text files
																			 {".txt", ICON_FILE_DOCUMENT},
																			 {".md", ICON_FILE_DOCUMENT},
																			 {".log", ICON_FILE_DOCUMENT},
																			 {".csv", ICON_FILE_DOCUMENT},
																			 {".ini", ICON_FILE_DOCUMENT},
																			 {".cfg", ICON_FILE_DOCUMENT},
																			 {".json", ICON_FILE_DOCUMENT},
																			 {".xml", ICON_FILE_DOCUMENT},
																			 {".yaml", ICON_FILE_DOCUMENT},
																			 {".yml", ICON_FILE_DOCUMENT},
																			 // Code / source files
																			 {".c", ICON_FILE_CODE},
																			 {".cpp", ICON_FILE_CODE},
																			 {".h", ICON_FILE_CODE},
																			 {".hpp", ICON_FILE_CODE},
																			 {".cs", ICON_FILE_CODE},
																			 {".java", ICON_FILE_CODE},
																			 {".py", ICON_FILE_CODE},
																			 {".js", ICON_FILE_CODE},
																			 {".ts", ICON_FILE_CODE},
																			 {".html", ICON_FILE_CODE},
																			 {".css", ICON_FILE_CODE},
																			 {".lua", ICON_FILE_CODE},
																			 {".asm", ICON_FILE_CODE},
																			 {".s", ICON_FILE_CODE},
																			 // AngelScript files
																			 {".as", ICON_SCRIPT_TEXT},
																			 // PDF
																			 {".pdf", ICON_FILE_PDF_BOX},
																			 // Office documents
																			 {".doc", ICON_FILE_WORD},
																			 {".docx", ICON_FILE_WORD},
																			 {".xls", ICON_FILE_EXCEL},
																			 {".xlsx", ICON_FILE_EXCEL},
																			 {".ppt", ICON_FILE_POWERPOINT},
																			 {".pptx", ICON_FILE_POWERPOINT},
																			 // Archive files
																			 {".zip", ICON_FOLDER_ZIP},
																			 {".rar", ICON_FOLDER_ZIP},
																			 {".7z", ICON_FOLDER_ZIP},
																			 {".tar", ICON_FOLDER_ZIP},
																			 {".gz", ICON_FOLDER_ZIP}};

		if (path.has_extension()) {
			std::string ext = path.extension().string();
			auto it = iconMap.find(ext);
			if (it != iconMap.end())
				return it->second;
		}
		return ICON_FILE;
	}

	//
	// Render context menu actions for a single node. Both actions are visible; unavailable one is disabled.
	//
	void ProjectView::RenderContextMenu(FileTreeNode& node) {
		if (node.isDirectory) {
			if (!ImGui::BeginPopupContextItem())
				return;
			//
			// Context menu for directory nodes inside the Build section
			//
			if (node.inBuildSection) {
				if (ImGui::MenuItem(ICON_MAP " New Map...")) {
					std::string prefix = GetBuildPathPrefix(node);
					memset(g_newMapNameBuf, 0, sizeof(g_newMapNameBuf));
					if (!prefix.empty() && prefix.size() < sizeof(g_newMapNameBuf))
						memcpy(g_newMapNameBuf, prefix.c_str(), prefix.size());
					g_showNewMapDialog = true;
				}
				if (ImGui::MenuItem(ICON_HAMMER " New Build...")) {
					std::string prefix = GetBuildPathPrefix(node);
					memset(g_newBuildNameBuf, 0, sizeof(g_newBuildNameBuf));
					if (!prefix.empty() && prefix.size() < sizeof(g_newBuildNameBuf))
						memcpy(g_newBuildNameBuf, prefix.c_str(), prefix.size());
					g_showNewBuildDialog = true;
				}
				if (ImGui::MenuItem(ICON_PALETTE " New Palette...")) {
					std::string prefix = GetBuildPathPrefix(node);
					memset(g_newPaletteNameBuf, 0, sizeof(g_newPaletteNameBuf));
					if (!prefix.empty() && prefix.size() < sizeof(g_newPaletteNameBuf))
						memcpy(g_newPaletteNameBuf, prefix.c_str(), prefix.size());
					g_showNewPaletteDialog = true;
				}
				//
				// Virtual folder creation: available on the Build root and on any build-section folder
				//
				if (ImGui::MenuItem(ICON_FOLDER_PLUS " New Folder...")) {
					g_newVirtualFolderParentPath = node.buildItemPath;
					memset(g_newVirtualFolderNameBuf, 0, sizeof(g_newVirtualFolderNameBuf));
					g_showNewVirtualFolderDialog = true;
				}
				//
				// Rename/Remove only available for explicitly-created virtual folders
				//
				if (node.isVirtualFolder) {
					ImGui::Separator();
					if (ImGui::MenuItem(ICON_PENCIL " Rename Folder...")) {
						g_renameVirtualFolderOldPath = node.buildItemPath;
						//
						// Pre-fill with the leaf name (last path segment) so the user edits just the label
						//
						std::string leafName = node.buildItemPath;
						size_t slash = leafName.rfind('/');
						if (slash != std::string::npos)
							leafName = leafName.substr(slash + 1);
						memset(g_renameVirtualFolderNameBuf, 0, sizeof(g_renameVirtualFolderNameBuf));
						if (leafName.size() < sizeof(g_renameVirtualFolderNameBuf))
							memcpy(g_renameVirtualFolderNameBuf, leafName.c_str(), leafName.size());
						g_showRenameVirtualFolderDialog = true;
					}
					if (ImGui::MenuItem(ICON_FOLDER_REMOVE " Remove Folder")) {
						std::string folderToRemove = node.buildItemPath;
						ConfirmDialog::Show("Remove folder \"" + folderToRemove + "\"?\nAll items inside will lose the folder qualifier.", [folderToRemove]() {
							if (RetrodevLib::Project::FolderRemove(folderToRemove))
								g_forceProjectTreeRebuild = true;
						});
					}
				}
				ImGui::EndPopup();
				return;
			}
			//
			// New submenu: create folder, text file, or image inside this folder
			//
			if (ImGui::BeginMenu(ICON_PLUS " New")) {
				if (ImGui::MenuItem(ICON_FOLDER_PLUS " Folder...")) {
					g_newFolderParentPath = node.fullPath.string();
					memset(g_newFolderNameBuf, 0, sizeof(g_newFolderNameBuf));
					g_showNewFolderDialog = true;
				}
				if (ImGui::MenuItem(ICON_FILE_CODE " Source File...")) {
					g_newFileParentPath = node.fullPath.string();
					memset(g_newFileNameBuf, 0, sizeof(g_newFileNameBuf));
					g_showNewFileDialog = true;
				}
				if (ImGui::MenuItem(ICON_FILE_IMAGE " Image...")) {
					g_newImageParentPath = node.fullPath.string();
					memset(g_newImageNameBuf, 0, sizeof(g_newImageNameBuf));
					g_newImageWidth = 320;
					g_newImageHeight = 200;
					g_showNewImageDialog = true;
				}
				ImGui::EndMenu();
			}
			//
			// Rename is available for any non-root filesystem folder
			//
			if (!node.isRoot) {
				ImGui::Separator();
				if (ImGui::MenuItem(ICON_PENCIL " Rename Folder...")) {
					g_renameFolderOldPath = node.fullPath;
					std::string leafName = node.fullPath.filename().string();
					memset(g_renameFolderNameBuf, 0, sizeof(g_renameFolderNameBuf));
					if (leafName.size() < sizeof(g_renameFolderNameBuf))
						memcpy(g_renameFolderNameBuf, leafName.c_str(), leafName.size());
					g_showRenameFolderDialog = true;
				}
			}
			ImGui::Separator();
			bool hasProjectFiles = false;
			bool hasNonProjectFiles = false;
			auto scanNode = [&](auto&& self, const FileTreeNode& current) -> void {
				if (!current.isDirectory) {
					if (current.inProject)
						hasProjectFiles = true;
					else
						hasNonProjectFiles = true;
					return;
				}
				for (const auto& child : current.children)
					self(self, child);
			};
			scanNode(scanNode, node);
			ImGui::BeginDisabled(!hasProjectFiles);
			if (ImGui::MenuItem("Remove items")) {
				std::string folderPath = node.fullPath.string();
				ConfirmDialog::Show("Remove all project items in this folder?", [folderPath]() {
					bool anyChanged = false;
					std::error_code ec;
					for (std::filesystem::recursive_directory_iterator it(folderPath, ec), end; it != end && !ec; it.increment(ec)) {
						const auto& entry = *it;
						if (entry.is_directory())
							continue;
						if (RetrodevLib::Project::RemoveFile(entry.path().string()))
							anyChanged = true;
					}
					if (anyChanged)
						g_forceProjectTreeRebuild = true;
				});
			}
			ImGui::EndDisabled();
			ImGui::BeginDisabled(!hasNonProjectFiles);
			if (ImGui::MenuItem("Include items")) {
				bool anyChanged = false;
				std::error_code ec;
				for (std::filesystem::recursive_directory_iterator it(node.fullPath, ec), end; it != end && !ec; it.increment(ec)) {
					const auto& entry = *it;
					if (entry.is_directory())
						continue;
					if (RetrodevLib::Project::AddFile(entry.path().string(), GetFileType(entry.path())))
						anyChanged = true;
				}
				if (anyChanged)
					g_forceProjectTreeRebuild = true;
			}
			ImGui::EndDisabled();
			ImGui::EndPopup();
			return;
		}
		if (!ImGui::BeginPopupContextItem())
			return;
		//
		// Context menu for build items (only show removal options)
		//
		if (node.isBuildItem) {
			//
			// Determine build item type
			//
			bool isBitmap = (node.buildItemType == RetrodevLib::ProjectBuildType::Bitmap);
			bool isTileset = (node.buildItemType == RetrodevLib::ProjectBuildType::Tilemap);
			bool isSprite = (node.buildItemType == RetrodevLib::ProjectBuildType::Sprite);
			bool isMap = (node.buildItemType == RetrodevLib::ProjectBuildType::Map);
			bool isBuild = (node.buildItemType == RetrodevLib::ProjectBuildType::Build);
			//
			// Use the full build item path for removal
			//
			std::string buildItemName = node.buildItemPath.empty() ? node.name : node.buildItemPath;
			//
			// Remove bitmap conversion
			//
			ImGui::BeginDisabled(!isBitmap);
			if (ImGui::MenuItem("Remove bitmap conversion")) {
				ConfirmDialog::Show("Remove bitmap conversion \"" + buildItemName + "\"?", [buildItemName]() {
					if (RetrodevLib::Project::BitmapRemove(buildItemName)) {
						DocumentsView::CloseBuildItemDocument(buildItemName, RetrodevLib::ProjectBuildType::Bitmap);
						AppConsole::AddLogF(AppConsole::LogLevel::Info, "Removed bitmap conversion: %s", buildItemName.c_str());
						g_forceProjectTreeRebuild = true;
					}
				});
			}
			ImGui::EndDisabled();
			//
			// Remove tileset conversion
			//
			ImGui::BeginDisabled(!isTileset);
			if (ImGui::MenuItem("Remove tileset conversion")) {
				ConfirmDialog::Show("Remove tileset conversion \"" + buildItemName + "\"?", [buildItemName]() {
					if (RetrodevLib::Project::TilesetRemove(buildItemName)) {
						DocumentsView::CloseBuildItemDocument(buildItemName, RetrodevLib::ProjectBuildType::Tilemap);
						AppConsole::AddLogF(AppConsole::LogLevel::Info, "Removed tileset conversion: %s", buildItemName.c_str());
						g_forceProjectTreeRebuild = true;
					}
				});
			}
			ImGui::EndDisabled();
			//
			// Remove sprites conversion
			//
			ImGui::BeginDisabled(!isSprite);
			if (ImGui::MenuItem("Remove sprites conversion")) {
				ConfirmDialog::Show("Remove sprites conversion \"" + buildItemName + "\"?", [buildItemName]() {
					if (RetrodevLib::Project::SpriteRemove(buildItemName)) {
						DocumentsView::CloseBuildItemDocument(buildItemName, RetrodevLib::ProjectBuildType::Sprite);
						AppConsole::AddLogF(AppConsole::LogLevel::Info, "Removed sprite conversion: %s", buildItemName.c_str());
						g_forceProjectTreeRebuild = true;
					}
				});
			}
			ImGui::EndDisabled();
			//
			// Remove map
			//
			ImGui::BeginDisabled(!isMap);
			if (ImGui::MenuItem("Remove map")) {
				ConfirmDialog::Show("Remove map \"" + buildItemName + "\"?", [buildItemName]() {
					if (RetrodevLib::Project::MapRemove(buildItemName)) {
						DocumentsView::CloseBuildItemDocument(buildItemName, RetrodevLib::ProjectBuildType::Map);
						AppConsole::AddLogF(AppConsole::LogLevel::Info, "Removed map: %s", buildItemName.c_str());
						g_forceProjectTreeRebuild = true;
					}
				});
			}
			ImGui::EndDisabled();
			//
			// Remove build script
			//
			ImGui::BeginDisabled(!isBuild);
			if (ImGui::MenuItem("Remove build")) {
				ConfirmDialog::Show("Remove build \"" + buildItemName + "\"?", [buildItemName]() {
					if (RetrodevLib::Project::BuildRemove(buildItemName)) {
						DocumentsView::CloseBuildItemDocument(buildItemName, RetrodevLib::ProjectBuildType::Build);
						AppConsole::AddLogF(AppConsole::LogLevel::Info, "Removed build: %s", buildItemName.c_str());
						g_forceProjectTreeRebuild = true;
					}
				});
			}
			ImGui::EndDisabled();
			//
			// Remove palette
			//
			bool isPalette = (node.buildItemType == RetrodevLib::ProjectBuildType::Palette);
			ImGui::BeginDisabled(!isPalette);
			if (ImGui::MenuItem("Remove palette")) {
				ConfirmDialog::Show("Remove palette \"" + buildItemName + "\"?", [buildItemName]() {
					if (RetrodevLib::Project::PaletteRemove(buildItemName)) {
						DocumentsView::CloseBuildItemDocument(buildItemName, RetrodevLib::ProjectBuildType::Palette);
						AppConsole::AddLogF(AppConsole::LogLevel::Info, "Removed palette: %s", buildItemName.c_str());
						g_forceProjectTreeRebuild = true;
					}
				});
			}
			ImGui::EndDisabled();
			ImGui::Separator();
			//
			// New Map shortcut: pre-fill with the same folder prefix as this item
			//
			if (ImGui::MenuItem(ICON_MAP " New Map...")) {
				std::string prefix = GetBuildPathPrefix(node);
				memset(g_newMapNameBuf, 0, sizeof(g_newMapNameBuf));
				if (!prefix.empty() && prefix.size() < sizeof(g_newMapNameBuf))
					memcpy(g_newMapNameBuf, prefix.c_str(), prefix.size());
				g_showNewMapDialog = true;
			}
			if (ImGui::MenuItem(ICON_HAMMER " New Build...")) {
				std::string prefix = GetBuildPathPrefix(node);
				memset(g_newBuildNameBuf, 0, sizeof(g_newBuildNameBuf));
				if (!prefix.empty() && prefix.size() < sizeof(g_newBuildNameBuf))
					memcpy(g_newBuildNameBuf, prefix.c_str(), prefix.size());
				g_showNewBuildDialog = true;
			}
			if (ImGui::MenuItem(ICON_PALETTE " New Palette...")) {
				std::string prefix = GetBuildPathPrefix(node);
				memset(g_newPaletteNameBuf, 0, sizeof(g_newPaletteNameBuf));
				if (!prefix.empty() && prefix.size() < sizeof(g_newPaletteNameBuf))
					memcpy(g_newPaletteNameBuf, prefix.c_str(), prefix.size());
				g_showNewPaletteDialog = true;
			}
			ImGui::EndPopup();
			return;
		}
		// Context menu for regular files
		// "Remove from Project" enabled only when the item is already in the project
		ImGui::BeginDisabled(!node.inProject);
		if (ImGui::MenuItem("Remove from Project")) {
			std::string fullPathStr = node.fullPath.string();
			ConfirmDialog::Show("Remove \"" + node.name + "\" from the project?", [&node, fullPathStr]() {
				if (RetrodevLib::Project::RemoveFile(fullPathStr)) {
					node.inProject = false;
					g_forceProjectTreeRebuild = true;
				}
			});
		}
		ImGui::EndDisabled();
		// "Include in Project" enabled only when the item is not already in the project
		ImGui::BeginDisabled(node.inProject);
		if (ImGui::MenuItem("Include in Project")) {
			if (RetrodevLib::Project::AddFile(node.fullPath.string(), GetFileType(node.fullPath))) {
				node.inProject = true;
				g_forceProjectTreeRebuild = true;
			}
		}
		ImGui::EndDisabled();
		// Additional conversion actions (enabled only for Image type and in project)
		bool enableConversion = (GetFileType(node.fullPath) == RetrodevLib::ProjectFileType::Image) && node.inProject;
		ImGui::Separator();
		ImGui::BeginDisabled(!enableConversion);
		if (ImGui::MenuItem("Add bitmap conversion")) {
			// Extract bitmap name from file name (without extension)
			std::string bitmapName = node.fullPath.stem().string();
			std::string sourceFilePath = node.fullPath.string();
			if (RetrodevLib::Project::BitmapAdd(bitmapName, sourceFilePath)) {
				AppConsole::AddLogF(AppConsole::LogLevel::Info, "Added bitmap conversion: %s (source: %s)", bitmapName.c_str(), sourceFilePath.c_str());
				// Force tree rebuild on next frame
				g_forceProjectTreeRebuild = true;
				// Set pending scroll target to the new bitmap
				g_pendingScrollToBuildItem = bitmapName;
			} else {
				AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Bitmap conversion already exists: %s", bitmapName.c_str());
			}
		}
		if (ImGui::MenuItem("Add tileset conversion")) {
			//
			// Extract tileset name from file name (without extension)
			//
			std::string tilesetName = node.fullPath.stem().string();
			std::string sourceFilePath = node.fullPath.string();
			if (RetrodevLib::Project::TilesetAdd(tilesetName, sourceFilePath)) {
				AppConsole::AddLogF(AppConsole::LogLevel::Info, "Added tileset conversion: %s (source: %s)", tilesetName.c_str(), sourceFilePath.c_str());
				//
				// Force tree rebuild on next frame
				//
				g_forceProjectTreeRebuild = true;
				//
				// Set pending scroll target to the new tileset
				//
				g_pendingScrollToBuildItem = tilesetName;
			} else {
				AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Tileset conversion already exists: %s", tilesetName.c_str());
			}
		}
		if (ImGui::MenuItem("Add sprites conversion")) {
			//
			// Extract sprite name from file name (without extension)
			//
			std::string spriteName = node.fullPath.stem().string();
			std::string sourceFilePath = node.fullPath.string();
			if (RetrodevLib::Project::SpriteAdd(spriteName, sourceFilePath)) {
				AppConsole::AddLogF(AppConsole::LogLevel::Info, "Added sprite conversion: %s (source: %s)", spriteName.c_str(), sourceFilePath.c_str());
				//
				// Force tree rebuild on next frame
				//
				g_forceProjectTreeRebuild = true;
				//
				// Set pending scroll target to the new sprite
				//
				g_pendingScrollToBuildItem = spriteName;
			} else {
				AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Sprite conversion already exists: %s", spriteName.c_str());
			}
		}
		ImGui::EndDisabled();
		ImGui::EndPopup();
	}

	//
	// Get the project file type based on its extension
	//
	RetrodevLib::ProjectFileType ProjectView::GetFileType(const std::filesystem::path& path) {
		static const std::unordered_map<std::string, RetrodevLib::ProjectFileType> typeMap = {
			// Image files
			{".png", RetrodevLib::ProjectFileType::Image},
			{".jpg", RetrodevLib::ProjectFileType::Image},
			{".jpeg", RetrodevLib::ProjectFileType::Image},
			{".bmp", RetrodevLib::ProjectFileType::Image},
			{".tga", RetrodevLib::ProjectFileType::Image},
			{".gif", RetrodevLib::ProjectFileType::Image},
			{".svg", RetrodevLib::ProjectFileType::Image},
			{".ico", RetrodevLib::ProjectFileType::Image},
			// Audio files
			{".mp3", RetrodevLib::ProjectFileType::Audio},
			{".wav", RetrodevLib::ProjectFileType::Audio},
			{".ogg", RetrodevLib::ProjectFileType::Audio},
			{".flac", RetrodevLib::ProjectFileType::Audio},
			{".aac", RetrodevLib::ProjectFileType::Audio},
			// Source / code files
			{".c", RetrodevLib::ProjectFileType::Source},
			{".cpp", RetrodevLib::ProjectFileType::Source},
			{".h", RetrodevLib::ProjectFileType::Source},
			{".hpp", RetrodevLib::ProjectFileType::Source},
			{".cs", RetrodevLib::ProjectFileType::Source},
			{".java", RetrodevLib::ProjectFileType::Source},
			{".py", RetrodevLib::ProjectFileType::Source},
			{".js", RetrodevLib::ProjectFileType::Source},
			{".ts", RetrodevLib::ProjectFileType::Source},
			{".lua", RetrodevLib::ProjectFileType::Source},
			{".asm", RetrodevLib::ProjectFileType::Source},
			{".s", RetrodevLib::ProjectFileType::Source},
			// AngelScript files
			{".as", RetrodevLib::ProjectFileType::Script},
		};

		if (path.has_extension()) {
			std::string ext = path.extension().string();
			auto it = typeMap.find(ext);
			if (it != typeMap.end())
				return it->second;
		}
		return RetrodevLib::ProjectFileType::Data;
	}

	//
	// Get directory contents recursively and build a tree structure
	//
	FileTreeNode ProjectView::GetDirectoryTree(const std::filesystem::path& path) {
		FileTreeNode root;
		root.name = path.filename().string();
		if (root.name.empty()) {
			root.name = path.string();
		}
		root.fullPath = path;
		root.isDirectory = std::filesystem::is_directory(path);

		if (root.isDirectory) {
			PopulateTreeNode(root);
		}
		return root;
	}

	//
	// Recursively populate the tree node with directory contents
	//
	void ProjectView::PopulateTreeNode(FileTreeNode& node) {
		std::error_code ec;

		for (const auto& entry : std::filesystem::directory_iterator(node.fullPath, ec)) {
			if (ec) {
				return;
			}
			FileTreeNode child;
			child.name = entry.path().filename().string();
			child.fullPath = entry.path();
			child.isDirectory = entry.is_directory(ec);
			if (ec) {
				continue;
			}
			if (child.isDirectory) {
				PopulateTreeNode(child);
			} else {
				child.inProject = RetrodevLib::Project::IsFileInProject(child.fullPath.string());
			}
			node.children.push_back(std::move(child));
		}
		// Sort children: directories first, then files, alphabetically within each group
		std::sort(node.children.begin(), node.children.end(), [](const FileTreeNode& a, const FileTreeNode& b) {
			if (a.isDirectory != b.isDirectory) {
				return a.isDirectory > b.isDirectory;
			}
			return a.name < b.name;
		});
	}

	//
	// Recursively mark directory nodes whose virtual path is in the explicit folders list
	//
	void ProjectView::MarkVirtualFolders(FileTreeNode& node, const std::vector<std::string>& folders) {
		for (auto& child : node.children) {
			if (child.isDirectory && child.inBuildSection && !child.isBuildItem) {
				for (const auto& f : folders) {
					if (f == child.buildItemPath) {
						child.isVirtualFolder = true;
						break;
					}
				}
				MarkVirtualFolders(child, folders);
			}
		}
	}

	//
	// Populate the Build node with build items from the project
	//
	void ProjectView::PopulateBuildNode(FileTreeNode& node) {
		//
		// Seed all explicitly-created virtual folders so empty ones still appear
		//
		std::vector<std::string> folders = RetrodevLib::Project::GetFolders();
		for (const auto& folderPath : folders) {
			AddBuildItemToTree(node, folderPath, false, RetrodevLib::ProjectBuildType::VirtualFolder);
		}
		//
		// Get all bitmap build items
		//
		std::vector<std::string> bitmaps = RetrodevLib::Project::GetBuildItemsByType(RetrodevLib::ProjectBuildType::Bitmap);
		for (const auto& bitmapName : bitmaps) {
			AddBuildItemToTree(node, bitmapName, true, RetrodevLib::ProjectBuildType::Bitmap);
		}
		//
		// Get all tileset build items
		//
		std::vector<std::string> tilesets = RetrodevLib::Project::GetBuildItemsByType(RetrodevLib::ProjectBuildType::Tilemap);
		for (const auto& tilesetName : tilesets) {
			AddBuildItemToTree(node, tilesetName, true, RetrodevLib::ProjectBuildType::Tilemap);
		}
		//
		// Get all sprite build items
		//
		std::vector<std::string> sprites = RetrodevLib::Project::GetBuildItemsByType(RetrodevLib::ProjectBuildType::Sprite);
		for (const auto& spriteName : sprites) {
			AddBuildItemToTree(node, spriteName, true, RetrodevLib::ProjectBuildType::Sprite);
		}
		//
		// Get all map build items
		//
		std::vector<std::string> maps = RetrodevLib::Project::GetBuildItemsByType(RetrodevLib::ProjectBuildType::Map);
		for (const auto& mapName : maps) {
			AddBuildItemToTree(node, mapName, true, RetrodevLib::ProjectBuildType::Map);
		}
		//
		// Get all build script items
		//
		std::vector<std::string> builds = RetrodevLib::Project::GetBuildItemsByType(RetrodevLib::ProjectBuildType::Build);
		for (const auto& buildName : builds) {
			AddBuildItemToTree(node, buildName, true, RetrodevLib::ProjectBuildType::Build);
		}
		//
		// Get all palette items
		//
		std::vector<std::string> palettes = RetrodevLib::Project::GetBuildItemsByType(RetrodevLib::ProjectBuildType::Palette);
		for (const auto& paletteName : palettes) {
			AddBuildItemToTree(node, paletteName, true, RetrodevLib::ProjectBuildType::Palette);
		}
		//
		// Mark directory nodes that correspond to explicitly-created virtual folders
		//
		MarkVirtualFolders(node, folders);
		//
		// Sort children alphabetically
		//
		std::sort(node.children.begin(), node.children.end(), [](const FileTreeNode& a, const FileTreeNode& b) {
			if (a.isDirectory != b.isDirectory) {
				return a.isDirectory > b.isDirectory;
			}
			return a.name < b.name;
		});
	}

	//
	// Add a build item to the tree, creating intermediate folders as needed
	//
	void ProjectView::AddBuildItemToTree(FileTreeNode& parent, const std::string& itemPath, bool isBuildItem, RetrodevLib::ProjectBuildType buildType) {
		//
		// Split the path by '/'
		//
		std::vector<std::string> parts;
		std::string current;
		for (char c : itemPath) {
			if (c == '/') {
				if (!current.empty()) {
					parts.push_back(current);
					current.clear();
				}
			} else {
				current += c;
			}
		}
		if (!current.empty()) {
			parts.push_back(current);
		}
		//
		// Navigate/create the tree structure
		//
		std::string accPath;
		FileTreeNode* currentNode = &parent;
		for (size_t i = 0; i < parts.size(); ++i) {
			const std::string& part = parts[i];
			bool isLast = (i == parts.size() - 1);
			if (!accPath.empty())
				accPath += '/';
			accPath += part;
			//
			// Try to find existing child with this name AND matching type
			// (we want both a folder and a build item with the same name to coexist)
			//
			bool wantDirectory = !isLast || !isBuildItem;
			FileTreeNode* found = nullptr;
			for (auto& child : currentNode->children) {
				if (child.name == part && child.isDirectory == wantDirectory) {
					found = &child;
					break;
				}
			}
			//
			// Create new child if not found
			//
			if (!found) {
				FileTreeNode newNode;
				newNode.name = part;
				newNode.fullPath = currentNode->fullPath;
				newNode.isDirectory = !isLast || !isBuildItem;
				newNode.isBuildItem = isLast && isBuildItem;
				newNode.inProject = true;
				newNode.inBuildSection = true;
				newNode.isOpen = false;
				//
				// Store the virtual path for all folder segments and for leaf build items
				//
				if (isLast && isBuildItem) {
					newNode.buildItemPath = itemPath;
					newNode.buildItemType = buildType;
				} else {
					newNode.buildItemPath = accPath;
				}
				currentNode->children.push_back(std::move(newNode));
				found = &currentNode->children.back();
			}
			currentNode = found;
		}
	}

	//
	// Expand parent folders to make a build item visible
	//
	bool ProjectView::ExpandPathToItem(FileTreeNode& node, const std::string& targetItemName) {
		// Check if this node contains the target item
		for (auto& child : node.children) {
			// Check if this child is the target build item
			if (child.isBuildItem && child.name == targetItemName) {
				node.isOpen = true;
				return true;
			}
			// Recursively search in directories
			if (child.isDirectory) {
				if (ExpandPathToItem(child, targetItemName)) {
					node.isOpen = true;
					return true;
				}
			}
		}
		return false;
	}

	//
	// Render a tree node in ImGui
	//
	void ProjectView::RenderTreeNode(FileTreeNode& node, ImGuiTreeNodeFlags baseFlags) {
		ImGuiTreeNodeFlags nodeFlags = baseFlags;

		// Root node: project icon, framed style, open by default
		if (node.isRoot) {
			nodeFlags |= ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen;
		}

		//
		// Check if this node should be selected and scrolled to
		//
		bool shouldScrollTo = false;
		if (node.isBuildItem && !g_pendingScrollToBuildItem.empty()) {
			//
			// Check if this build item name matches the pending scroll target
			// Use the full build item path for comparison
			//
			std::string buildItemName = node.buildItemPath.empty() ? node.name : node.buildItemPath;
			if (buildItemName == g_pendingScrollToBuildItem) {
				nodeFlags |= ImGuiTreeNodeFlags_Selected;
				shouldScrollTo = true;
			}
		}

		//
		// Choose icon based on whether it's a file or directory and its open state
		//
		const char* icon;
		std::string displayName = node.name;
		if (node.isRoot) {
			icon = ICON_CUBE;
			//
			// Add asterisk if project has unsaved changes
			//
			if (RetrodevLib::Project::IsModified()) {
				displayName = node.name + " *";
			}
		} else if (node.isBuildItem) {
			if (node.buildItemType == RetrodevLib::ProjectBuildType::Map)
				icon = ICON_MAP;
			else if (node.buildItemType == RetrodevLib::ProjectBuildType::Build)
				icon = ICON_HAMMER;
			else if (node.buildItemType == RetrodevLib::ProjectBuildType::Palette)
				icon = ICON_PALETTE;
			else
				icon = ICON_FILE_IMAGE;
		} else if (node.isDirectory) {
			icon = node.isOpen ? ICON_FOLDER_OPEN : ICON_FOLDER;
		} else {
			icon = GetFileIcon(node.fullPath);
		}

		// Leaf nodes (files or empty directories) should not have arrow
		if (!node.isDirectory || node.children.empty()) {
			nodeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}

		// Dim non-project leaf nodes
		bool isLeaf = !node.isDirectory || node.children.empty();
		bool dimmed = isLeaf && !node.inProject;
		if (dimmed) {
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.25f);
		}

		// Create unique ID: use buildItemPath for build section nodes, fullPath for filesystem nodes
		// Prefix with type indicator to ensure folders and build items with the same name get different IDs
		std::string uniqueId;
		if (node.inBuildSection) {
			uniqueId = node.isBuildItem ? "B:" + node.buildItemPath : "F:" + node.buildItemPath;
		} else {
			uniqueId = node.fullPath.string();
		}
		ImGui::PushID(uniqueId.c_str());

		//
		// Render the tree node
		//
		bool nodeOpen = ImGui::TreeNodeEx(node.name.c_str(), nodeFlags, "%s %s", icon, displayName.c_str());

		// Update the open state for next frame
		node.isOpen = nodeOpen;

		if (dimmed) {
			ImGui::PopStyleVar();
		}

		// Scroll to the newly added build item
		if (shouldScrollTo) {
			ImGui::SetScrollHereY(0.5f);
			g_pendingScrollToBuildItem.clear();
		}

		// Context menu for leaf items and for all Build-section directory nodes
		if (isLeaf || node.isDirectory) {
			RenderContextMenu(node);
		}
		//
		// Drag source: build items can be dragged to virtual folders or to the build root
		//
		if (node.isBuildItem) {
			if (ImGui::BeginDragDropSource()) {
				BuildItemDragPayload payload = {};
				size_t pathLen = node.buildItemPath.size();
				if (pathLen >= sizeof(payload.buildItemPath))
					pathLen = sizeof(payload.buildItemPath) - 1;
				memcpy(payload.buildItemPath, node.buildItemPath.c_str(), pathLen);
				payload.buildItemPath[pathLen] = '\0';
				payload.buildItemType = node.buildItemType;
				ImGui::SetDragDropPayload("BUILD_ITEM", &payload, sizeof(payload));
				ImGui::Text("%s %s", icon, node.name.c_str());
				ImGui::EndDragDropSource();
			}
		}
		//
		// Drag source: regular files can be dragged to other folders
		//
		if (!node.isDirectory && !node.isBuildItem && node.inProject) {
			if (ImGui::BeginDragDropSource()) {
				FileDragPayload payload = {};
				std::string filePathStr = node.fullPath.string();
				size_t pathLen = filePathStr.size();
				if (pathLen >= sizeof(payload.filePath))
					pathLen = sizeof(payload.filePath) - 1;
				memcpy(payload.filePath, filePathStr.c_str(), pathLen);
				payload.filePath[pathLen] = '\0';
				ImGui::SetDragDropPayload("FILE_ITEM", &payload, sizeof(payload));
				ImGui::Text("%s %s", icon, node.name.c_str());
				ImGui::EndDragDropSource();
			}
		}
		//
		// Drop target: filesystem directories accept dragged files
		//
		if (node.isDirectory && !node.inBuildSection) {
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload("FILE_ITEM")) {
					const FileDragPayload* data = static_cast<const FileDragPayload*>(accepted->Data);
					std::filesystem::path oldPath = data->filePath;
					std::filesystem::path newPath = node.fullPath / oldPath.filename();
					if (oldPath != newPath && oldPath != node.fullPath) {
						std::string oldPathStr = oldPath.lexically_normal().string();
						std::string newPathStr = newPath.lexically_normal().string();
						//
						// Query all build items once at the beginning
						//
						std::vector<std::string> bitmaps = RetrodevLib::Project::GetBuildItemsByType(RetrodevLib::ProjectBuildType::Bitmap);
						std::vector<std::string> tilesets = RetrodevLib::Project::GetBuildItemsByType(RetrodevLib::ProjectBuildType::Tilemap);
						std::vector<std::string> sprites = RetrodevLib::Project::GetBuildItemsByType(RetrodevLib::ProjectBuildType::Sprite);
						//
						// Check if this file is used by any build items
						//
						bool hasReferences = false;
						for (const auto& name : bitmaps) {
							std::string src = RetrodevLib::Project::BitmapGetSourcePath(name);
							std::filesystem::path srcNorm = std::filesystem::path(src).lexically_normal();
							if (srcNorm == oldPath) {
								hasReferences = true;
								break;
							}
						}
						if (!hasReferences) {
							for (const auto& name : tilesets) {
								std::string src = RetrodevLib::Project::TilesetGetSourcePath(name);
								std::filesystem::path srcNorm = std::filesystem::path(src).lexically_normal();
								if (srcNorm == oldPath) {
									hasReferences = true;
									break;
								}
							}
						}
						if (!hasReferences) {
							for (const auto& name : sprites) {
								std::string src = RetrodevLib::Project::SpriteGetSourcePath(name);
								std::filesystem::path srcNorm = std::filesystem::path(src).lexically_normal();
								if (srcNorm == oldPath) {
									hasReferences = true;
									break;
								}
							}
						}
						//
						// Move the physical file first
						//
						std::error_code ec;
						std::filesystem::rename(oldPath, newPath, ec);
						if (!ec) {
							//
							// Update the file entry in the project (remove old, add new)
							//
							RetrodevLib::ProjectFileType fileType = GetFileType(oldPath);
							RetrodevLib::Project::RemoveFile(oldPathStr);
							RetrodevLib::Project::AddFile(newPathStr, fileType);
							//
							// Update all build items that reference this file
							//
							if (hasReferences) {
								for (const auto& name : bitmaps) {
									std::string src = RetrodevLib::Project::BitmapGetSourcePath(name);
									std::filesystem::path srcNorm = std::filesystem::path(src).lexically_normal();
									if (srcNorm == oldPath)
										RetrodevLib::Project::BitmapSetSourcePath(name, newPathStr);
								}
								for (const auto& name : tilesets) {
									std::string src = RetrodevLib::Project::TilesetGetSourcePath(name);
									std::filesystem::path srcNorm = std::filesystem::path(src).lexically_normal();
									if (srcNorm == oldPath)
										RetrodevLib::Project::TilesetSetSourcePath(name, newPathStr);
								}
								for (const auto& name : sprites) {
									std::string src = RetrodevLib::Project::SpriteGetSourcePath(name);
									std::filesystem::path srcNorm = std::filesystem::path(src).lexically_normal();
									if (srcNorm == oldPath)
										RetrodevLib::Project::SpriteSetSourcePath(name, newPathStr);
								}
							}
							AppConsole::AddLogF(AppConsole::LogLevel::Info, "Moved file: %s -> %s", oldPathStr.c_str(), newPathStr.c_str());
							g_forceProjectTreeRebuild = true;
						} else {
							AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Failed to move file: %s", ec.message().c_str());
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
		}
		//
		// Drop target: build section folders accept dragged build items
		//
		if (node.inBuildSection && node.isDirectory) {
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* accepted = ImGui::AcceptDragDropPayload("BUILD_ITEM")) {
					const BuildItemDragPayload* data = static_cast<const BuildItemDragPayload*>(accepted->Data);
					std::string oldPath = data->buildItemPath;
					//
					// Extract the leaf name (part after the last '/') from the dragged item path
					//
					size_t slash = oldPath.rfind('/');
					std::string leafName = (slash != std::string::npos) ? oldPath.substr(slash + 1) : oldPath;
					//
					// New path: folder/leafName, or just leafName when dropping on the build root
					//
					std::string newPath = node.buildItemPath.empty() ? leafName : node.buildItemPath + "/" + leafName;
					if (newPath != oldPath) {
						//
						// TODO: We may have documents that uses this item
						// (a build, a palette solver) we need to fix them
						//

						RetrodevLib::Project::RenameBuildItem(data->buildItemType, oldPath, newPath);
						DocumentsView::RenameBuildItemDocument(oldPath, newPath, data->buildItemType);
						g_forceProjectTreeRebuild = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
		}
		//
		// Handle build item selection on mouse release so that starting a drag does not
		// immediately open the document (IsItemClicked fires on mouse-down, before any
		// drag threshold is reached; IsMouseReleased fires only after the button is up,
		// at which point the mouse is no longer hovering the source item if it was dragged)
		//
		if (node.isBuildItem && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && ImGui::IsItemHovered()) {
			//
			// Determine build item type
			//
			bool isBitmap = (node.buildItemType == RetrodevLib::ProjectBuildType::Bitmap);
			bool isTileset = (node.buildItemType == RetrodevLib::ProjectBuildType::Tilemap);
			bool isSprite = (node.buildItemType == RetrodevLib::ProjectBuildType::Sprite);
			bool isMap = (node.buildItemType == RetrodevLib::ProjectBuildType::Map);
			bool isBuild = (node.buildItemType == RetrodevLib::ProjectBuildType::Build);
			//
			// Use the full build item path, not just the node name
			// This handles cases like "amstrad.cpc/tiles" where the name is split into virtual folders
			//
			std::string buildItemName = node.buildItemPath.empty() ? node.name : node.buildItemPath;
			//
			// Open bitmap conversion document
			//
			if (isBitmap) {
				std::string sourceFilePath = RetrodevLib::Project::BitmapGetSourcePath(buildItemName);
				if (!sourceFilePath.empty()) {
					//
					// Try to activate existing document first (with build type check)
					//
					if (!DocumentsView::ActivateDocument(buildItemName, sourceFilePath, RetrodevLib::ProjectBuildType::Bitmap)) {
						//
						// Document not open, create a new one
						//
						auto doc = std::make_shared<DocumentBitmap>(buildItemName, sourceFilePath);
						DocumentsView::OpenDocument(doc);
					}
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Error, "Source file not found for bitmap: %s", buildItemName.c_str());
				}
			}
			//
			// Open tileset conversion document
			//
			if (isTileset) {
				std::string sourceFilePath = RetrodevLib::Project::TilesetGetSourcePath(buildItemName);
				if (!sourceFilePath.empty()) {
					//
					// Try to activate existing document first (with build type check)
					//
					if (!DocumentsView::ActivateDocument(buildItemName, sourceFilePath, RetrodevLib::ProjectBuildType::Tilemap)) {
						//
						// Document not open, create a new one
						//
						DocumentsView::OpenDocument(std::make_shared<DocumentTiles>(buildItemName, sourceFilePath));
					}
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Error, "Source file not found for tileset: %s", buildItemName.c_str());
				}
			}
			//
			// Open sprite conversion document
			//
			if (isSprite) {
				std::string sourceFilePath = RetrodevLib::Project::SpriteGetSourcePath(buildItemName);
				if (!sourceFilePath.empty()) {
					//
					// Try to activate existing document first (with build type check)
					//
					if (!DocumentsView::ActivateDocument(buildItemName, sourceFilePath, RetrodevLib::ProjectBuildType::Sprite)) {
						//
						// Document not open, create a new one
						//
						DocumentsView::OpenDocument(std::make_shared<DocumentSprite>(buildItemName, sourceFilePath));
					}
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Error, "Source file not found for sprite: %s", buildItemName.c_str());
				}
			}
			//
			// Open map document (maps have no source file)
			//
			if (isMap) {
				if (!DocumentsView::ActivateDocument(buildItemName, "", RetrodevLib::ProjectBuildType::Map))
					DocumentsView::OpenDocument(std::make_shared<DocumentMap>(buildItemName));
			}
			//
			// Open build document
			//
			if (isBuild) {
				if (!DocumentsView::ActivateDocument(buildItemName, "", RetrodevLib::ProjectBuildType::Build))
					DocumentsView::OpenDocument(std::make_shared<DocumentBuild>(buildItemName));
			}
			//
			// Open palette document
			//
			bool isPalette = (node.buildItemType == RetrodevLib::ProjectBuildType::Palette);
			if (isPalette) {
				if (!DocumentsView::ActivateDocument(buildItemName, "", RetrodevLib::ProjectBuildType::Palette))
					DocumentsView::OpenDocument(std::make_shared<DocumentPalette>(buildItemName));
			}
		}
		//
		// Handle regular file selection
		//
		if (!node.isDirectory && !node.isBuildItem && node.inProject && ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
			RetrodevLib::ProjectFileType fileType = GetFileType(node.fullPath);
			if (fileType == RetrodevLib::ProjectFileType::Image) {
				// Try to activate existing document first
				if (!DocumentsView::ActivateDocument(node.name, node.fullPath.string())) {
					// Document not open, create a new one
					auto doc = std::make_shared<DocumentImage>(node.name, node.fullPath.string());
					DocumentsView::OpenDocument(doc);
				}
			}
			//
			// Open source file in text editor
			//
			if (fileType == RetrodevLib::ProjectFileType::Source) {
				if (!DocumentsView::ActivateDocument(node.name, node.fullPath.string())) {
					auto doc = std::make_shared<DocumentText>(node.name, node.fullPath.string());
					DocumentsView::OpenDocument(doc);
				}
			}
			//
			// Open AngelScript file in text editor with AS syntax highlighting
			//
			if (fileType == RetrodevLib::ProjectFileType::Script) {
				if (!DocumentsView::ActivateDocument(node.name, node.fullPath.string())) {
					auto doc = std::make_shared<DocumentText>(node.name, node.fullPath.string());
					DocumentsView::OpenDocument(doc);
				}
			}
			//
			// Open generic data file in the data document viewer
			//
			if (fileType == RetrodevLib::ProjectFileType::Data) {
				if (!DocumentsView::ActivateDocument(node.name, node.fullPath.string())) {
					auto doc = std::make_shared<DocumentData>(node.name, node.fullPath.string());
					DocumentsView::OpenDocument(doc);
				}
			}
		}
		// If it's an open directory with children, render them
		if (nodeOpen) {
			if (node.isDirectory && !node.children.empty()) {
				for (auto& child : node.children) {
					RenderTreeNode(child, baseFlags);
				}
				ImGui::TreePop();
			}
		}
		ImGui::PopID();
	}

	//
	// Render the Project view, including the workspace tree and handling interactions
	//
	void ProjectView::Perform() {
		ImGui::TextEditor::TickGlobalCodeLensParse();

		static FileTreeNode workspaceTree;
		static bool treeInitialized = false;
		static std::string loadedProjectPath;

		// Rebuild the tree when a project is opened/closed or changes
		std::string currentPath = RetrodevLib::Project::IsOpen() ? RetrodevLib::Project::GetProjectFolder() : "";
		if (currentPath != loadedProjectPath) {
			loadedProjectPath = currentPath;
			treeInitialized = false;
			if (loadedProjectPath.empty())
				ImGui::TextEditor::ClearCodeLensData();
		}

		//
		// Periodically scan the project folder for filesystem changes and rebuild the tree if needed.
		// Runs once per second when a project is open and the tree has been initialised.
		//
		static double g_lastScanTime = 0.0;
		if (treeInitialized && !loadedProjectPath.empty()) {
			double now = ImGui::GetTime();
			if (now - g_lastScanTime >= 1.0) {
				g_lastScanTime = now;
				if (RetrodevLib::Project::ScanFiles())
					g_forceProjectTreeRebuild = true;
			}
		}
		//
		// Force rebuild if requested (e.g., after adding build items)
		//
		if (g_forceProjectTreeRebuild) {
			treeInitialized = false;
			g_forceProjectTreeRebuild = false;
		}

		if (!treeInitialized && !loadedProjectPath.empty()) {
			RefreshProjectCodeLens(loadedProjectPath);
			// Build the project tree: root is the project name with "Files" and "Build" children
			workspaceTree = FileTreeNode();
			workspaceTree.name = RetrodevLib::Project::GetName();
			workspaceTree.fullPath = loadedProjectPath;
			workspaceTree.isDirectory = true;
			workspaceTree.isOpen = true;
			workspaceTree.isRoot = true;

			// "Files" child: contains the scanned directory contents
			FileTreeNode filesNode = GetDirectoryTree(loadedProjectPath);
			filesNode.name = "Files";

			// "Build" child: populated with build items from project
			FileTreeNode buildNode;
			buildNode.name = "Project";
			buildNode.fullPath = loadedProjectPath;
			buildNode.isDirectory = true;
			buildNode.inProject = true;
			buildNode.inBuildSection = true;
			buildNode.isOpen = !g_pendingScrollToBuildItem.empty();
			buildNode.isBuildItem = false;
			// Populate build node with build items
			PopulateBuildNode(buildNode);
			// Expand parent folders for pending scroll target
			if (!g_pendingScrollToBuildItem.empty()) {
				ExpandPathToItem(buildNode, g_pendingScrollToBuildItem);
			}
			//
			// "Sdk" child: scan the sdk folder next to the executable (if it exists)
			// Placed first so it appears at the top of the tree (Sdk -> Project -> Files)
			//
			const std::string& sdkPath = GetSdkFolderPath();
			if (!sdkPath.empty()) {
				FileTreeNode sdkNode = GetDirectoryTree(sdkPath);
				sdkNode.name = "Sdk";
				workspaceTree.children.push_back(std::move(sdkNode));
			}
			workspaceTree.children.push_back(std::move(buildNode));
			workspaceTree.children.push_back(std::move(filesNode));
			treeInitialized = true;
		}

		static ImGuiTreeNodeFlags base_flags =
			ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DrawLinesFull;

		// Render the actual workspace tree
		if (treeInitialized && !workspaceTree.name.empty()) {
			RenderTreeNode(workspaceTree, base_flags);
		} else {
		}
		//
		// New Map modal dialog (triggered by right-clicking the Build node)
		//
		if (g_showNewMapDialog) {
			ImGui::OpenPopup("New Map");
			g_showNewMapDialog = false;
			g_popupJustOpened = true;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
		if (ImGui::BeginPopupModal("New Map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Map name:");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
			if (g_popupJustOpened) {
				ImGui::SetKeyboardFocusHere();
				g_popupJustOpened = false;
			}
			bool enterPressed = ImGui::InputText("##NewMapName", g_newMapNameBuf, sizeof(g_newMapNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
			bool nameValid = g_newMapNameBuf[0] != '\0';
			if (enterPressed && nameValid) {
				if (RetrodevLib::Project::MapAdd(g_newMapNameBuf)) {
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Created map: %s", g_newMapNameBuf);
					g_forceProjectTreeRebuild = true;
					g_pendingScrollToBuildItem = g_newMapNameBuf;
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Map already exists: %s", g_newMapNameBuf);
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::BeginDisabled(!nameValid);
			if (ImGui::Button("Create", ImVec2(80.0f, 0.0f))) {
				if (RetrodevLib::Project::MapAdd(g_newMapNameBuf)) {
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Created map: %s", g_newMapNameBuf);
					g_forceProjectTreeRebuild = true;
					g_pendingScrollToBuildItem = g_newMapNameBuf;
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Map already exists: %s", g_newMapNameBuf);
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
		if (g_showNewBuildDialog) {
			ImGui::OpenPopup("New Build");
			g_showNewBuildDialog = false;
			g_popupJustOpened = true;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
		if (ImGui::BeginPopupModal("New Build", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Build name:");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
			if (g_popupJustOpened) {
				ImGui::SetKeyboardFocusHere();
				g_popupJustOpened = false;
			}
			bool enterPressed = ImGui::InputText("##NewBuildName", g_newBuildNameBuf, sizeof(g_newBuildNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
			bool nameValid = g_newBuildNameBuf[0] != '\0';
			if (enterPressed && nameValid) {
				if (RetrodevLib::Project::BuildAdd(g_newBuildNameBuf)) {
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Created build: %s", g_newBuildNameBuf);
					g_forceProjectTreeRebuild = true;
					g_pendingScrollToBuildItem = g_newBuildNameBuf;
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Build already exists: %s", g_newBuildNameBuf);
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::BeginDisabled(!nameValid);
			if (ImGui::Button("Create", ImVec2(80.0f, 0.0f))) {
				if (RetrodevLib::Project::BuildAdd(g_newBuildNameBuf)) {
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Created build: %s", g_newBuildNameBuf);
					g_forceProjectTreeRebuild = true;
					g_pendingScrollToBuildItem = g_newBuildNameBuf;
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Build already exists: %s", g_newBuildNameBuf);
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
		//
		// New Folder modal
		//
		if (g_showNewFolderDialog) {
			ImGui::OpenPopup("New Folder");
			g_showNewFolderDialog = false;
			g_popupJustOpened = true;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
		if (ImGui::BeginPopupModal("New Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Folder name:");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
			if (g_popupJustOpened) {
				ImGui::SetKeyboardFocusHere();
				g_popupJustOpened = false;
			}
			bool enterPressed = ImGui::InputText("##NewFolderName", g_newFolderNameBuf, sizeof(g_newFolderNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
			bool nameValid = g_newFolderNameBuf[0] != '\0';
			if (enterPressed && nameValid) {
				std::filesystem::path newFolder = std::filesystem::path(g_newFolderParentPath) / g_newFolderNameBuf;
				std::error_code ec;
				if (std::filesystem::create_directory(newFolder, ec)) {
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Created folder: %s", newFolder.string().c_str());
					g_forceProjectTreeRebuild = true;
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Failed to create folder: %s", newFolder.string().c_str());
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::BeginDisabled(!nameValid);
			if (ImGui::Button("Create", ImVec2(80.0f, 0.0f))) {
				std::filesystem::path newFolder = std::filesystem::path(g_newFolderParentPath) / g_newFolderNameBuf;
				std::error_code ec;
				if (std::filesystem::create_directory(newFolder, ec)) {
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Created folder: %s", newFolder.string().c_str());
					g_forceProjectTreeRebuild = true;
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Failed to create folder: %s", newFolder.string().c_str());
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
		//
		// New Source File modal
		//
		if (g_showNewFileDialog) {
			ImGui::OpenPopup("New Source File");
			g_showNewFileDialog = false;
			g_popupJustOpened = true;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
		if (ImGui::BeginPopupModal("New Source File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("File name (include extension, e.g. main.asm):");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
			if (g_popupJustOpened) {
				ImGui::SetKeyboardFocusHere();
				g_popupJustOpened = false;
			}
			bool enterPressed = ImGui::InputText("##NewFileName", g_newFileNameBuf, sizeof(g_newFileNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
			bool nameValid = g_newFileNameBuf[0] != '\0';
			if (enterPressed && nameValid) {
				std::filesystem::path newFile = std::filesystem::path(g_newFileParentPath) / g_newFileNameBuf;
				std::ofstream ofs(newFile);
				if (ofs.is_open()) {
					ofs.close();
					RetrodevLib::Project::AddFile(newFile.string(), GetFileType(newFile));
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Created file: %s", newFile.string().c_str());
					g_forceProjectTreeRebuild = true;
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Failed to create file: %s", newFile.string().c_str());
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::BeginDisabled(!nameValid);
			if (ImGui::Button("Create", ImVec2(80.0f, 0.0f))) {
				std::filesystem::path newFile = std::filesystem::path(g_newFileParentPath) / g_newFileNameBuf;
				std::ofstream ofs(newFile);
				if (ofs.is_open()) {
					ofs.close();
					RetrodevLib::Project::AddFile(newFile.string(), GetFileType(newFile));
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Created file: %s", newFile.string().c_str());
					g_forceProjectTreeRebuild = true;
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Failed to create file: %s", newFile.string().c_str());
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
		//
		// New Image modal: name, width, height; creates a paletized 256-colour PNG
		//
		if (g_showNewImageDialog) {
			ImGui::OpenPopup("New Image");
			g_showNewImageDialog = false;
			g_popupJustOpened = true;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
		if (ImGui::BeginPopupModal("New Image", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::Text("Image name (without extension):");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
			if (g_popupJustOpened) {
				ImGui::SetKeyboardFocusHere();
				g_popupJustOpened = false;
			}
			bool enterPressed = ImGui::InputText("##NewImageName", g_newImageNameBuf, sizeof(g_newImageNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::Text("Resolution:");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
			ImGui::InputInt("Width##ImgW", &g_newImageWidth, 0);
			ImGui::SameLine();
			ImGui::AlignTextToFramePadding();
			ImGui::Text("x");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
			ImGui::InputInt("Height##ImgH", &g_newImageHeight, 0);
			if (g_newImageWidth < 1)
				g_newImageWidth = 1;
			if (g_newImageHeight < 1)
				g_newImageHeight = 1;
			if (g_newImageWidth > 4096)
				g_newImageWidth = 4096;
			if (g_newImageHeight > 4096)
				g_newImageHeight = 4096;
			bool nameValid = g_newImageNameBuf[0] != '\0';
			if (enterPressed && nameValid) {
				std::filesystem::path newImg = std::filesystem::path(g_newImageParentPath) / (std::string(g_newImageNameBuf) + ".png");
				auto img = RetrodevLib::Image::ImageCreate(g_newImageWidth, g_newImageHeight, true);
				if (img && img->Save(newImg.string())) {
					RetrodevLib::Project::AddFile(newImg.string(), RetrodevLib::ProjectFileType::Image);
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Created image: %s (%dx%d, 256 colours)", newImg.string().c_str(), g_newImageWidth, g_newImageHeight);
					g_forceProjectTreeRebuild = true;
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Failed to create image: %s", newImg.string().c_str());
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::BeginDisabled(!nameValid);
			if (ImGui::Button("Create", ImVec2(80.0f, 0.0f))) {
				std::filesystem::path newImg = std::filesystem::path(g_newImageParentPath) / (std::string(g_newImageNameBuf) + ".png");
				auto img = RetrodevLib::Image::ImageCreate(g_newImageWidth, g_newImageHeight, true);
				if (img && img->Save(newImg.string())) {
					RetrodevLib::Project::AddFile(newImg.string(), RetrodevLib::ProjectFileType::Image);
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Created image: %s (%dx%d, 256 colours)", newImg.string().c_str(), g_newImageWidth, g_newImageHeight);
					g_forceProjectTreeRebuild = true;
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Failed to create image: %s", newImg.string().c_str());
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
		//
		// New Palette modal
		//
		if (g_showNewPaletteDialog) {
			ImGui::OpenPopup("New Palette");
			g_showNewPaletteDialog = false;
			g_popupJustOpened = true;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
		if (ImGui::BeginPopupModal("New Palette", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Palette name:");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
			if (g_popupJustOpened) {
				ImGui::SetKeyboardFocusHere();
				g_popupJustOpened = false;
			}
			bool enterPressed = ImGui::InputText("##NewPaletteName", g_newPaletteNameBuf, sizeof(g_newPaletteNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
			bool nameValid = g_newPaletteNameBuf[0] != '\0';
			if (enterPressed && nameValid) {
				if (RetrodevLib::Project::PaletteAdd(g_newPaletteNameBuf)) {
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Created palette: %s", g_newPaletteNameBuf);
					g_forceProjectTreeRebuild = true;
					g_pendingScrollToBuildItem = g_newPaletteNameBuf;
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Palette already exists: %s", g_newPaletteNameBuf);
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::BeginDisabled(!nameValid);
			if (ImGui::Button("Create", ImVec2(80.0f, 0.0f))) {
				if (RetrodevLib::Project::PaletteAdd(g_newPaletteNameBuf)) {
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Created palette: %s", g_newPaletteNameBuf);
					g_forceProjectTreeRebuild = true;
					g_pendingScrollToBuildItem = g_newPaletteNameBuf;
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Palette already exists: %s", g_newPaletteNameBuf);
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
		//
		// New Virtual Folder modal
		//
		if (g_showNewVirtualFolderDialog) {
			ImGui::OpenPopup("New Virtual Folder");
			g_showNewVirtualFolderDialog = false;
			g_popupJustOpened = true;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
		if (ImGui::BeginPopupModal("New Virtual Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::AlignTextToFramePadding();
			ImGui::Text("Folder name:");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
			if (g_popupJustOpened) {
				ImGui::SetKeyboardFocusHere();
				g_popupJustOpened = false;
			}
			bool enterPressed = ImGui::InputText("##NewVirtualFolderName", g_newVirtualFolderNameBuf, sizeof(g_newVirtualFolderNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
			bool nameValid = g_newVirtualFolderNameBuf[0] != '\0';
			if (enterPressed && nameValid) {
				//
				// Build the full virtual path: parent + "/" + name (skip leading "/" when parent is root/empty)
				//
				std::string fullPath = g_newVirtualFolderParentPath.empty() ? g_newVirtualFolderNameBuf : g_newVirtualFolderParentPath + "/" + g_newVirtualFolderNameBuf;
				if (RetrodevLib::Project::FolderAdd(fullPath)) {
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Created virtual folder: %s", fullPath.c_str());
					g_forceProjectTreeRebuild = true;
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Virtual folder already exists: %s", fullPath.c_str());
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::BeginDisabled(!nameValid);
			if (ImGui::Button("Create", ImVec2(80.0f, 0.0f))) {
				//
				// Build the full virtual path: parent + "/" + name (skip leading "/" when parent is root/empty)
				//
				std::string fullPath = g_newVirtualFolderParentPath.empty() ? g_newVirtualFolderNameBuf : g_newVirtualFolderParentPath + "/" + g_newVirtualFolderNameBuf;
				if (RetrodevLib::Project::FolderAdd(fullPath)) {
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Created virtual folder: %s", fullPath.c_str());
					g_forceProjectTreeRebuild = true;
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Virtual folder already exists: %s", fullPath.c_str());
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
		//
		// Rename Virtual Folder modal
		//
		if (g_showRenameVirtualFolderDialog) {
			ImGui::OpenPopup("Rename Folder");
			g_showRenameVirtualFolderDialog = false;
			g_popupJustOpened = true;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
		if (ImGui::BeginPopupModal("Rename Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::AlignTextToFramePadding();
			ImGui::Text("New folder name:");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
			if (g_popupJustOpened) {
				ImGui::SetKeyboardFocusHere();
				g_popupJustOpened = false;
			}
			bool enterPressed = ImGui::InputText("##RenameFolderName", g_renameVirtualFolderNameBuf, sizeof(g_renameVirtualFolderNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
			bool nameValid = g_renameVirtualFolderNameBuf[0] != '\0';
			ImGui::BeginDisabled(!nameValid);
			bool doRename = ImGui::Button("Rename", ImVec2(80.0f, 0.0f));
			ImGui::EndDisabled();
			if ((doRename || (enterPressed && nameValid))) {
				//
				// Compute new full path: same parent prefix, new leaf name
				//
				std::string newLeaf = g_renameVirtualFolderNameBuf;
				std::string oldPath = g_renameVirtualFolderOldPath;
				size_t slash = oldPath.rfind('/');
				std::string newPath = (slash != std::string::npos) ? oldPath.substr(0, slash + 1) + newLeaf : newLeaf;
				if (newPath != oldPath) {
					std::string prefix = oldPath + "/";
					std::string newPrefix = newPath + "/";
					//
					// Remove the old folder entry and add the new name
					//
					RetrodevLib::Project::FolderRemove(oldPath);
					RetrodevLib::Project::FolderAdd(newPath);
					//
					// Also rename any child virtual folders that were under the old path
					//
					std::vector<std::string> allFolders = RetrodevLib::Project::GetFolders();
					for (const auto& f : allFolders) {
						if (f.rfind(prefix, 0) == 0) {
							std::string newChildPath = newPrefix + f.substr(prefix.size());
							RetrodevLib::Project::FolderRemove(f);
							RetrodevLib::Project::FolderAdd(newChildPath);
						}
					}
					//
					// Rename all build items whose path starts with the old folder prefix
					//
					auto renameItems = [&](RetrodevLib::ProjectBuildType type) {
						std::vector<std::string> items = RetrodevLib::Project::GetBuildItemsByType(type);
						for (const auto& item : items) {
							if (item.rfind(prefix, 0) == 0) {
								std::string newItemName = newPrefix + item.substr(prefix.size());
								RetrodevLib::Project::RenameBuildItem(type, item, newItemName);
							}
						}
					};
					renameItems(RetrodevLib::ProjectBuildType::Bitmap);
					renameItems(RetrodevLib::ProjectBuildType::Tilemap);
					renameItems(RetrodevLib::ProjectBuildType::Sprite);
					renameItems(RetrodevLib::ProjectBuildType::Map);
					renameItems(RetrodevLib::ProjectBuildType::Build);
					renameItems(RetrodevLib::ProjectBuildType::Palette);
					g_forceProjectTreeRebuild = true;
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Renamed folder: %s -> %s", oldPath.c_str(), newPath.c_str());
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
		//
		// Rename Folder (filesystem) modal
		//
		if (g_showRenameFolderDialog) {
			ImGui::OpenPopup("Rename Folder##fs");
			g_showRenameFolderDialog = false;
			g_popupJustOpened = true;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));
		if (ImGui::BeginPopupModal("Rename Folder##fs", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::AlignTextToFramePadding();
			ImGui::Text("New folder name:");
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
			if (g_popupJustOpened) {
				ImGui::SetKeyboardFocusHere();
				g_popupJustOpened = false;
			}
			bool enterPressed = ImGui::InputText("##RenameFolderFsName", g_renameFolderNameBuf, sizeof(g_renameFolderNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
			bool nameValid = g_renameFolderNameBuf[0] != '\0';
			ImGui::BeginDisabled(!nameValid);
			bool doRename = ImGui::Button("Rename", ImVec2(80.0f, 0.0f));
			ImGui::EndDisabled();
			if (doRename || (enterPressed && nameValid)) {
				std::filesystem::path newPath = g_renameFolderOldPath.parent_path() / g_renameFolderNameBuf;
				std::error_code ec;
				std::filesystem::rename(g_renameFolderOldPath, newPath, ec);
				if (!ec) {
					//
					// Update any project files whose paths fall under the renamed folder
					//
					std::string oldPrefix = g_renameFolderOldPath.string();
					std::string newPrefix = newPath.string();
					RetrodevLib::Project::RenameFilesWithPrefix(oldPrefix, newPrefix);
					AppConsole::AddLogF(AppConsole::LogLevel::Info, "Renamed folder: %s -> %s", oldPrefix.c_str(), newPrefix.c_str());
					g_forceProjectTreeRebuild = true;
				} else {
					AppConsole::AddLogF(AppConsole::LogLevel::Warning, "Failed to rename folder: %s", ec.message().c_str());
				}
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel", ImVec2(80.0f, 0.0f)))
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		ImGui::PopStyleVar();
	}
}