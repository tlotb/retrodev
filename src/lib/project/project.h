// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Lib
//
// Project -- load, save and manage the .retrodev project file.
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#pragma once

#include <retrodev.lib.h>
#include <convert/convert.bitmap.params.h>
#include <convert/convert.tileset.params.h>
#include <convert/convert.sprites.params.h>
#include <assets/map/map.params.h>
#include <assets/source/source.params.h>
#include <assets/palette/palette.params.h>
#include <export/export.params.h>
#include <string>
#include <vector>

namespace RetrodevLib {
	//
	// Project file types
	//
	enum class ProjectFileType {
		// Image item tracked in the project as source of conversion build items (bitmaps, tilesets, sprites)
		Image,
		// Audio file tracked in the project (not yet used, placeholder for future features)
		Audio,
		// Source / code file tracked in the project (not yet used, placeholder for future features)
		Source,
		// AngelScript file tracked in the project -- opened in the text editor with AS syntax
		Script,
		// Generic data file tracked in the project (not yet used, placeholder for future features)
		Data
	};

	//
	// Project build types (not related to file types, sections in the json)
	//
	enum class ProjectBuildType {
		// Bitmap conversion build item, which specifies how to convert an original
		// image into a bitmap resource for the target platform
		Bitmap,
		// Tileset conversion build item, which specifies how to convert an original
		// image into a tileset resource for the target platform (not yet implemented, placeholder for future features)
		Tilemap,
		// Sprite conversion build item, which specifies how to convert an original
		// image into a sprite resource for the target platform (not yet implemented, placeholder for future features)
		Sprite,
		//
		// Map build item, stores a tile map matrix referencing tileset build items
		//
		Map,
		//
		// Build script item, stores a named build pipeline with sources and an export script
		//
		Build,
		//
		// Palette build item, stores a named palette configuration for the target platform
		//
		Palette,
		//
		// Virtual folder -- a logical grouping node in the build section, not a real filesystem directory
		//
		VirtualFolder
	};

	//
	// Project file types
	// The filePath is relative to the project file location and is used to track the files in the project.
	//
	// In the project list, those items are just a reference of files we want to keep under control
	//
	// ----------------------------------------------------------------------------------------------------

	//
	// Image file entry
	//
	struct ProjectImageEntry {
		std::string filePath;
	};

	//
	// Audio file entry
	//
	struct ProjectAudioEntry {
		std::string filePath;
	};

	//
	// Source file entry
	//
	struct ProjectSourceEntry {
		std::string filePath;
	};
	//
	// AngelScript file entry
	//
	struct ProjectScriptEntry {
		std::string filePath;
	};
	//
	// Generic data file entry
	//
	struct ProjectDataEntry {
		std::string filePath;
	};

	//
	// Build are not related to files but sections in the json
	//
	// The name may contain slashes to indicate virtual subfolders in the build section,
	// but the sourceFilePath is the actual file path on disk relative to the project file.
	// -------------------------------------------------------------------------------------

	//
	// Build a converted bitmap from an original image
	//
	struct ProjectBuildBitmapEntry {
		std::string name;
		std::string sourceFilePath;
		// Conversion params here (to be reviewed)
		GFXParams params;
		//
		// Export configuration for this build item
		//
		ExportParams exportParams;
	};

	//
	// Build a converted tileset from an original image
	//
	struct ProjectBuildTilesEntry {
		std::string name;
		std::string sourceFilePath;
		//
		// Conversion params (same as bitmaps)
		//
		GFXParams params;
		//
		// Tile extraction params
		//
		TileExtractionParams tileParams;
		//
		// Export configuration for this build item
		//
		ExportParams exportParams;
	};

	//
	// build a converted sprites from an original image
	//
	struct ProjectBuildSpriteEntry {
		std::string name;
		std::string sourceFilePath;
		//
		// Conversion params (same as bitmaps/tilesets)
		//
		GFXParams params;
		//
		// Sprite extraction params (sprite definitions)
		//
		SpriteExtractionParams spriteParams;
		//
		// Export configuration for this build item
		//
		ExportParams exportParams;
	};

	//
	// Build source item: a named build pipeline with a sources list and an export configuration
	//
	struct ProjectBuildBuildEntry {
		std::string name;
		//
		// Source build parameters
		//
		SourceParams sourceParams;
		//
		// Export configuration for this build item
		//
		ExportParams exportParams;
	};
	//
	// Palette build item
	//
	struct ProjectBuildPaletteEntry {
		std::string name;
		//
		// Palette configuration: zones, participants, target system/mode
		//
		PaletteParams paletteParams;
		//
		// Export configuration for this build item
		//
		ExportParams exportParams;
	};
	//
	// Build a map from a tile matrix
	//
	struct ProjectBuildMapEntry {
		std::string name;
		//
		// Map parameters (dimensions, tilesets, tile data)
		//
		MapParams mapParams;
		//
		// Export configuration for this build item
		//
		ExportParams exportParams;
	};
	//
	// Holds the project data including all tracked files
	//
	struct ProjectFile {
		//
		// Project file format version. Written on every save, checked on open.
		// Version 0 means the field is absent (pre-versioning files).
		// Current supported version is 1.
		//
		int version = 1;
		std::string ProjectName;
		std::vector<ProjectImageEntry> images;
		std::vector<ProjectAudioEntry> audio;
		std::vector<ProjectSourceEntry> sources;
		std::vector<ProjectScriptEntry> scripts;
		std::vector<ProjectDataEntry> data;

		std::vector<ProjectBuildBitmapEntry> buildBitmaps;
		std::vector<ProjectBuildTilesEntry> buildTiles;
		std::vector<ProjectBuildSpriteEntry> buildSprites;
		std::vector<ProjectBuildMapEntry> buildMaps;
		std::vector<ProjectBuildBuildEntry> buildBuilds;
		std::vector<ProjectBuildPaletteEntry> buildPalettes;
		//
		// Explicitly-created virtual folder paths under the Build section.
		// Each entry is a slash-separated virtual path (e.g. "graphics/cpc").
		// Folders derived implicitly from build item names are not stored here.
		//
		std::vector<std::string> buildFolders;
		//
		// Name of the currently selected build item (persisted with the project)
		//
		std::string selectedBuildItem;
	};

	class Project {
	public:
		//
		// Set the absolute path to the SDK folder.
		// Must be called once at startup before any project is opened.
		// Pass an empty string if no SDK folder is present.
		//
		static void SetSdkFolder(const std::string& path);
		//
		// Return the current SDK folder path (empty if not set).
		//
		static std::string GetSdkFolder();
		//
		// Expand a stored project path to an absolute filesystem path.
		// Replaces the $(sdk) variable with the configured SDK folder path.
		// For regular relative paths, resolves against the current project folder.
		//
		static std::string ExpandPath(const std::string& storedPath);
		//
		// Collapse an absolute filesystem path to its portable stored form.
		// If the path lives under the SDK folder it is stored as "$(sdk)/relative".
		// Otherwise it is stored relative to the project folder.
		// This is the inverse of ExpandPath.
		//
		static std::string CollapsePath(const std::string& absolutePath);
		//
		// Create a new empty project at the given path (.retrodev file).
		// Initializes project state and sets the project name from the filename stem.
		// Returns true on success.
		//
		static bool New(const std::string& path);

		//
		// Result codes returned by Project::Open.
		//
		enum class OpenResult {
			// File opened and parsed successfully.
			Ok,
			// File does not exist on disk.
			NotFound,
			// File format is outdated or from a future version -- cannot be loaded.
			UnsupportedVersion,
			// File exists but could not be parsed (corrupt or unrecognised format).
			ParseFailed
		};

		//
		// Open an existing project from the given path (.retrodev file).
		// Reads and parses the JSON project file.
		// Returns OpenResult::Ok on success, or a specific failure code.
		//
		static OpenResult Open(const std::string& path);

		//
		// Save the current project to the given path (.retrodev file).
		// Serializes the project data as prettified JSON. Returns false if
		// no project is open or the write fails.
		// Clears the modified flag on successful save.
		//
		static bool Save(const std::string& path);

		//
		// Close the current project and reset all project state.
		// Returns true on success.
		//
		static bool Close();

		//
		// Check whether a project is currently open.
		//
		static bool IsOpen();
		//
		// Check whether the project has unsaved changes
		//
		static bool IsModified();
		//
		// Mark the project as modified (has unsaved changes)
		//
		static void MarkAsModified();
		//
		// Clear the modified flag (after saving)
		//
		static void ClearModified();

		//
		// Get the full path to the current project file (.retrodev).
		// Returns an empty string if no project is open.
		//
		static std::string GetPath();

		//
		// Get the display name of the current project.
		// Returns an empty string if no project is open.
		//
		static std::string GetName();

		//
		// Get the folder containing the current project file.
		// Returns an empty string if no project is open.
		//
		static std::string GetProjectFolder();

		//
		// Add a file to the current project under the given type category.
		// The file path is stored relative to the project folder.
		// Returns false if no project is open or the file is already tracked.
		//
		static bool AddFile(const std::string& filePath, ProjectFileType type);

		//
		// Remove a file from the current project (searches all type categories).
		// The file path is converted to a relative path before matching.
		// Returns false if no project is open or the file is not found.
		//
		static bool RemoveFile(const std::string& filePath);

		//
		// Check whether a file is tracked in the current project.
		// The file path is converted to a relative path before matching.
		// Returns false if no project is open or the file is not found.
		//
		static bool IsFileInProject(const std::string& filePath);

		// ----------------------------------------------------------------------------------------------------
		//
		// Enumerate from "build" section (not related to files, sections in the json)
		//
		// ----------------------------------------------------------------------------------------------------

		//
		// Get the list of build item names for a given build type category
		// It receives the build type (e.g. Bitmap, Tilemap, Sprite) and returns a vector of build item names for that type.
		// Returns an empty vector if no project is open.
		// The returned vector contains the build item names, which are unique identifiers for the build items
		// and can be used to retrieve their configurations or source file paths.
		//
		static std::vector<std::string> GetBuildItemsByType(ProjectBuildType type);
		//
		// Return the relative paths of all source files tracked in the project
		// Returns an empty vector if no project is open
		//
		static std::vector<std::string> GetSourceFiles();
		//
		// Return the unique relative folder paths derived from all tracked project files.
		// Always includes "." (project root). Suitable for populating include-dir pickers.
		// Returns an empty vector if no project is open.
		//
		static std::vector<std::string> GetProjectFolders();
		//
		// Return the absolute paths of all AngelScript files tracked in the project.
		// Path variables (e.g. $(sdk)) are expanded to real filesystem paths.
		// Returns an empty vector if no project is open.
		//
		static std::vector<std::string> GetScriptFiles();
		//
		// Return relative paths of all files under the project folder whose extension
		// matches any entry in extensions (case-insensitive, without leading dot).
		// Scans recursively.  Returns an empty vector if no project is open.
		//
		static std::vector<std::string> GetFilesByExtensions(const std::vector<std::string>& extensions);
		//
		// Scan the project folder and tracked lists to reflect filesystem changes.
		// Removes tracked entries for files that no longer exist on disk.
		// Detects any change in the set of files present on disk (new or deleted files)
		// by comparing against a cached snapshot from the previous call.
		// Returns true if anything changed (tracked list modified or disk snapshot differs),
		// which signals the caller to rebuild the project tree.
		//
		static bool ScanFiles();
		//
		// Rename a build item in the current project
		// Generic function that delegates to the specific rename function based on build type
		// type: type of build item (Bitmap, Tilemap, Sprite)
		// oldName: current name of the build item
		// newName: desired new name for the build item
		// Returns false if no project is open, the old name does not exist, or the new name already exists
		//
		static bool RenameBuildItem(ProjectBuildType type, const std::string& oldName, const std::string& newName);

		//
		// Get the source file path for a bitmap build item.
		// Returns empty string if not found or no project is open.
		// name: bitmap build item name
		//
		static std::string BitmapGetSourcePath(const std::string& name);

		//
		// Add a bitmap conversion build item to the current project.
		// Returns false if no project is open or the bitmap name already exists.
		// name: unique name for the build item
		// sourceFilePath: path to the source image file
		//
		static bool BitmapAdd(const std::string& name, const std::string& sourceFilePath);

		//
		// Remove a bitmap conversion build item from the current project.
		// Returns false if no project is open or nothing was removed.
		// nameOrSourcePath: bitmap name to remove (exact match), or source file path (removes all bitmaps using that source)
		//
		static bool BitmapRemove(const std::string& nameOrSourcePath);

		//
		// Return a pointer the configuration for a given name if it exists
		// true if the pointer is valid and configuration was found. False otherwise
		//
		static bool BitmapGetCfg(const std::string& name, GFXParams** config);

		//
		// Rename a bitmap conversion build item in the current project.
		// Returns false if no project is open, the old name does not exist, or the new name already exists.
		static bool BitmapRename(const std::string& oldName, const std::string& newName);
		//
		// Update the source file path for a bitmap build item.
		// Returns false if no project is open or the bitmap name does not exist.
		// name: bitmap build item name
		// newSourceFilePath: new absolute path to the source image file
		//
		static bool BitmapSetSourcePath(const std::string& name, const std::string& newSourceFilePath);
		//
		// Get the source file path for a tileset build item.
		// Returns empty string if not found or no project is open.
		// name: tileset build item name
		//
		static std::string TilesetGetSourcePath(const std::string& name);
		//
		// Add a tileset conversion build item to the current project.
		// Returns false if no project is open or the tileset name already exists.
		// name: unique name for the build item
		// sourceFilePath: path to the source image file
		//
		static bool TilesetAdd(const std::string& name, const std::string& sourceFilePath);
		//
		// Remove a tileset conversion build item from the current project.
		// Returns false if no project is open or nothing was removed.
		// nameOrSourcePath: tileset name to remove (exact match), or source file path (removes all tilesets using that source)
		//
		static bool TilesetRemove(const std::string& nameOrSourcePath);
		//
		// Get configuration for a tileset build item
		// Returns the GFXParams for the tileset (same structure as bitmaps)
		// config: output parameter - pointer will be set to the tileset's params
		// Returns true if tileset found, false otherwise
		//
		static bool TilesetGetCfg(const std::string& name, GFXParams** config);
		//
		// Get tile extraction parameters for a tileset build item
		// Returns the TileExtractionParams for the tileset
		// tileParams: output parameter - pointer will be set to the tileset's tile params
		// Returns true if tileset found, false otherwise
		//
		static bool TilesetGetTileParams(const std::string& name, TileExtractionParams** tileParams);
		//
		// Return a const reference to the full buildTiles list.
		// Used by the UI to build sorted deleted-tile lists for compact<->absolute translation.
		//
		static const std::vector<ProjectBuildTilesEntry>& GetBuildTiles();
		//
		// Rename a tileset conversion build item in the current project.
		// Returns false if no project is open, the old name does not exist, or the new name already exists.
		//
		static bool TilesetRename(const std::string& oldName, const std::string& newName);
		//
		// Update the source file path for a tileset build item.
		// Returns false if no project is open or the tileset name does not exist.
		// name: tileset build item name
		// newSourceFilePath: new absolute path to the source image file
		//
		static bool TilesetSetSourcePath(const std::string& name, const std::string& newSourceFilePath);
		//
		// Get the source file path for a sprite build item.
		// Returns empty string if not found or no project is open.
		// name: sprite build item name
		//
		static std::string SpriteGetSourcePath(const std::string& name);
		//
		// Add a sprite conversion build item to the current project.
		// Returns false if no project is open or the sprite name already exists.
		// name: unique name for the build item
		// sourceFilePath: path to the source image file
		//
		static bool SpriteAdd(const std::string& name, const std::string& sourceFilePath);
		//
		// Remove a sprite conversion build item from the current project.
		// Returns false if no project is open or nothing was removed.
		// nameOrSourcePath: sprite name to remove (exact match), or source file path (removes all sprites using that source)
		//
		static bool SpriteRemove(const std::string& nameOrSourcePath);
		//
		// Get configuration for a sprite build item (placeholder - uses GFXParams temporarily)
		// Returns the GFXParams for the sprite
		// config: output parameter - pointer will be set to the sprite's params
		// Returns true if sprite found, false otherwise
		//
		static bool SpriteGetCfg(const std::string& name, GFXParams** config);
		//
		// Rename a sprite conversion build item in the current project.
		// Returns false if no project is open, the old name does not exist, or the new name already exists.
		//
		static bool SpriteRename(const std::string& oldName, const std::string& newName);
		//
		// Update the source file path for a sprite build item.
		// Returns false if no project is open or the sprite name does not exist.
		// name: sprite build item name
		// newSourceFilePath: new absolute path to the source image file
		//
		static bool SpriteSetSourcePath(const std::string& name, const std::string& newSourceFilePath);
		//
		// Get sprite extraction parameters for a sprite build item
		// Returns the SpriteExtractionParams for the sprite
		// spriteParams: output parameter - pointer will be set to the sprite's sprite params
		// Returns true if sprite found, false otherwise
		//
		static bool SpriteGetSpriteParams(const std::string& name, SpriteExtractionParams** spriteParams);
		//
		// Get export parameters for a bitmap build item.
		// exportParams: output pointer set to the item's ExportParams.
		// Returns true if the item was found, false otherwise.
		//
		static bool BitmapGetExportParams(const std::string& name, ExportParams** exportParams);
		//
		// Get export parameters for a tileset build item.
		//
		static bool TilesetGetExportParams(const std::string& name, ExportParams** exportParams);
		//
		// Get export parameters for a sprite build item.
		//
		static bool SpriteGetExportParams(const std::string& name, ExportParams** exportParams);
		//
		// Get export parameters for a map build item.
		//
		static bool MapGetExportParams(const std::string& name, ExportParams** exportParams);

		//
		// Add a map build item to the current project.
		// Returns false if no project is open or the map name already exists.
		//
		static bool MapAdd(const std::string& name);
		//
		// Remove a map build item from the current project.
		// Returns false if no project is open or nothing was removed.
		//
		static bool MapRemove(const std::string& name);
		//
		// Get parameters for a map build item.
		// params: output pointer set to the map's MapParams
		// Returns true if the map was found, false otherwise.
		//
		static bool MapGetParams(const std::string& name, MapParams** params);
		//
		// Rename a map build item in the current project.
		// Returns false if no project is open, old name missing, or new name exists.
		//
		static bool MapRename(const std::string& oldName, const std::string& newName);

		//
		// Get parameters for a palette build item.
		// params: output pointer set to the item's PaletteParams.
		// Returns true if the item was found, false otherwise.
		//
		static bool PaletteGetParams(const std::string& name, PaletteParams** params);
		//
		// Add a palette build item to the current project.
		// Returns false if no project is open or the name already exists.
		//
		static bool PaletteAdd(const std::string& name);
		//
		// Remove a palette build item from the current project.
		// Returns false if no project is open or nothing was removed.
		//
		static bool PaletteRemove(const std::string& name);
		//
		// Rename a palette build item in the current project.
		// Returns false if no project is open, old name missing, or new name exists.
		//
		static bool PaletteRename(const std::string& oldName, const std::string& newName);
		//
		// Get export parameters for a palette build item.
		//
		static bool PaletteGetExportParams(const std::string& name, ExportParams** exportParams);

		//
		// Add a build script item to the current project.
		// Returns false if no project is open or the name already exists.
		//
		static bool BuildAdd(const std::string& name);
		//
		// Remove a build script item from the current project.
		// Returns false if no project is open or nothing was removed.
		//
		static bool BuildRemove(const std::string& name);
		//
		// Get parameters for a build script item.
		// params: output pointer set to the item's BuildParams.
		// Returns true if the item was found, false otherwise.
		//
		static bool BuildGetParams(const std::string& name, SourceParams** params);
		//
		// Rename a build script item in the current project.
		// Returns false if no project is open, old name missing, or new name exists.
		//
		static bool BuildRename(const std::string& oldName, const std::string& newName);
		//
		// Get export parameters for a build script item.
		//
		static bool BuildGetExportParams(const std::string& name, ExportParams** exportParams);
		//
		// Process all dependencies listed in the given build item's SourceParams in order.
		// Each dependency is a build item name (bitmap, tileset, sprite, map, palette, or build) that
		// must be converted/executed before the build executes.
		// Returns true if all dependencies were dispatched successfully, false on the first failure.
		// A failure aborts the remaining dependencies and stops the build.
		//
		static bool BuildProcessDependencies(const std::string& name);
		//
		// Check whether adding targetDep as a dependency to buildItemName would create a circular
		// or diamond dependency. Returns true if the dependency is safe to add, false if it would
		// create a cycle or diamond.
		// buildItemName: the build item that will gain a new dependency
		// targetDep: the candidate dependency name to add
		//
		static bool BuildCanAddDependency(const std::string& buildItemName, const std::string& targetDep);
		//
		// Virtual-folder management for the Build section.
		// folderPath is a slash-separated virtual path (e.g. "graphics/cpc").
		//
		// Add a virtual folder.  Returns false if already exists or no project open.
		//
		static bool FolderAdd(const std::string& folderPath);
		//
		// Remove a virtual folder and strip its prefix from all contained build items.
		// Returns false if the folder does not exist or no project is open.
		//
		static bool FolderRemove(const std::string& folderPath);
		//
		// Return true if folderPath is an explicitly-created virtual folder.
		//
		static bool FolderExists(const std::string& folderPath);
		//
		// Return all explicitly-created virtual folder paths.
		//
		static std::vector<std::string> GetFolders();
		//
		// Get the name of the currently selected build item (persisted in the project).
		// Returns an empty string if no project is open or nothing is selected.
		//
		static std::string GetSelectedBuildItem();
		//
		// Set the name of the currently selected build item and mark the project as modified.
		// Has no effect if no project is open.
		//
		static void SetSelectedBuildItem(const std::string& name);
		//
		// Rename all tracked file entries whose stored path starts with oldAbsPrefix
		// to use newAbsPrefix instead. Both arguments are absolute filesystem paths.
		// Used after a filesystem folder rename to keep the project in sync.
		// Returns the number of entries updated.
		//
		static int RenameFilesWithPrefix(const std::string& oldAbsPrefix, const std::string& newAbsPrefix);
	};

}
