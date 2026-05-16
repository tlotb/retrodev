// --------------------------------------------------------------------------------------------------------------
//
// Retrodev Raster
//
// Standalone raster editor bootstrap.
//
// (c) TLOTB 2026
//
// --------------------------------------------------------------------------------------------------------------

#include "retro.main.h"
#include <app/raster.app.h>

using namespace RetrodevGui;

int rastermain(int, char**) {
	if (!RasterApplication::Initialize())
		return -1;
	RasterApplication::Run();
	RasterApplication::Shutdown();
	return 0;
}
