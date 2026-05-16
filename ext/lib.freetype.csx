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
KValue libfriendlyname = "Freetype (master)";
// Path to store the lib sources
KValue libpath = "freetype/";
// Branch to use for the lib sources
KValue libbranch = "master";
// Name of the library for the output
KValue libname = "freetype";
// Repository to use for the lib sources
KValue librepo = "https://github.com/freetype/freetype.git";

//
// Manage the library dependency
//
int dependencies(string[] args){
	if (Args.Get(0) == "clean") {
		Msg.Print($"Cleaning {libfriendlyname} sources");
		Folders.Delete(libpath,true);
		return 0;
	}
	if (Args.Get(0) == "update") {
		if (Folders.Exists(libpath)){
			Msg.Print($"Updating {libname} sources");
			Git.Pull(libpath);
			return 0;
		}
	}
	if ( (Args.Get(0) == "install") || (Args.Get(0) == "update")) {
		Msg.Print($"Cloning {libfriendlyname} sources");
		if (Folders.Exists(libpath)){
			Msg.BeginIndent();
			Msg.Print($"{libfriendlyname} folder already present. Skip");
			Msg.EndIndent();
			return 0;
		}
		Git.Clone(librepo,libpath,libbranch);
		return 0;
	}
	Msg.Print("No valid parameter action supplied.");
	return -1;
}


// Build the library Action
int build(string[] args){
	Msg.Print($"Building {libfriendlyname} library");
	Msg.BeginIndent();
	if (Host.IsLinux() && HasSystemFreetype()) {
		Msg.Print("Linux host detected. Using system FreeType package instead of source build.");
		Msg.EndIndent();
		return register(args);
	}
	// Check if the sdl2 sources folder is present
	if (!Folders.Exists(libpath)){
		Msg.PrintAndAbort($"{libfriendlyname} sources not found. Please run 'mkb dependencies install' to get the sources.");
	}
	// Output paths
	KValue OutputBin = KValue.Import("OutputBin");
	KValue OutputLib = KValue.Import("OutputLib");
	KValue OutputTmp = KValue.Import("OutputTmp");
	// Compilation flags
	KList Flags = new KList {};
	Flags += "-Wno-deprecated-declarations";
	// The list of defines to use
	KList Defines = new KList { "FT2_BUILD_LIBRARY" };
	if (Host.IsLinux()) {
		Defines += "HAVE_UNISTD_H";
		Defines += "HAVE_FCNTL_H";
	}
	// Include directories
	KList Includes = new KList();
	Includes += libpath+"include/";
	Includes += libpath;
	// Add the artifact name into the output folders
	OutputBin += libname + "/";
	OutputLib += libname + "/";
	// Create an instance of the clang tool.
	Clang clang = new Clang();
	// Create the list of sources to be compiled
	// We pass the relative path from the current folder
	KList src = CreateSourceList(libpath);
	clang.Options.SwitchesCC += Flags;
	clang.Options.SwitchesCXX += Flags;
	clang.Options.Defines += Defines;
	clang.Options.IncludeDirs += Includes;
	// Generate the list of object files to be used as output
	KList objs = src.WithExtension(clang.Options.ObjectExtension).WithPrefix(OutputTmp);
	// And compile the sources
	clang.Compile(src, objs);
	// Use the librarian to generate a static library
	clang.Librarian(objs, OutputLib + libname + clang.Options.LibExtension);
	// ------------------------------------------------------------------------
	Msg.PrintTask("Building static library: " + libname + clang.Options.LibExtension);
	Msg.PrintTaskSuccess(" done");
	Msg.Print("---------------------------------------------------------------------------------------");
	Msg.EndIndent();
	// Libraries are registered in the stack when built
	register(args);
	return 0;
}

//
// Register the library in the stack to be used
//
int register(string[] args) {
	Msg.Print($"Registering {libfriendlyname} library");
	Msg.BeginIndent();
	if (Host.IsLinux() && HasSystemFreetype()) {
		string incpath = "/usr/include/freetype2/";
		string libpath = "/usr/lib/x86_64-linux-gnu/";
		if (System.IO.File.Exists(incpath + "ft2build.h") == false) {
			if (System.IO.File.Exists("/usr/include/freetype2/ft2build.h")) {
				incpath = "/usr/include/freetype2/";
			} else {
				Msg.PrintAndAbort("FreeType headers not found. Install package: libfreetype-dev");
			}
		}
		if (System.IO.File.Exists(libpath + "libfreetype.so") == false) {
			if (System.IO.File.Exists("/usr/lib/libfreetype.so")) {
				libpath = "/usr/lib/";
			} else {
				Msg.PrintAndAbort("FreeType shared library not found. Install package: libfreetype-dev");
			}
		}
		Msg.Print($"Registering {libfriendlyname} library under the name: " + libname);
		Msg.Print("  libname: freetype");
		Share.Register(libname, "libname", "freetype");
		Msg.Print("  libpath: " + libpath);
		Share.Register(libname, "libpath", libpath);
		Msg.Print("  incpath: " + incpath);
		Share.Register(libname, "incpath", incpath);
		Msg.PrintTask("Registered system library: " + libname);
		Msg.PrintTaskSuccess(" done");
		Msg.Print("---------------------------------------------------------------------------------------");
		Msg.EndIndent();
		return 0;
	}
	KValue OutputLib = KValue.Import("OutputLib");
	OutputLib += libname + "/";
	// Create an instance of the clang tool.
	Clang clang = new Clang();
	// Register the output to make it available for everyone
	Msg.Print($"Registering {libfriendlyname} library under the name: "+libname);
	Msg.Print("  libname: " + libname + clang.Options.LibExtension);
	Share.Register(libname,"libname",libname + clang.Options.LibExtension);
	Msg.Print("  libpath: " + RealPath(OutputLib));
	Share.Register(libname,"libpath",RealPath(OutputLib));
	Msg.Print("  incpath: " + RealPath(libpath+"include/"));
	Share.Register(libname,"incpath",RealPath(libpath+"include/"));
	Msg.PrintTask("Registered static library: " + libname);
	Msg.PrintTaskSuccess(" done");
	Msg.Print("---------------------------------------------------------------------------------------");
	Msg.EndIndent();
	return 0;
}

private bool HasSystemFreetype() {
	string incpath = "/usr/include/freetype2/";
	string libpath = "/usr/lib/x86_64-linux-gnu/";
	return System.IO.File.Exists(incpath + "ft2build.h") &&
		(System.IO.File.Exists(libpath + "libfreetype.so") || System.IO.File.Exists("/usr/lib/libfreetype.so"));
}
//
// Clean artifacts
//
int clean(string[] args){
	// Import Output paths
	Msg.Print("Cleaning: "+libname);
	KValue OutputBin = KValue.Import("OutputBin");
	KValue OutputLib = KValue.Import("OutputLib");
	KValue OutputTmp = KValue.Import("OutputTmp");
	KList folders = new KList();
	folders += OutputBin+libname+"/";
	folders += OutputLib+libname+"/";
	folders += OutputTmp+libname+"/";
	Msg.BeginIndent();
	foreach(KValue folder in folders){
		Msg.Print("Deleting: "+RealPath(folder));
	}
	Msg.EndIndent();
	// Clean the folders
	Folders.Delete(folders,true);
	return 0;
}


// Create the source code list to be compiled
private KList CreateSourceList(KValue libpath){
	KList src = new KList();
	// common source code
	src += libpath + "src/autofit/autofit.c";
	src += libpath + "src/bdf/bdf.c";
	src += libpath + "src/cff/cff.c";
	src += libpath + "src/dlg/dlgwrap.c";
	src += libpath + "src/base/ftbase.c";
	src += libpath + "src/cache/ftcache.c";
	src += libpath + "src/gzip/ftgzip.c";
	src += libpath + "src/bzip2/ftbzip2.c";
	src += libpath + "src/base/ftinit.c";
	src += libpath + "src/lzw/ftlzw.c";
	src += libpath + "src/hvf/hvf.c";

	src += libpath + "src/pcf/pcf.c";
	src += libpath + "src/pfr/pfr.c";
	src += libpath + "src/psaux/psaux.c";
	src += libpath + "src/pshinter/pshinter.c";
	src += libpath + "src/psnames/psmodule.c";
	src += libpath + "src/raster/raster.c";
	src += libpath + "src/sdf/sdf.c";
	src += libpath + "src/sfnt/sfnt.c";
	src += libpath + "src/smooth/smooth.c";
	src += libpath + "src/svg/svg.c";
	src += libpath + "src/truetype/truetype.c";
	src += libpath + "src/type1/type1.c";
	src += libpath + "src/cid/type1cid.c";
	src += libpath + "src/type42/type42.c";
	src += libpath + "src/winfonts/winfnt.c";

	src += libpath + "src/base/ftbbox.c";
	src += libpath + "src/base/ftbdf.c";
	src += libpath + "src/base/ftbitmap.c";
	src += libpath + "src/base/ftcid.c";
	src += libpath + "src/base/ftfstype.c";
	src += libpath + "src/base/ftgasp.c";
	src += libpath + "src/base/ftglyph.c";
	src += libpath + "src/base/ftgxval.c";
	src += libpath + "src/base/ftmm.c";
	src += libpath + "src/base/ftotval.c";
	src += libpath + "src/base/ftpatent.c";
	src += libpath + "src/base/ftpfr.c";
	src += libpath + "src/base/ftstroke.c";
	src += libpath + "src/base/ftsynth.c";
	src += libpath + "src/base/fttype1.c";
	src += libpath + "src/base/ftwinfnt.c";

	if (Host.IsWindows()){
		src += libpath + "builds/windows/ftdebug.c";
		src += libpath + "builds/windows/ftsystem.c";
	}
	if (Host.IsMacOS()){
		// Not yet.
	}
	if (Host.IsLinux()){
		src += libpath + "src/base/ftdebug.c";
		src += libpath + "builds/unix/ftsystem.c";
	}
	return src;
}