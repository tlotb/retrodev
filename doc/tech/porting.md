# Porting Retrodev to Other Platforms

## Cross-platform library choices

Every third-party library in Retrodev was chosen for explicit cross-platform support. None of them require platform-specific code paths in the application layer:

| Library | Role | Platforms |
|---|---|---|
| SDL3 | Window, renderer, input, file I/O | Windows, Linux, macOS, and more |
| Dear ImGui (SDL3 + SDLRenderer3 backends) | UI | Any platform SDL3 runs on |
| FreeType | Font rasterisation | All platforms |
| AngelScript | Scripting engine | All platforms |
| RASM | Z80 assembler | All platforms |
| Glaze | JSON serialisation (header-only) | All platforms |
| CTRE | Compile-time regex (header-only) | All platforms |
| Clang | Compiler used for all builds | Windows, Linux, macOS |

The result is that the vast majority of the codebase compiles and runs identically on every platform with no conditional code at all.

## Path convention

All source file paths and all build artifact paths use forward slashes (`/`) throughout the entire codebase, including the Kombine build scripts. There are no calls to Windows-specific path APIs (`GetFullPathNameW`, backslash concatenation, etc.) and no POSIX-specific path assumptions anywhere in the application code. Kombine and the underlying OS normalise the separator when making actual filesystem calls.

## Platform filtering in Kombine scripts

Platform-specific source files are placed in subfolders named `win/`, `lnx/`, or `osx/`. The Kombine build scripts (`retro.dev.lib.csx` and `retro.dev.gui.csx`) build a full source list from globs and then filter it at build time using the Kombine host detection API:

```csharp
// Host.IsWindows() — skip Linux and macOS sources
if (filex.Contains("/osx/") || filex.Contains("/lnx/"))
    continue;

// Host.IsLinux() — skip Windows and macOS sources
if (filex.Contains("/win/") || filex.Contains("/osx/"))
    continue;

// Host.IsMacOS() — skip Windows and Linux sources
if (filex.Contains("/win/") || filex.Contains("/lnx/"))
    continue;
```

Any new platform-specific implementation file placed in the correct subfolder is automatically picked up or excluded by this mechanism — no changes to the build scripts are needed beyond adding the folder filter if a new platform name is introduced.

On Windows release builds the GUI script additionally adds the `.rc` resource file to the source list (application icon and manifest) and passes the following linker flags, which are Windows-only:

```
-Wl,/subsystem:windows
-Wl,/manifest:embed
-Wl,/manifestinput:gui/os/win/retrodev.manifest
```

## Platform-specific source files

### Application entry point

`src/gui/os/win/entry.cpp` provides the Windows entry point. In release mode it defines `WinMain`, converts the LPSTR command line to a UTF-8 `argv` array via `WideCharToMultiByte`, and calls the shared `retromain()`. In debug mode the same file defines a standard `main()` so the application runs as a console program.

Linux now has an explicit entry point at `src/gui/os/lnx/entry.cpp`. It defines `main(int argc, char** argv)` and forwards directly to `retromain()`.

macOS still needs an equivalent `src/gui/os/osx/entry.cpp` file.

### Emulator process launching

This is the only area where platform-specific system API calls are required. The interface is declared in `src/lib/assets/source/source.emulator.native.h` and implemented separately per platform:

| File | Platform | API used |
|---|---|---|
| `src/lib/assets/source/win/source.emulator.native.win.cpp` | Windows | `CreateProcessW`, `WaitForSingleObject`, `GetExitCodeProcess`, `CloseHandle` |
| `src/lib/assets/source/lnx/source.emulator.native.lnx.cpp` | Linux | `posix_spawn`, `posix_spawn_file_actions_t`, `waitpid` |
| `src/lib/assets/source/osx/source.emulator.native.osx.cpp` | macOS | `posix_spawn`, `posix_spawn_file_actions_t`, `waitpid` |

All three implementations are already present in the repository. The Linux and macOS files share the same `posix_spawn` approach, including a command-line tokeniser that correctly handles quoted paths with spaces.

### Windows-only resources

`src/gui/os/win/` also contains `retrodev.rc`, `resource.h`, `retrodev.ico`, and `retrodev.manifest`. These are compiled only on Windows release builds by the Kombine source filter and linker flags described above. They have no equivalent on other platforms and none is needed.

### Windows SDK libraries

`retro.dev.gui.csx` links the following Windows SDK libraries on Windows builds only (guarded by `Host.IsWindows()`):

```
dwmapi.lib  user32.lib  kernel32.lib  gdi32.lib  winmm.lib
setupapi.lib  imm32.lib  shell32.lib  ole32.lib  advapi32.lib
version.lib  oleaut32.lib  Shcore.lib  uxtheme.lib  Ws2_32.lib
```

Linux now links `m`, `dl`, `pthread`, and `rt` in `retro.dev.gui.csx`. macOS still has a `// TODO` linker block.

## Summary: what remains for a Linux or macOS port

| Item | Status |
|---|---|
| All application and library C++ sources | ✅ Already cross-platform |
| Third-party libraries (SDL3, ImGui, FreeType, etc.) | ✅ All support Linux and macOS |
| Kombine platform filtering infrastructure | ✅ Already handles Linux and macOS folder exclusion |
| Emulator spawn (`posix_spawn`) | ✅ `lnx` and `osx` files already exist |
| Application entry point (`main()`) | ⚠️ Linux done (`src/gui/os/lnx/entry.cpp`), macOS pending (`src/gui/os/osx/entry.cpp`) |
| System linker libraries | ⚠️ Linux done (`m`, `dl`, `pthread`, `rt`), macOS pending |
