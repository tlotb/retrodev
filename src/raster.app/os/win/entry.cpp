// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Raster
//
// Windows platform entry point.
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#include <windows.h>
#include <vector>
#include <string>
#include <retro.main.h>

#ifdef RELEASE

static std::vector<std::string> ConvertCommandLineToArgs() {
	std::vector<std::string> args;
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (argv != nullptr) {
		for (int i = 0; i < argc; i++) {
			int argLen = WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
			std::string arg(argLen, '\0');
			WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, &arg[0], argLen, nullptr, nullptr);
			args.push_back(arg);
		}
		LocalFree(argv);
	}
	return args;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	std::vector<std::string> args = ConvertCommandLineToArgs();
	std::vector<char*> argv;
	argv.reserve(args.size());
	for (auto& arg : args)
		argv.push_back(arg.data());
	return rastermain(static_cast<int>(argv.size()), argv.data());
}

#else

int main(int argc, char** argv) {
	return rastermain(argc, argv);
}

#endif
