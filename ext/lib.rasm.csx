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
KValue libfriendlyname = "Rasm (v3.0.9)";
// Path to store the lib sources
KValue libpath = "rasm/";
// Branch to use for the lib sources
KValue libbranch = "v3.0.9";
// Name of the library for the output
KValue libname = "rasm";
// Repository to use for the lib sources
KValue librepo = "https://github.com/EdouardBERGE/rasm.git";

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
	// Check if the sources folder is present
	if (!Folders.Exists(libpath)){
		Msg.PrintAndAbort($"{libfriendlyname} sources not found. Please run 'mkb dependencies install' to get the sources.");
	}
	// Output paths
	KValue OutputBin = KValue.Import("OutputBin");
	KValue OutputLib = KValue.Import("OutputLib");
	KValue OutputTmp = KValue.Import("OutputTmp");
	// Compilation flags
	KList Flags = new KList {
		"--target=x86_64-pc-windows-msvc",
		"-fms-extensions",
		"-fms-compatibility",
		"-fms-compatibility-version=19",
		"-include intrin.h",
		"-Wno-deprecated-declarations",
		"-Wno-pointer-sign",
		"-Wno-constant-conversion",
		"-Wno-sizeof-pointer-memaccess",
		"-Wno-dangling-else"
	};
	
	// The list of defines to use
	KList Defines = new KList { 
		"INTEGRATED_ASSEMBLY", 
		"NOAPULTRA=1",
		"DOS_WIN=1"
	};
	
	// Include directories
	KList Includes = new KList();
	// We will wrap rasm inside our own .c file to tweak it.
	Includes += libpath;
	Includes += "rasm.ext/";
	/*
	// Not needed on Windows but on other platforms (leave here for reference)
	Includes += libpath+"z80-master/";
	Includes += libpath+"salvador/src/";
	Includes += libpath+"apultra-master/src/";
	Includes += libpath+"lzsa-master/src/";
	Includes += libpath+"lzsa-master/src/libdivsufsort/include/";
	Includes += libpath;
	*/
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
	Msg.Print("  incpath: " + RealPath(libpath));
	Share.Register(libname,"incpath",RealPath(libpath));
	//
	// We publish also the rasm.ext folder to be included
	//
	Msg.Print("  extpath: " + RealPath("rasm.ext/"));
	Share.Register(libname, "extpath", RealPath("rasm.ext/"));
	Msg.PrintTask("Registered static library: " + libname);
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
	/*
	// Not needed on Windows builds. Leave here for reference
	// z80
	src += libpath+"z80-master/z80.c";
	// salvador
	src += libpath+"salvador/src/matchfinder.c";
	src += libpath+"salvador/src/expand.c";
	src += libpath+"salvador/src/shrink.c";
	// apultra
	src += libpath+"apultra-master/src/expand.c";
	src += libpath+"apultra-master/src/matchfinder.c";
	src += libpath+"apultra-master/src/shrink.c";
	// lzsa
	src += libpath+"lzsa-master/src/dictionary.c";
	src += libpath+"lzsa-master/src/expand_block_v1.c";
	src += libpath+"lzsa-master/src/expand_block_v2.c";
	src += libpath+"lzsa-master/src/expand_context.c";
	src += libpath+"lzsa-master/src/expand_inmem.c";
	src += libpath+"lzsa-master/src/frame.c";
	src += libpath+"lzsa-master/src/matchfinder.c";
	src += libpath+"lzsa-master/src/shrink_block_v1.c";
	src += libpath+"lzsa-master/src/shrink_block_v2.c";
	src += libpath+"lzsa-master/src/shrink_context.c";
	src += libpath+"lzsa-master/src/shrink_inmem.c";
	// lzsa — libdivsufsort
	src += libpath+"lzsa-master/src/libdivsufsort/lib/divsufsort.c";
	src += libpath+"lzsa-master/src/libdivsufsort/lib/divsufsort_utils.c";
	src += libpath+"lzsa-master/src/libdivsufsort/lib/sssort.c";
	src += libpath+"lzsa-master/src/libdivsufsort/lib/trsort.c";
	*/
	// rasm
	//src += libpath+"rasm.c";
	src += "rasm.ext/rasm.api.c";
	if (Host.IsWindows()){
	}
	if (Host.IsMacOS()){
	}
	if (Host.IsLinux()){
	}
	return src;
}
