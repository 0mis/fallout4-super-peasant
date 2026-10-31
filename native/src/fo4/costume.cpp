// OWNER: STEP 7.6 (One Punch Man look). His look is the One Punch Man costume (Meshes\SuperPeasant\SuperPeasant.nif /
// SuperPeasant_Rigid.nif, the esp's own ARMA paths). [Tabs] look.costume = 0 is the safety fallback to the old TABS
// Super Peasant look: once at kGameDataReady (before any 3D of him loads) both ARMA records (SP_BodyAA 0x803,
// SP_BodyRigidAA 0x804) get the shipped TABS meshes (SuperPeasant_TABS.nif / SuperPeasant_TABS_Rigid.nif) as their model.
// SEH-guarded: any fault or a missing record leaves the One Punch Man look and logs one line.
#include "fo4/costume.h"

#include "config.h"
#include "feel_switch.h"
#include "papyrus/natives.h"

namespace sp::fo4
{
	namespace
	{
		int SwapImpl()
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) return -1;
			struct Rec
			{
				std::uint32_t id;
				const char*   path;
			};
			static constexpr Rec kRecs[] = { { 0x803, "SuperPeasant\\SuperPeasant_TABS.nif" }, { 0x804, "SuperPeasant\\SuperPeasant_TABS_Rigid.nif" } };
			int n = 0;
			for (const auto& r : kRecs) {
				auto* a = dh->LookupForm<RE::TESObjectARMA>(r.id, "SuperPeasant.esp");
				if (!a) continue;
				for (int s = 0; s < 2; ++s) a->bipedModel[s].SetModel(r.path);
				++n;
			}
			return n;
		}
		int SwapSEH() noexcept
		{
			__try {
				return SwapImpl();
			} __except (1) {
				return -2;
			}
		}
	}

	void ApplyCostume()
	{
		const int costume = static_cast<int>(sp::cfg::Tunable("look.costume", 1.0));
		if (costume != 0) {
			REX::INFO("SPN|LOOK|costume: One Punch Man (look.costume {})", costume);
			return;
		}
		const int n = SwapSEH();
		if (n == 2) REX::INFO("SPN|LOOK|costume: TABS Super Peasant fallback (look.costume 0): both body ARMA records now use the TABS meshes");
		else REX::WARN("SPN|LOOK|costume: TABS fallback asked but {} -> One Punch Man look stays", n == -2 ? "the swap faulted" : (n < 0 ? "no data handler" : "an ARMA record was not found"));
	}

	// TEST builds only (SP_FEEL 1, never shipped): [Tabs] debug.gameHour = h (0..24) sets the game clock after each load,
	// for the step-7.6 look screenshots at night. Shipped builds compile this to nothing.
	void TestTimeOfDay()
	{
#if SP_FEEL
		if (sp::cfg::Tunable("debug.thirdPerson", 0.0) != 0) sp::papyrus::TestForceThirdPerson();
		const double h = sp::cfg::Tunable("debug.gameHour", -1.0);
		if (h < 0.0 || h > 24.0) return;
		if (auto* gh = RE::TESForm::GetFormByID(0x00000038); gh && gh->GetFormType() == RE::ENUM_FORM_ID::kGLOB) {
			static_cast<RE::TESGlobal*>(gh)->value = static_cast<float>(h);
			static int s_n = 0;
			if (s_n++ < 2) REX::INFO("SPN|LOOK|TEST debug.gameHour: GameHour = {:.1f}", h);
		}
#endif
	}
}