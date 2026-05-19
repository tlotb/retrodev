# Building Retrodev from Source

Retrodev uses **[Kombine](https://github.com/kollective-networks/kltv.kombine)** as its build system. Kombine is a C#-scripted build tool: build logic is written in `.csx` files (C# scripts) and executed by the **`mkb`** command-line runner (mkb stands for *make kombine*). There is no CMake or MSBuild involved in the compilation — only the Kombine scripts.

## Prerequisites

| Tool | Notes |
|---|---|
| [Kombine](https://github.com/kollective-networks/kltv.kombine) (`mkb`) | The build runner. A single self-contained executable — no .NET runtime, no Python, no additional dependencies. Install so that `mkb` is on `PATH`. |
| [Clang](https://releases.llvm.org/) | Used as the compiler and linker for all source. Must be on `PATH`. |
| [Git](https://git-scm.com/) | Required to fetch and update external dependencies. Must be on `PATH`. |
| Visual Studio or Microsoft Build Tools | Clang on Windows links against the MSVC CRT and Windows SDK. These are not bundled with Clang — they must be installed separately. See [Windows CRT requirements](#windows-crt-requirements) below. |

Retrodev currently builds on **Windows and Linux**. macOS support is still in progress.

## Linux requirements

On Linux, Retrodev links SDL3, SDL3_image, and FreeType from system packages instead of building them from source.

```sh
sudo apt update
sudo apt install -y clang lld git libsdl3-dev libsdl3-image-dev libfreetype-dev
```

After dependencies are installed, the standard Kombine flow is the same as on Windows:

```sh
mkb dependencies install
mkb build debug deps
```

The Linux GUI binary is produced at `out/bin/lnx/debug/retro.dev.gui/retro.dev.gui.out`.

## Windows CRT requirements

Clang on Windows does not ship its own C runtime or Windows SDK headers. It expects to find them in the standard locations that Visual Studio or the Microsoft Build Tools install them to. Without these components the linker will fail to resolve CRT symbols (`__CrtDbgReport`, `_wassert`, etc.) and the Windows API headers will be missing.

You can satisfy this requirement in one of two ways:

**Option A — Visual Studio (recommended if you already have it)**

Install [Visual Studio](https://visualstudio.microsoft.com/) and, in the installer's *Workloads* tab, select **Desktop development with C++**. This pulls in the MSVC compiler toolchain, the CRT, and the Windows SDK in one step. Clang will locate them automatically via the registry entries that the VS installer writes. See the [Visual Studio documentation](https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation) for a step-by-step walkthrough.

**Option B — Microsoft Build Tools (minimal install, no IDE)**

Install the [Build Tools for Visual Studio](https://visualstudio.microsoft.com/visual-cpp-build-tools/) (a standalone, IDE-free package). In the installer select the **Desktop development with C++** workload. At minimum the following individual components are required:

- **MSVC v143 (or later) — VS C++ x64/x86 build tools** — provides the CRT import libraries and headers.
- **Windows 11 SDK (10.0.22621 or later)** — provides `windows.h` and the Win32 API link libraries.

The SDK version does not need to match Windows 11 exactly; any recent SDK (10.0.19041 or newer) is sufficient. Clang discovers the installation through the registry; no manual `PATH` or `LIB` configuration is needed.

> **Note:** the Visual Studio Developer Command Prompt / PowerShell sets additional environment variables (`INCLUDE`, `LIB`, `LIBPATH`) that are not required by Kombine's Clang invocations — Kombine passes all necessary paths explicitly. A normal shell with `mkb` and `clang` on `PATH` is sufficient.

## Cloning

```sh
git clone https://github.com/tlotb/retrodev.git
cd retrodev
```

## First build

After cloning, fetch and build all external dependencies first, then build the application:

```sh
mkb dependencies install
mkb build debug deps
```

`dependencies install` clones every dependency repository at the pinned tag. `build debug deps` compiles the dependencies from source and then compiles the application in debug mode. On subsequent builds you can omit `deps` to skip recompiling the dependencies — Kombine will register their already-built artifacts instead:

```sh
mkb build debug
```

## Build verbs

All verbs are invoked via the `mkb` command-line tool:

```sh
mkb <verb> [parameters]
```

The entry-point script is `kombine.csx` at the repository root. It delegates to per-library and per-module sub-scripts automatically.

### `build`

Compiles the application. Processes all declared dependencies first (either building or registering them), then builds the two main modules (`retro.dev.lib` and `retro.dev.gui`) and syncs the `sdk/` folder into the binary output directory.

```sh
mkb build
mkb build release
mkb build release deps
mkb build verbose
```

**Parameters:**

| Parameter | Description |
|---|---|
| `debug` | Debug build with full debug info and no optimisation (default). |
| `release` | Release build with `-O2`, static runtime, and version header stamping. |
| `deps` | Also build the external dependency libraries (SDL3, ImGui, AngelScript, RASM, …). Omit to register pre-built deps. |
| `verbose` | Print the full Clang invocation for every compiled file. |
| `production` | Remove developer settings and debug information from the output. |

For release builds the version header (`src/lib/system/version.h`) is stamped with a generated build number, the build runs, and the header is restored afterwards regardless of success or failure. The full version string is also written to `out/pkg/version.txt`.

**Output paths** are structured as:

```
out/bin/<os>/<debug|release>/retro.dev.gui/   ← executable and sdk/
out/lib/<os>/<debug|release>/                 ← static libraries
out/tmp/<os>/<debug|release>/                 ← object files, compile_commands.json
```

### `dependencies`

Manages the external dependency libraries. Run this before the first build or after changing dependency versions. All dependencies are cloned from Git — Git must be on `PATH`.

```sh
mkb dependencies update
mkb dependencies install
mkb dependencies clean
```

**Parameters:**

| Parameter | Description |
|---|---|
| `update` | Update the dependency sources (git pull / re-fetch). |
| `install` | Install / prepare the dependency sources. |
| `clean` | Remove dependency build artifacts. |

> **Build vs register:** when the `deps` parameter is passed to `mkb build`, Kombine compiles every dependency library from source. When `deps` is omitted, Kombine instead *registers* the dependencies — it pushes their paths and library names onto the internal share stack so the linker can find them, but does not recompile anything. Use `deps` only when the dependency sources have changed; omit it for all normal incremental application builds.

**Dependency sources:**

| Library | Repository | Tag / Branch |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | `https://github.com/libsdl-org/SDL.git` | `release-3.4.x` |
| [SDL3_image](https://github.com/libsdl-org/SDL_image) | `https://github.com/libsdl-org/SDL_image.git` | `release-3.4.x` |
| [Dear ImGui](https://github.com/ocornut/imgui) | `https://github.com/ocornut/imgui.git` | `v1.92.6` |
| [AngelScript](https://github.com/anjo76/angelscript) | `https://github.com/anjo76/angelscript.git` | `v2.38.0` |
| [RASM](https://github.com/EdouardBERGE/rasm) | `https://github.com/EdouardBERGE/rasm.git` | `v3.0.8` |
| [Glaze](https://github.com/stephenberry/glaze) | `https://github.com/stephenberry/glaze` | `v2.9.5` |
| [CTRE](https://github.com/hanickadot/compile-time-regular-expressions) | `https://github.com/hanickadot/compile-time-regular-expressions.git` | `v3.10.0` |
| [FreeType](https://github.com/freetype/freetype) | `https://github.com/freetype/freetype.git` | `master` |

### `clean`

Removes build artifacts for the selected configuration.

```sh
mkb clean
mkb clean release
mkb clean deps
```

**Parameters:**

| Parameter | Description |
|---|---|
| `debug` | Clean debug artifacts (default). |
| `release` | Clean release artifacts. |
| `deps` | Also clean dependency artifacts. The application is always cleaned. |

### `format`

Applies `clang-format` (using the `.clang-format` file at the repository root) to all `.h`, `.cpp` and `.c` files under `src/` and the extended ImGui and RASM sources under `ext/`.

```sh
mkb format
```

### `help`

Prints a summary of all verbs and their parameters.

```sh
mkb help
```

## Visual Studio solution

The repository includes `retro.dev.sln`, a Visual Studio solution with three projects grouped under `build.projects`:

| Project | Type | Purpose |
|---|---|---|
| `retro.dev` | Shared items (`.vcxitems`) | Contains all source files. Shared into the other projects so IntelliSense and the editor work across the full codebase. |
| `retro.dev.gui` | Makefile project | Builds the application by invoking `mkb build`. The Visual Studio debugger attaches to the resulting process, so you can build and debug without leaving the IDE. |
| `debug.build` | Makefile project | Debugs the Kombine build scripts themselves. Launches `mkb` under the Visual Studio debugger so you can step through `.csx` build logic. |

Opening `retro.dev.sln` in Visual Studio is the recommended way to work on the source — the actual compilation is still driven by Kombine, but you get full IDE editing, IntelliSense and debugger support.

## Compiler flags summary

All modules share a common set of compiler flags defined in the `buildCompilerFlags()` function in `kombine.csx`:

- **C standard:** C17 (`-std=c17`)
- **C++ standard:** C++20 (`-std=c++20`, `-pedantic`, `-Wall`, `-Wextra`)
- **Exceptions:** disabled (`-fno-exceptions`)
- **Debug:** `-g -glldb -gfull -O0`, static debug runtime
- **Release:** `-O2`, static release runtime, `NDEBUG` defined

A `compile_commands.json` database is written to `out/tmp/<os>/<config>/compile_commands.json` for IDE and tooling integration.
