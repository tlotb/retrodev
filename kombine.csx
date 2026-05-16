/*---------------------------------------------------------------------------------------------------------

	Retro Dev

	Main build script

	(c) TLOTB 2026

---------------------------------------------------------------------------------------------------------*/
#load "bld/ext/clang.csx"
#load "bld/ext/clang.doc.csx"
#load "bld/ext/git.csx"
#load "bld/ext/github.csx"
#load "bld/csx/build.flags.csx"

#r "bld/bin/mkb.dll"
using Kltv.Kombine.Api;
using Kltv.Kombine.Types;
using static Kltv.Kombine.Api.Statics;
using static Kltv.Kombine.Api.Tool;


// Initialize the build flags
BuildFlags.Init();


// Show the help
//
//---------------------------------------------------
int help(string[] args){
	Msg.Print("");
	Msg.Print("RetroDev");
	Msg.Print("  build - Build RetroDev");
	Msg.Print("      parameters: ");
	Msg.Print("                  ----How to build----");
	Msg.Print("                  verbose (show the clang invocation)");
	Msg.Print("                  debug(default) or release");
	Msg.Print("                  production (remove debug information / developer settings)");
	Msg.Print("                  ----What to build----");
	Msg.Print("                  deps (build also the dependencies)");
	Msg.Print("");
	Msg.Print("  dependencies - Manage the dependencies used by the application.");
	Msg.Print("      parameters:");
	Msg.Print("                  update (update the dependencies)");
	Msg.Print("                  install (install the dependencies)");
	Msg.Print("                  clean (clean the dependencies)");
	Msg.Print("");
	Msg.Print("  format - Apply clang-format to all source files");
	Msg.Print("");
	Msg.Print("  clean - Clean the output artifacts");
	Msg.Print("      parameters:");
	Msg.Print("                  debug(default) or release");
	Msg.Print("                  deps (clean also the dependencies artifacts)");
	Msg.Print("                     application is always cleaned");
	Msg.Print("");
	Msg.Print("  publish - Assemble the release package and optionally publish it to GitHub.");
	Msg.Print("      Requires a prior release build (out/pkg/version.txt must exist).");
	Msg.Print("      Always packages the release build output (build mode is forced to release).");
	Msg.Print("      Stages the release binary, sdk/ and examples/ into a zip named");
	Msg.Print("      retrodev-<version>.zip in out/pkg/.");
	Msg.Print("      parameters:");
	Msg.Print("                  upload  also create the GitHub release, upload the zip,");
	Msg.Print("                          and stamp doc/changelog.md with the new version tag.");
	Msg.Print("                          Requires the tlotb_token environment variable to be set.");
	Msg.Print("                          Without this parameter only the zip is produced.");
	Msg.Print("      Release notes are read from doc/changelog.md (entries above the first version tag).");
	Msg.Print("");
	Msg.Print("  help - This help");
	return 0;
}


// Build RetroDev Action
//
//---------------------------------------------------
int build(string[] args) {
	Msg.Print("Building RetroDev...");
	BuildFlags.Print();
	// Define Clang Options for all the subprojects
	//
	//---------------------------------------------------
	Clang clang = new Clang();
	// Place to store the compile commands database
	clang.OpenCompileCommands("out/tmp/compile_commands.json");
	// Clang Compiler Flags
	// Construct the compiler flags to use as default for building.
	// If you want to change the flags, go to this function.
	buildCompilerFlags(clang);
	// Fetch or construct the output paths for the artifacts
	// If you want to change the output, go to this function.
	buildOutputPaths();
	// And set the options as default
	clang.Options.SetAsDefault();
	// Build or register dependencies
	//---------------------------------------------------
	if (BuildFlags.Flags.Deps) {
		Kombine("ext/lib.freetype.csx", "build", args);
		Kombine("ext/lib.sdl.csx", "build",args);
		Kombine("ext/lib.sdl.image.csx", "build",args);
		Kombine("ext/lib.imgui.csx", "build",args);
		Kombine("ext/lib.glaze.csx", "build", args);
		Kombine("ext/lib.ascript.csx", "build", args);
		Kombine("ext/lib.rasm.csx", "build", args);
		Kombine("ext/lib.ctre.csx", "build", args);
	} else {
		Kombine("ext/lib.freetype.csx", "register", args);
		Kombine("ext/lib.sdl.csx", "register",args);
		Kombine("ext/lib.sdl.image.csx", "register",args);
		Kombine("ext/lib.imgui.csx", "register",args);
		Kombine("ext/lib.glaze.csx", "register", args);
		Kombine("ext/lib.ascript.csx", "register", args);
		Kombine("ext/lib.rasm.csx", "register", args);
		Kombine("ext/lib.ctre.csx", "register", args);
	}
	// For release builds, stamp the version header with a real build number
	// and restore it afterwards regardless of success or failure.
	string versionHeader = RealPath("src/lib/system/version.h");
	string versionBackup = RealPath("src/lib/system/version.h.bak");
	if (BuildFlags.Flags.BuildMode == "release") {
		stampVersionHeader(versionHeader, versionBackup);
		if (Kombine("src/retro.dev.lib.csx", "build", args,false) != 0) {
			restoreVersionHeader(versionHeader, versionBackup);
			return -1;
		}
		if (Kombine("src/retro.dev.gui.csx", "build", args,false) != 0) {
			restoreVersionHeader(versionHeader, versionBackup);
			return -1;
		}
		if (Kombine("src/retro.dev.raster.csx", "build", args,false) != 0) {
			restoreVersionHeader(versionHeader, versionBackup);
			return -1;
		}
		restoreVersionHeader(versionHeader, versionBackup);

	} else {
		Kombine("src/retro.dev.lib.csx", "build", args);
		Kombine("src/retro.dev.gui.csx", "build", args);
		Kombine("src/retro.dev.raster.csx", "build", args);
	}
	// Copy the sdk folder from the solution root to the binary output folder
	// so the tool can find it when launched from the output directory during development.
	KValue OutputBin = KValue.Import("OutputBin");
	string sdkSource = RealPath("sdk/");
	string sdkDest = RealPath(OutputBin + "retro.dev.gui/sdk/");
	Msg.Print("Syncing sdk folder to output...");
	Folders.Copy(sdkSource, sdkDest,
		Folders.CopyOptions.IncludeSubFolders |
		Folders.CopyOptions.OnlyModifiedFiles |
		Folders.CopyOptions.DeleteMissingFiles);
	return 0;
}

//
// Dependencies management action
//
//---------------------------------------------------
int dependencies(string[] args) {
	buildOutputPaths();
	// Build the subprojects
	Msg.Print("Dependencies for RetroDev...");
	// Dependencies for each dependency library
	//---------------------------------------------------
	Kombine("ext/lib.freetype.csx", "dependencies", args);
	Kombine("ext/lib.sdl.csx", "dependencies",args);
	Kombine("ext/lib.sdl.image.csx", "dependencies",args);
	Kombine("ext/lib.imgui.csx", "dependencies",args);
	Kombine("ext/lib.glaze.csx", "dependencies", args);
	Kombine("ext/lib.ascript.csx", "dependencies", args);
	Kombine("ext/lib.rasm.csx", "dependencies", args);
	Kombine("ext/lib.ctre.csx", "dependencies", args);
	Kombine("src/retro.dev.lib.csx", "dependencies", args);
	Kombine("src/retro.dev.gui.csx", "dependencies", args);
	Kombine("src/retro.dev.raster.csx", "dependencies", args);
	return 0;
}

//
// Clean artifacts action
//
//---------------------------------------------------
int clean(string[] args){
	Msg.Print("Clean build artifacts RetroDev...");
	Msg.BeginIndent();
	// Paths are required to be set before cleaning since we will clean
	// the requested configuration.
	buildOutputPaths();
	//---------------------------------------------------
	if (BuildFlags.Flags.Deps) {
		Msg.Print("Cleaning dependencies for RetroDev...");
		Kombine("ext/lib.freetype.csx", "clean", args);
		Kombine("ext/lib.sdl.csx", "clean", args);
		Kombine("ext/lib.sdl.image.csx", "clean", args);
		Kombine("ext/lib.imgui.csx", "clean", args);
		Kombine("ext/lib.glaze.csx", "clean", args);
		Kombine("ext/lib.ascript.csx", "clean", args);
		Kombine("ext/lib.rasm.csx", "clean", args);
		Kombine("ext/lib.ctre.csx", "clean", args);
	}
	Msg.Print("Cleaning application...");
	Kombine("src/retro.dev.lib.csx", "clean", args);
	Kombine("src/retro.dev.gui.csx", "clean", args);
	Kombine("src/retro.dev.raster.csx", "clean", args);
	Msg.EndIndent();
	return 0;
}

//
// Format source files action
//
//---------------------------------------------------
int format(string[] args) {
	Msg.Print("Formatting RetroDev sources...");
	Msg.BeginIndent();
	KList src = new KList();
	src += Glob("src/**/*.h");
	src += Glob("src/**/*.cpp");
	src += Glob("src/**/*.c");
	src += Glob("ext/imgui.ext/**/*.h");
	src += Glob("ext/imgui.ext/**/*.cpp");
	src += Glob("ext/rasm.ext/**/*.h");
	src += Glob("ext/rasm.ext/**/*.cpp");
	src += Glob("ext/rasm.ext/**/*.c");
	Clang.Format(src, "-i");
	Msg.EndIndent();
	return 0;
}

//
// Stamp the version header with a generated build number for release builds.
// Backs up the original tokenized header first.
// Writes out/pkg/version.txt with the full version string.
//
//------------------------------------------------------------------------------------------
void stampVersionHeader(string headerPath, string backupPath) {
	// Back up the tokenized header
	System.IO.File.Copy(headerPath, backupPath, true);
	// Generate the build number and compose the full version string
	string buildNumber = GenVersionBuildNumber();
	string src = System.IO.File.ReadAllText(headerPath);
	string stamped = src.Replace("@BUILD@", buildNumber);
	System.IO.File.WriteAllText(headerPath, stamped);
	// Write out/pkg/version.txt for downstream packaging tools
	string major = "";
	string minor = "";
	foreach (string line in src.Split('\n')) {
		if (line.Contains("k_versionMajor")) {
			int eq = line.IndexOf('='); int semi = eq >= 0 ? line.IndexOf(';', eq) : -1;
			if (eq >= 0 && semi > eq) major = line.Substring(eq + 1, semi - eq - 1).Trim();
		}
		if (line.Contains("k_versionMinor")) {
			int eq = line.IndexOf('='); int semi = eq >= 0 ? line.IndexOf(';', eq) : -1;
			if (eq >= 0 && semi > eq) minor = line.Substring(eq + 1, semi - eq - 1).Trim();
		}
	}
	string versionString = major + "." + minor + "." + buildNumber;
	Folders.Create(RealPath("out/pkg/"));
	System.IO.File.WriteAllText(RealPath("out/pkg/version.txt"), versionString);
	Msg.Print("Version stamped: " + versionString);
}
//
// Restore the tokenized version header from its backup and remove the backup.
//
//------------------------------------------------------------------------------------------
void restoreVersionHeader(string headerPath, string backupPath) {
	// Retry up to 10 times with a 1-second delay between attempts
	// (the file may be briefly locked by the compiler or linker).
	bool restored = false;
	for (int attempt = 1; attempt <= 10; attempt++) {
		try {
			System.IO.File.Copy(backupPath, headerPath, true);
			restored = true;
			break;
		} catch (System.Exception) {}
		Msg.Print($"restoreVersionHeader: attempt {attempt}/10 failed, retrying in 1s...");
		System.Threading.Thread.Sleep(10000);
	}
	if (!restored) {
		Msg.PrintError("restoreVersionHeader: version.h could not be restored from backup after 10 attempts. Manual restore required: copy version.h.bak to version.h");
	} else {
		System.IO.File.Delete(backupPath);
		Msg.Print("Version header restored.");
	}
}
//
// Build the output paths to be used by the subprojects artifacts
// The output paths are exported to be used by the subprojects
//
//------------------------------------------------------------------------------------------
void buildOutputPaths() {
	// The opuput paths for the artifacts defined here
	KValue OutputBin = RealPath("out/bin/");
	KValue OutputLib = RealPath("out/lib/");
	KValue OutputTmp = RealPath("out/tmp/");
	// Add the current building target system into the output paths
	OutputBin += Host.GetOSKind();
	OutputLib += Host.GetOSKind();
	OutputTmp += Host.GetOSKind();
	// Add the current building configuration into the output paths
	if (BuildFlags.Flags.BuildMode == "release") {
		OutputBin += "/release/";
		OutputLib += "/release/";
		OutputTmp += "/release/";
	}
	else if (BuildFlags.Flags.BuildMode == "debug") {
		OutputBin += "/debug/";
		OutputLib += "/debug/";
		OutputTmp += "/debug/";
	} else{
		Msg.PrintAndAbort("Invalid Build Configuration: " + BuildFlags.Flags.BuildMode);
	}
	// Export the newly defined ones for the subprojects
	OutputBin.Export("OutputBin");
	OutputLib.Export("OutputLib");
	OutputTmp.Export("OutputTmp");
}

//
// Construct the compiler flags to use as default for building.
// If you want to change the flags affecting all the subprojects this is the place
//
//------------------------------------------------------------------------------------------
void buildCompilerFlags(Clang clang) {
	// Set Verbose by command line
	if (BuildFlags.Flags.Verbose) {
		clang.Options.Verbose = true;
	}
	// Language version & disable built in char8_t for utf8 (so u8 casts to const char* like C++11)
	clang.Options.SwitchesCC = new KList() {
		"-std=c17" ,
		"-fno-char8_t"};
	clang.Options.SwitchesCXX = new KList() {
		"-std=c++20",
		"-fno-char8_t",
		"-pedantic",
		"-Wall",
		"-Wextra",
		"-Wno-unused",
		"-Wno-c++26-extensions",
		"-fno-exceptions"};
	if (Host.IsWindows()){
		clang.Options.Defines = new KList() {
			"WINDOWS",
			"_WIN64",
			"_WIN32",
			"_CRT_SECURE_NO_WARNINGS" };
		if (BuildFlags.Flags.BuildMode == "release") {
			clang.Options.SwitchesCC +=  "-O2";
			clang.Options.SwitchesCXX += "-O2";
			clang.Options.SwitchesCC += "-fms-runtime-lib=static";
			clang.Options.SwitchesCXX += "-fms-runtime-lib=static";
			clang.Options.Defines += "NDEBUG";
			clang.Options.Defines += "RELEASE";
		} else if (BuildFlags.Flags.BuildMode == "debug") {
			KList options = new KList() {
				"-g",
				"-glldb",
				"-gfull",
				"-O0" };
			clang.Options.SwitchesCC += options;
			clang.Options.SwitchesCXX += options;
			clang.Options.SwitchesCC += "-fms-runtime-lib=static_dbg";
			clang.Options.SwitchesCXX += "-fms-runtime-lib=static_dbg";
			clang.Options.Defines += "DEBUG";
			clang.Options.Defines += "NRELEASE";
			clang.Options.SwitchesLD = new KList() {
				"-g",
				"-glldb",
				"-gfull" };
		} else {
			Msg.PrintAndAbort("Invalid Build Configuration:" + BuildFlags.Flags.BuildMode);
		}
	}
	if (Host.IsLinux()){
		clang.Options.Defines = new KList() {
			"LINUX"
		};
		if (BuildFlags.Flags.BuildMode == "release") {
			clang.Options.SwitchesCC += "-O2";
			clang.Options.SwitchesCXX += "-O2";
			clang.Options.Defines += "NDEBUG";
			clang.Options.Defines += "RELEASE";
		} else if (BuildFlags.Flags.BuildMode == "debug") {
			KList options = new KList() {
				"-g",
				"-O0"
			};
			clang.Options.SwitchesCC += options;
			clang.Options.SwitchesCXX += options;
			clang.Options.Defines += "DEBUG";
			clang.Options.Defines += "NRELEASE";
			clang.Options.SwitchesLD = new KList() {
				"-g"
			};
		} else {
			Msg.PrintAndAbort("Invalid Build Configuration:" + BuildFlags.Flags.BuildMode);
		}
	}
	if (Host.IsMacOS()){
	}
}

//
// Extract the unreleased section from doc/changelog.md.
// Returns all lines from the top of the file up to (but not including) the first
// line that starts with "## Version". If no version tag exists the entire file is returned.
//
//------------------------------------------------------------------------------------------
string extractReleaseNotes(string changelogPath) {
	if (!System.IO.File.Exists(changelogPath))
		return "";
	System.Text.StringBuilder sb = new System.Text.StringBuilder();
	foreach (string line in System.IO.File.ReadAllLines(changelogPath)) {
		if (line.StartsWith("## Version"))
			break;
		sb.AppendLine(line);
	}
	return sb.ToString().Trim();
}
//
// Prepend a version tag line to doc/changelog.md so that entries added after this
// point will be collected for the next release, and the current entries are archived.
//
//------------------------------------------------------------------------------------------
void stampChangelogVersion(string changelogPath, string versionNumber) {
	string existing = System.IO.File.Exists(changelogPath)
		? System.IO.File.ReadAllText(changelogPath)
		: "";
	string stamp = "## Version " + versionNumber + "\n\n";
	System.IO.File.WriteAllText(changelogPath, stamp + existing);
	Msg.Print("Changelog stamped with version " + versionNumber);
}

//
// Assemble the release package into out/pkg and optionally publish it to GitHub.
// Steps:
//   1) Extract unreleased entries from doc/changelog.md (lines before the first version tag).
//   2) Copy the release binary output + sdk/ + examples/ into a staging folder.
//   3) Zip the staging folder as retrodev-<version>.zip in out/pkg/.
//   4) If the tlotb_token environment variable is set, create a GitHub release and upload the zip.
//   5) Stamp doc/changelog.md with the new version tag so future entries start fresh.
//
int publish(string[] args) {
	//
	// Determine whether to upload to GitHub and stamp the changelog
	//
	bool doUpload = false;
	foreach (string arg in args)
		if (arg == "upload") { doUpload = true; break; }
	//
	// Read the version number written by stampVersionHeader during the release build
	//
	string pkgVersionFile = RealPath("out/pkg/version.txt");
	if (!System.IO.File.Exists(pkgVersionFile)) {
		Msg.PrintAndAbort("out/pkg/version.txt not found. Run a release build first.");
	}
	string versionNumber = System.IO.File.ReadAllText(pkgVersionFile).Trim();
	if (versionNumber == "") {
		Msg.PrintAndAbort("Version number is empty in out/pkg/version.txt.");
	}
	Msg.Print("Publishing RetroDev version " + versionNumber);
	//
	// publish always packages the release build output — force the mode regardless
	// of what flags were passed on the command line
	//
	BuildFlags.Flags.BuildMode = "release";
	//
	// Extract unreleased changelog entries (everything before the first version tag)
	//
	string changelogPath = RealPath("doc/changelog.md");
	string releaseNotes = extractReleaseNotes(changelogPath);
	if (releaseNotes == "") {
		Msg.Print("No release notes found in doc/changelog.md — release will have an empty body.");
	} else {
		Msg.Print("Release notes extracted from doc/changelog.md.");
	}
	//
	// Resolve paths
	//
	buildOutputPaths();
	KValue OutputBin = KValue.Import("OutputBin");
	string binDir    = RealPath(OutputBin + "retro.dev.gui/");
	string sdkDir    = RealPath("sdk/");
	string examplesDir = RealPath("examples/");
	string stagingDir  = RealPath("out/pkg/staging/");
	string zipName     = "retrodev-" + versionNumber + ".zip";
	string zipPath     = RealPath("out/pkg/" + zipName);
	//
	// Build the staging folder: binary output + sdk + examples
	//
	Msg.Print("Assembling package in staging folder...");
	Folders.Delete(stagingDir);
	Folders.Create(stagingDir);
	Folders.Copy(binDir, stagingDir,
		Folders.CopyOptions.IncludeSubFolders |
		Folders.CopyOptions.OnlyModifiedFiles);
	//
	// Strip debug and local-settings files that must not ship in the release package
	//
	foreach (string pdb in System.IO.Directory.GetFiles(stagingDir, "*.pdb", System.IO.SearchOption.AllDirectories))
		System.IO.File.Delete(pdb);
	foreach (string ini in System.IO.Directory.GetFiles(stagingDir, "*.ini", System.IO.SearchOption.AllDirectories))
		System.IO.File.Delete(ini);
	//
	// Copy sdk/ only if it is not already present in the binary output (the build action
	// syncs it there, but we include the canonical root copy for the package)
	//
	string stagingSdk = stagingDir + "sdk/";
	if (!System.IO.Directory.Exists(stagingSdk)) {
		Folders.Copy(sdkDir, stagingSdk,
			Folders.CopyOptions.IncludeSubFolders |
			Folders.CopyOptions.OnlyModifiedFiles);
	}
	//
	// Always include examples/ from the project root
	//
	Folders.Copy(examplesDir, stagingDir + "examples/",
		Folders.CopyOptions.IncludeSubFolders |
		Folders.CopyOptions.OnlyModifiedFiles);
	//
	// Compress the staging folder into the final zip
	//
	Msg.Print("Creating " + zipName + "...");
	Compress.Zip.CompressFolder(stagingDir, zipPath, true, false);
	Folders.Delete(stagingDir);
	Msg.Print("Package ready: out/pkg/" + zipName);
	if (!doUpload) {
		Msg.Print("Upload skipped — run 'mkb publish upload' to create the GitHub release.");
		return 0;
	}
	//
	// Publish to GitHub — requires the tlotb_token environment variable
	//
	KValue token = KValue.Import("tlotb_token", "");
	if (token == "") {
		Msg.PrintAndAbort("tlotb_token not set — cannot upload. Package is at out/pkg/" + zipName);
	}
	Msg.Print("tlotb_token found — creating GitHub release...");
	Github github = new Github();
	github.Repository = "retrodev";
	github.Owner = "tlotb";
	github.Token = token.ToString();
	//
	// Create the release (draft=false, no release notes — added manually on GitHub)
	//
	string releaseTag = "Release-" + versionNumber;
	string releaseId = github.CreateRelease(releaseTag, "Release " + versionNumber, releaseNotes, false);
	if (releaseId == "") {
		Msg.PrintAndAbort("Failed to create the GitHub release.");
	}
	//
	// Upload the single zip asset
	//
	string[] assets = new string[] { zipPath };
	if (github.UploadAssets(releaseId, assets) == false) {
		Msg.PrintAndAbort("Failed to upload assets to the GitHub release.");
	}
	if (github.PublishRelease(releaseId) == false) {
		Msg.PrintAndAbort("Failed to publish the GitHub release.");
	}
	Msg.Print("GitHub release published: " + releaseTag);
	//
	// Write bld/version.json with the real version and URLs so the update checker
	// can find the latest release without @BUILD@ tokens.
	//
	string versionJsonPath = RealPath("bld/version.json");
	string releaseNotesUrl = "https://github.com/tlotb/retrodev/releases/tag/" + releaseTag;
	string downloadUrl     = "https://github.com/tlotb/retrodev/releases/download/" + releaseTag + "/" + zipName;
	string versionJson =
		"{\n" +
		"    \"latestVersion\": \"" + versionNumber + "\",\n" +
		"    \"releaseNotesUrl\": \"" + releaseNotesUrl + "\",\n" +
		"    \"downloadUrl\": \"" + downloadUrl + "\"\n" +
		"}\n";
	System.IO.File.WriteAllText(versionJsonPath, versionJson);
	Msg.Print("bld/version.json updated to version " + versionNumber);
	//
	// Stamp the changelog so future entries are collected for the next release
	//
	stampChangelogVersion(changelogPath, versionNumber);
	return 0;
}