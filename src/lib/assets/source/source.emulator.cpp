// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Lib
//
// Source asset -- emulator launch configuration.
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#include "source.emulator.h"
#include "source.emulator.native.h"
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_iostream.h>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdio>

namespace RetrodevLib {

	namespace EmulatorImpl {

		//
		// Emit a diagnostic message on LogChannel::Build
		//
		static void Emit(LogLevel level, const char* msg) {
			if (level == LogLevel::Error)
				Log::Error(LogChannel::Build, "%s", msg);
			else if (level == LogLevel::Warning)
				Log::Warning(LogChannel::Build, "%s", msg);
			else
				Log::Info(LogChannel::Build, "%s", msg);
		}
		//
		// Append a space-separated token to an args string
		//
		static void Append(std::string& args, const char* token) {
			if (!args.empty())
				args += ' ';
			args += token;
		}
		//
		// Append a string token, quoting it if it contains spaces
		//
		static void AppendQuoted(std::string& args, const std::string& token) {
			if (token.empty())
				return;
			bool needQuotes = (token.find(' ') != std::string::npos);
			if (!args.empty())
				args += ' ';
			if (needQuotes)
				args += '"';
			args += token;
			if (needQuotes)
				args += '"';
		}
		//
		// Resolve a file path to absolute using the project folder as base.
		// If the path is already absolute it is returned unchanged.
		// An empty input path is returned as-is.
		// All directory separators are normalized to the platform preferred form.
		//
		static std::string AbsPath(const std::string& path, const std::string& projectFolder) {
			if (path.empty())
				return path;
			std::filesystem::path p(path);
			if (p.is_absolute())
				return p.make_preferred().string();
			return (std::filesystem::path(projectFolder) / p).make_preferred().string();
		}
		//
		// Tracking record for a running emulator process -- kept alive so we can
		// drain stdout/stderr each frame and detect exit.
		// proc is non-null for SDL-managed processes (WinAPE, ACE-DL).
		// nativeHandle is non-null for processes launched via SourceEmulatorNative (RVM).
		// Exactly one of the two is non-null for any given LiveProcess entry.
		//
		struct LiveProcess {
			SDL_Process* proc;
			void* nativeHandle;
			std::string lineBuf;
		};
		static std::vector<LiveProcess> s_liveProcesses;
		static bool SpawnNative(const std::string& exePath, const std::string& rawCmdLine, const std::string& workDir);
		//
		// Spawn a process with the given executable, argument string and working directory.
		// exePath is always argv[0]; args is a pre-built shell-style argument string that
		// is split into tokens here.  workDir may be empty (current directory is inherited).
		// Returns true if SDL_CreateProcessWithProperties succeeded; the process is added
		// to s_liveProcesses so Poll() can detect when it exits.
		//
		static bool Spawn(const std::string& exePath, const std::string& args, const std::string& workDir, const std::string& rawCmdLine = "") {
		#if !defined(_WIN32)
			std::string absWorkDir = workDir.empty() ? std::filesystem::path(exePath).parent_path().string() : workDir;
			std::string nativeCmdLine;
			if (!rawCmdLine.empty()) {
				nativeCmdLine = rawCmdLine;
			} else {
				nativeCmdLine = "\"" + exePath + "\"";
				if (!args.empty()) {
					nativeCmdLine += " ";
					nativeCmdLine += args;
				}
			}
			return SpawnNative(exePath, nativeCmdLine, absWorkDir);
		#else
			//
			// Split args into tokens respecting double-quoted spans.
			// Each token becomes one element of the argv array passed to SDL.
			//
			std::vector<std::string> tokens;
			tokens.push_back(exePath);
			size_t i = 0;
			while (i < args.size()) {
				//
				// Skip whitespace between tokens
				//
				while (i < args.size() && args[i] == ' ')
					i++;
				if (i >= args.size())
					break;
				std::string tok;
				if (args[i] == '"') {
					//
					// Quoted token: collect until closing quote
					//
					i++;
					while (i < args.size() && args[i] != '"')
						tok += args[i++];
					if (i < args.size())
						i++;
				} else {
					//
					// Unquoted token: collect until space
					//
					while (i < args.size() && args[i] != ' ')
						tok += args[i++];
				}
				if (!tok.empty())
					tokens.push_back(tok);
			}
			//
			// Build a null-terminated array of C-string pointers for SDL
			//
			std::vector<const char*> argv;
			argv.reserve(tokens.size() + 1);
			for (const auto& t : tokens)
				argv.push_back(t.c_str());
			argv.push_back(nullptr);
			//
			// Set up properties for SDL_CreateProcessWithProperties:
			//
			//   ARGS    -- null-terminated argv array; argv[0] is the executable path,
			//             followed by the pre-split argument tokens.
			//
			//   WORKING_DIRECTORY -- always set to a non-empty path so the emulator
			//             finds its own data files regardless of our CWD.  When the
			//             caller does not supply an explicit workDir (WinAPE, RVM) we
			//             default to the directory that contains the executable itself.
			//
			//   STDIN   -- SDL_PROCESS_STDIO_NULL: the emulator does not read from
			//             stdin so we close that end of the pipe immediately.
			//
			//   STDOUT  -- SDL_PROCESS_STDIO_APP: SDL creates a pipe and exposes it
			//             via SDL_GetProcessOutput().  Poll() drains this pipe each
			//             frame to forward emulator output to the app log.
			//
			//   STDERR_TO_STDOUT -- merge stderr into the same pipe as stdout so a
			//             single SDL_ReadIO loop captures both streams.
			//
			// Note: SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN is intentionally NOT
			//       set.  Background mode disables all I/O and makes the exit code
			//       always 0, which would prevent both output capture and exit
			//       detection.
			//
			SDL_PropertiesID props = SDL_CreateProperties();
			SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(reinterpret_cast<const char**>(argv.data())));
			//
			// When a raw command line is provided (RVM), pass it verbatim to CreateProcess
			// via CMDLINE_STRING so SDL does not re-quote embedded quotes in any token.
			// ARGS_POINTER is still required -- SDL uses argv[0] to locate the executable.
			//
			if (!rawCmdLine.empty())
				SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_CMDLINE_STRING, rawCmdLine.c_str());
			std::string absWorkDir = workDir.empty() ? std::filesystem::path(exePath).parent_path().string() : workDir;
			SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, absWorkDir.c_str());
			SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
			SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
			SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
			SDL_Process* proc = SDL_CreateProcessWithProperties(props);
			SDL_DestroyProperties(props);
			if (!proc) {
				char errMsg[512];
				std::snprintf(errMsg, sizeof(errMsg), "[Emulator] Failed to launch: %s: %s", exePath.c_str(), SDL_GetError());
				Emit(LogLevel::Error, errMsg);
				return false;
			}
			char msg[512];
			std::snprintf(msg, sizeof(msg), "[Emulator] Launched: %s %s", exePath.c_str(), args.c_str());
			Emit(LogLevel::Info, msg);
			//
			// Keep the process handle alive so Poll() can drain its output and detect exit
			//
			s_liveProcesses.push_back({proc, nullptr, ""});
			return true;
		#endif
		}
		//
		// Launch a GUI process via SourceEmulatorNative with no stdio redirection.
		// Used for RVM which fails when its handles are overridden by SDL's
		// unconditional STARTF_USESTDHANDLES.
		//
		static bool SpawnNative(const std::string& exePath, const std::string& rawCmdLine, const std::string& workDir) {
			void* handle = SourceEmulatorNative::Spawn(exePath, rawCmdLine, workDir);
			if (!handle)
				return false;
			char msg[512];
			std::snprintf(msg, sizeof(msg), "[Emulator] Launched: %s", rawCmdLine.c_str());
			Emit(LogLevel::Info, msg);
			//
			// Keep the handle alive so Poll() can detect when the process exits
			//
			s_liveProcesses.push_back({nullptr, handle, ""});
			return true;
		}
		//
		// WinAPE command line:
		//   WinAPE [mediaFile] [/A[:command]] [/T:autoTypeFile] [/SN:snapshot] [/SYM:symbolFile]
		//
		static bool LaunchWinAPE(const SourceParams::EmulatorParams& ep, const std::string& projectFolder) {
			if (ep.winape.exePath.empty()) {
				Emit(LogLevel::Error, "[Emulator] WinAPE executable path is not set.");
				return false;
			}
			const auto& c = ep.common;
			std::string args;
			//
			// Positional media file -- must come first
			//
			AppendQuoted(args, AbsPath(c.mediaFile, projectFolder));
			//
			// sendCPM takes priority: bare /A causes WinAPE to boot the disc without a program name,
			// then |CPM is not directly passable so we emit /A to auto-start whatever is on disc.
			// command maps to /A:<name> when non-empty.
			//
			if (c.sendCPM) {
				Append(args, "/A");
			} else if (!c.command.empty()) {
				std::string flag = "/A:" + c.command;
				Append(args, flag.c_str());
			} else if (!c.mediaFile.empty()) {
				//
				// Disc present but no explicit program -- auto-start with bare /A
				//
				Append(args, "/A");
			}
			//
			// /SN:snapshot
			//
			if (!c.snapshot.empty()) {
				std::string flag = "/SN:" + AbsPath(c.snapshot, projectFolder);
				Append(args, flag.c_str());
			}
			//
			// /SYM:symbolFile
			//
			if (!c.symbolFile.empty()) {
				std::string flag = "/SYM:" + AbsPath(c.symbolFile, projectFolder);
				Append(args, flag.c_str());
			}
			return Spawn(ep.winape.exePath, args, "");
		}
		//
		// RVM command line:
		//   RetroVirtualMachine -boot=<id> [-insert <file>] [-snapshot <file>] [-command=<text>]
		//
		static bool LaunchRVM(const SourceParams::EmulatorParams& ep, const std::string& projectFolder) {
			if (ep.rvm.exePath.empty()) {
				Emit(LogLevel::Error, "[Emulator] RVM executable path is not set.");
				return false;
			}
			const auto& c = ep.common;
			std::string args;
			//
			// Don't use console colors
			//
			Append(args, "-nocolor");
			//
			// -boot=<machine> is required; default to cpc6128 when unset
			//
			std::string machineId = c.machine.empty() ? "cpc6128" : c.machine;
			Append(args, ("-boot=" + machineId).c_str());
			//
			// -insert <mediaFile> -- always quoted so CreateProcessW cmdline parsing is unambiguous
			//
			if (!c.mediaFile.empty()) {
				Append(args, "-insert");
				std::string absMedia = AbsPath(c.mediaFile, projectFolder);
				if (!args.empty())
					args += ' ';
				args += '"';
				args += absMedia;
				args += '"';
			}
			//
			// -snapshot <file> -- always quoted
			//
			if (!c.snapshot.empty()) {
				Append(args, "-snapshot");
				std::string absSnap = AbsPath(c.snapshot, projectFolder);
				if (!args.empty())
					args += ' ';
				args += '"';
				args += absSnap;
				args += '"';
			}
			//
			// sendCPM: inject |CPM\n to boot CP/M.
			// command: format is -command=run"<name>"\n (quotes around filename, no space after run).
			//
			if (c.sendCPM) {
				Append(args, "-command=|CPM\\n");
			} else if (!c.command.empty()) {
				std::string flag = "-command=run\"" + c.command + "\"\\n";
				Append(args, flag.c_str());
			}
			//
			// Launch via SourceEmulatorNative so RVM receives no redirected handles.
			// SDL unconditionally sets STARTF_USESTDHANDLES which breaks RVM's startup.
			//
			std::string rawCmdLine = "\"" + ep.rvm.exePath + "\" " + args;
			return SpawnNative(ep.rvm.exePath, rawCmdLine, "");
		}
		//
		// ACE-DL command line:
		//   AceDL [mediaFile] [symbolFile] [-autoRunFile <f>] [-autoRunCPM] [-crtc n] [-ram n]
		//         [-fuk|-ffr|-fsp|-fdk] [-speed n|MAX] [-alone] [-skipConfigFile]
		// CWD must be set to the directory containing AceDL.exe.
		//
		static bool LaunchACEDL(const SourceParams::EmulatorParams& ep, const std::string& projectFolder) {
			if (ep.acedl.exePath.empty()) {
				Emit(LogLevel::Error, "[Emulator] ACE-DL executable path is not set.");
				return false;
			}
			const auto& c = ep.common;
			std::string args;
			//
			// Positional media file (disc/tape/snapshot)
			//
			AppendQuoted(args, AbsPath(c.mediaFile, projectFolder));
			//
			// Positional symbol file
			//
			AppendQuoted(args, AbsPath(c.symbolFile, projectFolder));
			//
			// sendCPM takes priority over command
			//
			if (c.sendCPM) {
				Append(args, "-autoRunCPM");
			} else if (!c.command.empty()) {
				Append(args, "-autoRunFile");
				AppendQuoted(args, c.command);
			}
			//
			// -crtc n  (omitted when crtc == -1)
			//
			if (ep.acedl.crtc >= 0) {
				char buf[32];
				std::snprintf(buf, sizeof(buf), "-crtc %d", ep.acedl.crtc);
				Append(args, buf);
			}
			//
			// -ram n  (omitted when ram == 0)
			//
			if (ep.acedl.ram > 0) {
				char buf[32];
				std::snprintf(buf, sizeof(buf), "-ram %d", ep.acedl.ram);
				Append(args, buf);
			}
			//
			// Firmware locale: -fuk / -ffr / -fsp / -fdk
			//
			if (!ep.acedl.firmware.empty()) {
				std::string flag = "-f" + ep.acedl.firmware;
				Append(args, flag.c_str());
			}
			//
			// -speed n or -speed MAX  (omitted when speed == 0)
			// speed == -1 means MAX
			//
			if (ep.acedl.speed != 0) {
				if (ep.acedl.speed == -1)
					Append(args, "-speed MAX");
				else {
					char buf[32];
					std::snprintf(buf, sizeof(buf), "-speed %d", ep.acedl.speed);
					Append(args, buf);
				}
			}
			//
			// -alone
			//
			if (ep.acedl.alone)
				Append(args, "-alone");
			//
			// -skipConfigFile
			//
			if (ep.acedl.skipConfigFile)
				Append(args, "-skipConfigFile");
			//
			// CWD must be the directory containing AceDL.exe
			//
			std::string workDir = std::filesystem::path(ep.acedl.exePath).parent_path().string();
			return Spawn(ep.acedl.exePath, args, workDir);
		}

	}

	//
	// Public entry point: dispatch to the correct emulator back-end
	//
	bool SourceEmulator::Launch(const SourceParams* params) {
		if (!EmulatorImpl::s_liveProcesses.empty()) {
			EmulatorImpl::Emit(LogLevel::Warning, "[Emulator] An emulator instance is already running.");
			return false;
		}
		if (params == nullptr) {
			EmulatorImpl::Emit(LogLevel::Error, "[Emulator] No build parameters provided.");
			return false;
		}
		const SourceParams::EmulatorParams& ep = params->emulatorParams;
		if (ep.emulator.empty())
			return true;
		std::string projectFolder = Project::GetProjectFolder();
		if (ep.emulator == "WinAPE")
			return EmulatorImpl::LaunchWinAPE(ep, projectFolder);
		if (ep.emulator == "RVM")
			return EmulatorImpl::LaunchRVM(ep, projectFolder);
		if (ep.emulator == "ACE-DL")
			return EmulatorImpl::LaunchACEDL(ep, projectFolder);
		char msg[128];
		std::snprintf(msg, sizeof(msg), "[Emulator] Unknown emulator: '%s'", ep.emulator.c_str());
		EmulatorImpl::Emit(LogLevel::Error, msg);
		return false;
	}

	//
	// Per-frame poll: drain stdout from all running emulator processes,
	// emit complete lines on LogChannel::Build, and destroy processes that have exited.
	//
	void SourceEmulator::Poll() {
		char buf[1024];
		for (auto it = EmulatorImpl::s_liveProcesses.begin(); it != EmulatorImpl::s_liveProcesses.end();) {
			//
			// Native-tracked process (RVM): no stdout pipe, poll for exit via SourceEmulatorNative
			//
			if (it->nativeHandle != nullptr) {
				int exitcode = 0;
				if (SourceEmulatorNative::Poll(it->nativeHandle, exitcode)) {
					char msg[64];
					std::snprintf(msg, sizeof(msg), "[Emulator] Exited with code %d", exitcode);
					EmulatorImpl::Emit(exitcode == 0 ? LogLevel::Info : LogLevel::Warning, msg);
					SourceEmulatorNative::Destroy(it->nativeHandle);
					it = EmulatorImpl::s_liveProcesses.erase(it);
				} else {
					++it;
				}
				continue;
			}
			//
			// SDL-managed process (WinAPE, ACE-DL): drain stdout/stderr pipe each frame
			//
			SDL_IOStream* out = SDL_GetProcessOutput(it->proc);
			if (out) {
				size_t n;
				while ((n = SDL_ReadIO(out, buf, sizeof(buf) - 1)) > 0) {
					buf[n] = '\0';
					for (size_t k = 0; k < n; k++) {
						if (buf[k] == '\n') {
							if (!it->lineBuf.empty())
								EmulatorImpl::Emit(LogLevel::Info, it->lineBuf.c_str());
							it->lineBuf.clear();
						} else if (buf[k] != '\r') {
							it->lineBuf += buf[k];
						}
					}
				}
			}
			//
			// Non-blocking exit check -- flush pending line, log exit code, release handle
			//
			int exitcode = 0;
			if (SDL_WaitProcess(it->proc, false, &exitcode)) {
				if (!it->lineBuf.empty()) {
					EmulatorImpl::Emit(LogLevel::Info, it->lineBuf.c_str());
					it->lineBuf.clear();
				}
				char msg[64];
				std::snprintf(msg, sizeof(msg), "[Emulator] Exited with code %d", exitcode);
				EmulatorImpl::Emit(exitcode == 0 ? LogLevel::Info : LogLevel::Warning, msg);
				SDL_DestroyProcess(it->proc);
				it = EmulatorImpl::s_liveProcesses.erase(it);
			} else {
				++it;
			}
		}
	}

}
