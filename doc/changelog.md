## Version 0.9.24144546



* Fixed: Add / remove tilesets was broken due to changes to improve the usability (always show deleted tiles)
* Fixed: In the exporters, last option in comboboxes was always not recognized
* Fixed: Text editor, cursor going offscreen due to inserted codelens
* Fixed: Cursor getting at the start of the file when ctrl+s
* Fixed: Text editor receiving input even if a dialog box was on top
* Fixed: File drag-and-drop now correctly updates build item source paths
* Fixed: Collisions in project tree between build items and folders with the same name
* Added: Folder renaming for file tree
* Added: Drag and drop to move files between folders
* Added: Build items can now depend on other build items for multi-stage builds
  - Supports dependency chains (e.g., kernel → game → final)
  - Circular dependency detection prevents A→B→A cycles
  - Diamond dependency detection prevents multiple paths to the same item
  - Each build dependency is fully assembled before the dependent build executes
  - Invalid dependencies are automatically filtered from the picker UI


## Version 0.9.24124993


On the road to the version 1.0 this is the first bulk and massive update to cover missing topics on the initial preview and add all the usability features encountered during usage among bugfixing.


### Features and fixes
* Fixed: Crash when building outputing to a DSK file. Updated to Rasm last version with the fix.
* Fixed: When open a project close the currently opened one gracefully
* Fixed: Sdk files now participate correctly in the codelens
* Fixed: Autocompletion was not being showed up sometimes
* Fixed: Build UI item space for includes and defines,usability
* Fixed: When closing the application ask properly if there are unsaved changes
* Added: Expose IsTransparent and IsOpaque to the exporters
* Added: Macros and functions to the SDK (Amstrad CPC)
* Added: Improved examples and new ones
* Fixed: Codelens inline appearing on comments
* Fixed: Codelens tooltip appearing in the symbol definition itself
* Fixed: Ini file now is stored correctly in app path
* Added: Pack-to-Grid in tile extraction -- auto-detects content regions in irregular tile sheets (e.g. magenta-background sprite dumps) and rearranges them into a uniform grid so the standard tile extractor can process them directly
* Added: App now remembers last used directory for file dialogs
* Fixed: Documents with same name but different type or path can now be opened
* Added: Add a palette to allow constraints on solve palette
* Added: Improved transparent color and pen handling so it can be properly defined for palette solving steps
* Added: That transparency handling also available on the exporter scripts.
* Fixed: XH/XL/IH and IL does not trigger invalid instruction anymore.
* Fixed: When some tiles where deleted the tile preview was selecting the wrong one
* Fixed: The tile preview list was clipping some tiles
* Added: When a tile is deleted now the tile stills visible but just marked as deleted
* Added: Added a button to delete duplicated tiles
* Added: Added a button to undelete all deleted tiles
* Added: Autohide to the console output
* Fixed: Wrong save state indicator when building or debugging
* Added: Improved tile and sprite preview
* Fixed: Application hungs when adding a color constraint in palette solver.
* Fixed: Open an error from the console may deliver to duplicate opened document
* Fixed: After palette validate, participant documents showed converter error instead of refreshing
* Added: Palette user-validation flag — build pipeline now applies imperfect (overflow-remapped) palette solutions when the user has explicitly validated them
* Fixed: Build with the latest version of freetype was not working (they changed it recently)
* Added: Improved the tooltips over all the application
* Added: Project version number to migrate to newer formats if the project format receives breaking changes
* Added: Improved sprite operations so you can preshift, duplicate and flip in any way
* Added: Multiselection on tiles and sprites for bulk operations
* Fixed: Editor sizing and keyboard issues
* Added: Improved Map usability (fixup against tiles marked as deleted)
* Added: Sync between deleted tiles across variants in map editor
* Added: Detection and dialog if a file has been modified externally
* Added: It version checks against github to notify if a new release is made
* Added: If a dependency fails on build, build is stopped.

## Version 0.9.24114562

### Features

* Update palette solver

  * Now it allows different solving methods
  * Improved usability
* Added tab contextual menus
* Improved doc and about dialogs
* Fixed: Example projects had remainings from the development test and were not working properly.
* Added confirmation dialog on overwriting an existing project
* Sprites can now be removed from the extracted sprite list via context menu
* Added sprite constraints to avoid extracting one sprite which doesn't match the platform specs
* Added tile constraints to avoid extracting tiles which doesn't match the platform specs
* Improved selection boxes
* Cleanedup the examples (still missing good examples)

### Know issues

* There are stills a crash when assembling in weird situations

