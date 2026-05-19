/*---------------------------------------------------------------------------------------------------------



---------------------------------------------------------------------------------------------------------*/
#load "bld/ext/clang.csx"
#load "bld/csx/build.flags.csx"

#r "../bld/bin/mkb.dll"
using Kltv.Kombine.Api;
using Kltv.Kombine.Types;
using static Kltv.Kombine.Api.Statics;
using static Kltv.Kombine.Api.Tool;

KValue appname = "RetroDev (Raster)";
KValue srcpath = "raster.app/";
KValue binname = "retro.dev.raster";

int dependencies(string[] args) {
	return 0;
}

int build(string[] args) {
	Msg.Print($"Building {appname} application");
	Msg.BeginIndent();

	KValue OutputBin = KValue.Import("OutputBin");
	KValue OutputLib = KValue.Import("OutputLib");
	KValue OutputTmp = KValue.Import("OutputTmp");

	KList CompilerFlags = new KList { "-Wall -Wextra -Wno-unused-parameter -Wno-missing-braces" };
	KList Defines = new KList();
	Defines += "IMGUI_USER_CONFIG=\\\"retrodev.imconfig.h\\\"";

	KList Includes = new KList();
	Includes += Share.Registry("retrodev", "incpath");
	Includes += Share.Registry("imgui", "incpath");
	Includes += Share.Registry("imgui", "incbackendspath");
	Includes += Share.Registry("imgui", "incconfigpath");
	Includes += Share.Registry("imgui", "incextensionspath");
	Includes += Share.Registry("sdl", "incpath");
	Includes += Share.Registry("sdl.img", "incpath");
	Includes += Share.Registry("freetype", "incpath");
	Includes += Share.Registry("glaze", "incpath");
	Includes += Share.Registry("ctre", "incpath");
	Includes += srcpath;
	Includes += "gui/";

	KList LibraryDirs = new KList();
	LibraryDirs += Share.Registry("retrodev", "libpath");
	LibraryDirs += Share.Registry("sdl", "libpath");
	LibraryDirs += Share.Registry("sdl.img", "libpath");
	LibraryDirs += Share.Registry("freetype", "libpath");
	LibraryDirs += Share.Registry("ascript", "libpath");
	LibraryDirs += Share.Registry("rasm", "libpath");

	KList Libraries = new KList();
	Libraries += Share.Registry("retrodev", "libname");
	Libraries += Share.Registry("sdl", "libname");
	Libraries += Share.Registry("sdl.img", "libname");
	Libraries += Share.Registry("freetype", "libname");
	Libraries += Share.Registry("ascript", "libname");
	Libraries += Share.Registry("rasm", "libname");

	if (Host.IsWindows()) {
		Libraries += "dwmapi.lib";
		Libraries += "user32.lib";
		Libraries += "kernel32.lib";
		Libraries += "gdi32.lib";
		Libraries += "winmm.lib";
		Libraries += "setupapi.lib";
		Libraries += "imm32.lib";
		Libraries += "shell32.lib";
		Libraries += "ole32.lib";
		Libraries += "advapi32.lib";
		Libraries += "version.lib";
		Libraries += "oleaut32.lib";
		Libraries += "Shcore.lib";
		Libraries += "uxtheme.lib";
		Libraries += "Ws2_32.lib";
	}
	if (Host.IsLinux()) {
		Libraries += "m";
		Libraries += "dl";
		Libraries += "pthread";
		Libraries += "rt";
	}

	Clang clang = new Clang();

	if (Host.IsWindows()) {
		if (BuildFlags.Flags.BuildMode == "release") {
			clang.Options.SwitchesLD += "-Wl,/subsystem:windows";
			clang.Options.SwitchesLD += "-Wl,/manifest:embed";
		}
	}

	OutputBin += binname + "/";
	OutputLib += binname + "/";
	OutputTmp += binname + "/";

	KList src = CreateSourceList(srcpath);

	clang.Options.SwitchesCC += CompilerFlags;
	clang.Options.SwitchesCXX += CompilerFlags;
	clang.Options.IncludeDirs += Includes;
	clang.Options.LibraryDirs += LibraryDirs;
	clang.Options.Libraries += Libraries;
	clang.Options.Defines += Defines;

	KList objs = src.WithExtension(clang.Options.ObjectExtension).WithPrefix(OutputTmp);
	clang.ProcessFile += CustomParameters;
	clang.Compile(src, objs);
	clang.Linker(objs, OutputBin + binname + clang.Options.BinaryExtension);

	Msg.PrintTask("Building binary: " + binname + clang.Options.BinaryExtension);
	Msg.PrintTaskSuccess(" done");
	Msg.Print("---------------------------------------------------------------------------------------");
	Msg.EndIndent();
	return 0;
}

int clean(string[] args) {
	Msg.Print($"Cleaning {appname}");
	KValue OutputBin = KValue.Import("OutputBin");
	KValue OutputLib = KValue.Import("OutputLib");
	KValue OutputTmp = KValue.Import("OutputTmp");
	KList folders = new KList();
	folders += OutputBin + binname + "/";
	folders += OutputLib + binname + "/";
	folders += OutputTmp + binname + "/";
	folders += OutputTmp + "ext/";
	Msg.BeginIndent();
	foreach (KValue folder in folders) {
		Msg.Print("Deleting: " + RealPath(folder));
	}
	Msg.EndIndent();
	Folders.Delete(folders, true);
	return 0;
}

private KList CreateSourceList(KValue srcpath) {
	KList src = new KList();
	Msg.Print("Creating source list for: " + RealPath(srcpath));

	src += Glob(srcpath + "**/*.cpp");
	src += Glob(srcpath + "**/*.c");

	// Shared raster editor implementation from the main GUI codebase.
	src += "gui/views/raster/document.raster.cpc.cpp";
	src += "gui/widgets/palette.widget.cpp";
	src += "gui/views/text/langs/lang.asm.z80.cpp";

	string imgui = Share.Registry("imgui", "srcpath");
	imgui = Path.GetRelativePath(CurrentScriptFolder, imgui);
	src += imgui + "imgui.cpp";
	src += imgui + "imgui_demo.cpp";
	src += imgui + "imgui_draw.cpp";
	src += imgui + "imgui_tables.cpp";
	src += imgui + "imgui_widgets.cpp";
	src += imgui + "backends/imgui_impl_sdl3.cpp";
	src += imgui + "backends/imgui_impl_sdlrenderer3.cpp";
	src += imgui + "misc/freetype/imgui_freetype.cpp";

	string imguiExt = Share.Registry("imgui", "incextensionspath");
	imguiExt = Path.GetRelativePath(CurrentScriptFolder, imguiExt);
	src += Glob(imguiExt + "**/*.cpp");

	KList srcFiltered = new KList();
	foreach (KValue file in src) {
		string filex = file;
		if (Host.IsWindows()) {
			if (filex.Contains("/osx/") || filex.Contains("/lnx/"))
				continue;
		}
		if (Host.IsLinux()) {
			if (filex.Contains("/win/") || filex.Contains("/osx/"))
				continue;
		}
		if (Host.IsMacOS()) {
			if (filex.Contains("/win/") || filex.Contains("/lnx/"))
				continue;
		}
		srcFiltered += file;
	}

	return srcFiltered;
}

public string CustomParameters(string file) {
	string addArgs = "";
	if (file == @"..\ext\imgui\misc/freetype/imgui_freetype.cpp" ||
		file == "../ext/imgui/misc/freetype/imgui_freetype.cpp") {
		addArgs = " -Wno-unused-function";
		Msg.Print("Applying: Custom warning removal for imgui freetype");
	}
	return addArgs;
}
