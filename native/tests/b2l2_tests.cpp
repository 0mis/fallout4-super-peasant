// Ground-idle settling regressions on the real body, independent of the abandoned lane-2 experiments.
#include "s7.7_scenes.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static int g_pass = 0, g_fail = 0;
#define CHECK(c, ...) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

#include "idle_pose_regressions.inc"

int main(int argc, char** argv)
{
	bool quick = false;
	int selectedSeed = 0, selectedScene = -1;
	const char* ini = "SuperPeasantNative.ini";
	for (int i = 1; i < argc; ++i) {
		if (!std::strcmp(argv[i], "--quick")) quick = true;
		if (!std::strcmp(argv[i], "--ini") && i + 1 < argc) ini = argv[++i];
		if (!std::strcmp(argv[i], "--case") && i + 2 < argc) {
			selectedSeed = std::atoi(argv[++i]);
			selectedScene = std::atoi(argv[++i]);
			if (selectedSeed <= 0 || selectedScene < 0 || selectedScene > 2) {
				std::fprintf(stderr, "--case requires a positive seed and scene 0, 1, or 2\n");
				return 2;
			}
		}
	}
	const bool loaded = sp::cfg::LoadIni(ini);
	CHECK(loaded, "idle regressions load the requested ini (%s)", ini);
	s77api::Set(sp::cfg::Tunable("look.realism", 0.85));
	idle_pose::Run(quick, selectedSeed, selectedScene);
	std::printf("b2l2_tests: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
