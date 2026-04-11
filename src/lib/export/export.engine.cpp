// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Lib
//
// Export engine -- AngelScript VM setup and script execution.
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#include "export.engine.h"
#include <project/project.h>
#include <scriptstdstring.h>
#include <scriptfile.h>
#include <scriptarray.h>
#include <scriptmath.h>
#include <assets/image/image.h>
#include <assets/image/image.color.h>
#include <convert/convert.palette.h>
#include <log/log.h>
#include <filesystem>
#include <cstdio>

namespace RetrodevLib {
	namespace ExportImpl {

		std::vector<std::string> g_errors;
		bool g_hasError = false;
		ScriptEngine g_engine;
		//
		static std::string MakeRelativeSectionPath(const char* section) {
			if (section == nullptr || section[0] == '\0')
				return "<unknown>";
			std::string projectFolder = RetrodevLib::Project::GetProjectFolder();
			if (projectFolder.empty())
				return section;
			std::error_code ec;
			std::filesystem::path rel = std::filesystem::relative(std::filesystem::path(section), std::filesystem::path(projectFolder), ec);
			if (ec || rel.empty())
				return section;
			//
			// Normalise to forward slashes for consistent display
			//
			std::string result = rel.generic_string();
			return result;
		}
		//
		void ReportError(const std::string& msg) {
			g_errors.push_back(msg);
			g_hasError = true;
			Log::Error(LogChannel::Script, "%s", msg.c_str());
		}
		//
		// Create the parent directory of outputPath (and any missing ancestors).
		// Returns true if the directory already exists or was created successfully.
		// Returns false and reports an error if creation fails.
		//
		bool EnsureOutputDirectory(const std::string& outputPath) {
			std::filesystem::path dir = std::filesystem::path(outputPath).parent_path();
			if (dir.empty())
				return true;
			std::error_code ec;
			std::filesystem::create_directories(dir, ec);
			if (ec) {
				ReportError("[Script] Failed to create output directory '" + dir.string() + "': " + ec.message());
				return false;
			}
			return true;
		}
		//
		void MessageCallback(const asSMessageInfo* msg, void* /*param*/) {
			std::string relPath = MakeRelativeSectionPath(msg->section);
			char buf[1024];
			if (msg->type == asMSGTYPE_ERROR) {
				std::snprintf(buf, sizeof(buf), "[Script] Error: [%s:%d] %s", relPath.c_str(), msg->row, msg->message);
				g_errors.push_back(buf);
				g_hasError = true;
				Log::Error(LogChannel::Script, "%s", buf);
			} else if (msg->type == asMSGTYPE_WARNING) {
				std::snprintf(buf, sizeof(buf), "[Script] Warning: [%s:%d] %s", relPath.c_str(), msg->row, msg->message);
				g_errors.push_back(buf);
				Log::Warning(LogChannel::Script, "%s", buf);
			} else {
				std::snprintf(buf, sizeof(buf), "[Script] [%s:%d] %s", relPath.c_str(), msg->row, msg->message);
				g_errors.push_back(buf);
				Log::Info(LogChannel::Script, "%s", buf);
			}
		}

		// -------------------------------------------------------------- //
		// Script-side log global functions                               //
		// -------------------------------------------------------------- //

		//
		// Resolve the call-site location from the active AngelScript context.
		// Returns a formatted "relpath:line" string, or "<unknown>:0" if unavailable.
		//
		static std::string GetScriptCallSiteLocation() {
			asIScriptContext* ctx = asGetActiveContext();
			if (ctx == nullptr)
				return "<unknown>:0";
			const char* section = nullptr;
			int line = ctx->GetLineNumber(0, nullptr, &section);
			std::string relPath = MakeRelativeSectionPath(section);
			char loc[512];
			std::snprintf(loc, sizeof(loc), "%s:%d", relPath.c_str(), line);
			return loc;
		}
		//
		static void ScriptLog_Info_Generic(asIScriptGeneric* gen) {
			const std::string& msg = *static_cast<std::string*>(gen->GetArgObject(0));
			std::string loc = GetScriptCallSiteLocation();
			char buf[1024];
			std::snprintf(buf, sizeof(buf), "[Script] [%s] %s", loc.c_str(), msg.c_str());
			Log::Info(LogChannel::Script, "%s", buf);
		}
		static void ScriptLog_Warning_Generic(asIScriptGeneric* gen) {
			const std::string& msg = *static_cast<std::string*>(gen->GetArgObject(0));
			std::string loc = GetScriptCallSiteLocation();
			char buf[1024];
			std::snprintf(buf, sizeof(buf), "[Script] Warning: [%s] %s", loc.c_str(), msg.c_str());
			Log::Warning(LogChannel::Script, "%s", buf);
		}
		static void ScriptLog_Error_Generic(asIScriptGeneric* gen) {
			const std::string& msg = *static_cast<std::string*>(gen->GetArgObject(0));
			std::string loc = GetScriptCallSiteLocation();
			char buf[1024];
			std::snprintf(buf, sizeof(buf), "[Script] Error: [%s] %s", loc.c_str(), msg.c_str());
			g_hasError = true;
			Log::Error(LogChannel::Script, "%s", buf);
		}

		// -------------------------------------------------------------- //
		// ScriptEngine                                                     //
		// -------------------------------------------------------------- //

		bool ScriptEngine::Initialize() {
			if (initialized)
				return true;
			engine = asCreateScriptEngine();
			if (engine == nullptr)
				return false;
			engine->SetMessageCallback(asFUNCTION(MessageCallback), nullptr, asCALL_CDECL);
			RegisterStdString(engine);
			RegisterAddons(engine);
			//
			// Register Log_Info, Log_Warning, Log_Error as global script functions
			// so scripts can emit messages that reach the UI console via Log::*
			//
			engine->RegisterGlobalFunction("void Log_Info(const string &in)", asFUNCTION(ScriptLog_Info_Generic), asCALL_GENERIC);
			engine->RegisterGlobalFunction("void Log_Warning(const string &in)", asFUNCTION(ScriptLog_Warning_Generic), asCALL_GENERIC);
			engine->RegisterGlobalFunction("void Log_Error(const string &in)", asFUNCTION(ScriptLog_Error_Generic), asCALL_GENERIC);
			initialized = true;
			return true;
		}
		void ScriptEngine::Shutdown() {
			if (engine != nullptr) {
				engine->ShutDownAndRelease();
				engine = nullptr;
			}
			initialized = false;
			rgbColorRegistered = false;
			imageRegistered = false;
			bitmapContextRegistered = false;
			tilesetContextRegistered = false;
			spriteContextRegistered = false;
			mapContextRegistered = false;
			paletteRegistered = false;
			addonsRegistered = false;
		}
		bool ScriptEngine::BeginModule(const std::string& name) {
			int r = builder.StartNewModule(engine, name.c_str());
			if (r < 0) {
				ReportError("[Script] BeginModule: failed to start module '" + name + "'");
				return false;
			}
			//
			// Register the #include callback.
			// Resolution order:
			//   1. Relative to the directory of the including script (sibling files).
			//   2. Search all unique directories of project script files.
			//
			builder.SetIncludeCallback(
				[](const char* include, const char* from, CScriptBuilder* b, void*) -> int {
					//
					// Step 1: try relative to the including script's directory
					//
					std::filesystem::path base = std::filesystem::path(from).parent_path();
					std::filesystem::path resolved = base / include;
					std::error_code ec;
					if (std::filesystem::exists(resolved, ec))
						return b->AddSectionFromFile(resolved.string().c_str());
					//
					// Step 2: search the parent directory of every project script file.
					// GetScriptFiles() returns absolute paths ($(sdk)/... already expanded),
					// so SDK subdirectories are covered without a separate SDK search step.
					//
					std::vector<std::string> scriptFiles = RetrodevLib::Project::GetScriptFiles();
					std::string includeName = std::filesystem::path(include).filename().string();
					for (const auto& scriptFile : scriptFiles) {
						std::filesystem::path candidate = std::filesystem::path(scriptFile).parent_path() / includeName;
						if (std::filesystem::exists(candidate, ec))
							return b->AddSectionFromFile(candidate.string().c_str());
					}
					//
					// Not found -- return negative so AngelScript reports the error
					//
					return -1;
				},
				nullptr);
			return true;
		}
		bool ScriptEngine::AddSectionFromFile(const std::string& path) {
			int r = builder.AddSectionFromFile(path.c_str());
			if (r < 0) {
				ReportError("[Script] AddSectionFromFile: could not read '" + path + "'");
				return false;
			}
			return true;
		}
		bool ScriptEngine::Build() {
			int r = builder.BuildModule();
			if (r < 0) {
				g_hasError = true;
				return false;
			}
			return true;
		}
		void ScriptEngine::Discard(const std::string& name) {
			asIScriptModule* mod = engine->GetModule(name.c_str());
			if (mod != nullptr)
				mod->Discard();
		}
		std::string ScriptEngine::GetScriptDescription(const std::string& scriptPath) {
			return GetScriptMetadata(scriptPath).description;
		}
		RetrodevLib::ScriptMetadata ScriptEngine::GetScriptMetadata(const std::string& scriptPath) {
			//
			// Single-pass scan for all recognised // @tag lines.
			// Never touches the AngelScript engine -- safe before Initialize().
			//
			RetrodevLib::ScriptMetadata meta;
			std::FILE* f = std::fopen(scriptPath.c_str(), "r");
			if (f == nullptr)
				return meta;
			char line[1024];
			while (std::fgets(line, sizeof(line), f)) {
				const char* p = line;
				while (*p == ' ' || *p == '\t')
					p++;
				if (p[0] != '/' || p[1] != '/')
					continue;
				p += 2;
				while (*p == ' ' || *p == '\t')
					p++;
				//
				// Helper: extract trimmed value after a matched tag
				//
				auto extract = [&](const char* tag, size_t tagLen, std::string& out) -> bool {
					if (std::strncmp(p, tag, tagLen) != 0)
						return false;
					const char* v = p + tagLen;
					while (*v == ' ' || *v == '\t')
						v++;
					out = v;
					while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' '))
						out.pop_back();
					return true;
				};
				std::string tmp;
				if (meta.description.empty() && extract("@description", 12, tmp)) {
					meta.description = tmp;
					continue;
				}
				if (meta.exporter.empty() && extract("@exporter", 9, tmp)) {
					meta.exporter = tmp;
					continue;
				}
				if (meta.target.empty() && extract("@target", 7, tmp)) {
					meta.target = tmp;
					continue;
				}
				//
				// @param <key> <type> <default> <label...>
				//
				if (std::strncmp(p, "@param", 6) == 0) {
					const char* v = p + 6;
					while (*v == ' ' || *v == '\t')
						v++;
					//
					// Parse key
					//
					const char* tok = v;
					while (*v && *v != ' ' && *v != '\t' && *v != '\n' && *v != '\r')
						v++;
					if (tok == v) {
						Log::Warning(LogChannel::Script, "[Script] %s: @param tag has no key -- line ignored", scriptPath.c_str());
						continue;
					}
					RetrodevLib::ScriptParamDef def;
					def.key.assign(tok, v);
					while (*v == ' ' || *v == '\t')
						v++;
					//
					// Parse type
					//
					tok = v;
					while (*v && *v != ' ' && *v != '\t' && *v != '\n' && *v != '\r')
						v++;
					if (tok == v) {
						Log::Warning(LogChannel::Script, "[Script] %s: @param '%s' has no type -- line ignored", scriptPath.c_str(), def.key.c_str());
						continue;
					}
					def.type.assign(tok, v);
					if (def.type != "bool" && def.type != "int" && def.type != "string" && def.type != "combo") {
						Log::Warning(LogChannel::Script, "[Script] %s: @param '%s' has unknown type '%s' -- line ignored", scriptPath.c_str(), def.key.c_str(), def.type.c_str());
						continue;
					}
					while (*v == ' ' || *v == '\t')
						v++;
					//
					// Parse default
					//
					tok = v;
					while (*v && *v != ' ' && *v != '\t' && *v != '\n' && *v != '\r')
						v++;
					if (tok == v) {
						Log::Warning(LogChannel::Script, "[Script] %s: @param '%s' has no default value -- line ignored", scriptPath.c_str(), def.key.c_str());
						continue;
					}
					def.defaultValue.assign(tok, v);
					while (*v == ' ' || *v == '\t')
						v++;
					//
					// For combo type the next whitespace-delimited token is the pipe-separated
					// options list (e.g. "chunky|screenmemory"). The label is everything after.
					//
					if (def.type == "combo") {
						tok = v;
						while (*v && *v != ' ' && *v != '\t' && *v != '\n' && *v != '\r')
							v++;
						if (tok == v) {
							Log::Warning(LogChannel::Script, "[Script] %s: @param '%s' combo has no options list -- line ignored", scriptPath.c_str(), def.key.c_str());
							continue;
						}
						//
						// Split "opt1|opt2|opt3" into def.options
						//
						const char* op = tok;
						while (op < v) {
							const char* pipe = op;
							while (pipe < v && *pipe != '|')
								pipe++;
							def.options.emplace_back(op, pipe);
							op = (pipe < v) ? pipe + 1 : v;
						}
						if (def.options.empty()) {
							Log::Warning(LogChannel::Script, "[Script] %s: @param '%s' combo options list is empty -- line ignored", scriptPath.c_str(), def.key.c_str());
							continue;
						}
						while (*v == ' ' || *v == '\t')
							v++;
					}
					//
					// Remainder is the label (trim trailing whitespace/newline)
					//
					def.label = v;
					while (!def.label.empty() && (def.label.back() == '\n' || def.label.back() == '\r' || def.label.back() == ' '))
						def.label.pop_back();
					meta.params.push_back(std::move(def));
					continue;
				}
			}
			std::fclose(f);
			return meta;
		}

		// -------------------------------------------------------------- //
		// Binding registration helpers                                     //
		// -------------------------------------------------------------- //

		//
		// Generic wrappers for Image -- required by AS_MAX_PORTABILITY builds
		// (asCALL_GENERIC is the only calling convention always available)
		//
		static void Image_GetWidth_Generic(asIScriptGeneric* gen) {
			gen->SetReturnDWord((asDWORD) static_cast<Image*>(gen->GetObject())->GetWidth());
		}
		static void Image_GetHeight_Generic(asIScriptGeneric* gen) {
			gen->SetReturnDWord((asDWORD) static_cast<Image*>(gen->GetObject())->GetHeight());
		}
		static void Image_GetPixelColor_Generic(asIScriptGeneric* gen) {
			Image* self = static_cast<Image*>(gen->GetObject());
			int x = (int)gen->GetArgDWord(0);
			int y = (int)gen->GetArgDWord(1);
			*(RgbColor*)gen->GetAddressOfReturnLocation() = self->GetPixelColor(x, y);
		}
		//
		//
		static void RgbColor_IsTransparent_Generic(asIScriptGeneric* gen) {
			gen->SetReturnByte((asBYTE) static_cast<RgbColor*>(gen->GetObject())->IsTransparent());
		}
		static void RgbColor_IsOpaque_Generic(asIScriptGeneric* gen) {
			gen->SetReturnByte((asBYTE) static_cast<RgbColor*>(gen->GetObject())->IsOpaque());
		}
		//
		void RegisterRgbColorBinding(asIScriptEngine* engine) {
			if (g_engine.rgbColorRegistered)
				return;
			//
			// RgbColor -- 4-byte POD value type; asOBJ_APP_CLASS | asOBJ_APP_CLASS_ALLINTS
			// ensures AS uses the correct calling convention on all platforms
			//
			engine->RegisterObjectType("RgbColor", sizeof(RgbColor), asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS | asOBJ_APP_CLASS_ALLINTS);
			engine->RegisterObjectProperty("RgbColor", "uint8 r", asOFFSET(RgbColor, r));
			engine->RegisterObjectProperty("RgbColor", "uint8 g", asOFFSET(RgbColor, g));
			engine->RegisterObjectProperty("RgbColor", "uint8 b", asOFFSET(RgbColor, b));
			engine->RegisterObjectProperty("RgbColor", "uint8 a", asOFFSET(RgbColor, a));
			engine->RegisterObjectMethod("RgbColor", "bool IsTransparent() const", asFUNCTION(RgbColor_IsTransparent_Generic), asCALL_GENERIC);
			engine->RegisterObjectMethod("RgbColor", "bool IsOpaque() const", asFUNCTION(RgbColor_IsOpaque_Generic), asCALL_GENERIC);
			g_engine.rgbColorRegistered = true;
		}
		void RegisterImageBinding(asIScriptEngine* engine) {
			if (g_engine.imageRegistered)
				return;
			//
			// Image -- ref type, no script-side reference counting (C++ owns the lifetime)
			//
			engine->RegisterObjectType("Image", 0, asOBJ_REF | asOBJ_NOCOUNT);
			engine->RegisterObjectMethod("Image", "int GetWidth() const", asFUNCTION(Image_GetWidth_Generic), asCALL_GENERIC);
			engine->RegisterObjectMethod("Image", "int GetHeight() const", asFUNCTION(Image_GetHeight_Generic), asCALL_GENERIC);
			engine->RegisterObjectMethod("Image", "RgbColor GetPixelColor(int x, int y) const", asFUNCTION(Image_GetPixelColor_Generic), asCALL_GENERIC);
			g_engine.imageRegistered = true;
		}

		// -------------------------------------------------------------- //
		// Addon registration (file, array, math)                          //
		// -------------------------------------------------------------- //

		void RegisterAddons(asIScriptEngine* engine) {
			if (g_engine.addonsRegistered)
				return;
			RegisterScriptFile(engine);
			RegisterScriptArray(engine, true);
			RegisterScriptMath(engine);
			g_engine.addonsRegistered = true;
		}

		// -------------------------------------------------------------- //
		// IPaletteConverter binding                                        //
		// -------------------------------------------------------------- //

		static void Palette_PaletteMaxColors_Generic(asIScriptGeneric* gen) {
			gen->SetReturnDWord((asDWORD) static_cast<IPaletteConverter*>(gen->GetObject())->PaletteMaxColors());
		}
		static void Palette_PenGetColor_Generic(asIScriptGeneric* gen) {
			int pen = (int)gen->GetArgDWord(0);
			*(RgbColor*)gen->GetAddressOfReturnLocation() = static_cast<IPaletteConverter*>(gen->GetObject())->PenGetColor(pen);
		}
		static void Palette_GetSystemIndexByColor_Generic(asIScriptGeneric* gen) {
			IPaletteConverter* self = static_cast<IPaletteConverter*>(gen->GetObject());
			const RgbColor& color = *static_cast<RgbColor*>(gen->GetArgObject(0));
			const std::string& mode = *static_cast<std::string*>(gen->GetArgObject(1));
			gen->SetReturnDWord((asDWORD)self->GetSystemIndexByColor(color, mode));
		}
		static void Palette_PenGetColorIndex_Generic(asIScriptGeneric* gen) {
			int pen = (int)gen->GetArgDWord(0);
			gen->SetReturnDWord((asDWORD) static_cast<IPaletteConverter*>(gen->GetObject())->PenGetColorIndex(pen));
		}
		static void Palette_PenGetIndexByColor_Generic(asIScriptGeneric* gen) {
			const RgbColor& color = *static_cast<RgbColor*>(gen->GetArgObject(0));
			gen->SetReturnDWord((asDWORD) static_cast<IPaletteConverter*>(gen->GetObject())->PenGetIndex(color));
		}
		//
		void RegisterPaletteBinding(asIScriptEngine* engine) {
			if (g_engine.paletteRegistered)
				return;
			//
			// IPaletteConverter -- ref type, no script-side reference counting (C++ owns lifetime)
			//
			engine->RegisterObjectType("Palette", 0, asOBJ_REF | asOBJ_NOCOUNT);
			engine->RegisterObjectMethod("Palette", "int PaletteMaxColors() const", asFUNCTION(Palette_PaletteMaxColors_Generic), asCALL_GENERIC);
			engine->RegisterObjectMethod("Palette", "RgbColor PenGetColor(int pen) const", asFUNCTION(Palette_PenGetColor_Generic), asCALL_GENERIC);
			engine->RegisterObjectMethod("Palette", "int GetSystemIndexByColor(const RgbColor &in, const string &in) const", asFUNCTION(Palette_GetSystemIndexByColor_Generic),
										 asCALL_GENERIC);
			engine->RegisterObjectMethod("Palette", "int PenGetColorIndex(int pen) const", asFUNCTION(Palette_PenGetColorIndex_Generic), asCALL_GENERIC);
			engine->RegisterObjectMethod("Palette", "int PenGetIndex(const RgbColor &in) const", asFUNCTION(Palette_PenGetIndexByColor_Generic), asCALL_GENERIC);
			g_engine.paletteRegistered = true;
		}

	}
}
