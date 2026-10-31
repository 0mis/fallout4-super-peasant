// OWNER: B2 ENGINE-FACTS (BRAIN2, new file). See scan.h.
#include "fo4/scan.h"

#include "config.h"
#include "core/brain.h"  // ROUND3: PreemptReaches (pure)
#include "fo4/actors.h"
#include "fo4/hitsink.h"
#include "fo4/rays.h"
#include "fo4/threads.h"
#include "runtime.h"  // R6 step 4: RequestDump (the scan pin's crash shield)

#include <REX/W32/KERNEL32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sp::fo4::scan
{
	using sp::cfg::Tunable;
	using namespace sp::math;

	namespace
	{
		// Fallout4.esm forms (vanilla_index.json, verified 2026-10-10 against the installed Fallout4.esm)
		constexpr std::uint32_t kWorkshopNPCFaction = 0x000337F3;
		constexpr std::uint32_t kCurrentCompanionFaction = 0x00023C01;
		constexpr std::uint32_t kHasBeenCompanionFaction = 0x000A1B85;
		constexpr std::uint32_t kPlayerFaction = 0x0001C21C;
		constexpr std::uint32_t kWorkshopItemKeyword = 0x00054BA6;
		// HOTFIX2 F1 (hostile kinds by race / faction). Form IDs read 2026-10-12 from the installed Fallout4.esm and DLC masters
		// (tools/esp/esm_reader.py: FACT / KYWD / RACE EditorIDs). DLC forms are local IDs resolved by file at kGameDataReady.
		constexpr std::uint32_t kPotentialCompanionFaction = 0x001EC1B9;
		constexpr std::uint32_t kHostileFactions[] = {
			0x0001CBED,  // RaiderFaction
			0x00058303,  // GunnerFaction
			0x00058305,  // SuperMutantFaction
			0x00058304,  // RobotFaction
			0x0011EA70,  // TurretFaction
			0x00077C6C,  // FeralGhoulFaction
			0x0002689D,  // MirelurkFaction
			0x0001E722,  // DeathclawFaction
			0x00020B89,  // RadScorpionFaction
			0x000B3D82,  // YaoGuaiFaction
			0x00031B22,  // MoleRatFaction
			0x000EDCE3,  // BloatflyFaction
			0x000EDCE4,  // StingwingFaction
			0x000EDCE2,  // BloodbugFaction
			0x000EDCE1,  // RadroachFaction
			0x00028670,  // BugFaction
			0x000948B4,  // MutantHoundFaction
			0x0001D15B,  // ViciousDogFaction
			0x00083AD0,  // TriggermanFaction
			0x000AC57B,  // CZRaiderFaction
		};
		struct DlcForm
		{
			std::uint32_t local;
			const char*   file;
		};
		constexpr DlcForm kHostileFactionsDlc[] = {
			{ 0x004385, "DLCRobot.esm" },      // DLC01RaiderFaction (Rust Devils)
			{ 0x043223, "DLCCoast.esm" },      // DLC03FogCrawler
			{ 0x02793F, "DLCCoast.esm" },      // DLC03_GulperFaction
			{ 0x00958A, "DLCCoast.esm" },      // DLC03AnglerFaction
			{ 0x002DB4, "DLCCoast.esm" },      // DLC03TrapperFaction
			{ 0x002DB5, "DLCCoast.esm" },      // DLC03TrapperFactionWoods
			{ 0x002DB6, "DLCCoast.esm" },      // DLC03TrapperFactionCoast
			{ 0x056F29, "DLCNukaWorld.esm" },  // DLC04HostileGhoulFaction
			{ 0x00F43A, "DLCNukaWorld.esm" },  // DLC04GangPackFaction (allied when the player is Overboss: the Ally tie wins)
			{ 0x00F439, "DLCNukaWorld.esm" },  // DLC04GangOperatorsFaction
			{ 0x00F438, "DLCNukaWorld.esm" },  // DLC04GangDisciplesFaction
		};
		constexpr std::uint32_t kMonsterRaceKeywords[] = {
			0x0006D7B6,  // ActorTypeSuperMutant (SuperMutantRace)
			0x0014F6A5,  // ActorTypeSuperMutantBehemoth (SupermutantBehemothRace: Swan)
			0x0006B4F2,  // ActorTypeFeralGhoul
			0x000317AA,  // ActorTypeDeathclaw (incl. the Nuka-World gatorclaw)
			0x0002CB71,  // ActorTypeMirelurk
			0x00108178,  // ActorTypeMirelurkQueen
			0x000E58C7,  // ActorTypeMirelurkKing
			0x000636DE,  // ActorTypeRadScorpion
			0x000B3D83,  // ActorTypeYaoGuai
			0x0002866C,  // ActorTypeBug (bloatfly, stingwing, radroach, bloodbug, rad ant, cave cricket)
			0x0002CB70,  // ActorTypeMolerat
			0x0002CB97,  // ActorTypeFeralDog (vicious dog, mutated wolf)
		};
		constexpr DlcForm kMonsterRaceKeywordsDlc[] = {
			{ 0x0270C0, "DLCCoast.esm" },  // ActorTypeGulper
			{ 0x009589, "DLCCoast.esm" },  // DLC03_ActorTypeAngler
		};
		constexpr std::uint32_t kMachineRaceKeywords[] = {
			0x0002CB73,  // ActorTypeRobot (assaultron, sentry bot, protectron, Mr Gutsy / Handy, eyebot, robobrain)
			0x000B2BF3,  // ActorTypeTurret
		};
		constexpr std::uint32_t kMonsterRaceIds[] = {
			0x00090C33,  // FEVHoundRace (super mutant hound: no ActorType keyword of its own)
		};
		constexpr DlcForm kMonsterRacesDlc[] = {
			{ 0x000C82, "DLCworkshop01.esm" },  // DLC02FEVHoundRace
			{ 0x0180A8, "DLCCoast.esm" },       // DLC03_HermitCrabRace
			{ 0x014174, "DLCCoast.esm" },       // DLC03_FogCrawlerRace
			{ 0x00C606, "DLCNukaWorld.esm" },   // DLC04_RadRatRace
			{ 0x00A1A6, "DLCNukaWorld.esm" },   // DLC04_BloodwormRace
			{ 0x00B028, "DLCNukaWorld.esm" },   // DLC04_SwarmRace
		};

		struct Entry
		{
			RE::ActorHandle handle{};
			ActorFacts      f{};
			std::uint64_t   seenCycle{ 0 };
			double          lastExamined{ -1e9 };
			double          bonesAt{ -1e9 };
			double          explAt{ -1e9 };  // completion lane 1 (brain.scan.explosiveTtl): the last explosive-weapon read
			double          explSeenAt{ -1e9 };  // s13 r1 fix lane 3 (brain.scan.explosiveKeep), merged: the last read that found one
			bool            suicSeen{ false };   // explore cycle m1 fix lane 1 (brain.scan.suiciderRead): a read found it a suicider (stays one)
		};

		struct Forms
		{
			const RE::TESFaction* workshopNPC{ nullptr };
			const RE::TESFaction* currentCompanion{ nullptr };
			const RE::TESFaction* hasBeenCompanion{ nullptr };
			const RE::TESFaction* playerFaction{ nullptr };
			RE::BGSKeyword*       workshopItem{ nullptr };
			// HOTFIX2 F1
			const RE::TESFaction*              potentialCompanion{ nullptr };
			std::vector<const RE::TESFaction*> hostileFactions;
			std::vector<const RE::BGSKeyword*> monsterKw, machineKw;
			std::vector<const RE::TESRace*>    monsterRaces;
		};

		struct State
		{
			Params                                     p{};
			Grid                                       grid{};
			PlayerFacts                                pl{};
			Vec3                                       plPrevFeet{};
			double                                     plPrevAt{ -1e9 };
			std::vector<Entry>                         entries;
			std::unordered_map<std::uint32_t, std::size_t> index;
			std::vector<ActorFacts>                    kept;
			std::vector<RE::ActorHandle>               keptHandles;
			std::vector<Bystander>                     bys;
			std::vector<RE::ActorHandle>               bysHandles;
			Window                                     win{};
			AttackerTable                              attackers;
			PlayerHitAdmissions                        hitAdmissions;
			std::uint64_t                              hotAdmits{ 0 };
			QuarrelList                                quarrels;
			Forms                                      forms{};
			Stats                                      st{};
			double                                     nextScanAt{ -1e9 };
			bool                                       broken{ false };
			bool                                       afterLoad{ false };
			std::uint64_t                              afterLoadCycle{ 0 };
			double                                     logStartAt{ -1 }, nextLogAt{ -1e9 };
			std::unordered_set<std::uint32_t>          seenP3, seenP6, seenP7;
			double                                     safeSecond{ -1 };
			int                                        safeLogs{ 0 };
			double                                     obsClock{ 0 }, obsNextAt{ 0 }, p2NextAt{ 0 }, g2NextAt{ 0 };
			std::uint64_t                              scanUsN{ 0 }, refreshUsN{ 0 };
			double                                     scanUsSum{ 0 }, refreshUsSum{ 0 };
			int                                        liveAsksFrame{ 0 };
			std::vector<scan::HitRec>                  deferred;  // BRAIN2 (INTEGRATOR, SR1-3): player hits with no live-ask budget
			bool                                       ownerCheck{ false }, weaponRead{ true };
			// s11 fix lane 1 (brain.protectBomberR): the explosive-weapon read (brain.scan.explosiveRead) and its crash shield
			bool                                       explosiveRead{ true }, explosiveOff{ false };
			std::uint64_t                              explosiveReads{ 0 }, explosiveTrue{ 0 };
			double                                     explosiveTtl{ 0 };  // completion lane 1 (brain.scan.explosiveTtl, s; 0 = with the static read only)
			double                                     explosiveKeep{ 0 };  // s13 r1 fix lane 3 (brain.scan.explosiveKeep, s; 0 = the last read only)
			std::uint64_t                              explosiveFast{ 0 };  // reads between two static reads (explFast= on the scan line)
			std::unordered_set<std::uint32_t>          seenExpl;  // SPN|PROBE|EXPL once per actor (at most 30)
			// explore cycle m1 fix lane 1: brain.scan.explosiveGuns (0 = a gun whose projectile explodes - missile launcher, Fat Man - is a
			// gunner, not a thrower: EXPLORE cyclem1 A7 / rank 10); brain.scan.suiciderRead (the super mutant suicider read, its own crash shield)
			bool                                       explosiveGuns{ true }, suiciderRead{ false }, suiciderOff{ false };
			bool                                       throwerAllSlots{ false };  // B2 codex review (S1): brain.scan.throwerAllSlots
			std::uint64_t                              explosiveGunSkips{ 0 }, suiciderReads{ 0 }, suiciderTrue{ 0 };
			std::unordered_set<std::uint32_t>          seenSuic;  // the suicider's SPN|PROBE|EXPL line once per actor (at most 30)
			// completion side lane 4 (brain.protectRushMelee): the exact melee read (with the explosive read; brain.scan.meleeRead) and its
			// own crash shield (a fault turns only this read off: no rush rank, the bomber rule keeps its read)
			bool                                       meleeRead{ true }, meleeOff{ false };
			std::uint64_t                              meleeReads{ 0 }, meleeTrue{ 0 };
			std::unordered_set<std::uint32_t>          seenMelee;  // SPN|PROBE|MELEE once per actor (at most 30)
			// HOTFIX round5C: SPN|I|defend / SPN|SCAN|skip log gates (per id) and counters
			std::unordered_map<std::uint32_t, double>  defendLogAt, skipLogAt;
			double                                     hfLogSecond{ -1 };
			int                                        hfLogsThisSecond{ 0 };
			std::uint64_t                              defendLifts{ 0 }, skips{ 0 }, hkNoQuarrel{ 0 }, reasks{ 0 };
			// HOTFIX2: race kinds (cache), hostile-kind log gate, the per-frame watch (cursor, asks, kept now, turned), test inject
			std::unordered_map<const RE::TESRace*, std::uint8_t> raceKind;
			std::unordered_map<std::uint32_t, double>  kindLogAt, watchLogAt;
			std::size_t                                watchCursor{ 0 };
			std::uint64_t                              wAsks{ 0 }, wKeeps{ 0 }, wTurns{ 0 }, kindReads{ 0 }, kindTrue{ 0 }, injected{ 0 };
			// R6 step 4 (Warden 2): the Warden's target is pinned into the kept list (PinKeep, re-asserted every brain frame)
			std::uint32_t                              pinId{ 0 };
			double                                     pinAt{ -1e9 };
			std::uint64_t                              pinAdds{ 0 }, pinFails{ 0 }, pinFaults{ 0 };
			bool                                       pinOff{ false };
			// R6 step 3: the Warden's view of every known actor (rebuilt at each full scan) and its R6 asks
			std::vector<ActorFacts>                    knownView;
			std::uint64_t                              scanSerial{ 0 };
			bool                                       r6AsksOff{ false };
			std::uint64_t                              r6AskFaults{ 0 };
			bool                                       r6KillRole{ false }, r6Workshop{ false }, r6Probe{ false };  // SetR6Checks (load-time proofs)
			bool                                       knownViewOn{ true };  // s4 fix lane 3: SetKnownView
			int                                        r6KmLines{ 0 }, r6WsLines{ 0 };
			std::unordered_set<std::uint32_t>          r6WsSeen;
		};
		State g;

		using Clock = std::chrono::steady_clock;
		double UsSince(Clock::time_point a_t0) { return std::chrono::duration<double, std::micro>(Clock::now() - a_t0).count(); }
		std::int64_t NowUs() { return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count(); }

		// s4 fix lane 2 (P03): the provoked hostile kinds (id, until in NowUs), written on the pump by DrainHits, read anywhere
		struct ProvRec
		{
			std::atomic<std::uint32_t> id{ 0 };
			std::atomic<std::int64_t>  untilUs{ 0 };
		};
		constexpr int             kProvN = 16;
		ProvRec                   g_prov[kProvN];
		std::atomic<std::uint32_t> g_provNext{ 0 };
		std::atomic<bool>         g_provOn{ true };
		std::atomic<bool>         g_tcOff{ false };  // completion side lane 3: the turned-companion rule switched off by its crash shield (session)
		void NoteProvoked(std::uint32_t a_id, std::int64_t a_untilUs)
		{
			for (auto& r : g_prov)
				if (r.id.load(std::memory_order_relaxed) == a_id) {
					r.untilUs.store(a_untilUs, std::memory_order_relaxed);
					return;
				}
			auto& r = g_prov[g_provNext.fetch_add(1, std::memory_order_relaxed) % kProvN];
			r.untilUs.store(0, std::memory_order_relaxed);
			r.id.store(a_id, std::memory_order_relaxed);
			r.untilUs.store(a_untilUs, std::memory_order_relaxed);
		}

		void Fault(const char* a_what)
		{
			++g.st.faults;
			if (!g.broken) {
				g.broken = true;
				REX::ERROR("SPN|E|scan fault in {} (thread {}): native brain OFF for this session (SAFETY G4 breaker)", a_what, REX::W32::GetCurrentThreadId());
			}
		}

		// ------------------------------------------------ SEH shells (no objects with destructors in a __try body)
		bool HostileImpl(RE::Actor* t, RE::Actor* pl) { return t->GetHostileToActor(pl); }
		bool SehHostile(RE::Actor* t, RE::Actor* pl, bool* out) noexcept
		{
			__try { *out = HostileImpl(t, pl); return true; } __except (1) { return false; }
		}
		bool InCombatImpl(RE::Actor* t) { return t->IsInCombat(); }
		bool SehInCombat(RE::Actor* t, bool* out) noexcept
		{
			__try { *out = InCombatImpl(t); return true; } __except (1) { return false; }
		}
		bool InFactionImpl(RE::Actor* t, const RE::TESFaction* f) { return t->IsInFaction(f); }  // Actor vfunc 0x10A: base + ref-level
		bool SehInFaction(RE::Actor* t, const RE::TESFaction* f, bool* out) noexcept
		{
			__try { *out = InFactionImpl(t, f); return true; } __except (1) { return false; }
		}
		bool BaseInFactionImpl(RE::TESNPC* npc, const RE::TESFaction* f) { return npc->IsInFaction(f); }
		bool SehBaseInFaction(RE::TESNPC* npc, const RE::TESFaction* f, bool* out) noexcept
		{
			__try { *out = BaseInFactionImpl(npc, f); return true; } __except (1) { return false; }
		}
		bool CrimeImpl(RE::Actor* t) { return t->GetCrimeTrackingFaction() != nullptr; }
		bool SehCrime(RE::Actor* t, bool* out) noexcept
		{
			__try { *out = CrimeImpl(t); return true; } __except (1) { return false; }
		}
		bool LinkedImpl(RE::Actor* t, RE::BGSKeyword* kw) { return t->GetLinkedRef(kw) != nullptr; }
		bool SehLinked(RE::Actor* t, RE::BGSKeyword* kw, bool* out) noexcept
		{
			__try { *out = LinkedImpl(t, kw); return true; } __except (1) { return false; }
		}
		RE::TESForm* OwnerImpl(RE::Actor* t) { return t->GetOwner(); }
		// R6 step 3 (HOOKS s3 B7): the owner of the linked workbench (TESObjectREFR::GetOwner: the ref's own ExtraOwnership)
		RE::TESForm* WorkshopOwnerImpl(RE::Actor* t, RE::BGSKeyword* kw)
		{
			auto* ws = t->GetLinkedRef(kw);
			return ws ? ws->GetOwner() : nullptr;
		}
		bool SehWorkshopOwner(RE::Actor* t, RE::BGSKeyword* kw, RE::TESForm** out) noexcept
		{
			__try { *out = WorkshopOwnerImpl(t, kw); return true; } __except (1) { return false; }
		}
		bool         SehOwner(RE::Actor* t, RE::TESForm** out) noexcept
		{
			__try { *out = OwnerImpl(t); return true; } __except (1) { return false; }
		}
		bool SceneImpl(RE::TESObjectREFR* t) { return t->GetCurrentScene() != nullptr; }  // TESObjectREFR vfunc 0x5C
		bool SehScene(RE::TESObjectREFR* t, bool* out) noexcept
		{
			__try { *out = SceneImpl(t); return true; } __except (1) { return false; }
		}
		bool GhostImpl(RE::TESNPC* npc) { return npc->GetIsGhost(); }
		bool SehGhost(RE::TESNPC* npc, bool* out) noexcept
		{
			__try { *out = GhostImpl(npc); return true; } __except (1) { return false; }
		}
		bool AliasImpl(RE::Actor* t)
		{
			if (!t->extraList) return false;
			const auto* x = t->extraList->GetByType<RE::ExtraAliasInstanceArray>();
			return x && !x->aliasArray.empty();
		}
		bool SehAlias(RE::Actor* t, bool* out) noexcept
		{
			__try { *out = AliasImpl(t); return true; } __except (1) { return false; }
		}
		// HOTFIX2 F3: the live combat target kind (and lifeState) of one actor, for the per-frame watch
		bool CombatReadImpl(RE::Actor* t, RE::Actor* pl, RE::Actor* me, CombatKind* ck, bool* down)
		{
			*down = t->lifeState != 0;
			*ck = CombatKind::kNone;
			if (t->currentCombatTarget) {
				if (const auto ct = t->currentCombatTarget.get()) {
					const RE::Actor* c = ct.get();
					if (pl && c == pl) *ck = CombatKind::kPlayer;
					else if (c == me || sp::fo4::IsPeasant(c)) *ck = CombatKind::kHim;
					else if (c && (c->niFlags.flags & (1u << 26)) != 0) *ck = CombatKind::kTeammate;
					else if (c) *ck = CombatKind::kOther;
				}
			}
			return true;
		}
		bool SehCombatRead(RE::Actor* t, RE::Actor* pl, RE::Actor* me, CombatKind* ck, bool* down) noexcept
		{
			__try { return CombatReadImpl(t, pl, me, ck, down); } __except (1) { return false; }
		}
		// HOTFIX round5C B: hostile by nature. true = a faction of it (the base NPC's list and the ref-level changes, rank >= 0)
		// is an Enemy of PlayerFaction (either side's reaction list), or its base AI aggression is Very Aggressive /
		// Frenzied (attacks neutrals on sight); false = none of that, or any Ally / Friend tie to PlayerFaction (or a member
		// of it). Member reads only (TESReactionForm::reactionList, TESActorBaseData::factions, ExtraFactionChanges).
		int ReactionOf(const RE::TESFaction* a_fac, const void* a_to)
		{
			// the non-const list: CommonLibF4's const BSSimpleList::begin() has no return statement (C4716)
			auto& list = const_cast<RE::BSSimpleList<RE::GROUP_REACTION*>&>(a_fac->reactionList);
			for (const RE::GROUP_REACTION* r : list)
				if (r && static_cast<const void*>(r->form) == a_to) return static_cast<int>(r->fightReaction);
			return -1;
		}
		// HOTFIX2 F1: the race kind (monster / machine / other), cached per race form
		std::uint8_t RaceKindOf(const RE::TESRace* a_race)
		{
			if (!a_race) return kRaceOther;
			if (const auto it = g.raceKind.find(a_race); it != g.raceKind.end()) return it->second;
			std::uint8_t k = kRaceOther;
			for (const auto* r : g.forms.monsterRaces)
				if (r == a_race) k = kRaceMonster;
			if (k == kRaceOther && a_race->keywords) {
				const std::uint32_t n = std::min<std::uint32_t>(a_race->numKeywords, 64);
				for (std::uint32_t i = 0; i < n && k == kRaceOther; ++i) {
					const RE::BGSKeyword* kw = a_race->keywords[i];
					if (!kw) continue;
					for (const auto* m : g.forms.monsterKw)
						if (m == kw) k = kRaceMonster;
					if (k != kRaceOther) break;
					for (const auto* m : g.forms.machineKw)
						if (m == kw) k = kRaceMachine;
				}
			}
			if (g.raceKind.size() < 1024) g.raceKind.emplace(a_race, k);
			return k;
		}
		// Every faction of an NPC: its own list, the factions template chain (TPTA slot 2 while "use factions" is set; NPC
		// templates only, <= 5 deep) and the ref's ExtraFactionChanges (rank >= 0; a rank < 0 change removes a base faction).
		void CollectFactions(const RE::TESNPC* a_npc, const RE::ExtraFactionChanges* a_xf, std::vector<const RE::TESFaction*>& a_out)
		{
			auto removed = [&](const RE::TESFaction* fac) {
				if (!a_xf) return false;
				for (const auto& fr : a_xf->factionChanges)
					if (fr.faction == fac && fr.rank < 0) return true;
				return false;
			};
			auto add = [&](const RE::TESFaction* fac) {
				if (fac && a_out.size() < 64 && std::find(a_out.begin(), a_out.end(), fac) == a_out.end()) a_out.push_back(fac);
			};
			const RE::TESNPC* n = a_npc;
			for (int depth = 0; n && depth < 5; ++depth) {
				for (const auto& fr : n->factions)
					if (fr.faction && fr.rank >= 0 && !removed(fr.faction)) add(fr.faction);
				if (!n->UsesTemplate() || !n->actorData.templateUseFlags.all(RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG::kFactions) || !n->templateForms) break;
				const RE::TESForm* tf = n->templateForms[2];  // TPTA: traits, stats, FACTIONS, spells, AI data, ...
				n = tf ? tf->As<RE::TESNPC>() : nullptr;
			}
			if (a_xf)
				for (const auto& fr : a_xf->factionChanges)
					if (fr.faction && fr.rank >= 0) add(fr.faction);
		}
		// HOTFIX round5C B / HOTFIX2 F1: the facts of the hostile-kind rule (scan_core.h HostileKindRule). Member reads plus the
		// engine's own Actor::IsInFaction (base NPC, templates and the ref's changes) for the fixed faction sets.
		bool KindFactsImpl(RE::Actor* t, const RE::TESFaction* a_pf, KindFacts* a_k)
		{
			KindFacts&  k = *a_k;
			const auto& fm = g.forms;
			auto*       npc = t->GetNPC();
			auto*       pl = RE::PlayerCharacter::GetSingleton();
			auto        in = [&](const RE::TESFaction* f) { return f && t->IsInFaction(f); };
			k.inPlayerFaction = in(a_pf);
			k.companion = in(fm.currentCompanion) || in(fm.hasBeenCompanion) || in(fm.potentialCompanion);
			k.settler = in(fm.workshopNPC) || (npc && fm.workshopNPC && npc->IsInFaction(fm.workshopNPC));
			k.workshopLink = fm.workshopItem && t->GetLinkedRef(fm.workshopItem) != nullptr;
			for (const auto* f : fm.hostileFactions)
				if (in(f)) {
					k.hostileFaction = true;
					break;
				}
			k.teammate = (t->niFlags.flags & (1u << 26)) != 0;
			k.commanded = t->boolFlags.any(RE::Actor::BOOL_FLAGS::kIsCommandedActor);
			k.crime = t->GetCrimeTrackingFaction() != nullptr;
			k.unique = npc && npc->actorData.actorBaseFlags.all(RE::ACTOR_BASE_DATA::Flag::kUnique);
			k.race = RaceKindOf(t->race);
			k.aggression = npc ? static_cast<int>(npc->aiData.aggression) : 0;
			if (auto* avs = RE::ActorValue::GetSingleton(); avs && avs->aggression) {
				const float av = t->GetActorValue(*avs->aggression);
				if (std::isfinite(av)) k.aggression = std::max(k.aggression, static_cast<int>(av));
			}
			// faction ties: every faction of it x the player's factions (PlayerFaction + the player's own memberships)
			if (a_pf) {
				std::vector<const RE::TESFaction*> mine, plf;
				CollectFactions(npc, t->extraList ? t->extraList->GetByType<RE::ExtraFactionChanges>() : nullptr, mine);
				plf.push_back(a_pf);
				if (pl) CollectFactions(pl->GetNPC(), pl->extraList ? pl->extraList->GetByType<RE::ExtraFactionChanges>() : nullptr, plf);
				for (const auto* fac : mine) {
					for (const auto* pf : plf) {
						if (fac == pf) {
							k.inPlayerFaction = true;
							continue;
						}
						for (int s = 0; s < 2; ++s) {
							const int r = s == 0 ? ReactionOf(fac, pf) : ReactionOf(pf, fac);
							if (r == static_cast<int>(RE::FIGHT_REACTION::kEnemy)) k.enemyTie = true;
							else if (r == static_cast<int>(RE::FIGHT_REACTION::kAlly) || r == static_cast<int>(RE::FIGHT_REACTION::kFriend)) k.friendTie = true;
							else if (r == static_cast<int>(RE::FIGHT_REACTION::kNeutral)) k.neutralTie = true;
						}
					}
				}
			}
			return true;
		}
		bool SehKindFacts(RE::Actor* t, const RE::TESFaction* a_pf, KindFacts* a_k) noexcept
		{
			__try { return KindFactsImpl(t, a_pf, a_k); } __except (1) { return false; }
		}
		// The hostile-kind verdict (HostileKindRule over KindFacts); defined after the log helpers (SPN|SCAN|kind).
		bool SehHostileKind(RE::Actor* t, const RE::TESFaction* a_pf, bool* out, double a_now, bool a_log = false);
		bool EssentialOnlyImpl(RE::Actor* t)
		{
			if (t->boolFlags.any(RE::Actor::BOOL_FLAGS::kEssential)) return true;
			const auto* npc = t->GetNPC();
			return npc && npc->IsEssential();
		}
		bool SehEssentialOnly(RE::Actor* t, bool* out) noexcept
		{
			__try { *out = EssentialOnlyImpl(t); return true; } __except (1) { return false; }
		}
		// [?] equipped gun / thrown weapon: MiddleHighProcessData::equippedItems (read without its spin lock: the pump is a
		// serial phase of the frame, C247 overlapPump = 0). Optional: [Tabs] brain.scan.weaponRead = 0 turns it off.
		bool WeaponImpl(RE::Actor* t)
		{
			auto* proc = t->currentProcess;
			auto* mh = proc ? proc->middleHigh : nullptr;
			if (!mh) return false;
			const auto n = std::min<std::uint32_t>(mh->equippedItems.size(), 4);
			for (std::uint32_t i = 0; i < n; ++i) {
				const auto* form = mh->equippedItems[i].item.object;
				const auto* w = form ? form->As<RE::TESObjectWEAP>() : nullptr;
				if (w && (w->IsGunWeapon() || w->IsThrownWeapon())) return true;
			}
			return false;
		}
		bool SehWeapon(RE::Actor* t, bool* out) noexcept
		{
			__try { *out = WeaponImpl(t); return true; } __except (1) { return false; }
		}
		// s11 fix lane 1 (brain.protectBomberR; the s11 gate's soak death 1: a molotov thrower 'MolotovCocktail' 0010C3C6 killed the
		// player at full health in one hit): an equipped explosive / missile weapon. The same equippedItems as WeaponImpl (up to 4:
		// the hands and the grenade slot), then plain form data: a THROWN weapon (WEAPON_TYPE grenade / mine: frag, plasma, pulse,
		// molotov, cryo, mines) or a gun whose projectile has an explosion (the ranged data's override projectile, else its
		// ammo's projectile: missile launcher, Fat Man, grenade rifles). a_form = the weapon found (0 = none). Read only.
		bool ExplosiveImpl(RE::Actor* t, std::uint32_t* a_form, bool* a_thrown)
		{
			*a_form = 0;
			*a_thrown = false;
			auto* proc = t->currentProcess;
			auto* mh = proc ? proc->middleHigh : nullptr;
			if (!mh) return false;
			const auto n = std::min<std::uint32_t>(mh->equippedItems.size(), 4);
			std::array<std::uint8_t, 4>  kind{};  // B2 codex review (S1): every slot classified first, then ExplosiveSlot picks
			std::array<std::uint32_t, 4> forms{};
			for (std::uint32_t i = 0; i < n; ++i) {
				const auto* form = mh->equippedItems[i].item.object;
				const auto* w = form ? form->As<RE::TESObjectWEAP>() : nullptr;
				if (!w) continue;
				// s11 fix integrator: the weapon TYPE is one enum value (WEAP DNAM animation type), not a flag set - CommonLibF4's
				// IsThrownWeapon / IsGunWeapon use TEnumSet::any (a bit test: kGrenade | kMine = 0xB matches every type with bit 0, 1
				// or 3 set), so in game every PipeGun, Board, Baton and turret read as "thrown" (s11f hf / dt runs: 30 / 30 probes,
				// expl=1099 of 1308). Exact values here.
				const auto wt = w->weaponData.type.underlying();
				if (wt == static_cast<std::uint8_t>(RE::WEAPON_TYPE::kGrenade) || wt == static_cast<std::uint8_t>(RE::WEAPON_TYPE::kMine)) {
					kind[i] = 2;
					forms[i] = w->GetFormID();
					break;  // a thrown slot ends the read (the old read stopped at the first explosive slot too)
				}
				if (wt != static_cast<std::uint8_t>(RE::WEAPON_TYPE::kGun)) continue;
				const RE::BGSProjectile* proj = nullptr;
				if (const auto* rd = w->weaponData.rangedData; rd && rd->overrideProjectile) proj = rd->overrideProjectile;
				if (!proj && w->weaponData.ammo) proj = w->weaponData.ammo->data.projectile;
				if (proj && proj->data.explosionType) {
					kind[i] = 1;
					forms[i] = w->GetFormID();
					if (!g.throwerAllSlots || g.explosiveGuns) break;  // the old read: the first explosive slot ends it
				}
			}
			const int at = ExplosiveSlot(std::span<const std::uint8_t>(kind.data(), n), g.explosiveGuns, g.throwerAllSlots, a_thrown);
			if (at < 0) return false;
			*a_form = forms[static_cast<std::size_t>(at)];
			return true;
		}
		bool SehExplosive(RE::Actor* t, bool* out, std::uint32_t* a_form, bool* a_thrown) noexcept
		{
			__try { *out = ExplosiveImpl(t, a_form, a_thrown); return true; } __except (1) { return false; }
		}
		// explore cycle m1 fix lane 1 (brain.scan.suiciderRead; m1 Dunwich: the suicider killed the player 2 of 2 - its bomb is no grenade /
		// mine / exploding gun, so the explosive read never saw it and the brain ranked it like any raider): a super mutant SUICIDER = its
		// SuperMutantSuiciderBomb (Fallout4.esm 0014245D) in the same <= 4 equippedItems, or the suicider combat style csSuperMutantSuicider
		// (0010C64E) on its NPC or on the template that gives it its AI data (TPTA AI data = templateForms[4]; the leveled suicider's
		// template EncSuperMutantTemplateSuicider carries it). a_via 1 = the bomb, 2 = the combat style. Read only (plain form data).
		constexpr std::uint32_t kSuiciderBomb = 0x0014245D;   // WEAP SuperMutantSuiciderBomb (SuperMutantSuiciderScript's bomb property)
		constexpr std::uint32_t kSuiciderStyle = 0x0010C64E;  // CSTY csSuperMutantSuicider
		bool SuiciderImpl(RE::Actor* t, int* a_via)
		{
			*a_via = 0;
			if (auto* proc = t->currentProcess; proc && proc->middleHigh) {
				auto*      mh = proc->middleHigh;
				const auto n = std::min<std::uint32_t>(mh->equippedItems.size(), 4);
				for (std::uint32_t i = 0; i < n; ++i) {
					const auto* form = mh->equippedItems[i].item.object;
					if (form && form->GetFormID() == kSuiciderBomb) {
						*a_via = 1;
						return true;
					}
				}
			}
			const RE::TESNPC* npc = t->GetNPC();
			for (int depth = 0; npc && depth < 5; ++depth) {
				if (const auto* cs = npc->combatStyle; cs && cs->GetFormID() == kSuiciderStyle) {
					*a_via = 2;
					return true;
				}
				if (!npc->UsesTemplate() || !npc->actorData.templateUseFlags.all(RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG::kAIData) || !npc->templateForms) break;
				const RE::TESForm* tf = npc->templateForms[4];  // TPTA: AI DATA (the combat style)
				npc = tf ? tf->As<RE::TESNPC>() : nullptr;
			}
			return false;
		}
		bool SehSuicider(RE::Actor* t, bool* out, int* a_via) noexcept
		{
			__try { *out = SuiciderImpl(t, a_via); return true; } __except (1) { return false; }
		}
		// completion side lane 4 (brain.protectRushMelee; the s8.1f rush rule's in-game misfire 10-21 00:53:26: raiders with guns running
		// up ranked as melee rushers): a MELEE fighter = none of the equipped weapons (the same <= 4 equippedItems as WeaponImpl /
		// ExplosiveImpl) is a gun or a thrown weapon by the EXACT WEAPON_TYPE value (WeaponImpl's IsGunWeapon / IsThrownWeapon are
		// TEnumSet bit tests that read most melee types as ranged - see ExplosiveImpl). Melee types 0-6 (hand to hand, swords, daggers,
		// axes, maces / boards / bats) or no weapon at all (creatures, fists) = melee. a_known = false without process data (no answer).
		// Read only.
		bool MeleeImpl(RE::Actor* t, bool* a_known, std::uint32_t* a_form)
		{
			*a_known = false;
			*a_form = 0;
			auto* proc = t->currentProcess;
			auto* mh = proc ? proc->middleHigh : nullptr;
			if (!mh) return false;
			*a_known = true;
			const auto n = std::min<std::uint32_t>(mh->equippedItems.size(), 4);
			for (std::uint32_t i = 0; i < n; ++i) {
				const auto* form = mh->equippedItems[i].item.object;
				const auto* w = form ? form->As<RE::TESObjectWEAP>() : nullptr;
				if (!w) continue;
				const auto wt = w->weaponData.type.underlying();
				if (wt > static_cast<std::uint8_t>(RE::WEAPON_TYPE::kTwoHandAxe) && wt != static_cast<std::uint8_t>(RE::WEAPON_TYPE::kNone)) return false;  // bow / staff / gun / grenade / mine
				if (wt != static_cast<std::uint8_t>(RE::WEAPON_TYPE::kNone) && *a_form == 0) *a_form = w->GetFormID();
			}
			return true;
		}
		bool SehMelee(RE::Actor* t, bool* out, bool* a_known, std::uint32_t* a_form) noexcept
		{
			__try { *out = MeleeImpl(t, a_known, a_form); return true; } __except (1) { return false; }
		}
		std::uint32_t MountImpl(RE::Actor* pl)
		{
			const RE::ActorHandle h = pl->GetMountHandle();
			const auto            a = h.get();
			return a ? a->GetFormID() : 0u;
		}
		bool SehMount(RE::Actor* pl, std::uint32_t* out) noexcept
		{
			__try { *out = MountImpl(pl); return true; } __except (1) { return false; }
		}
		bool SneakImpl(RE::Actor* pl) { return pl->IsSneaking(); }
		bool SehSneak(RE::Actor* pl, bool* out) noexcept
		{
			__try { *out = SneakImpl(pl); return true; } __except (1) { return false; }
		}
		bool DialogueImpl() { return RE::IsPlayerInDialogue(); }  // engine: real MenuTopicManager menuOpen || dialogue camera

		// The engine's IsPlayerInDialogue (ID 2188686, RVA 0x22D680 in 1.11.240) starts
		//   48 83 EC 28 | 48 8B 05 <disp32> | 80 78 4C 00      sub rsp,28h; mov rax,[MenuTopicManager]; cmp byte [rax+4Ch],0
		// and the rip-relative slot is ID 4796064 (RVA 0x30E6E50). CommonLibF4's MenuTopicManager::Singleton is ID 4796375
		// (RVA 0x30E8B10), another address: in the observe run 20261010-023508-b2-observe the player read as "in dialogue"
		// for the whole session. So the speaker is read through 4796064, and only while the function bytes prove it.
		bool CodeImpl(const std::uint8_t* a_fn, std::uint8_t* a_out, int a_n)
		{
			for (int i = 0; i < a_n; ++i) a_out[i] = a_fn[i];
			return true;
		}
		bool SehCode(const std::uint8_t* a_fn, std::uint8_t* a_out, int a_n) noexcept
		{
			__try { return CodeImpl(a_fn, a_out, a_n); } __except (1) { return false; }
		}
		RE::MenuTopicManager* RealMenuTopicManager()
		{
			static int                    s_state = 0;  // 0 unknown, 1 verified, -1 off
			static RE::MenuTopicManager** s_slot = nullptr;
			if (s_state == 0) {
				s_state = -1;
				const std::uintptr_t fn = REL::ID(2188686).address();
				const std::uintptr_t want = REL::ID(4796064).address();
				std::uint8_t         b[15]{};
				if (SehCode(reinterpret_cast<const std::uint8_t*>(fn), b, 15) && b[0] == 0x48 && b[1] == 0x83 && b[2] == 0xEC && b[3] == 0x28 && b[4] == 0x48 &&
					b[5] == 0x8B && b[6] == 0x05 && b[11] == 0x80 && b[12] == 0x78 && b[13] == 0x4C && b[14] == 0x00) {
					std::int32_t disp = 0;
					std::memcpy(&disp, b + 7, 4);
					const std::uintptr_t target = fn + 11 + static_cast<std::intptr_t>(disp);
					if (target == want) {
						s_slot = reinterpret_cast<RE::MenuTopicManager**>(target);
						s_state = 1;
					}
					REX::INFO("SPN|I|scan: MenuTopicManager slot from IsPlayerInDialogue = {:X} (ID 4796064 = {:X}, CommonLibF4 ID 4796375 = {:X}) -> speaker check {}", target,
						want, REL::ID(4796375).address(), s_state == 1 ? "on" : "OFF");
				} else {
					REX::WARN("SPN|W|scan: IsPlayerInDialogue bytes unexpected; dialogue speaker check off (IsPlayerInDialogue still used)");
				}
			}
			return s_state == 1 && s_slot ? *s_slot : nullptr;
		}
		bool SehDialogue(bool* out) noexcept
		{
			__try { *out = DialogueImpl(); return true; } __except (1) { return false; }
		}
		bool CellXYImpl(RE::TESObjectCELL* c, int* x, int* y)
		{
			*x = c->GetDataX();
			*y = c->GetDataY();
			return true;
		}
		bool SehCellXY(RE::TESObjectCELL* c, int* x, int* y) noexcept
		{
			__try { return CellXYImpl(c, x, y); } __except (1) { return false; }
		}
		RE::Actor* FormActorImpl(std::uint32_t id)
		{
			auto* f = RE::TESForm::GetFormByID(id);
			return f ? f->As<RE::Actor>() : nullptr;
		}
		bool SehFormActor(std::uint32_t id, RE::Actor** out) noexcept
		{
			__try { *out = FormActorImpl(id); return true; } __except (1) { return false; }
		}
		// G2 cross-check (observe mode only): the engine's own "hostile actors near" list. Copies <= a_cap handles.
		int HostileNearImpl(RE::ProcessLists* a_lists, RE::ActorHandle* a_out, int a_cap)
		{
			RE::BSScrapArray<RE::ActorHandle> arr;
			(void)a_lists->AreHostileActorsNear(&arr);
			int k = 0;
			for (std::uint32_t i = 0; i < arr.size() && k < a_cap; ++i) a_out[k++] = arr[i];
			return static_cast<int>(arr.size());
		}
		int SehHostileNear(RE::ProcessLists* a_lists, RE::ActorHandle* a_out, int a_cap) noexcept
		{
			__try { return HostileNearImpl(a_lists, a_out, a_cap); } __except (1) { return -1; }
		}
		// Copy handles [a_first, a_first + a_n) of a process list (bounds-checked against size(); SAFETY G2).
		int CopyImpl(const RE::BSTArray<RE::ActorHandle>* a_list, std::uint32_t a_first, int a_n, RE::ActorHandle* a_out)
		{
			const std::uint32_t size = a_list->size();
			int                 k = 0;
			for (std::uint32_t i = a_first; i < size && k < a_n; ++i) a_out[k++] = (*a_list)[i];
			return k;
		}
		int SehCopy(const RE::BSTArray<RE::ActorHandle>* a_list, std::uint32_t a_first, int a_n, RE::ActorHandle* a_out) noexcept
		{
			__try { return CopyImpl(a_list, a_first, a_n, a_out); } __except (1) { return -1; }
		}
		std::uint32_t SizeImpl(const RE::BSTArray<RE::ActorHandle>* a_list) { return a_list->size(); }
		bool          SehSize(const RE::BSTArray<RE::ActorHandle>* a_list, std::uint32_t* out) noexcept
		{
			__try { *out = SizeImpl(a_list); return true; } __except (1) { return false; }
		}

		// ------------------------------------------------ bones (static names: one string-pool lookup per session)
		const RE::BSFixedString& ChestName()
		{
			static const RE::BSFixedString s{ "Chest" };
			return s;
		}
		const RE::BSFixedString& HeadName()
		{
			static const RE::BSFixedString s{ "HEAD" };
			return s;
		}
		bool BonesImpl(RE::Actor* t, Vec3* chest, Vec3* head, double* height, bool* haveChest, bool* haveHead)
		{
			const RE::NiPoint3 feet = t->GetPosition();
			auto*              root = t->Get3D();
			*haveChest = *haveHead = false;
			if (root) {
				if (auto* n = root->GetObjectByName(ChestName())) {
					const auto& p = n->world.translate;
					*chest = { p.x, p.y, p.z };
					*haveChest = true;
				}
				if (auto* n = root->GetObjectByName(HeadName())) {
					const auto& p = n->world.translate;
					*head = { p.x, p.y, p.z };
					*haveHead = true;
				}
			}
			const double bTop = (root && std::isfinite(root->worldBound.center.z) && root->worldBound.fRadius > 1.0f && root->worldBound.fRadius < 1.0e5f) ?
			                        static_cast<double>(root->worldBound.center.z + root->worldBound.fRadius) :
			                        feet.z + 128.0;
			if (!*haveChest) {
				*chest = { feet.x, feet.y, feet.z + 100.0 };  // actors::GetChestPos fallback
				if (root && std::isfinite(root->worldBound.center.z) && root->worldBound.fRadius > 60.0f) chest->z = std::max(chest->z, static_cast<double>(root->worldBound.center.z));
			}
			if (!*haveHead) *head = { chest->x, chest->y, std::max(chest->z + 20.0, std::min(bTop, chest->z + 60.0)) };
			*height = std::max(10.0, head->z - feet.z);
			return true;
		}
		bool SehBones(RE::Actor* t, Vec3* chest, Vec3* head, double* height, bool* haveChest, bool* haveHead) noexcept
		{
			__try { return BonesImpl(t, chest, head, height, haveChest, haveHead); } __except (1) { return false; }
		}
		// R6 step 4 (PinKeep): the same live checks as the per-frame refresh, inside a shell (CRASH SHIELD: a fault turns the
		// pin off for the session, the D2f keep list goes on). a_root: its 3D for the rays' actor pass.
		bool PinReadImpl(RE::Actor* t, Vec3* feet, RE::NiAVObject** root)
		{
			if (!t || t->IsDeleted() || t->IsDisabled() || t->lifeState != 0 || !t->parentCell || !t->parentCell->IsAttached()) return false;
			*root = t->Get3D();
			if (!*root) return false;
			const auto p = t->GetPosition();
			*feet = Vec3{ p.x, p.y, p.z };
			return true;
		}
		bool SehPinRead(RE::Actor* t, Vec3* feet, RE::NiAVObject** root, bool* ok) noexcept
		{
			__try { *ok = PinReadImpl(t, feet, root); return true; } __except (1) { return false; }
		}

		// ------------------------------------------------ helpers
		bool IsTeammate(const RE::Actor* a) { return a && (a->niFlags.flags & (1u << 26)) != 0; }

		std::uint32_t WorldOf(const RE::TESObjectCELL* c)
		{
			if (!c || c->IsInterior()) return 0;
			return c->worldSpace ? c->worldSpace->GetFormID() : 0;
		}

		// V22, read live (member reads): interior = the player's very cell; exterior = the player's worldspace.
		bool SameSpace(const RE::Actor* t)
		{
			if (!t || !t->parentCell) return false;
			const auto* pl = RE::PlayerCharacter::GetSingleton();
			const auto* pc = pl ? pl->parentCell : nullptr;
			if (!pc) return false;
			if (pc->IsInterior()) return t->parentCell == pc;
			return !t->parentCell->IsInterior() && WorldOf(t->parentCell) == WorldOf(pc);
		}

		// HOTFIX round5C: per-id log gate (one line per id every a_every s) and a global cap of 8 such lines a second
		bool HfLogDue(std::unordered_map<std::uint32_t, double>& a_at, std::uint32_t a_id, double a_now, double a_every)
		{
			const double sec = std::floor(a_now);
			if (sec != g.hfLogSecond) {
				g.hfLogSecond = sec;
				g.hfLogsThisSecond = 0;
			}
			if (g.hfLogsThisSecond >= 8) return false;
			auto it = a_at.find(a_id);
			if (it != a_at.end() && a_now - it->second < a_every && a_now >= it->second) return false;
			if (a_at.size() > 512) a_at.clear();
			a_at[a_id] = a_now;
			++g.hfLogsThisSecond;
			return true;
		}

		// HOTFIX2 F1: the hostile-kind verdict of one actor (HostileKindRule over its KindFacts). false = the read faulted (no
		// verdict). Logged once per actor every 30 s: SPN|SCAN|kind <id> kind= race= hf= enemy= friend= ...
		bool SehHostileKind(RE::Actor* t, const RE::TESFaction* a_pf, bool* out, double a_now, bool a_log)
		{
			KindFacts k;
			if (!t || !SehKindFacts(t, a_pf, &k)) return false;
			*out = HostileKindRule(k, g.p);
			++g.kindReads;
			if (*out) ++g.kindTrue;
			if (a_log && HfLogDue(g.kindLogAt, t->GetFormID(), a_now, 30.0))
				REX::INFO("SPN|SCAN|kind {:08X} kind={} race={} hostileFaction={} enemy={} friend={} neutral={} playerFaction={} companion={} settler={} link={} crime={} unique={} "
				          "teammate={} agg={} rule={} n={}/{}",
					t->GetFormID(), *out, static_cast<int>(k.race), k.hostileFaction, k.enemyTie, k.friendTie, k.neutralTie, k.inPlayerFaction, k.companion, k.settler,
					k.workshopLink, k.crime, k.unique, k.teammate, k.aggression, g.p.hostileKindRace ? "race+faction" : "round5C", g.kindTrue, g.kindReads);
			return true;
		}

		Entry* FindEntry(std::uint32_t id)
		{
			auto it = g.index.find(id);
			return it == g.index.end() ? nullptr : &g.entries[it->second];
		}

		Entry* FindOrAdd(std::uint32_t id)
		{
			if (auto* e = FindEntry(id)) return e;
			if (g.entries.size() >= 256) return nullptr;  // a crowd beyond 256 known actors: the rest wait for a prune
			g.index[id] = g.entries.size();
			auto& e = g.entries.emplace_back();
			e.f.id = id;
			return &e;
		}

		void Reindex()
		{
			g.index.clear();
			for (std::size_t i = 0; i < g.entries.size(); ++i) g.index[g.entries[i].f.id] = i;
		}

		void LoadParams()
		{
			Params& p = g.p;
			p.scanEvery = std::clamp(Tunable("brain.scanEvery", 0.1), 0.02, 1.0);
			p.examine = static_cast<int>(std::clamp(Tunable("brain.scanExamine", 64.0), 8.0, 256.0));
			p.cap = static_cast<int>(std::clamp(Tunable("brain.cap", 24.0), 1.0, 48.0));
			p.hostileAsks = static_cast<int>(std::clamp(Tunable("brain.hostileAsksPerScan", 8.0), 1.0, 64.0));
			p.hostileTtl = std::clamp(Tunable("brain.hostileRecheck", 1.0), 0.1, 10.0);
			p.nonHostileTtl = std::clamp(Tunable("brain.hostileRecheckNo", 2.0), 0.1, 10.0);  // B1 Params name
			// DPREP-T (G8): 0 = round 5 B
			p.combatFlipRecheck = Tunable("brain.hostileFlipRecheck", 0.0) != 0.0;
			p.nonHostileTtlCombat = std::clamp(Tunable("brain.hostileRecheckNoCombat", 0.0), 0.0, 10.0);
			p.combatRecheckR = std::clamp(Tunable("brain.hostileRecheckNoCombatR", 3000.0), 0.0, 20000.0);
			p.staticTtl = std::clamp(Tunable("brain.scan.staticTtl", 5.0), 0.5, 60.0);
			const double leash = Tunable("brain.leashExtra", 642.0);
			p.scanRange = std::clamp(Tunable("brain.scan.range", g.grid.maxR + leash), 500.0, 20000.0);
			// ROUND3: the first-strike keep reach follows the Hunt mode (FP): idle hostiles / hostiles busy with someone else.
			// The scan does not know about Wait mode; the brain forces range 0 while waiting, so extra kept actors get no tier.
			{
				const int  huntRange = std::clamp(static_cast<int>(std::lround(Tunable("brain.range", 1.0))), 0, 2);
				const auto pre = sp::brain::PreemptReaches(huntRange, Tunable("brain.rangedGuard", 2054.0), Tunable("brain.preemptFar", 4096.0),
					Tunable("brain.preemptFarBusy", 6144.0), Tunable("brain.preemptAnywhere", 8192.0), Tunable("brain.preemptAnywhereBusy", 12288.0),
					g.grid.farR, g.grid.maxR);
				p.preemptKeep = std::clamp(pre.idle + leash, 0.0, 20000.0);
				p.preemptKeepBusy = std::clamp(pre.busy + leash, 0.0, 20000.0);
			}
			p.protectedTiers = Tunable("brain.protectedTiers", 0.0) != 0.0;
			p.bystanderRange = std::clamp(Tunable("brain.scan.bystanderRange", 2000.0), 0.0, 10000.0);
			p.bystanderCap = static_cast<int>(std::clamp(Tunable("brain.scan.bystanderCap", 24.0), 0.0, 48.0));
			p.attackerValid = Tunable("brain.attackerValid", 3.0);
			p.hitMemory = Tunable("brain.hitMemory", 5.0);
			p.foughtMemory = std::clamp(Tunable("brain.foughtMemory", 30.0), 0.0, 600.0);  // ROUND5
			p.quarrelFor = Tunable("brain.quarrelFor", 30.0);
			p.hitForget = std::max(p.hitMemory, Tunable("brain.scan.hitForget", 12.0));
			p.v09Mode = static_cast<int>(Tunable("brain.v09Mode", 2.0));  // P3: hostiles fighting the player carry kAngryWithPlayer
			p.midHighTargets = Tunable("brain.midHighTargets", 0.0) != 0.0;
			p.requireCombat = Tunable("brain.requireCombat", 0.0) != 0.0;
			p.bigRaceSize = static_cast<int>(Tunable("punch.big.minRaceSize", 2.0));
			p.rangedHitDist = Tunable("brain.rangedHitDist", 400.0);
			// [?] GetOwner may fall back to the CELL owner (a raider inside a player-owned settlement would read as
			// "player owned"): off by default; settlers / workshop robots / turrets are covered by the faction and the
			// WorkshopItemKeyword link.
			g.ownerCheck = Tunable("brain.scan.ownerCheck", 0.0) != 0.0;
			// HOTFIX round5C (A / B / E): every rule on by default (the shipped fix), each one can be rolled back
			p.defend = Tunable("brain.defendRule", 1.0) != 0.0;
			p.defendWindow = std::clamp(Tunable("brain.defendWindow", 3.0), 0.0, 30.0);
			p.defendCombat = Tunable("brain.defendCombat", 1.0) != 0.0;
			p.defendMidHigh = Tunable("brain.defendMidHigh", 1.0) != 0.0;
			p.defendTurretLink = Tunable("brain.defendTurretLink", 1.0) != 0.0;
			p.quarrelHostileKind = Tunable("brain.quarrelHostileKind", 1.0) != 0.0;
			p.defendReask = Tunable("brain.defendReask", 1.0) != 0.0;
			// HOTFIX2 (in-game smoke F1 / F3): every rule on by default, each one can be rolled back
			p.hostileKindRace = Tunable("brain.hostileKindRace", 1.0) != 0.0;
			p.watch = Tunable("brain.watch", 1.0) != 0.0;
			p.watchBudget = static_cast<int>(std::clamp(Tunable("brain.watchBudget", 64.0), 8.0, 256.0));
			p.watchAsks = static_cast<int>(std::clamp(Tunable("brain.watchAsks", 4.0), 0.0, 16.0));
			p.watchAskEvery = std::clamp(Tunable("brain.watchAskEvery", 0.1), 0.0, 2.0);
			p.provokedKindFoe = Tunable("brain.provokedKindFoe", 1.0) != 0.0;  // s4 fix lane 2 (P03)
			g_provOn.store(p.provokedKindFoe, std::memory_order_relaxed);
			p.provokedFight = Tunable("brain.provokedFight", 0.0) != 0.0;  // s11 fix lane 3 (P03; the brain reads the same key)
			p.turnedCompanion = Tunable("brain.turnedCompanion", 0.0) != 0.0 && !g_tcOff.load(std::memory_order_relaxed);  // completion side lane 3 (user rule c: a turned companion is a foe)
			g.weaponRead = Tunable("brain.scan.weaponRead", 1.0) != 0.0;
			g.explosiveRead = Tunable("brain.scan.explosiveRead", 1.0) != 0.0;  // s11 fix lane 1
			g.explosiveTtl = std::clamp(Tunable("brain.scan.explosiveTtl", 0.0), 0.0, 60.0);  // completion lane 1
			g.explosiveKeep = std::clamp(Tunable("brain.scan.explosiveKeep", 0.0), 0.0, 30.0);  // s13 r1 fix lane 3 (merged)
			g.meleeRead = Tunable("brain.scan.meleeRead", 1.0) != 0.0;  // completion side lane 4 (brain.protectRushMelee)
			// explore cycle m1 fix lane 1 (all off at their code defaults = the s13c scan): the turned-ex-teammate memory, guns with an
			// exploding projectile as gunners, the super mutant suicider read
			p.turnedAllyMem = std::clamp(Tunable("brain.turnedAllyMem", 0.0), 0.0, 600.0);
			g.explosiveGuns = Tunable("brain.scan.explosiveGuns", 1.0) != 0.0;
			g.throwerAllSlots = Tunable("brain.scan.throwerAllSlots", 0.0) != 0.0;  // B2 codex review (S1)
			g.suiciderRead = Tunable("brain.scan.suiciderRead", 0.0) != 0.0;
		}

		// ------------------------------------------------ the player's facts (once per scan)
		void ReadPlayer(RE::Actor* a_me, double a_now)
		{
			PlayerFacts  pf;
			auto*        pl = RE::PlayerCharacter::GetSingleton();
			if (!pl || !pl->parentCell) {
				g.pl = pf;
				return;
			}
			pf.valid = true;
			pf.feet = sp::fo4::ToVec(pl->GetPosition());
			pf.chest = sp::fo4::ToVec(sp::fo4::GetChestPos(pl));
			if (g.pl.valid) pf.vel = g.pl.vel;  // per-frame smoothed (Refresh)
			else if (g.plPrevAt > -1e8 && a_now > g.plPrevAt) pf.vel = (pf.feet - g.plPrevFeet) / (a_now - g.plPrevAt);
			g.plPrevFeet = pf.feet;
			g.plPrevAt = a_now;
			bool ok = true, b = false;
			if (SehSneak(pl, &b)) pf.sneaking = b; else ok = false;
			if (SehInCombat(pl, &b)) pf.inCombat = b; else ok = false;
			if (SehDialogue(&b)) pf.inDialogue = b; else ok = false;
			// the speaker (V16): through the engine's own MenuTopicManager slot, never RE::MenuTopicManager::GetSingleton()
			if (auto* mtm = RealMenuTopicManager(); mtm && mtm->menuOpen) {
				pf.menuTopicOpen = true;
				if (const auto spk = mtm->speaker.get()) pf.speakerId = spk->GetFormID();
			}
			if (SehScene(pl, &b)) pf.inScene = b; else ok = false;
			std::uint32_t mount = 0;
			if (SehMount(pl, &mount)) pf.mountId = mount; else ok = false;
			pf.mounted = mount != 0;
			pf.killMove = pl->boolFlags.any(RE::Actor::BOOL_FLAGS::kIsInKillMove);
			// completion side lane 4 s13r2 (brain.preemptAimKeep): the player's weapon drawn - an inline read of ActorState::weaponState on
			// the live player (no engine call, like boolFlags above)
			pf.weaponDrawn = pl->GetWeaponMagicDrawn();
			if (auto* v = RE::VATS::GetSingleton()) pf.vatsPlayback = v->mode.any(RE::VATS::VATS_MODE_ENUM::kPlayback);
			if (auto* ui = RE::UI::GetSingleton()) {
				static const RE::BSFixedString s_workshop{ "WorkshopMenu" };
				pf.workshop = ui->GetMenuOpen(s_workshop);
			}
			auto* cell = pl->parentCell;
			pf.interior = cell->IsInterior();
			pf.cellId = cell->GetFormID();
			pf.worldId = WorldOf(cell);
			if (!pf.interior) {
				int x = 0, y = 0;
				if (SehCellXY(cell, &x, &y)) {
					pf.cellX = x;
					pf.cellY = y;
				} else {
					ok = false;
				}
			}
			(void)a_me;
			if (!ok) Fault("player facts");
			g.pl = pf;
		}

		// ------------------------------------------------ facts of one examined actor
		// Cheap: member reads plus two light engine calls (IsInCombat vfunc, the combat-target handle). Returns true when
		// the actor may need a hostility ask (it passed the cheap vetoes).
		bool FillCheap(Entry& e, RE::Actor* t, RE::Actor* a_me, const Vec3& a_centre, std::uint8_t a_list, double a_now)
		{
			auto& f = e.f;
			auto* pl = RE::PlayerCharacter::GetSingleton();
			const bool       prevInCombat = f.inCombat;  // DPREP-T (G8): what the last scan saw
			const CombatKind prevKind = f.combatKind;
			f.list = a_list;
			f.processLevel = t->currentProcess ? t->currentProcess->processLevel : 255;
			f.midHigh = a_list != 0 || !t->currentProcess || !t->currentProcess->high;
			f.loaded = !t->IsDisabled() && !t->IsDeleted() && t->Get3D() && t->parentCell && t->parentCell->IsAttached() &&
			           !t->boolFlags.any(RE::Actor::BOOL_FLAGS::kMovingIntoLoadedArea);
			f.sameSpace = SameSpace(t);
			const Vec3 prev = f.feet;
			const double prevAt = e.lastExamined;
			f.feet = sp::fo4::ToVec(t->GetPosition());
			if (prevAt > -1e8 && a_now > prevAt + 1e-3 && a_now - prevAt < 1.0) f.vel = (f.feet - prev) / (a_now - prevAt);
			f.chest = f.feet + f.chestOff;
			f.head = f.feet + f.headOff;
			f.distCentre = GuardDist(f.feet, a_centre);
			f.distMe = a_me ? Length(f.feet - sp::fo4::ToVec(a_me->GetPosition())) : 0.0;
			f.down = t->lifeState != 0;
			f.knocked = t->knockState != 0;
			f.teammate = IsTeammate(t);
			NoteAlly(f, a_now);  // explore cycle m1 fix lane 1 (brain.turnedAllyMem): the teammate memory (scan_core TurnedAlly)
			f.commanded = t->boolFlags.any(RE::Actor::BOOL_FLAGS::kIsCommandedActor);
			f.angry = t->boolFlags.any(RE::Actor::BOOL_FLAGS::kAngryWithPlayer);
			const bool prevKillMove = f.killMove;  // R6 step 3: the bit-14 edge (probe km)
			f.killMove = t->boolFlags.any(RE::Actor::BOOL_FLAGS::kIsInKillMove);
			f.underwater = t->boolFlags.any(RE::Actor::BOOL_FLAGS::kUnderwater);
			const bool sceneFlags = t->boolFlags.any(RE::Actor::BOOL_FLAGS::kScenePackage, RE::Actor::BOOL_FLAGS::kInRandomScene);
			f.speaker = g.pl.speakerId != 0 && g.pl.speakerId == f.id;
			f.weaponDrawn = t->GetWeaponMagicDrawn();
			if (const auto* r = t->race) {
				f.raceSize = static_cast<int>(r->data.raceSize);
				f.child = (r->data.flags & 0x4) != 0;  // ROUND3 fix 2: RACE DATA flag Child (the native twin of Papyrus Unfit's child rule)
				f.immobile = (r->data.flags & 0x200) != 0;  // R6 step 3 (HOOKS s3 B3): Immobile (turrets); a fact for the Warden only
			}
			// R6 step 3 (HOOKS s3 B4): the kill-move partner, read ONLY while bit 14 is set (+0x384 also holds the killer at death)
			{
				const std::uint32_t prevPartner = f.killMovePartner;
				f.killMovePartner = 0;
				f.killMoveVictim = false;
				if (f.killMove && g.r6KillRole) {
					if (const auto p = t->myKiller.get()) {
						f.killMovePartner = p->GetFormID();
						f.killMoveVictim = true;
					}
				}
				if ((f.killMove != prevKillMove || (f.killMove && f.killMovePartner != prevPartner)) && g.r6Probe && g.r6KmLines < 200) {
					++g.r6KmLines;
					REX::INFO("SPN|R6|PROBE|km id={:08X} bit14={} partner={} role={} playerKm={}", f.id, f.killMove ? 1 : 0,
						f.killMovePartner ? std::format("{:08X}", f.killMovePartner) : std::string("none"), !f.killMove ? "-" : (f.killMoveVictim ? "victim" : "attacker"),
						(pl && f.killMovePartner == pl->GetFormID()) ? 1 : 0);
				}
			}
			if (const auto* o = t->Get3D(); o && std::isfinite(o->worldBound.fRadius)) f.boundR = o->worldBound.fRadius;
			// quarrel / attacker facts from the tables
			f.quarrel = g.quarrels.Has(f.id, a_now);
			if (const auto* ar = g.attackers.Get(f.id)) {
				f.lastHitPlayerAt = ar->lastHitPlayerAt;
				f.lastHitMeAt = ar->lastHitMeAt;
				f.lastHitTeammateAt = ar->lastHitTeammateAt;
				f.hitDist = ar->hitDist;
			}
			const bool cheapOk = f.loaded && f.sameSpace && !f.down && !f.teammate && !f.commanded && f.distCentre <= g.p.scanRange;
			if (!cheapOk) {
				f.inCombat = false;
				f.combatKind = CombatKind::kNone;
				f.scene = sceneFlags;
				return false;
			}
			bool b = false;
			if (SehInCombat(t, &b)) f.inCombat = b; else Fault("IsInCombat");
			f.combatKind = CombatKind::kNone;
			if (t->currentCombatTarget) {
				if (const auto ct = t->currentCombatTarget.get()) {
					const RE::Actor* c = ct.get();
					if (pl && c == pl) f.combatKind = CombatKind::kPlayer;
					else if (c == a_me || sp::fo4::IsPeasant(c)) f.combatKind = CombatKind::kHim;
					else if (IsTeammate(c)) f.combatKind = CombatKind::kTeammate;
					else f.combatKind = CombatKind::kOther;
				}
			}
			if (f.CombatWithGroup()) {  // ROUND5: the fought-the-group memory (KeepRank / ToCand foughtMemory)
				f.groupCombatAt = a_now;
				f.groupCombatPlayer = f.combatKind == CombatKind::kPlayer;
			}
			f.scene = sceneFlags;  // GetCurrentScene() is asked later, only for hostile actors not in combat
			if (CombatFlipRecheck(f, prevInCombat, prevKind, g.p)) ++g.st.flipAsks;  // DPREP-T (G8): asked first in this scan
			return true;
		}

		void ReadExplosive(Entry& e, RE::Actor* t, double a_now);  // completion lane 1 (below)
		void ReadMelee(Entry& e, RE::Actor* t);                    // completion side lane 4 (below)

		// Factions, flags, alias, gun: hostile actors only, every staticTtl.
		void FillStatic(Entry& e, RE::Actor* t, double a_now)
		{
			auto& f = e.f;
			auto* npc = t->GetNPC();
			auto* pl = RE::PlayerCharacter::GetSingleton();
			bool  b = false, settler = false, companion = false, link = false;
			const auto& fm = g.forms;
			if (fm.workshopNPC) {
				if (SehInFaction(t, fm.workshopNPC, &b)) settler |= b; else Fault("IsInFaction(workshop)");
				if (npc && SehBaseInFaction(npc, fm.workshopNPC, &b)) settler |= b;
			}
			if (fm.workshopItem) {
				if (SehLinked(t, fm.workshopItem, &b)) link |= b; else Fault("GetLinkedRef");
			}
			// R6 step 3 (HOOKS s3 B7, N07): the owner of the linked WORKBENCH (GetOwner 2202616: no cell fallback); a fault
			// switches this read off for the session (playerWorkshop false: N08 still protects the player's own machines)
			f.playerWorkshop = false;
			if (link && fm.workshopItem && g.r6Workshop) {
				RE::TESForm* o = nullptr;
				if (SehWorkshopOwner(t, fm.workshopItem, &o)) {
					f.workshopOwner = o ? o->GetFormID() : 0u;
					f.playerWorkshop = o && ((fm.playerFaction && o == fm.playerFaction) || (pl && o == pl->GetNPC()));
					if (g.r6Probe && g.r6WsLines < 100 && !g.r6WsSeen.contains(f.id)) {
						g.r6WsSeen.insert(f.id);
						++g.r6WsLines;
						REX::INFO("SPN|R6|PROBE|ws id={:08X} owner={} playerWs={} hostile={}", f.id, f.workshopOwner ? std::format("{:08X}", f.workshopOwner) : std::string("none"),
							f.playerWorkshop ? 1 : 0, (f.hostileKnown && f.hostile) ? 1 : 0);
					}
				} else {
					g.r6Workshop = false;
					REX::ERROR("SPN|R6|off feat=percept.workshop why=seh id={:08X} (playerWorkshop false for this session)", f.id);
				}
			}
			if (g.ownerCheck) {
				RE::TESForm* owner = nullptr;
				if (SehOwner(t, &owner)) {
					if (owner && ((fm.playerFaction && owner == fm.playerFaction) || (pl && owner == pl->GetNPC()))) settler = true;
				} else {
					Fault("GetOwner");
				}
			}
			// HOTFIX round5C: the settler NPC (faction / owner) and the workshop link are kept apart (the defend rule lifts the
			// link of a HOSTILE turret / robot, never a settler); `settler` stays their OR (V08 as before)
			f.settlerNpc = settler;
			f.workshopLink = link;
			settler = settler || link;
			if (fm.currentCompanion) {
				if (SehInFaction(t, fm.currentCompanion, &b)) companion |= b; else Fault("IsInFaction(companion)");
			}
			if (fm.hasBeenCompanion) {
				if (SehInFaction(t, fm.hasBeenCompanion, &b)) companion |= b; else Fault("IsInFaction(hasBeenCompanion)");
			}
			f.settler = settler;
			f.companion = companion;
			if (SehCrime(t, &b)) f.crime = b; else Fault("GetCrimeTrackingFaction");
			f.essential = t->boolFlags.any(RE::Actor::BOOL_FLAGS::kEssential, RE::Actor::BOOL_FLAGS::kProtected) || (npc && (npc->IsEssential() || npc->IsProtected()));
			if (SehEssentialOnly(t, &b)) f.essentialOnly = b; else f.essentialOnly = f.essential;  // HOTFIX round5C C
			if (SehHostileKind(t, fm.playerFaction, &b, a_now)) {  // HOTFIX round5C B (HOTFIX2 F1: race + faction rule)
				f.hostileKind = b;
				f.hostileKindAt = a_now;
			}
			f.unique = npc && npc->actorData.actorBaseFlags.all(RE::ACTOR_BASE_DATA::Flag::kUnique);
			bool ghost = npc && npc->actorData.actorBaseFlags.all(RE::ACTOR_BASE_DATA::Flag::kInvulnerable);
			if (npc && !ghost) {
				if (SehGhost(npc, &b)) ghost = b; else Fault("GetIsGhost");
			}
			f.ghost = ghost;
			if (SehAlias(t, &b)) f.alias = b; else Fault("ExtraAliasInstanceArray");
			if (g.weaponRead) {
				if (SehWeapon(t, &b)) {
					f.rangedWeapon = b;
					f.rangedKnown = true;
				} else {
					f.rangedKnown = false;  // optional read: its fault is logged but does not trip the breaker
					static int s_wf = 0;
					if (s_wf++ < 3) REX::WARN("SPN|W|scan: equipped-weapon read faulted on {:08X} (ranged class from hits only)", f.id);
				}
			}
			ReadExplosive(e, t, a_now);  // (completion lane 1: its own function, also on brain.scan.explosiveTtl)
			f.staticAt = a_now;
			++g.st.staticReads;
		}

		// s11 fix lane 1 (brain.protectBomberR): the explosive-weapon read. CRASH SHIELD: a fault switches this read off for the
		// session (one line + a black-box / bookmark dump); every actor counts as not explosive then (R6b never acts) and the
		// game keeps running.
		// completion lane 1 (brain.scan.explosiveTtl): also re-read every explosiveTtl s for the hostiles the scan examines - with the
		// static read alone (staticTtl 5 s) the s11f gate's GK3 thrower FF0016AC was read 4 s after his first pick, and the bomber rule
		// never saw it (the same 0.5-5 s lag lets a raider that equips its molotov mid-fight throw it first)
		void ReadExplosive(Entry& e, RE::Actor* t, double a_now)
		{
			auto& f = e.f;
			bool  b = false;
			// s13 r1 fix lane 3 (brain.scan.explosiveKeep, merged): one found stays a thrower this long - a raider that just threw its
			// molotov has the next one (0 = the last read only: the s11 / completion-lane-1 behaviour)
			const bool kept = g.explosiveKeep > 0 && e.explSeenAt > -1e8 && a_now >= e.explSeenAt && a_now - e.explSeenAt <= g.explosiveKeep;
			e.explAt = a_now;
			f.explosiveWeapon = false;
			if (g.explosiveRead && !g.explosiveOff) {
				std::uint32_t wf = 0;
				bool          thrown = false;
				if (SehExplosive(t, &b, &wf, &thrown)) {
					// explore cycle m1 fix lane 1 (brain.scan.explosiveGuns 0; EXPLORE cyclem1 A7 / rank 10: the thrower rule took him to
					// missile-launcher raiders - MissileLauncher 0003F6F8, 'thrown=false' - 1052 u away while others hit the player): a gun
					// whose projectile explodes is a GUNNER for the thrower rule (only grenades / mines / molotovs are throwers); its probe
					// line stays (gunner=1)
					const bool counts = ExplosiveCounts(b, thrown, g.explosiveGuns);
					if (b && !counts) ++g.explosiveGunSkips;
					f.explosiveWeapon = counts || kept;
					++g.explosiveReads;
					if (counts) {
						e.explSeenAt = a_now;
						++g.explosiveTrue;
					}
					if (b && g.seenExpl.size() < 30 && g.seenExpl.insert(f.id).second)
						REX::INFO("SPN|PROBE|EXPL {:08X} weapon={:08X} thrown={} inCombat={} combat={} d={:.0f}{}", f.id, wf, thrown, f.inCombat, static_cast<int>(f.combatKind), f.distCentre,
							counts ? "" : " gunner=1");
				} else {
					g.explosiveOff = true;
					REX::ERROR("SPN|SCAN|off feat=explosiveRead why=seh actor={:08X} (the explosive-weapon read is off for this session: no bomber rule; the game keeps running)", f.id);
					sp::runtime::RequestDump("scan explosiveRead crash shield");
				}
			}
			// explore cycle m1 fix lane 1 (brain.scan.suiciderRead): the super mutant suicider read, same cadence. CRASH SHIELD: a fault
			// switches only this read off for the session (one line + a bookmark-style dump; no suicider is first then - the s13c rank)
			// and the game keeps running. Once found, it stays a suicider (the bomb is unequipped only by its death)
			f.suicider = e.suicSeen;
			if (g.suiciderRead && !g.suiciderOff && !e.suicSeen) {
				bool s = false;
				int  via = 0;
				if (SehSuicider(t, &s, &via)) {
					++g.suiciderReads;
					if (s) {
						e.suicSeen = true;
						f.suicider = true;
						++g.suiciderTrue;
						if (g.seenSuic.size() < 30 && g.seenSuic.insert(f.id).second)
							REX::INFO("SPN|PROBE|EXPL {:08X} weapon={:08X} thrown=false inCombat={} combat={} d={:.0f} suicider=1 via={}", f.id, kSuiciderBomb, f.inCombat,
								static_cast<int>(f.combatKind), f.distCentre, via == 1 ? "bomb" : "style");
					}
				} else {
					g.suiciderOff = true;
					REX::ERROR("SPN|SCAN|off feat=suiciderRead why=seh actor={:08X} (the suicider read is off for this session: suiciders are ranked like any foe; the game keeps running)", f.id);
					sp::runtime::RequestDump("scan suiciderRead crash shield");
				}
			}
			ReadMelee(e, t);  // completion side lane 4 (same cadence: the static read and brain.scan.explosiveTtl)
		}

		// completion side lane 4 (brain.protectRushMelee): the exact melee read. CRASH SHIELD: a fault switches this read off for the
		// session (one line + a black-box / bookmark dump); every actor counts as not melee then (the rush rank never acts - the
		// shipped behaviour before this lane) and the game keeps running. brain.scan.meleeRead 0 = off.
		void ReadMelee(Entry& e, RE::Actor* t)
		{
			auto& f = e.f;
			f.meleeArmed = false;
			if (!g.meleeRead || g.meleeOff) return;
			bool          m = false, known = false;
			std::uint32_t wf = 0;
			if (SehMelee(t, &m, &known, &wf)) {
				if (!known) return;
				++g.meleeReads;
				f.meleeArmed = m && !f.explosiveWeapon;
				if (f.meleeArmed) {
					++g.meleeTrue;
					if (f.hostile && g.seenMelee.size() < 30 && g.seenMelee.insert(f.id).second)
						REX::INFO("SPN|PROBE|MELEE {:08X} weapon={:08X} inCombat={} combat={} d={:.0f}", f.id, wf, f.inCombat, static_cast<int>(f.combatKind), f.distCentre);
				}
			} else {
				g.meleeOff = true;
				REX::ERROR("SPN|SCAN|off feat=meleeRead why=seh actor={:08X} (the melee-weapon read is off for this session: no rush rank; the game keeps running)", f.id);
				sp::runtime::RequestDump("scan meleeRead crash shield");
			}
		}

		void ProbeLines(const ActorFacts& f, double a_now)
		{
			(void)a_now;
			if (!f.hostile) return;
			// P3: kAngryWithPlayer on hostile actors (a raider in combat must NOT carry it, else V09 narrows: brain.v09Mode 2)
			if (f.angry && g.seenP3.size() < 20 && g.seenP3.insert(f.id).second)
				REX::INFO("SPN|PROBE|P3 angry {:08X} inCombat={} combat={} crime={} quarrel={} seenNonHostile={:.1f}s ago d={:.0f}", f.id, f.inCombat, static_cast<int>(f.combatKind), f.crime,
					f.quarrel, a_now - f.lastNonHostileAt, f.distCentre);
			// P7: crime faction / unique / alias / essential among hostiles (must be ~0 for raiders, gunners, super mutants, creatures)
			if ((f.crime || f.unique || f.alias || f.essential) && g.seenP7.size() < 30 && g.seenP7.insert(f.id).second)
				REX::INFO("SPN|PROBE|P7 {:08X} crime={} unique={} alias={} essential={} inCombat={} combat={} race={} d={:.0f}", f.id, f.crime, f.unique, f.alias, f.essential, f.inCombat,
					static_cast<int>(f.combatKind), f.raceSize, f.distCentre);
			// P6: a hostile middle-high actor (the harness picks one to test fling + damage there)
			if (f.midHigh && g.seenP6.size() < 10 && g.seenP6.insert(f.id).second)
				REX::INFO("SPN|PROBE|P6 midHigh {:08X} list={} level={} d={:.0f} inCombat={}", f.id, f.list, f.processLevel, f.distCentre, f.inCombat);
		}

		int KeptIndex(std::uint32_t a_id);
		// ------------------------------------------------ hits (every frame)
		void DrainHits(RE::Actor* a_me, double a_now)
		{
			// BRAIN2 (INTEGRATOR, shadow review SR1-3): one shotgun blast is ~8 TESHitEvents on one target. Records 5-8 found
			// no live-ask budget (4 per frame) and became a quarrel (V11, hands off 30 s, extended by every later hit), so he
			// never attacked the raider the player was fighting. Now one live answer per target per drain is reused (memo),
			// and a record with no budget left waits for the next frame (up to 0.5 s old) instead of failing safe at once.
			constexpr int kDeferCap = 32, kMemo = 16;
			scan::HitRec  buf[scan::HitRing::kCap + kDeferCap];
			int           n = 0;
			for (const auto& d : g.deferred)
				if (n < kDeferCap) buf[n++] = d;
			g.deferred.clear();
			const int drained = hitsink::Drain(buf + n, scan::HitRing::kCap);
			if (drained > 0) n += drained;
			if (n <= 0) return;
			std::uint32_t memoId[kMemo]{};
			bool          memoH[kMemo]{};
			int           memoN = 0;
			const std::int64_t nowUs = NowUs();
			auto*              pl = RE::PlayerCharacter::GetSingleton();
			for (int i = 0; i < n; ++i) {
				const auto&  r = buf[i];
				const double age = std::max(0.0, (nowUs - r.stampUs) / 1.0e6);
				const double at = a_now - std::min(age, 5.0);
				++g.st.hitsDrained;
				if (r.kind != HitKind::kPlayerHit) {
					double dist = -1;
					if (!r.fallback) {
						const double dx = r.causePos[0] - r.targetPos[0], dy = r.causePos[1] - r.targetPos[1], dz = r.causePos[2] - r.targetPos[2];
						dist = std::sqrt(dx * dx + dy * dy + dz * dz);
					}
					g.attackers.Note(r.cause, r.kind, at, dist);
					// Only a real player-victim event may accelerate admission. Logging/fallback events never do.
					if (g.hitAdmissions.Enabled() && r.kind == HitKind::kPlayer && !r.fallback && KeptIndex(r.cause) < 0) {
						auto* known = FindEntry(r.cause);
						const auto knownHandle = known && known->handle ? known->handle.get_handle() : 0u;
						g.hitAdmissions.Note(r, NowUs(), false, knownHandle);
					}
					if (auto* e = FindEntry(r.cause)) {
						if (const auto* ar = g.attackers.Get(r.cause)) {
							e->f.lastHitPlayerAt = ar->lastHitPlayerAt;
							e->f.lastHitMeAt = ar->lastHitMeAt;
							e->f.lastHitTeammateAt = ar->lastHitTeammateAt;
							e->f.hitDist = ar->hitDist;
						}
						// HOTFIX round5C A (Combat Zone 03:33: an arena raider shot the player while the cache still said "not
						// hostile"): an attacker cached as not hostile is asked again now (live-ask budget), else at the next scan
						if (g.p.defendReask && (!e->f.hostileKnown || !e->f.hostile) && a_now - e->f.hostileAt > 0.2) {
							bool done = false;
							for (int k = 0; k < memoN && !done; ++k)
								if (memoId[k] == r.cause) {
									NoteHostile(e->f, memoH[k], a_now);
									done = true;
								}
							RE::Actor* t = nullptr;
							if (!done && g.liveAsksFrame < 4 && pl && SehFormActor(r.cause, &t) && t && t != a_me && t != pl) {
								bool h = false;
								if (SehHostile(t, pl, &h)) {
									NoteHostile(e->f, h, a_now);
									++g.liveAsksFrame;
									++g.st.liveAsks;
									++g.reasks;
									if (memoN < kMemo) {
										memoId[memoN] = r.cause;
										memoH[memoN++] = h;
									}
									done = true;
								}
							}
							if (!done) e->f.hostileKnown = false;  // never-asked first at the next scan
						}
					}
					continue;
				}
				// the player hit r.target: quarrel rule V11 (hostility as known BEFORE the hit). Still on the list: extend it
				// (the player keeps fighting his own quarrel; hands off until it has been quiet for quarrelFor).
				Entry*     e = FindEntry(r.target);
				if (e) e->f.playerHitAt = std::max(e->f.playerHitAt, at);  // HOTFIX2 F3: the watch re-asks it for defendWindow s
				if (g.quarrels.Has(r.target, a_now)) {
					if (!(e && QuarrelExempt(e->f, g.p))) g.quarrels.Add(r.target, a_now + g.p.quarrelFor);  // HOTFIX round5C B
					continue;
				}
				const bool known = e && e->f.hostileKnown;
				bool       live = false, asked = false;
				if (!(known && e->f.hostileAt <= at)) {
					for (int k = 0; k < memoN && !asked; ++k) {
						if (memoId[k] == r.target) {
							live = memoH[k];
							asked = true;
						}
					}
					if (!asked && g.liveAsksFrame < 4 && pl) {
						RE::Actor* t = nullptr;
						if (SehFormActor(r.target, &t) && t && t != a_me && t != pl) {
							bool h = false;
							if (SehHostile(t, pl, &h)) {
								live = h;
								asked = true;
								++g.liveAsksFrame;
								++g.st.liveAsks;
								if (memoN < kMemo) {
									memoId[memoN] = r.target;
									memoH[memoN++] = h;
								}
							}
						}
					} else if (!asked && pl && age < 0.5 && static_cast<int>(g.deferred.size()) < kDeferCap) {
						g.deferred.push_back(r);  // no budget left this frame: decide next frame
						continue;
					}
				}
				if (!IsQuarrel(known, known && e->f.hostile, known ? e->f.hostileAt : 1e18, at, live, asked)) continue;
				// HOTFIX round5C B (Swan 03:43:42): a hostile kind (super mutant, behemoth, raider, creature, turret of a hostile
				// faction ...) that was dormant / not yet hostile when the player shot it is no provoked neutral: no quarrel, and
				// its hostility is asked again at once (it turns on the player within a frame or two).
				bool hk = false;
				if (g.p.quarrelHostileKind && pl) {
					if (e && a_now - e->f.hostileKindAt < g.p.staticTtl) {
						hk = e->f.hostileKind;
					} else {
						RE::Actor* t = nullptr;
						bool       b = false;
						if (SehFormActor(r.target, &t) && t && t != a_me && t != pl && SehHostileKind(t, g.forms.playerFaction, &b, a_now, true)) {
							hk = b;
							if (e) {
								e->f.hostileKind = b;
								e->f.hostileKindAt = a_now;
							}
						}
					}
				}
				if (hk) {
					if (e && !(e->f.hostileKnown && e->f.hostile)) e->f.hostileKnown = false;
					if (g.p.provokedKindFoe) NoteProvoked(r.target, r.stampUs + static_cast<std::int64_t>(g.p.defendWindow * 1.0e6));  // s4 fix lane 2 (P03)
					if (++g.hkNoQuarrel <= 50) REX::INFO("SPN|SAFE|noQuarrel {:08X} (hostile kind: the player's hit makes it a foe, never a protected neutral) n={}", r.target, g.hkNoQuarrel);
					continue;
				}
				if (!g.quarrels.Has(r.target, a_now)) {
					++g.st.quarrels;
					if (g.st.quarrels <= 50) REX::INFO("SPN|SAFE|quarrel {:08X} (the player hit it while not hostile: hands off {:.0f} s) known={} live={}", r.target, g.p.quarrelFor, known, asked);
				}
				g.quarrels.Add(r.target, a_now + g.p.quarrelFor);
				if (e) e->f.quarrel = true;
			}
		}

		// ------------------------------------------------ the scan (every scanEvery)
		void Scan(RE::Actor* a_me, const Vec3& a_centre, double a_now)
		{
			const auto t0 = Clock::now();
			LoadParams();
			ReadPlayer(a_me, a_now);
			auto* pl = RE::PlayerCharacter::GetSingleton();
			auto* lists = RE::ProcessLists::GetSingleton();
			if (!lists || !pl) return;
			std::uint32_t hs = 0, ms = 0;
			if (!SehSize(&lists->highActorHandles, &hs) || !SehSize(&lists->middleHighActorHandles, &ms)) {
				Fault("process list size");
				return;
			}
			hs = std::min<std::uint32_t>(hs, 4096);
			ms = std::min<std::uint32_t>(ms, 4096);
			g.st.high = static_cast<int>(hs);
			g.st.midHigh = static_cast<int>(ms);
			std::size_t first = 0;
			bool        wrapped = false;
			const int   n = g.win.Next(static_cast<std::size_t>(hs) + ms, g.p.examine, first, wrapped);

			// copy the window's handles (it may span the end of high, the middle-high list and wrap to the start)
			std::vector<std::pair<RE::ActorHandle, std::uint8_t>> handles;
			handles.reserve(static_cast<std::size_t>(n));
			{
				RE::ActorHandle tmp[256];
				std::size_t     idx = first;
				int             left = n;
				const std::size_t total = static_cast<std::size_t>(hs) + ms;
				while (left > 0 && total > 0) {
					if (idx >= total) idx = 0;
					const bool          high = idx < hs;
					const auto*         list = high ? &lists->highActorHandles : &lists->middleHighActorHandles;
					const std::uint32_t at = static_cast<std::uint32_t>(high ? idx : idx - hs);
					const std::uint32_t avail = high ? hs - at : ms - at;
					const int           want = std::min<int>({ left, static_cast<int>(avail), 256 });
					const int           got = SehCopy(list, at, want, tmp);
					if (got < 0) {
						Fault("process list copy");
						return;
					}
					for (int k = 0; k < got; ++k) handles.emplace_back(tmp[k], static_cast<std::uint8_t>(high ? 0 : 1));
					if (got < want) break;  // the list shrank under us: next scan
					left -= got;
					idx += static_cast<std::size_t>(got);
				}
			}

			// examine (entries never reallocate inside a scan: <= 256 reserved)
			g.entries.reserve(256);
			struct Ex { std::size_t entry; RE::NiPointer<RE::Actor> a; bool cand; };
			std::vector<Ex> ex;
			ex.reserve(handles.size());
			double maxHigh = 0, maxMid = 0;
			int    highFar = 0, midFar = 0;
			for (const auto& [h, list] : handles) {
				auto a = h.get();
				RE::Actor* t = a.get();
				if (!t || t == a_me || t == pl || t->IsPlayerRef() || sp::fo4::IsPeasant(t)) continue;
				const std::uint32_t id = t->GetFormID();
				Entry*              e = FindOrAdd(id);
				if (!e) continue;
				// REVIEW: created references reuse FormIDs (in game: FF00111A was a dead neutral, then a settler 12 s
				// later). Another object under a known ID must not inherit the cached hostility (1-2 s) or the static
				// facts (settler / essential / companion, 5 s) of the old one: start its facts afresh.
				if (e->handle && !(e->handle == h)) {
					*e = Entry{};
					e->f.id = id;
					static int s_reused = 0;
					if (s_reused++ < 5) REX::INFO("SPN|SAFE|scan: FormID {:08X} reused by another actor: facts reset", id);
				}
				e->handle = h;
				const bool cand = FillCheap(*e, t, a_me, a_centre, list, a_now);
				e->seenCycle = g.win.cycle;
				e->lastExamined = a_now;
				const double dPl = Length(e->f.feet - g.pl.feet);
				if (list == 0) { maxHigh = std::max(maxHigh, dPl); highFar += dPl > 6000.0; }
				else { maxMid = std::max(maxMid, dPl); midFar += dPl > 6000.0; }
				ex.push_back({ static_cast<std::size_t>(e - g.entries.data()), std::move(a), cand });
				++g.st.examined;
			}
			g.st.maxHighDist = maxHigh;
			g.st.maxMidHighDist = maxMid;
			g.st.highFar = highFar;
			g.st.midHighFar = midFar;

			// hostility asks: stale candidates of this window, never-asked first, nearest first, <= hostileAsks
			{
				std::vector<ActorFacts>   facts;
				std::vector<std::uint8_t> cand;
				facts.reserve(ex.size());
				cand.reserve(ex.size());
				for (const auto& x : ex) {
					facts.push_back(g.entries[x.entry].f);
					cand.push_back(x.cand ? 1 : 0);
				}
				std::vector<int> asks;
				SelectAsks(facts, cand, a_now, g.p, asks);
				for (int i : asks) {
					auto& x = ex[static_cast<std::size_t>(i)];
					bool  h = false;
					if (!SehHostile(x.a.get(), pl, &h)) {
						Fault("GetHostileToActor");
						continue;
					}
					NoteHostile(g.entries[x.entry].f, h, a_now);
					++g.st.asks;
				}
			}

			// static facts (hostile only) + scene + vetoes for the examined ones
			int angryInCombat = 0, hostileInCombat = 0, crimeH = 0, uniqueH = 0, aliasH = 0, essH = 0, hostileFarHigh = 0, hostileFarMid = 0;
			for (auto& x : ex) {
				auto& e = g.entries[x.entry];
				auto& f = e.f;
				if (x.cand && f.hostileKnown && f.hostile) {
					if (a_now - f.staticAt >= g.p.staticTtl) FillStatic(e, x.a.get(), a_now);
					else if (g.explosiveTtl > 0 && a_now - e.explAt >= g.explosiveTtl) {
						ReadExplosive(e, x.a.get(), a_now);  // completion lane 1
						++g.explosiveFast;
					}
					if (!f.inCombat && !f.scene) {
						bool sc = false;
						if (SehScene(x.a.get(), &sc)) f.scene = sc; else Fault("GetCurrentScene");
					}
					if (f.inCombat) {
						++hostileInCombat;
						angryInCombat += f.angry;
					}
					crimeH += f.crime;
					uniqueH += f.unique;
					aliasH += f.alias;
					essH += f.essential;
					const double dPl = Length(f.feet - g.pl.feet);
					if (dPl > 6000.0) (f.list == 0 ? hostileFarHigh : hostileFarMid)++;
					ProbeLines(f, a_now);
				}
			}
			g.st.angryHostileInCombat = angryInCombat;
			g.st.hostileInCombat = hostileInCombat;
			g.st.crimeHostile = crimeH;
			g.st.uniqueHostile = uniqueH;
			g.st.aliasHostile = aliasH;
			g.st.essentialHostile = essH;
			g.st.hostileFarHigh = hostileFarHigh;
			g.st.hostileFarMid = hostileFarMid;

			// prune: not seen for two full cycles, or examined long ago (a crowd shrank)
			if (wrapped) {
				const std::uint64_t cyc = g.win.cycle;
				std::erase_if(g.entries, [&](const Entry& e) { return e.seenCycle + 2 < cyc || a_now - e.lastExamined > 3.0; });
				Reindex();
				++g.st.cycles;
				if (g.afterLoad && cyc >= g.afterLoadCycle) {
					g.afterLoad = false;
					REX::INFO("SPN|I|scan: first full cycle after load done (known {})", g.entries.size());
				}
			}

			// vetoes + allowed tiers for every known actor (quarrel / hostility TTL / player state may have changed)
			// HOTFIX round5C A: FinalVeto = VetoBits minus the DEFEND lift (an actor attacking the group: quarrel, angry, crime,
			// scene, alias / unique / essential, middle-high and a hostile turret's workshop link do not keep it out)
			for (auto& e : g.entries) {
				e.f.quarrel = g.quarrels.Has(e.f.id, a_now);
				bool          defend = false;
				std::uint32_t removed = 0;
				e.f.veto = FinalVeto(e.f, g.pl, g.p, a_now, &defend, &removed);
				e.f.defend = defend;
				e.f.lifted = defend ? DefendLift(e.f, g.p) : 0u;
				if (a_now - e.lastExamined > 1.0) e.f.veto |= kV21NotThere;  // stale facts are never a target
				e.f.allow = AllowedTiers(e.f.veto, g.p);
				// logged only when the lift changed the verdict (alias / unique / crime alone are facts, not blocks)
				const std::uint32_t blocking = removed & kHardVetoes;
				if (removed && AllowedTiers(e.f.veto | removed, g.p) != e.f.allow && HfLogDue(g.defendLogAt, e.f.id, a_now, 10.0)) {
					++g.defendLifts;
					REX::INFO("SPN|I|defend {:08X} override={} bits={:x} hitPl={:.1f}s hitGroup={:.1f}s ck={} d={:.0f} allow={:x} n={}", e.f.id,
						FirstVetoName(blocking ? blocking : removed), removed,
						std::min(99.9, a_now - e.f.lastHitPlayerAt), std::min(99.9, a_now - e.f.LastHitGroupAt()), static_cast<int>(e.f.combatKind), e.f.distCentre, e.f.allow,
						g.defendLifts);
				}
			}

			// keep list
			{
				std::vector<ActorFacts> all;
				all.reserve(g.entries.size());
				for (const auto& e : g.entries) all.push_back(e.f);
				std::vector<int> sel;
				SelectKept(all, a_now, g.p, sel);
				// R6 step 4 (Warden 2): the Warden's target stays kept while it is pinned (PinKeep within 0.5 s), whatever the D2f
				// keep rule says (a turned companion, a dormant foe the player shot): the old movement, the per-frame refresh and
				// the hits see it. Live facts only (examined within 1 s, loaded, same space, up).
				if (g.pinId && !g.pinOff && a_now - g.pinAt <= 0.5 && a_now >= g.pinAt) {
					if (auto it = g.index.find(g.pinId); it != g.index.end() && it->second < all.size()) {
						const int   pi = static_cast<int>(it->second);
						const auto& pe = g.entries[it->second];
						if (std::find(sel.begin(), sel.end(), pi) == sel.end() && a_now - pe.lastExamined <= 1.0 && pe.f.loaded && pe.f.sameSpace && !pe.f.down)
							sel.push_back(pi);
					}
				}
				g.kept.clear();
				g.keptHandles.clear();
				double maxKept = 0;
				for (int i : sel) {
					auto& e = g.entries[static_cast<std::size_t>(i)];
					// bones once per scan for kept actors (chest / head offsets; per frame = feet + offset). ROUND3 FPS guard: the
					// larger keep set (FP reach) reads bones only for attackers / engaged, within 2000 u, or every 0.5 s.
					const bool boneDue = KeepRank(e.f, a_now, g.p) <= 1 || e.f.distMe <= 2000.0 || a_now - e.bonesAt >= 0.5 || e.bonesAt < -1e8;
					if (auto a = e.handle.get()) {
						Vec3   ch{}, hd{};
						double ht = 0;
						bool   hc = false, hh = false;
						if (!boneDue) {
							++g.st.boneSkips;
						} else if (SehBones(a.get(), &ch, &hd, &ht, &hc, &hh)) {
							e.f.chestOff = ch - e.f.feet;
							e.f.headOff = hd - e.f.feet;
							e.f.chest = ch;
							e.f.head = hd;
							e.f.height = ht;
							e.bonesAt = a_now;
							++g.st.boneReads;
						} else {
							Fault("bones");
						}
					} else {
						continue;
					}
					g.kept.push_back(e.f);
					g.keptHandles.push_back(e.handle);
					maxKept = std::max(maxKept, e.f.distCentre);
				}
				g.st.kept = static_cast<int>(g.kept.size());
				g.st.maxKeptDist = maxKept;
			}

			// HOTFIX round5C E: an actor fighting the player (hit the player within defendWindow, or the player is its combat
			// target) that is NOT kept says why (per id every 5 s, <= 8 lines a second): next time the log shows the veto
			if (Tunable("brain.scan.skipLog", 1.0) != 0.0) {
				for (const auto& e : g.entries) {
					const auto& f = e.f;
					if (f.down || !f.loaded || a_now - e.lastExamined > 1.0) continue;
					if (!(a_now - f.lastHitPlayerAt <= g.p.defendWindow || f.combatKind == CombatKind::kPlayer)) continue;
					bool isKept = false;
					for (const auto& k : g.kept)
						if (k.id == f.id) {
							isKept = true;
							break;
						}
					if (isKept || !HfLogDue(g.skipLogAt, f.id, a_now, 5.0)) continue;
					++g.skips;
					const char* why = f.veto ? FirstVetoName(f.veto) : (f.allow == kAllowNone ? "allow" : (KeepRank(f, a_now, g.p) >= 3 ? "rank" : "cap"));
					REX::INFO("SPN|SCAN|skip {:08X} why={} veto={:x} ck={} hitPl={:.1f}s hostile={}/{} kind={} settler={}/{} mh={} d={:.0f} n={}", f.id, why, f.veto,
						static_cast<int>(f.combatKind), std::min(99.9, a_now - f.lastHitPlayerAt), f.hostileKnown, f.hostile, f.hostileKindAt < -1e8 ? -1 : static_cast<int>(f.hostileKind), f.settlerNpc, f.workshopLink, f.midHigh,
						f.distCentre, g.skips);
				}
			}

			// bystanders (fling lane K19): the player, teammates, then protected actors near him, nearest first
			{
				g.bys.clear();
				g.bysHandles.clear();
				if (g.pl.valid) {
					g.bys.push_back({ pl->GetFormID(), g.pl.chest, 0 });
					g.bysHandles.push_back(RE::ActorHandle{});
				}
				const Vec3 meFeet = a_me ? sp::fo4::ToVec(a_me->GetPosition()) : g.pl.feet;
				struct B { double d; std::size_t i; std::uint8_t why; };
				std::vector<B> cands;
				for (std::size_t i = 0; i < g.entries.size(); ++i) {
					const auto& f = g.entries[i].f;
					if (!f.loaded || !f.sameSpace || f.down || a_now - g.entries[i].lastExamined > 1.0) continue;
					const double d = Length(f.feet - meFeet);
					std::uint8_t why = 255;
					if (f.teammate) why = 1;
					else if (d <= g.p.bystanderRange && ((f.veto & (kV07NotHostile | kV08Settler | kV03Companion | kV04Dogmeat)) != 0)) why = 2;
					else if (d <= g.p.bystanderRange && (f.veto & (kV09Angry | kV10Crime | kV11Quarrel | kV16Speaker | kV15Scene)) != 0) why = 3;
					if (why == 255) continue;
					cands.push_back({ why == 1 ? -1.0 : d, i, why });
				}
				std::sort(cands.begin(), cands.end(), [](const B& a, const B& b) { return a.d < b.d; });
				for (const auto& c : cands) {
					if (static_cast<int>(g.bys.size()) >= g.p.bystanderCap + 1) break;
					const auto& e = g.entries[c.i];
					g.bys.push_back({ e.f.id, e.f.feet + (LengthSq(e.f.chestOff) > 1 ? e.f.chestOff : Vec3{ 0, 0, 100 }), c.why });
					g.bysHandles.push_back(e.handle);
				}
				g.st.bystanders = static_cast<int>(g.bys.size());
			}

			// R6 step 3: the Warden's view of every known actor (pure copy; stale facts are not "loaded"). s4 fix lane 3 (cost,
			// gate s4 item 6): only while a Warden runs - with [R6] Warden 0 nobody reads it (93 actors x ~350 bytes 10 times
			// a second in downtown)
			g.knownView.clear();
			if (g.knownViewOn) {
				g.knownView.reserve(g.entries.size());
				for (const auto& e : g.entries) {
					g.knownView.push_back(e.f);
					if (a_now - e.lastExamined > 1.0) g.knownView.back().loaded = false;
				}
			}
			++g.scanSerial;
			g.attackers.Forget(a_now, g.p.hitForget);
			g.quarrels.Forget(a_now);
			g.st.known = static_cast<int>(g.entries.size());
			++g.st.scans;
			const double us = UsSince(t0);
			g.scanUsSum += us;
			++g.scanUsN;
			g.st.scanUsAvg = g.scanUsSum / static_cast<double>(g.scanUsN);
			g.st.scanUsMax = std::max(g.st.scanUsMax, us);
		}

		// ------------------------------------------------ HOTFIX2 F3: the per-frame watch
		int KeptIndex(std::uint32_t a_id)
		{
			for (std::size_t i = 0; i < g.kept.size(); ++i)
				if (g.kept[i].id == a_id) return static_cast<int>(i);
			return -1;
		}

		// Shared read plumbing for the existing watch and fresh-hit admission. Eligibility is decided only by WatchKeep.
		bool ReadFreshKeptBones(Entry& e, RE::Actor* t, double a_now, const char* a_fault)
		{
			auto& f = e.f;
			f.feet = sp::fo4::ToVec(t->GetPosition());
			Vec3 ch{}, hd{};
			double ht = 0;
			bool hc = false, hh = false;
			if (!SehBones(t, &ch, &hd, &ht, &hc, &hh)) { Fault(a_fault); return false; }
			f.chestOff = ch - f.feet;
			f.headOff = hd - f.feet;
			f.chest = ch;
			f.head = hd;
			f.height = ht;
			e.bonesAt = a_now;
			return true;
		}
		bool AppendFreshKept(Entry& e, RE::Actor* t, double a_now, const char* a_fault)
		{
			if (static_cast<int>(g.kept.size()) >= g.p.cap + 8 || !ReadFreshKeptBones(e, t, a_now, a_fault)) return false;
			g.kept.push_back(e.f);
			g.keptHandles.push_back(e.handle);
			return true;
		}

		bool AdmissionActorImpl(std::uint32_t id, RE::ActorHandle* handle, RE::NiPointer<RE::Actor>* actor)
		{
			RE::Actor* found = nullptr;
			if (!SehFormActor(id, &found) || !found) return false;
			*handle = RE::BSPointerHandleManagerInterface<RE::Actor>::GetHandle(found);
			*actor = handle->get();
			return *handle && *actor && actor->get() == found && (*actor)->GetFormID() == id;
		}
		bool SehAdmissionActor(std::uint32_t id, RE::ActorHandle* handle, RE::NiPointer<RE::Actor>* actor) noexcept
		{
			__try { return AdmissionActorImpl(id, handle, actor); } __except (1) { return false; }
		}
		bool SehAdmissionCheap(Entry& e, RE::Actor* t, RE::Actor* a_me, const Vec3& a_centre, double a_now) noexcept
		{
			// Optional discovery only: incomplete/detached processes fall back to the ordinary process-list scan.
			// Level 0 is High; no other level is mapped to a list or promoted by a nonnull high-data pointer.
			__try {
				if (!t || !t->currentProcess || t->currentProcess->processLevel != 0) return false;
				return FillCheap(e, t, a_me, a_centre, 0, a_now) && PlayerHitAdmissions::HighProcessOnly(e.f);
			} __except (1) { return false; }
		}

		// Fresh actual player hits need not wait for the rotating process-list scan. Never changes a kept actor.
		// At most two hints per pump; every accepted actor uses the existing complete fact/veto/keep pipeline.
		void AdmitPlayerHits(RE::Actor* a_me, const Vec3& a_centre, double a_now)
		{
			if (!g.hitAdmissions.Enabled() || g.hitAdmissions.Size() == 0 || g.broken || !g.pl.valid || !a_me) return;
			auto* pl = RE::PlayerCharacter::GetSingleton();
			if (!pl || static_cast<int>(g.kept.size()) >= g.p.cap + 8) return;
			PlayerHitAdmission hint;
			while (g.liveAsksFrame < 4 && g.hitAdmissions.Pop(NowUs(), hint)) {
				if (KeptIndex(hint.id) >= 0) continue;
				RE::ActorHandle handle;
				RE::NiPointer<RE::Actor> actor; // keep the CURRENT generation alive through all reads and append
				if (!SehAdmissionActor(hint.id, &handle, &actor)) continue;
				auto* t = actor.get();
				if (t == a_me || t == pl || t->IsPlayerRef() || sp::fo4::IsPeasant(t)) continue;
				Entry* old = FindEntry(hint.id);
				const auto cachedHandle = old && old->handle ? old->handle.get_handle() : 0u;
				if (!PlayerHitAdmissions::HandleMatches(hint, handle.get_handle(), cachedHandle)) continue; // reused ID: normal scan owns reset
				if (!old && g.entries.size() >= 256) continue;
				Entry fresh = old ? *old : Entry{};
				fresh.handle = handle;
				fresh.f.id = hint.id;
				const bool cheap = SehAdmissionCheap(fresh, t, a_me, a_centre, a_now);
				if (g.broken) return;
				if (!cheap) continue;
				bool hostile = false;
				++g.liveAsksFrame;
				++g.st.liveAsks;
				if (!SehHostile(t, pl, &hostile)) { Fault("GetHostileToActor(hit admission)"); return; }
				NoteHostile(fresh.f, hostile, a_now);
				if (!hostile) continue; // a hit/throwable is NEVER authority to make a friendly hostile
				FillStatic(fresh, t, a_now); // fresh companion/settler/essential/scene facts; existing void contract
				if (g.broken) return;
				if (!fresh.f.inCombat && !fresh.f.scene) {
					bool scene = false;
					if (!SehScene(t, &scene)) { Fault("GetCurrentScene(hit admission)"); return; }
					fresh.f.scene = scene;
				}
				if (!WatchKeep(fresh.f, g.pl, g.p, a_now)) continue;
				if (!ReadFreshKeptBones(fresh, t, a_now, "bones (hit admission)")) return;
				// A slow engine read must not turn an expired hint into a fresh cache entry that Watch could then admit.
				if (!g.hitAdmissions.StillFresh(hint, NowUs())) continue;
				if (Tunable("brain.scan.hitAdmission", 1.0) == 0.0) { g.hitAdmissions.Clear(); return; }
				if (static_cast<int>(g.kept.size()) >= g.p.cap + 8) return;
				fresh.lastExamined = a_now;
				fresh.seenCycle = g.win.cycle;
				Entry* entry = FindOrAdd(hint.id);
				if (!entry) continue;
				*entry = fresh;
				g.kept.push_back(entry->f);
				g.keptHandles.push_back(entry->handle);
				++g.hotAdmits;
				if (Tunable("brain.scan.skipLog", 1.0) != 0.0 && (g.hotAdmits <= 16 || g.hotAdmits % 64 == 0))
					REX::INFO("SPN|SCAN|hit-admit {:08X} kind=0 ageMs={:.1f} knownHandle={} essential={} allow={:x} n={}", hint.id,
						(NowUs() - hint.stampUs) / 1000.0, hint.knownHandle != 0, entry->f.essentialOnly, entry->f.allow, g.hotAdmits);
				if (static_cast<int>(g.kept.size()) >= g.p.cap + 8) return;
			}
		}

		// In-game smoke (runs hf v1-v3, 05:02-05:33): a foe whose combat target became the player was skipped 'why=notHostile'
		// for 0.87-1.96 s - its cached "not hostile" verdict lives nonHostileTtl (2 s) and a crowd is examined in a rotating
		// window. Every frame (after the drain and the scan, before the refresh) up to watchBudget known actors are looked at:
		// the live combat target is read; one that fights the group, hit the group or was hit by the player within
		// defendWindow (WatchTrigger) and is cached "not hostile" is asked again AT ONCE (<= watchAsks a frame, each actor at
		// most every watchAskEvery s). When it now passes every veto (its static facts read first: settler, companion, turret
		// link ...) it joins the kept list in this very frame, so the brain picks it in the same Update (defend first).
		// Every hard veto still applies: not hostile, settler NPC, companion, teammate, Dogmeat, child, down, other space ...
		void Watch(RE::Actor* a_me, double a_now)
		{
			if (!g.p.watch || !g.pl.valid || g.entries.empty()) return;
			auto* pl = RE::PlayerCharacter::GetSingleton();
			if (!pl) return;
			const int total = static_cast<int>(g.entries.size());
			const int budget = std::min(total, std::max(1, g.p.watchBudget));
			int       asks = 0;
			for (int k = 0; k < budget; ++k) {
				if (g.watchCursor >= g.entries.size()) g.watchCursor = 0;
				Entry& e = g.entries[g.watchCursor++];
				auto&  f = e.f;
				if (a_now - e.lastExamined > 1.0 || !f.loaded || !f.sameSpace || f.down || f.teammate || f.commanded || f.child || f.distCentre > g.p.scanRange) continue;
				const int ki = KeptIndex(f.id);
				if (ki >= 0 && f.hostileKnown && f.hostile && f.CombatWithGroup()) continue;  // a kept fighter already: the refresh has it
				auto       a = e.handle.get();
				RE::Actor* t = a.get();
				if (!t || t == a_me || t == pl) continue;
				CombatKind ck = CombatKind::kNone;
				bool       down = false;
				if (!SehCombatRead(t, pl, a_me, &ck, &down)) {
					Fault("combat target (watch)");
					return;
				}
				if (down) continue;
				const bool wasGroup = f.CombatWithGroup();
				f.combatKind = ck;
				if (f.CombatWithGroup()) {
					f.groupCombatAt = a_now;
					f.groupCombatPlayer = ck == CombatKind::kPlayer;
					f.inCombat = true;
				}
				if (ki >= 0) {
					auto& kf = g.kept[static_cast<std::size_t>(ki)];
					kf.combatKind = f.combatKind;
					kf.groupCombatAt = f.groupCombatAt;
					kf.groupCombatPlayer = f.groupCombatPlayer;
					kf.inCombat = kf.inCombat || f.inCombat;
				}
				f.quarrel = g.quarrels.Has(f.id, a_now);
				if (!WatchTrigger(f, a_now, g.p)) continue;
				const bool turned = f.CombatWithGroup() && !wasGroup;
				if (turned) ++g.wTurns;
				bool asked = false;
				// s4 fix lane 2 (P03): a hostile kind the player hit within defendWindow is kept now, whatever the engine's
				// hostility flag says yet (it flips 0.3-0.5 s later); it is still asked (budget permitting) to learn the flag
				const bool provoked = ProvokedFoe(f, a_now, g.p);
				if (!(f.hostileKnown && f.hostile)) {
					if (!WatchAskDue(f, a_now, g.p)) {
						if (!provoked) continue;
					} else if (asks >= g.p.watchAsks) {
						f.hostileKnown = false;  // no budget left this frame: never-asked first at the next scan / frame
						if (!provoked) continue;
					} else {
						bool h = false;
						if (!SehHostile(t, pl, &h)) {
							Fault("GetHostileToActor(watch)");
							return;
						}
						NoteHostile(f, h, a_now);
						++asks;
						++g.st.liveAsks;
						++g.wAsks;
						asked = true;
						if (!h && !provoked) continue;
					}
				}
				if (a_now - f.staticAt >= g.p.staticTtl) FillStatic(e, t, a_now);
				else if (g.explosiveTtl > 0 && a_now - e.explAt >= g.explosiveTtl) {
					ReadExplosive(e, t, a_now);  // completion lane 1
					++g.explosiveFast;
				}
				if (!f.inCombat && !f.scene) {
					bool sc = false;
					if (SehScene(t, &sc)) f.scene = sc;
				}
				if (!WatchKeep(f, g.pl, g.p, a_now)) continue;
				if (ki >= 0) {
					// the kept copy takes the new verdict; its per-frame fields (position, hits) stay the refresh's
					ActorFacts  nf = f;
					const auto& kf = g.kept[static_cast<std::size_t>(ki)];
					nf.feet = kf.feet;
					nf.chest = kf.chest;
					nf.head = kf.head;
					nf.vel = kf.vel;
					nf.chestOff = kf.chestOff;
					nf.headOff = kf.headOff;
					nf.height = kf.height;
					nf.distCentre = kf.distCentre;
					nf.distMe = kf.distMe;
					nf.knocked = kf.knocked;
					nf.killMove = kf.killMove;
					g.kept[static_cast<std::size_t>(ki)] = nf;
				} else {
					if (!AppendFreshKept(e, t, a_now, "bones (watch)")) {
						if (g.broken) return;
						continue;
					}
				}
				++g.wKeeps;
				if (HfLogDue(g.watchLogAt, f.id, a_now, 2.0))
					REX::INFO("SPN|SCAN|watch {:08X} kept now: ck={} turned={} asked={} hostile={} hitPl={:.1f}s plHit={:.1f}s kind={} veto={:x} allow={:x} rank={} d={:.0f} n={}", f.id,
						static_cast<int>(f.combatKind), turned, asked, f.hostile, std::min(99.9, a_now - f.lastHitPlayerAt), std::min(99.9, a_now - f.playerHitAt),
						f.hostileKindAt < -1e8 ? -1 : static_cast<int>(f.hostileKind), f.veto, f.allow, KeepRank(f, a_now, g.p), f.distCentre, g.wKeeps);
			}
		}

		// HOTFIX2 TEST HOOK (in-game smoke only; inert unless set): the smoke Runner cannot make the player's own shot land
		// (SMOKE.md "harness limit"), so [Tabs] debug.injectPlayerHit = 1 with debug.injectHi / debug.injectLo (the target
		// FormID's upper / lower 16 bits, exact in a Papyrus float, via the SetTunable native) queues ONE "the player hit
		// <id>" record exactly as the TESHitEvent sink would (cause = the player, kind 3). It resets itself to 0. The shipped
		// ini never sets it.
		void TestInject(double a_now)
		{
			if (Tunable("debug.injectPlayerHit", 0.0) == 0.0) return;
			sp::cfg::SetTunable("debug.injectPlayerHit", 0.0);
			const auto          hi = static_cast<std::uint32_t>(std::lround(Tunable("debug.injectHi", 0.0))) & 0xFFFFu;
			const auto          lo = static_cast<std::uint32_t>(std::lround(Tunable("debug.injectLo", 0.0))) & 0xFFFFu;
			const std::uint32_t id = (hi << 16) | lo;
			auto*               pl = RE::PlayerCharacter::GetSingleton();
			if (!id || !pl || g.deferred.size() >= 32) return;
			HitRec r;
			r.cause = pl->GetFormID();
			r.target = id;
			r.kind = HitKind::kPlayerHit;
			r.fallback = true;
			r.stampUs = NowUs();
			r.inPump = true;
			g.deferred.push_back(r);
			++g.injected;
			REX::INFO("SPN|TEST|inject player hit {:08X} at {:.3f} (debug.injectPlayerHit; test only) n={}", id, a_now, g.injected);
		}

		// ------------------------------------------------ per-frame refresh of the kept list and the bystanders
		void Refresh(RE::Actor* a_me, const Vec3& a_centre, double a_now, double a_dt)
		{
			const auto t0 = Clock::now();
			// the player's fast facts every frame (member reads): feet / velocity / chest, VATS playback, kill move
			if (auto* pl = RE::PlayerCharacter::GetSingleton(); pl && g.pl.valid) {
				const Vec3 feet = sp::fo4::ToVec(pl->GetPosition());
				if (a_dt > 1e-4) {
					const Vec3 v = (feet - g.pl.feet) / a_dt;
					if (IsFinite(v) && LengthSq(v) < 1.0e8) {
						const double a = EmaAlpha(0.5, a_dt);  // ROUND4 (frame rate): the 60 fps half blend, per second
						g.pl.vel = g.pl.vel * (1.0 - a) + v * a;
					}
				}
				g.pl.chest = g.pl.chest + (feet - g.pl.feet);
				g.pl.feet = feet;
				g.plPrevFeet = feet;
				g.plPrevAt = a_now;
				g.pl.killMove = pl->boolFlags.any(RE::Actor::BOOL_FLAGS::kIsInKillMove);
				if (auto* v = RE::VATS::GetSingleton()) g.pl.vatsPlayback = v->mode.any(RE::VATS::VATS_MODE_ENUM::kPlayback);
			}
			auto&      world = rays::GetWorld();
			world.ClearRoots();  // this frame's roots only (raw pointers are never kept across frames: SAFETY G1)
			const Vec3 meFeet = a_me ? sp::fo4::ToVec(a_me->GetPosition()) : g.pl.feet;
			std::size_t w = 0;
			for (std::size_t i = 0; i < g.kept.size(); ++i) {
				auto&      k = g.kept[i];
				const auto a = g.keptHandles[i].get();
				RE::Actor* t = a.get();
				bool       ok = t && !t->IsDeleted() && !t->IsDisabled() && t->lifeState == 0 && t->Get3D() && t->parentCell && t->parentCell->IsAttached();
				if (ok) {
					const Vec3 feet = sp::fo4::ToVec(t->GetPosition());
					if (a_dt > 1e-4) {
						const Vec3 v = (feet - k.feet) / a_dt;
						if (IsFinite(v) && LengthSq(v) < 1.0e8) {
							const double ka = EmaAlpha(0.5, a_dt);  // ROUND4 (frame rate)
							k.vel = k.vel * (1.0 - ka) + v * ka;
						}
					}
					k.feet = feet;
					k.chest = feet + k.chestOff;
					k.head = feet + k.headOff;
					k.distCentre = GuardDist(feet, a_centre);
					k.distMe = Length(feet - meFeet);
					k.knocked = t->knockState != 0;
					k.killMove = t->boolFlags.any(RE::Actor::BOOL_FLAGS::kIsInKillMove);
					if (const auto* ar = g.attackers.Get(k.id)) {
						k.lastHitPlayerAt = ar->lastHitPlayerAt;
						k.lastHitMeAt = ar->lastHitMeAt;
						k.lastHitTeammateAt = ar->lastHitTeammateAt;
						k.hitDist = ar->hitDist;
					}
					world.SetRoot(k.id, t->Get3D());
					if (w != i) {
						g.kept[w] = k;
						g.keptHandles[w] = g.keptHandles[i];
					}
					++w;
				} else {
					++g.st.refreshDrops;
				}
			}
			g.kept.resize(w);
			g.keptHandles.resize(w);
			// bystanders: positions (the player is entry 0 with an empty handle)
			for (std::size_t i = 0; i < g.bys.size(); ++i) {
				if (i == 0 && g.bys[0].why == 0) {
					if (auto* pl = RE::PlayerCharacter::GetSingleton()) g.bys[0].pos = sp::fo4::ToVec(pl->GetPosition()) + Vec3{ 0, 0, 100 };
					continue;
				}
				if (const auto a = g.bysHandles[i].get()) g.bys[i].pos = sp::fo4::ToVec(a->GetPosition()) + Vec3{ 0, 0, 100 };
			}
			const double us = UsSince(t0);
			g.refreshUsSum += us;
			++g.refreshUsN;
			g.st.refreshUsAvg = g.refreshUsSum / static_cast<double>(g.refreshUsN);
			g.st.refreshUsMax = std::max(g.st.refreshUsMax, us);
		}

		void MaybeLog(double a_now)
		{
			if (g.logStartAt < 0) g.logStartAt = a_now;
			if (a_now < g.nextLogAt) return;
			const double every = (a_now - g.logStartAt) < 900.0 ? 10.0 : 60.0;  // P1: every 10 s for 15 min, then every 60 s
			g.nextLogAt = a_now + every;
			REX::INFO("SPN|SCAN|{}", Describe());
			REX::INFO("SPN|HITSINK|{}", hitsink::Describe());
			REX::INFO("SPN|RAY|{}", rays::Describe());
		}
	}

	// ================================================================ API
	void OnGameDataReady()
	{
		auto fac = [](std::uint32_t id) -> const RE::TESFaction* {
			auto* f = RE::TESForm::GetFormByID(id);
			return f ? f->As<RE::TESFaction>() : nullptr;
		};
		g.forms.workshopNPC = fac(kWorkshopNPCFaction);
		g.forms.currentCompanion = fac(kCurrentCompanionFaction);
		g.forms.hasBeenCompanion = fac(kHasBeenCompanionFaction);
		g.forms.playerFaction = fac(kPlayerFaction);
		if (auto* f = RE::TESForm::GetFormByID(kWorkshopItemKeyword)) g.forms.workshopItem = f->As<RE::BGSKeyword>();
		// HOTFIX2 F1: the hostile-kind forms (DLC ones only when that master is loaded)
		{
			auto* dh = RE::TESDataHandler::GetSingleton();
			auto  dlc = [&](const DlcForm& d) -> RE::TESForm* { return dh ? dh->LookupForm(d.local, d.file) : nullptr; };
			g.forms.potentialCompanion = fac(kPotentialCompanionFaction);
			g.forms.hostileFactions.clear();
			g.forms.monsterKw.clear();
			g.forms.machineKw.clear();
			g.forms.monsterRaces.clear();
			g.raceKind.clear();
			for (auto id : kHostileFactions)
				if (const auto* f = fac(id)) g.forms.hostileFactions.push_back(f);
			for (const auto& d : kHostileFactionsDlc)
				if (auto* f = dlc(d)) {
					if (const auto* ff = f->As<RE::TESFaction>()) g.forms.hostileFactions.push_back(ff);
				}
			for (auto id : kMonsterRaceKeywords)
				if (auto* f = RE::TESForm::GetFormByID(id)) {
					if (const auto* k = f->As<RE::BGSKeyword>()) g.forms.monsterKw.push_back(k);
				}
			for (const auto& d : kMonsterRaceKeywordsDlc)
				if (auto* f = dlc(d)) {
					if (const auto* k = f->As<RE::BGSKeyword>()) g.forms.monsterKw.push_back(k);
				}
			for (auto id : kMachineRaceKeywords)
				if (auto* f = RE::TESForm::GetFormByID(id)) {
					if (const auto* k = f->As<RE::BGSKeyword>()) g.forms.machineKw.push_back(k);
				}
			for (auto id : kMonsterRaceIds)
				if (auto* f = RE::TESForm::GetFormByID(id)) {
					if (const auto* r = f->As<RE::TESRace>()) g.forms.monsterRaces.push_back(r);
				}
			for (const auto& d : kMonsterRacesDlc)
				if (auto* f = dlc(d)) {
					if (const auto* r = f->As<RE::TESRace>()) g.forms.monsterRaces.push_back(r);
				}
			REX::INFO("SPN|I|scan hostile-kind forms (HOTFIX2): potentialCompanion={} hostileFactions={}/{} monsterKw={}/{} machineKw={}/{} monsterRaces={}/{}",
				g.forms.potentialCompanion != nullptr, g.forms.hostileFactions.size(), std::size(kHostileFactions) + std::size(kHostileFactionsDlc), g.forms.monsterKw.size(),
				std::size(kMonsterRaceKeywords) + std::size(kMonsterRaceKeywordsDlc), g.forms.machineKw.size(), std::size(kMachineRaceKeywords), g.forms.monsterRaces.size(),
				std::size(kMonsterRaceIds) + std::size(kMonsterRacesDlc));
		}
		REX::INFO("SPN|I|scan forms: workshopNPC={} currentCompanion={} hasBeenCompanion={} playerFaction={} workshopItemKW={}", g.forms.workshopNPC != nullptr,
			g.forms.currentCompanion != nullptr, g.forms.hasBeenCompanion != nullptr, g.forms.playerFaction != nullptr, g.forms.workshopItem != nullptr);

		// uGridsToLoad (HUNT §1, ROUND2 §3.2)
		int  u = 5;
		bool read = false;
		if (auto* s = RE::GetINISetting("uGridsToLoad:General")) {
			const auto type = s->GetType();
			if (type == RE::Setting::SETTING_TYPE::kInt || type == RE::Setting::SETTING_TYPE::kUInt) {
				u = s->GetInt();  // NDEBUG build: GetInt() returns the 32-bit value for i / u settings alike
				read = true;
			}
		}
		g.grid = GridFromUGrids(u, read);
		REX::INFO("SPN|HUNT|grid uGrids={} guaranteedR={:.0f} farR={:.0f} maxR={:.0f} huntAbort={:.0f} (from ini: {})", g.grid.uGrids, g.grid.guaranteedR, g.grid.farR, g.grid.maxR,
			g.grid.huntAbort, g.grid.read);
		LoadParams();
		hitsink::Install();
	}

	void OnPreLoad()
	{
		g.entries.clear();
		g.index.clear();
		g.knownView.clear();  // R6 step 3
		++g.scanSerial;
		g.kept.clear();
		g.keptHandles.clear();
		g.bys.clear();
		g.bysHandles.clear();
		g.attackers.Clear();
		g.hitAdmissions.Clear();
		g.quarrels.Clear();
		g.deferred.clear();
		g.defendLogAt.clear();
		g.skipLogAt.clear();
		g.kindLogAt.clear();  // HOTFIX2
		g.watchLogAt.clear();
		g.watchCursor = 0;
		g.pl = PlayerFacts{};
		g.plPrevAt = -1e9;
		g.win = Window{};
		g.nextScanAt = -1e9;
		g.afterLoad = false;
		hitsink::Clear();
	}

	void OnPostLoad()
	{
		g.afterLoad = true;
		g.afterLoadCycle = g.win.cycle + 1;
	}

	bool Tick(RE::Actor* a_me, const Vec3& a_centre, double a_now, double a_dt)
	{
		if (g.broken || !sp::fo4::threads::InPump()) return false;
		g.liveAsksFrame = 0;
		g.hitAdmissions.BeginPump(NowUs(), Tunable("brain.scan.hitAdmission", 1.0) != 0.0);
		TestInject(a_now);  // HOTFIX2: test hook (inert unless debug.injectPlayerHit is set)
		DrainHits(a_me, a_now);
		bool scanned = false;
		if (a_now >= g.nextScanAt) {
			// D2 (user 03:50, downtown: high=84 actors): with more than brain.d2DenseScanHigh high-process actors the scan runs
			// brain.d2DenseScan x less often (the per-frame watch and the hit sink still catch anyone turning on the group).
			// 0 = D1H.
			double every = g.p.scanEvery;
			if (const double k = Tunable("brain.d2DenseScan", 0.0); k > 1.0 && g.st.high > static_cast<int>(Tunable("brain.d2DenseScanHigh", 60.0))) every *= std::min(k, 3.0);
			g.nextScanAt = a_now + every;
			Scan(a_me, a_centre, a_now);
			scanned = true;
		}
		AdmitPlayerHits(a_me, a_centre, a_now);
		Watch(a_me, a_now);  // HOTFIX2 F3: turned on the group -> asked and kept in this frame
		Refresh(a_me, a_centre, a_now, a_dt);
		MaybeLog(a_now);
		return scanned;
	}

	std::span<const ActorFacts> Kept() { return g.kept; }
	std::span<const Bystander>  Bystanders() { return g.bys; }
	const PlayerFacts&          GetPlayerFacts() { return g.pl; }
	const Grid&                 GetGrid() { return g.grid; }
	const Params&               GetParams() { return g.p; }
	const AttackerRec*          Attacker(std::uint32_t a_id) { return g.attackers.Get(a_id); }
	bool                        IsQuarrel(std::uint32_t a_id, double a_now) { return g.quarrels.Has(a_id, a_now); }
	bool                        Broken() { return g.broken; }
	bool                        AfterLoad() { return g.afterLoad; }
	const Stats&                GetStats() { return g.st; }

	const ActorFacts* Find(std::uint32_t a_id)
	{
		for (const auto& k : g.kept)
			if (k.id == a_id) return &k;
		if (const auto* e = FindEntry(a_id)) return &e->f;
		return nullptr;
	}

	RE::Actor* Resolve(std::uint32_t a_id)
	{
		for (std::size_t i = 0; i < g.kept.size(); ++i)
			if (g.kept[i].id == a_id) return g.keptHandles[i].get().get();  // the Actor outlives this frame's NiPointer (pump = serial phase)
		if (auto* e = FindEntry(a_id)) return e->handle.get().get();
		return nullptr;
	}

	// R6 step 3
	std::span<const ActorFacts> KnownFacts() { return g.knownView; }

	// R6 step 4 (Warden 2): pin the Warden's target into the kept list now (see scan.h)
	bool PinKeep(std::uint32_t a_id, double a_now)
	{
		if (!a_id || g.broken || g.pinOff || !sp::fo4::threads::InPump()) return false;
		g.pinId = a_id;
		g.pinAt = a_now;
		if (KeptIndex(a_id) >= 0) return true;
		Entry* e = FindEntry(a_id);
		if (!e || a_now - e->lastExamined > 1.0 || !e->f.loaded || !e->f.sameSpace || e->f.down || static_cast<int>(g.kept.size()) >= g.p.cap + 8) {
			++g.pinFails;
			return false;
		}
		const auto       a = e->handle.get();
		RE::Actor*       t = a.get();
		Vec3             feet{};
		RE::NiAVObject*  root = nullptr;
		bool             ok = false;
		if (!t) {
			++g.pinFails;
			return false;
		}
		if (!SehPinRead(t, &feet, &root, &ok)) {
			++g.pinFaults;
			g.pinOff = true;
			REX::ERROR("SPN|R6|off feat=scan.pin why=seh (the Warden's target is no longer pinned into the kept list this session)");
			sp::runtime::RequestDump("scan pin crash shield");  // the crash-shield rule: a bookmark-style dump
			return false;
		}
		if (!ok) {
			++g.pinFails;
			return false;
		}
		ActorFacts f = e->f;
		Vec3       ch{}, hd{};
		double     ht = 0;
		bool       hc = false, hh = false;
		if (SehBones(t, &ch, &hd, &ht, &hc, &hh)) {
			f.chestOff = ch - feet;
			f.headOff = hd - feet;
			f.height = ht;
			e->bonesAt = a_now;
		}
		f.feet = feet;
		f.chest = feet + (LengthSq(f.chestOff) > 1.0 ? f.chestOff : Vec3{ 0, 0, 90 });
		f.head = feet + (LengthSq(f.headOff) > 1.0 ? f.headOff : Vec3{ 0, 0, 120 });
		g.kept.push_back(f);
		g.keptHandles.push_back(e->handle);
		rays::GetWorld().SetRoot(f.id, root);
		++g.pinAdds;
		return true;
	}
	std::string PinDescribe() { return std::format("pin={:08X} adds={} fails={} faults={}{}", g.pinId, g.pinAdds, g.pinFails, g.pinFaults, g.pinOff ? " OFF" : ""); }
	std::uint64_t               ScanSerial() { return g.scanSerial; }
	std::uint64_t               R6AskFaults() { return g.r6AskFaults; }
	void SetKnownView(bool a_on) { g.knownViewOn = a_on; }  // s4 fix lane 3
	void                        SetR6Checks(bool a_killRole, bool a_workshop, bool a_probe)
	{
		g.r6KillRole = a_killRole;
		g.r6Workshop = a_workshop;
		g.r6Probe = a_probe;
	}
	bool AskHostileR6(std::uint32_t a_id, bool* a_hostile)
	{
		if (!a_hostile || g.broken || g.r6AsksOff || !sp::fo4::threads::InPump()) return false;
		auto* pl = RE::PlayerCharacter::GetSingleton();
		auto* t = Resolve(a_id);
		if (!pl || !t || t == pl) return false;
		bool h = false;
		if (!SehHostile(t, pl, &h)) {  // crash shield: the R6 asks switch off for the session; the scan keeps running
			++g.r6AskFaults;
			g.r6AsksOff = true;
			REX::ERROR("SPN|R6|off feat=hitCauseAsk why=seh id={:08X} (the shadow Warden keeps the cached hostility)", a_id);
			return false;
		}
		*a_hostile = h;
		return true;
	}

	bool ReadBones(RE::Actor* a_t, Vec3& a_chest, Vec3& a_head, double& a_height)
	{
		if (!a_t) return false;
		bool hc = false, hh = false;
		if (!SehBones(a_t, &a_chest, &a_head, &a_height, &hc, &hh)) {
			Fault("bones");
			return false;
		}
		return true;
	}

	namespace
	{
		// completion side lane 3: the combat-target read of the turned-companion rule in its own SEH shell (crash shield: a fault turns
		// the rule off for the session - g_tcOff, the s13r1 companion veto again - with one line and a bookmark-style dump; the game goes on)
		bool CombatTargetIsImpl(RE::Actor* t, RE::Actor* who)
		{
			if (!t->currentCombatTarget) return false;
			const auto ct = t->currentCombatTarget.get();
			return ct && ct.get() == who;
		}
		bool SehCombatTargetIs(RE::Actor* t, RE::Actor* who, bool* out) noexcept
		{
			__try { *out = CombatTargetIsImpl(t, who); return true; } __except (1) { return false; }
		}

		// completion side lane 3 (brain.turnedCompanion): the live form of scan_core TurnedCompanion for the hit / damage / blink re-checks.
		// a_f = the actor's scan facts (static facts: companion; the caller just refreshed its hostility live). Reads what LiveVeto
		// already reads (teammate flag, combat-target handle - shelled here -, the attacker table). Pump only (the callers are).
		bool TurnedCompLive(RE::Actor* a_t, RE::PlayerCharacter* a_pl, const ActorFacts& a_f, double a_now, const char* a_stage)
		{
			if (!g.p.turnedCompanion || g_tcOff.load(std::memory_order_relaxed) || !a_t || !a_pl || !a_f.companion || a_f.id == 0x0001D162u) return false;
			if (IsTeammate(a_t) || a_t->boolFlags.any(RE::Actor::BOOL_FLAGS::kIsCommandedActor) || a_t->lifeState != 0) return false;
			bool ctPl = false;
			if (!SehCombatTargetIs(a_t, a_pl, &ctPl)) {
				g_tcOff.store(true, std::memory_order_relaxed);
				REX::ERROR("SPN|R6|off feat=scan.turnedComp why=seh id={:08X} (a companion is never a target again this session: the s13r1 rule)", a_f.id);
				sp::runtime::RequestDump("scan turnedComp crash shield");  // the crash-shield rule: a bookmark-style dump
				return false;
			}
			double hitAgo = 99.9;
			if (const auto* ar = g.attackers.Get(a_f.id); ar && ar->lastHitPlayerAt > -1e8) hitAgo = a_now - ar->lastHitPlayerAt;
			const bool on = ctPl || (hitAgo >= -1e-6 && hitAgo <= g.p.defendWindow);
			if (on) {
				static std::uint64_t s_lines = 0;
				++s_lines;
				if (s_lines <= 20 || (s_lines % 50) == 0)
					REX::INFO("SPN|SCAN|turnedComp {:08X} stage={} ct={} hitP={:.2f}s ess={} settler={} n={}", a_f.id, a_stage ? a_stage : "?", ctPl ? "P" : "-", std::min(99.9, hitAgo),
						a_f.essentialOnly ? 1 : 0, a_f.settlerNpc ? 1 : 0, s_lines);
			}
			return on;
		}
	}

	bool TurnedCompanionNow(RE::Actor* a_t, double a_now)
	{
		if (!a_t || !g.p.turnedCompanion || !sp::fo4::threads::InPump() || g.broken) return false;
		auto*        pl = RE::PlayerCharacter::GetSingleton();
		const Entry* e = FindEntry(a_t->GetFormID());
		if (!e || !pl || !e->f.hostileKnown || !e->f.hostile) return false;
		return TurnedCompLive(a_t, pl, e->f, a_now, "now");
	}

	bool SuiciderNow(std::uint32_t a_id)
	{
		if (!a_id || !sp::fo4::threads::InPump() || g.broken) return false;
		const Entry* e = FindEntry(a_id);
		return e && e->suicSeen;
	}

	bool ProvokedFoeNow(std::uint32_t a_id)
	{
		if (!a_id || !g_provOn.load(std::memory_order_relaxed)) return false;
		const std::int64_t now = NowUs();
		for (const auto& r : g_prov)
			if (r.id.load(std::memory_order_relaxed) == a_id && r.untilUs.load(std::memory_order_relaxed) >= now) return true;
		return false;
	}

	const char* LiveVeto(RE::Actor* a_me, RE::Actor* a_t, double a_now, const char* a_stage)
	{
		const char* why = nullptr;
		auto*       pl = RE::PlayerCharacter::GetSingleton();
		if (!sp::fo4::threads::InPump()) why = "offPump";
		else if (g.broken) why = "scanBroken";
		else if (!a_t || !pl) why = "none";
		else if (a_t == pl || a_t->IsPlayerRef() || a_t == a_me || sp::fo4::IsPeasant(a_t)) why = "self";
		else if (IsTeammate(a_t)) why = "teammate";
		else if (a_t->boolFlags.any(RE::Actor::BOOL_FLAGS::kIsCommandedActor)) why = "commanded";
		else if (a_t->lifeState != 0) why = "down";
		// V18: no hit / fling on it. D2 (gate D1H r5-120: the Concord deathclaw kill-moving a raider: every hit refused for
		// 7.6 s, 77 swings, kill at 11.4 s): with punch.d2KillMoveHit an NPC-on-NPC kill move is hit like anything else; the
		// veto stays while the PLAYER is in a kill move (the player's own kill cam / paired animation). 0 = D1H.
		else if (a_t->boolFlags.any(RE::Actor::BOOL_FLAGS::kIsInKillMove) &&
		         (Tunable("punch.d2KillMoveHit", 0.0) == 0 || pl->boolFlags.any(RE::Actor::BOOL_FLAGS::kIsInKillMove)))
			why = "killMove";
		else if (const auto* v = RE::VATS::GetSingleton(); v && v->mode.any(RE::VATS::VATS_MODE_ENUM::kPlayback)) why = "vats";  // V17
		else if (!a_t->parentCell || !a_t->parentCell->IsAttached() || !a_t->Get3D() || a_t->IsDisabled() || a_t->IsDeleted()) why = "unloaded";
		else if (!SameSpace(a_t)) why = "otherSpace";
		if (!why) {
			bool h = false;
			if (!SehHostile(a_t, pl, &h)) {
				Fault("GetHostileToActor(live)");
				why = "fault";
			} else {
				++g.st.liveAsks;
				Entry* e = FindOrAdd(a_t->GetFormID());
				if (e) NoteHostile(e->f, h, a_now);  // a new entry has no handle yet: the scan fills it when it examines the actor
				// s4 fix lane 2 (P03): a hostile kind the player hit within defendWindow is a foe at the hit (its flag flips later)
				if (!h && !(e && ProvokedFoe(e->f, a_now, g.p))) why = "notHostile";
				else {
					Entry tmp;
					Entry& use = e ? *e : tmp;
					use.f.id = a_t->GetFormID();
					if (a_now - use.f.staticAt >= g.p.staticTtl) FillStatic(use, a_t, a_now);
					else if (g.explosiveTtl > 0 && a_now - use.explAt >= g.explosiveTtl) {  // s13 r1 fix lane 3 (merged): the defend fill too
						ReadExplosive(use, a_t, a_now);
						++g.explosiveFast;
					}
					use.f.angry = a_t->boolFlags.any(RE::Actor::BOOL_FLAGS::kAngryWithPlayer);
					use.f.quarrel = g.quarrels.Has(use.f.id, a_now);
					bool inCombat = false;
					if (SehInCombat(a_t, &inCombat)) use.f.inCombat = inCombat;
					bool scene = a_t->boolFlags.any(RE::Actor::BOOL_FLAGS::kScenePackage, RE::Actor::BOOL_FLAGS::kInRandomScene);
					if (!inCombat && !scene) {
						bool sc = false;
						if (SehScene(a_t, &sc)) scene = sc;
					}
					// HOTFIX round5C A / B: the DEFEND rule live (it hit the player / him / a teammate within defendWindow, or
					// its combat target is one of them) lifts angry / quarrel / scene and a hostile turret's workshop link; a
					// hostile kind is never angry / quarrel protected. Settler NPCs, companions and Dogmeat stay protected.
					bool defend = false;
					if (g.p.defend) {
						if (const auto* ar = g.attackers.Get(use.f.id); ar && a_now - ar->Last() <= g.p.defendWindow) defend = true;
						if (!defend && g.p.defendCombat && a_t->currentCombatTarget) {
							if (const auto ct = a_t->currentCombatTarget.get()) {
								const RE::Actor* c = ct.get();
								defend = c == pl || c == a_me || sp::fo4::IsPeasant(c) || IsTeammate(c);
							}
						}
					}
					const bool exempt = QuarrelExempt(use.f, g.p) || defend;
					// completion side lane 3 (brain.turnedCompanion, user rule c): a companion that TURNED on the player - hostile by this
					// live ask (h; never the provoked-kind bridge), not a teammate / commanded / down (refused above), and its combat
					// target is the player or it hit the player within defendWindow s - is no companion / settler veto any more
					const bool turnedComp = h && TurnedCompLive(a_t, pl, use.f, a_now, a_stage);
					const bool settlerHere = use.f.settler && !turnedComp && !(defend && g.p.defendTurretLink && use.f.workshopLink && !use.f.settlerNpc);
					if (settlerHere) why = "settler";
					else if (use.f.companion && !turnedComp) why = "companion";
					else if (use.f.id == 0x0001D162u) why = "dogmeat";
					else if (!exempt && use.f.angry && (g.p.v09Mode == 1 || (g.p.v09Mode == 2 && (use.f.crime || use.f.quarrel || a_now - use.f.lastNonHostileAt <= 60.0)))) why = "angryAlly";
					else if (!exempt && use.f.quarrel) why = "quarrel";
					else if (!defend && scene && !inCombat) why = "scene";
					else if (g.pl.speakerId && g.pl.speakerId == use.f.id) why = "dialogue";
					else if (use.f.ghost) why = "ghost";
					else if (g.pl.mountId && g.pl.mountId == use.f.id) why = "mount";
				}
			}
		}
		if (why) {
			++g.st.liveDenies;
			const double sec = std::floor(a_now);
			if (sec != g.safeSecond) {
				g.safeSecond = sec;
				g.safeLogs = 0;
			}
			if (g.safeLogs < 5) {
				++g.safeLogs;
				REX::INFO("SPN|SAFE|deny {:08X} {} at {} n={}", a_t ? a_t->GetFormID() : 0u, why, a_stage ? a_stage : "?", g.st.liveDenies);
			}
		}
		return why;
	}

	std::uint8_t AllowNow(std::uint32_t a_id, double a_now)
	{
		const Entry* e = FindEntry(a_id);
		if (!e) return kAllowNone;
		ActorFacts f = e->f;
		f.quarrel = g.quarrels.Has(a_id, a_now);
		std::uint32_t v = FinalVeto(f, g.pl, g.p, a_now);  // HOTFIX round5C: with the DEFEND lift
		if (a_now - e->lastExamined > 1.0) v |= kV21NotThere;
		return AllowedTiers(v, g.p);
	}

	void NoteAttacker(RE::Actor* a_cause, int a_kind, double a_now)
	{
		if (!a_cause || a_kind < 0 || a_kind > 2) return;
		auto*        pl = RE::PlayerCharacter::GetSingleton();
		const Vec3   cp = sp::fo4::ToVec(a_cause->GetPosition());
		Vec3         vp = cp;
		if (a_kind == 0 && pl) vp = sp::fo4::ToVec(pl->GetPosition());
		else if (a_kind == 1) {
			if (auto* me = sp::fo4::PeekPeasant()) vp = sp::fo4::ToVec(me->GetPosition());
		}
		g.attackers.Note(a_cause->GetFormID(), static_cast<HitKind>(a_kind), a_now, a_kind == 2 ? -1.0 : Length(cp - vp));
	}

	std::string Describe()
	{
		const auto& s = g.st;
		return std::format("high={} mh={} examined={} known={} kept={} bys={} maxKeptDist={:.0f} maxHighDist={:.0f} maxMhDist={:.0f} far6000High={} far6000Mh={} hostileFarHigh={} hostileFarMh={} "
		                   "asks={} liveAsks={} static={} bones={} drops={} hits={} attackers={} quarrels={}/{} inCombat={} angryInCombat={} crime={} unique={} alias={} essential={} "
		                   "scans={} cycles={} scanUs={:.0f}/{:.0f} refreshUs={:.1f}/{:.0f} faults={} broken={} afterLoad={} grid={}/{:.0f}/{:.0f} pre={:.0f}/{:.0f} boneSkips={} prot={} flipAsks={}",
			s.high, s.midHigh, s.examined, s.known, s.kept, s.bystanders, s.maxKeptDist, s.maxHighDist, s.maxMidHighDist, s.highFar, s.midHighFar, s.hostileFarHigh, s.hostileFarMid,
			s.asks, s.liveAsks, s.staticReads, s.boneReads, s.refreshDrops, s.hitsDrained, g.attackers.Size(), g.quarrels.Size(), s.quarrels, s.hostileInCombat, s.angryHostileInCombat,
			s.crimeHostile, s.uniqueHostile, s.aliasHostile, s.essentialHostile, s.scans, s.cycles, s.scanUsAvg, s.scanUsMax, s.refreshUsAvg, s.refreshUsMax, s.faults, g.broken,
			g.afterLoad, g.grid.uGrids, g.grid.farR, g.grid.maxR, g.p.preemptKeep, g.p.preemptKeepBusy, s.boneSkips, g.p.protectedTiers, s.flipAsks) +
		       std::format(" hf=def{}/lift{}/skip{}/hkNoQ{}/reask{}", g.p.defend, g.defendLifts, g.skips, g.hkNoQuarrel, g.reasks) +
		       std::format(" hf2=watch{}/asks{}/keeps{}/turns{}/kind{}of{}/race{}/inj{}", g.p.watch, g.wAsks, g.wKeeps, g.wTurns, g.kindTrue, g.kindReads, g.p.hostileKindRace,
			       g.injected) +
		       std::format(" expl={}of{}/off{} explFast={}", g.explosiveTrue, g.explosiveReads, g.explosiveOff ? 1 : 0, g.explosiveFast) +  // s11 fix lane 1 (s13 r1 lane 3: + explFast)
		       std::format(" melee={}of{}/off{}", g.meleeTrue, g.meleeReads, g.meleeOff ? 1 : 0) +  // completion side lane 4
		       std::format(" suic={}of{}/off{} gunSkips={}", g.suiciderTrue, g.suiciderReads, g.suiciderOff ? 1 : 0, g.explosiveGunSkips);  // explore cycle m1 fix lane 1
	}

	void ObserveTick(RE::Actor* a_me, double a_now, double a_dt)
	{
		if (!sp::fo4::threads::InPump()) return;
		(void)a_now;
		g.obsClock += std::clamp(a_dt, 0.0, 0.1);
		const double now = g.obsClock;
		auto&        world = rays::GetWorld();
		world.Begin(a_me, now);
		const Vec3 centre = g.pl.valid ? g.pl.feet : (a_me ? sp::fo4::ToVec(a_me->GetPosition()) : Vec3{});
		Tick(a_me, centre, now, a_dt);
		if (now < g.obsNextAt || !a_me) return;
		g.obsNextAt = now + 1.0;
		// SPN|OBS|: the kept list (<= 6) with sight rays for the 3 nearest (eye = feet + 100 u, the actor capsule)
		const Vec3  eye = sp::fo4::ToVec(a_me->GetPosition()) + Vec3{ 0, 0, 100 };
		std::string line;
		int         rays = 0;
		for (std::size_t i = 0; i < g.kept.size() && i < 6; ++i) {
			const auto& k = g.kept[i];
			int         vis = -1;
			if (rays < 3 && world.Ready()) {
				vis = world.ClearTo(eye, k.chest, k.id, 48.0) ? 1 : 0;
				++rays;
			}
			const double hitAge = now - k.LastHitGroupAt();
			line += std::format(" [{:08X} r{} d{:.0f} me{:.0f} a{:x} ck{} hit{} {}{} vis{} h{:.0f}]", k.id, KeepRank(k, now, g.p), k.distCentre, k.distMe, k.allow,
				static_cast<int>(k.combatKind), hitAge < 60 ? std::format("{:.1f}", hitAge) : "-", k.Ranged(g.p.rangedHitDist) ? "R" : "", k.Big(g.p.bigRaceSize) ? "B" : "", vis,
				k.height);
		}
		const auto& pf = g.pl;
		REX::INFO("SPN|OBS|kept={} known={} bys={} pl[sneak={} combat={} dlg={} mto={} spk={:08X} scene={} mount={} vats={} int={} cell={},{}] rays={}{}", g.kept.size(),
			g.entries.size(), g.bys.size(), pf.sneaking, pf.inCombat, pf.inDialogue, pf.menuTopicOpen, pf.speakerId, pf.inScene, pf.mounted, pf.vatsPlayback, pf.interior,
			pf.cellX, pf.cellY, rays, line);
		// G2 (SAFETY): ProcessLists::AreHostileActorsNear (ID 2234105) - how far out does the engine's own list reach, and
		// does it hold actors our scan does not keep? Every 10 s, observe mode only. Off: [Tabs] brain.scan.probeG2 = 0.
		if (now >= g.g2NextAt && Tunable("brain.scan.probeG2", 1.0) != 0.0) {
			g.g2NextAt = now + 10.0;
			if (auto* lists = RE::ProcessLists::GetSingleton()) {
				RE::ActorHandle hs[64];
				const int       n = SehHostileNear(lists, hs, 64);
				if (n < 0) {
					REX::WARN("SPN|W|probe G2: AreHostileActorsNear faulted; probe off");
					sp::cfg::SetTunable("brain.scan.probeG2", 0.0);
				} else {
					double maxD = 0;
					int    inKept = 0, resolved = 0;
					for (int i = 0; i < std::min(n, 64); ++i) {
						const auto a = hs[i].get();
						if (!a) continue;
						++resolved;
						maxD = std::max(maxD, Length(sp::fo4::ToVec(a->GetPosition()) - pf.feet));
						for (const auto& k : g.kept)
							if (k.id == a->GetFormID()) { ++inKept; break; }
					}
					REX::INFO("SPN|PROBE|G2 hostileNear n={} resolved={} maxDist={:.0f} inKept={} kept={} maxKeptDist={:.0f}", n, resolved, maxD, inKept, g.kept.size(), g.st.maxKeptDist);
				}
			}
		}
		// P2: does a pick through HIS cell see the ground under a far kept actor (neighbouring exterior cell)?
		if (now >= g.p2NextAt && !pf.interior) {
			const ActorFacts* far = nullptr;
			for (const auto& k : g.kept)
				if (k.distMe > 2500.0 && (!far || k.distMe > far->distMe)) far = &k;
			if (far) {
				g.p2NextAt = now + 5.0;
				auto*         t = Resolve(far->id);
				rays::Hit     a{}, b{};
				const Vec3    from = far->feet + Vec3{ 0, 0, 60 }, to = far->feet - Vec3{ 0, 0, 200 };
				const bool    ha = rays::Cast(world.Cell(), from, to, rays::kFloorBlock, a_me->Get3D(), t ? t->Get3D() : nullptr, nullptr, a, 5);
				const bool    hb = t && t->parentCell ? rays::Cast(t->parentCell, from, to, rays::kFloorBlock, a_me->Get3D(), t->Get3D(), nullptr, b, 5) : false;
				REX::INFO("SPN|PROBE|P2 {:08X} d={:.0f} hisCell {:08X} hit={} z={:.0f} ownCell {:08X} hit={} z={:.0f} sameCell={}", far->id, far->distMe,
					world.Cell() ? world.Cell()->GetFormID() : 0u, ha, a.point.z, t && t->parentCell ? t->parentCell->GetFormID() : 0u, hb, b.point.z,
					t && t->parentCell == world.Cell());
			}
		}
	}
}
