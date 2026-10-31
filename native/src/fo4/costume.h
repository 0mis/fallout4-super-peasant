#pragma once
// OWNER: STEP 7.6. His costume switch ([Tabs] look.costume: 1 One Punch Man (default), 0 the old TABS look). Game thread,
// once at kGameDataReady.
namespace sp::fo4
{
	void ApplyCostume();
	void TestTimeOfDay();  // TEST builds only ([Tabs] debug.gameHour), a no-op in shipped builds
}
