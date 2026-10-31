// OWNER: SKELETON (integration point; other owners may add ONE line per lifecycle hook, nothing else).
// SuperPeasantNative: F4SE plugin entry. Log: Documents\My Games\Fallout4\F4SE\SuperPeasantNative.log (set up by
// F4SE::Init from the plugin name). Lifecycle per FO4_ENGINE_API §9.
#include "perf/cost_probe.h"
#include "config.h"
#include "fo4/bookmark_io.h"  // R6 BOOKMARK
#include "fo4/costume.h"      // STEP 7.6
#include "fo4/fling_now.h"    // R6 STEP 13
#include "fo4/engine_hits.h"  // R6 STEP 16 (A/B)
#include "fo4/hooks.h"
#include "fo4/loadprompt.h"
#include "fo4/mover2_glue.h"  // R6 step 6
#include "fo4/trails_glue.h"  // STEP 7.5
#include "fo4/threads.h"
#include "papyrus/natives.h"
#include "runtime.h"

#include <REX/W32/KERNEL32.h>

namespace
{
	void OnMessage(F4SE::MessagingInterface::Message* a_msg)
	{
			SP_COST_SCOPE(Lifecycle);
		SP_COST_GAP();
		if (!a_msg) return;
		using M = F4SE::MessagingInterface;
		switch (a_msg->type) {
		case M::kGameDataReady:
			REX::INFO("SPN|I|kGameDataReady (message thread {}; compare with the permanent task thread line)", REX::W32::GetCurrentThreadId());  // REVIEW: PROBE main thread
			sp::runtime::OnGameDataReady();
			sp::fo4::ApplyCostume();  // STEP 7.6: One Punch Man look, or the TABS look with [Tabs] look.costume 0
			sp::fo4::mv2::OnDataLoaded();  // R6 step 6: load checks; look.noPathWarp: NoPathFailureWarping_DO -> SP_NoPathWarpKW
			sp::fo4::flingnow::OnDataLoaded();  // R6 STEP 13: load checks of the fling engine calls (SPN|S13|check)
			sp::fo4::enginehits::OnDataLoaded();  // R6 STEP 16 (A/B): load checks of DoHitMe / HitData (SPN|S16|check)
			sp::fo4::trails::OnGameDataReady();  // STEP 7.5: TABS trails load checks + the two ribbons (look.trails)
			sp::fo4::loadprompt::Arm();  // POLISH (INTEGRATOR): debug-only, [Debug] AcceptLoadPrompt=1 (harness -SaveLoad run B)
			break;
		case M::kPreLoadGame:
			REX::INFO("SPN|I|kPreLoadGame");
			sp::runtime::OnPreLoadGame();
			break;
		case M::kNewGame:
			REX::INFO("SPN|I|kNewGame");
			sp::runtime::OnPreLoadGame(true);  // REVIEW: no kPostLoadGame follows, so no load window
			break;
		case M::kPostLoadGame:
			REX::INFO("SPN|I|kPostLoadGame ok={}", a_msg->data != nullptr);
			sp::runtime::OnPostLoadGame();
			sp::fo4::trails::OnPostLoadGame();  // STEP 7.5: make the ribbons if look.trails was turned on later
			sp::fo4::TestTimeOfDay();  // STEP 7.6: TEST builds only
			break;
		case M::kPreSaveGame:  // POLISH (NATIVES, harness -SaveLoad evidence; log only)
			REX::INFO("SPN|I|kPreSaveGame");
			break;
		case M::kPostSaveGame:  // POLISH (NATIVES, log only)
			REX::INFO("SPN|I|kSaveGame done (kPostSaveGame)");
			break;
		default:
			break;
		}
	}
}

F4SE_PLUGIN_PRELOAD(const F4SE::PreLoadInterface* a_f4se)
{
	F4SE::Init(a_f4se);
	return true;
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	// R6 step 2 (HOOKS.md A1): the trampoline for the look call-site hooks is allocated by look::Install (still inside this
	// Load call) only when [Hooks] LookMask asks for a call-site hook: the shipped build (LookMask 0) allocates nothing new
	F4SE::Init(a_f4se);
	sp::fo4::bookmark_io::InstallLogTap();  // R6 BOOKMARK: the last log lines go into a bookmark too
	sp::fo4::threads::OnPluginLoad();  // C247 (NATIVES): start-up thread id for the main-thread check
	REX::INFO("SPN|I|SuperPeasantNative {} loading (F4SE {}, runtime {})", F4SE::GetPluginVersion().string(), F4SE::GetF4SEVersion().string(), REX::FModule::GetExecutingModule().GetFileVersion().string());

	sp::cfg::LoadIni(sp::cfg::kIniPath, [](const std::string& s) { REX::INFO("{}", s); });
	SP_COST_INIT(a_f4se->RuntimeVersion().pack());
	REX::INFO("SPN|I|log writes deferred (log.deferWrites): {}", sp::fo4::bookmark_io::DeferLogWrites(sp::cfg::Tunable("log.deferWrites", 0.0) != 0.0));  // s12 fix round 2 lane 3

	if (const auto* msg = F4SE::GetMessagingInterface(); !msg || !msg->RegisterListener(OnMessage))
		REX::ERROR("SPN|E|messaging listener not registered");

	if (const auto* pap = F4SE::GetPapyrusInterface(); !pap || !pap->Register(sp::papyrus::RegisterFunctions))
		REX::ERROR("SPN|E|papyrus registration failed");
	sp::runtime::RegisterSerialization();  // CARRY (NATIVES): co-save, a carry saved mid-air resumes after the load

	if (sp::cfg::Get().enabled)
		sp::fo4::InstallHooks();
	else
		REX::INFO("SPN|I|disabled by ini: no hooks");

	REX::INFO("SPN|I|loaded");
	return true;
}
