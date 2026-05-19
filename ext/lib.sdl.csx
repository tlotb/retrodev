/*---------------------------------------------------------------------------------------------------------



---------------------------------------------------------------------------------------------------------*/
#load "bld/ext/clang.csx"
#load "bld/ext/git.csx"
#load "bld/csx/build.flags.csx"

#r "../bld/bin/mkb.dll"
using Kltv.Kombine.Api;
using Kltv.Kombine.Types;
using static Kltv.Kombine.Api.Statics;
using static Kltv.Kombine.Api.Tool;

// Name of the library for the output
KValue sdlfriendlyname = "SDL3 (v3.4.x)";
// Path to store the sdl sources
KValue sdl3path = "sdl/";
// Branch to use for the sdl sources
KValue sdl3branch = "release-3.4.x";
// Name of the library for the output
KValue sdl3name = "sdl";
// Repository to use for the sdl sources
KValue sdl3repo = "https://github.com/libsdl-org/SDL.git";

//
// Manage the SDL library dependency
//
int dependencies(string[] args){
	if (Args.Get(0) == "clean") {
		Msg.Print($"Cleaning {sdlfriendlyname} sources");
		Folders.Delete(sdl3path,true);
		return 0;
	}
	if (Args.Get(0) == "update") {
		if (Folders.Exists(sdl3path)){
			Msg.Print($"Updating {sdlfriendlyname} sources");
			//
			// SDL3 is not branched so update is just update the label
			//
			//Git.Pull(sdl3path);
			return 0;
		}
	}
	if ( (Args.Get(0) == "install") || (Args.Get(0) == "update")) {
		Msg.Print($"Cloning {sdlfriendlyname} sources");
		if (Folders.Exists(sdl3path)){
			Msg.BeginIndent();
			Msg.Print($"{sdlfriendlyname} folder already present. Skip");
			Msg.EndIndent();
			return 0;
		}
		Git.Clone(sdl3repo,sdl3path,sdl3branch);
		return 0;
	}
	Msg.Print("No valid parameter action supplied.");
	return -1;
}

//
// Build the SDL library Action
//
int build(string[] args){
	Msg.Print($"Building {sdlfriendlyname} library");
	Msg.BeginIndent();
	if (Host.IsLinux()) {
		Msg.Print("Linux host detected. Using system SDL3 package instead of source build.");
		Msg.EndIndent();
		return register(args);
	}
	// Check if the sdl3 sources folder is present
	if (!Folders.Exists(sdl3path)){
		Msg.PrintAndAbort($"{sdlfriendlyname} sources not found. Please run 'mkb dependencies install' to get the sources.");
	}
	// Output paths
	KValue OutputBin = KValue.Import("OutputBin");
	KValue OutputLib = KValue.Import("OutputLib");
	KValue OutputTmp = KValue.Import("OutputTmp");
	// Compilation flags
	KList Flags = new KList {
		"-msse3",
		"-Wno-empty-body",
		"-Wno-unused-parameter",
		"-Wno-language-extension-token",
		"-Wno-missing-field-initializers"
	};
	// The list of defines to use
	KList Defines = new KList { "SDL_STATIC","SDL_STATIC_LIB", "SDL_USE_BUILTIN_OPENGL_DEFINITIONS" };
	// Include directories
	KList Includes = new KList();
	Includes += sdl3path+"include/build_config/";
	Includes += sdl3path+"include/";
	Includes += sdl3path+"src/";
	// Add the artifact name into the output folders
	OutputBin += sdl3name + "/";
	OutputLib += sdl3name + "/";
	// Create an instance of the clang tool.
	Clang clang = new Clang();
	// Create the list of sources to be compiled
	// We pass the relative path from the current folder
	KList src = CreateSourceList(sdl3path);
	clang.Options.SwitchesCC += Flags;
	clang.Options.SwitchesCXX += Flags;
	clang.Options.Defines += Defines;
	clang.Options.IncludeDirs += Includes;
	// Generate the list of object files to be used as output
	KList objs = src.WithExtension(clang.Options.ObjectExtension).WithPrefix(OutputTmp);
	// And compile the sources
	clang.Compile(src, objs);
	// Use the librarian to generate a static library
	clang.Librarian(objs, OutputLib + sdl3name + clang.Options.LibExtension);
	// ------------------------------------------------------------------------
	Msg.PrintTask("Building static library: " + sdl3name + clang.Options.LibExtension);
	Msg.PrintTaskSuccess(" done");
	Msg.Print("---------------------------------------------------------------------------------------");
	Msg.EndIndent();
	register(args);
	return 0;
}

//
// Register the library
//
int register(string[] args) {
	Msg.Print($"Registering {sdlfriendlyname} library");
	Msg.BeginIndent();
	if (Host.IsLinux()) {
		string incpath = "/usr/include/";
		string libpath = "/usr/lib/x86_64-linux-gnu/";
		if (System.IO.File.Exists(incpath + "SDL3/SDL.h") == false) {
			Msg.PrintAndAbort("SDL3 headers not found. Install package: libsdl3-dev");
		}
		if (System.IO.File.Exists(libpath + "libSDL3.so") == false) {
			if (System.IO.File.Exists("/usr/lib/libSDL3.so")) {
				libpath = "/usr/lib/";
			} else {
				Msg.PrintAndAbort("SDL3 shared library not found. Install package: libsdl3-dev");
			}
		}
		Msg.Print($"Registering {sdlfriendlyname} library under the name: " + sdl3name);
		Msg.Print("  libname: SDL3");
		Share.Register(sdl3name, "libname", "SDL3");
		Msg.Print("  libpath: " + libpath);
		Share.Register(sdl3name, "libpath", libpath);
		Msg.Print("  incpath: " + incpath);
		Share.Register(sdl3name, "incpath", incpath);
		Msg.PrintTask("Registered system library: " + sdl3name);
		Msg.PrintTaskSuccess(" done");
		Msg.Print("---------------------------------------------------------------------------------------");
		Msg.EndIndent();
		return 0;
	}
	KValue OutputLib = KValue.Import("OutputLib");
	OutputLib += sdl3name + "/";
	// Create an instance of the clang tool.
	Clang clang = new Clang();
	// Register the output to make it available for everyone
	Msg.Print($"Registering {sdlfriendlyname} library under the name: "+sdl3name);
	Msg.Print("  libname: " + sdl3name + clang.Options.LibExtension);
	Share.Register(sdl3name,"libname",sdl3name + clang.Options.LibExtension);
	Msg.Print("  libpath: " + RealPath(OutputLib));
	Share.Register(sdl3name,"libpath",RealPath(OutputLib));
	Msg.Print("  incpath: " + RealPath(sdl3path+"include/"));
	Share.Register(sdl3name,"incpath",RealPath(sdl3path+"include/"));
	Msg.PrintTask("Registered static library: " + sdl3name);
	Msg.PrintTaskSuccess(" done");
	Msg.Print("---------------------------------------------------------------------------------------");
	Msg.EndIndent();
	return 0;
}
//
// Clean artifacts
//
int clean(string[] args){
	// Import Output paths
	Msg.Print("Cleaning: "+sdlfriendlyname);
	KValue OutputBin = KValue.Import("OutputBin");
	KValue OutputLib = KValue.Import("OutputLib");
	KValue OutputTmp = KValue.Import("OutputTmp");
	KList folders = new KList();
	folders += OutputBin+sdl3name+"/";
	folders += OutputLib+sdl3name+"/";
	folders += OutputTmp+sdl3name+"/";
	Msg.BeginIndent();
	foreach(KValue folder in folders){
		Msg.Print("Deleting: "+RealPath(folder));
	}
	Msg.EndIndent();
	// Clean the folders
	Folders.Delete(folders,true);
	return 0;
}

//
// Create the source code list to be compiled
//
private KList CreateSourceList(KValue sdl3path){
	KList src = new KList();

	//=== Main sources
	src += Glob(sdl3path+"src/*.c");

	//=== sdl3 common source code
	src += Glob(sdl3path+"src/atomic/*.c");
	// Audio
	src += Glob(sdl3path+"src/audio/*.c");
	src += Glob(sdl3path+"src/audio/disk/*.c");
	src += Glob(sdl3path+"src/audio/dummy/*.c");
	// Camera
	src += Glob(sdl3path+"src/camera/*.c");
	src += Glob(sdl3path+"src/camera/dummy/*.c");
	// Core
	src += Glob(sdl3path+"src/core/*.c");
	// Cpu info
	src += Glob(sdl3path+"src/cpuinfo/*.c");
	// Dialog
	src += Glob(sdl3path+"src/dialog/*.c");
	// Dynamic api
	src += Glob(sdl3path+"src/dynapi/*.c");
	// Events
	src += Glob(sdl3path+"src/events/*.c");
	// Filesystem
	src += Glob(sdl3path+"src/filesystem/*.c");
	// GPU
	src += Glob(sdl3path+"src/gpu/*.c");
	// Haptic
	src += Glob(sdl3path+"src/haptic/*.c");
	src += Glob(sdl3path+"src/haptic/dummy/*.c");
	src += Glob(sdl3path+"src/haptic/hidapi/*.c");

	// IO system
	src += Glob(sdl3path+"src/io/*.c");
	src += Glob(sdl3path+"src/io/generic/*.c");
	// Joystick
	src += Glob(sdl3path+"src/joystick/*.c");
	src += Glob(sdl3path+"src/joystick/dummy/*.c");
	src += Glob(sdl3path+"src/joystick/hidapi/*.c");
	src += Glob(sdl3path+"src/joystick/virtual/*.c");
	// LibM (as per defined only is needed s_modf.c, we include all)
	src += Glob(sdl3path+"src/libm/*.c");
	// Locale
	src += Glob(sdl3path+"src/locale/*.c");
	// Main
	src += Glob(sdl3path+"src/main/*.c");
	src += Glob(sdl3path+"src/main/generic/*.c");
	// Misc
	src += Glob(sdl3path+"src/misc/*.c");
	// Power
	src += Glob(sdl3path+"src/power/*.c");
	// Process
	src += Glob(sdl3path+"src/process/*.c");
	// Render
	src += Glob(sdl3path+"src/render/*.c");
	src += Glob(sdl3path+"src/render/opengl/*.c");
	src += Glob(sdl3path+"src/render/opengles2/*.c");
	src += Glob(sdl3path+"src/render/software/*.c");
	src += Glob(sdl3path+"src/render/gpu/*.c");
	// Sensors
	src += Glob(sdl3path+"src/sensor/*.c");
	src += Glob(sdl3path+"src/sensor/dummy/*.c");
	// Stdlib
	src += Glob(sdl3path+"src/stdlib/*.c");
	// Storage
	src += Glob(sdl3path+"src/storage/*.c");
	src += Glob(sdl3path+"src/storage/generic/*.c");
	src += Glob(sdl3path+"src/storage/steam/*.c");
	// Thread
	src += Glob(sdl3path+"src/thread/*.c");
	src += Glob(sdl3path+"src/thread/generic/*.c");
	// Time
	src += Glob(sdl3path+"src/time/*.c");
	// Timer
	src += Glob(sdl3path+"src/timer/*.c");
	// Tray
	src += Glob(sdl3path+"src/tray/*.c");
	// Video
	src += Glob(sdl3path+"src/video/*.c");
	src += Glob(sdl3path+"src/video/dummy/*.c");
	src += Glob(sdl3path+"src/video/offscreen/*.c");
	src += Glob(sdl3path+"src/video/yuv2rgb/*.c");

	//
	//
	if (Host.IsWindows()){
		//=== SDL3 windows source code
		// Audio
		src += Glob(sdl3path+"src/audio/directsound/*.c");
		src += Glob(sdl3path+"src/audio/wasapi/*.c");
		// Camera
		src += Glob(sdl3path+"src/camera/mediafoundation/*.c");
		// Core
		src += Glob(sdl3path+"src/core/windows/*.c");
		src += Glob(sdl3path+"src/core/windows/*.cpp");
		// Dialog
		src += Glob(sdl3path+"src/dialog/windows/*.c");
		// Filesystem
		src += Glob(sdl3path+"src/filesystem/windows/*.c");
		// GPU
		src += Glob(sdl3path+"src/gpu/d3d12/*.c");
		src += Glob(sdl3path+"src/gpu/vulkan/*.c");
		// Haptic
		src += Glob(sdl3path+"src/haptic/windows/*.c");
		// Hidapi
		src += Glob(sdl3path+"src/hidapi/*.c");
		// IO
		src += Glob(sdl3path+"src/io/windows/*.c");
		// Joystick
		src += Glob(sdl3path+"src/joystick/gdk/*.cpp");
		src += Glob(sdl3path+"src/joystick/windows/*.c");
		// Loadso
		src += Glob(sdl3path+"src/loadso/windows/*.c");
		// Locale
		src += Glob(sdl3path+"src/locale/windows/*.c");
		// Main
		src += Glob(sdl3path+"src/main/windows/*.c");
		// Misc
		src += Glob(sdl3path+"src/misc/windows/*.c");
		// Power
		src += Glob(sdl3path+"src/power/windows/*.c");
		// Process
		src += Glob(sdl3path+"src/process/windows/*.c");
		// Render
		src += Glob(sdl3path+"src/render/direct3d/*.c");
		src += Glob(sdl3path+"src/render/direct3d11/*.c");
		src += Glob(sdl3path+"src/render/direct3d12/*.c");
		src += Glob(sdl3path+"src/render/vulkan/*.c");
		// Sensor
		src += Glob(sdl3path+"src/sensor/windows/*.c");
		// Thread
		src += Glob(sdl3path+"src/thread/windows/*.c");
		// Time
		src += Glob(sdl3path+"src/time/windows/*.c");
		// Timer
		src += Glob(sdl3path+"src/timer/windows/*.c");
		// Tray
		src += Glob(sdl3path+"src/tray/windows/*.c");
		// Video
		src += Glob(sdl3path+"src/video/windows/*.c");
		src += Glob(sdl3path+"src/video/windows/*.cpp");
	}
	if (Host.IsMacOS()){
		// Not yet.
	}
	if (Host.IsLinux()){
		// Not yet.

	}
	return src;
}