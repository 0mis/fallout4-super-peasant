// OWNER: R6 (round 6 step 3, CORE lane).  See warden_glue.h. Engine facts: steps\s3\HOOKS.md (FINAL 02:17).
#include "warden_glue.h"

#include "brain_glue.h"
#include "config.h"
#include "core/brain.h"
#include "core/events.h"
#include "core/p4_eye.h"
#include "core/percept.h"
#include "core/r6_checks.h"
#include "core/router.h"
#include "core/shadow.h"
#include "core/shadow_cost.h"
#include "core/warden.h"
#include "core/warden_ext.h"  // R6 step 4: Decision -> brain::WardenExt
#include "fo4/actors.h"       // s4 fix lane 2: GetController (the defend jump's controller test)
#include "fo4/navmesh.h"
#include "fo4/rays.h"
#include "fo4/scan.h"
#include "fo4/scan_world.h"
#include "fo4/threads.h"
#include "runtime.h"

#include <algorithm>
#include <bit>
#include <span>
#include <chrono>
#include <cmath>
#include <cstring>
#include <excpt.h>
#include <format>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sp::warden_glue
{
	using sp::cfg::Tunable;
	using sp::math::Length;
	namespace W = sp::warden;
	namespace scan = sp::fo4::scan;

	namespace
	{
		using Clock = std::chrono::steady_clock;
		constexpr std::uint32_t kPlayerId = 0x14;

		struct AskMemo
		{
			bool   hostile{ false };
			double at{ -1e9 };
			double hitAt{ -1e9 };  // the hit edge we asked for
			int    reasks{ 0 };    // +0.1 s / +0.3 s re-asks of a "false" (HOOKS s3 B1 trap)
		};

		// ------------------------------------------------------------------ SEH shells (no objects with destructors inside)
		using Fn = void (*)(void*);
		bool SehRun(Fn a_fn, void* a_ctx, unsigned long* a_code) noexcept
		{
			__try {
				a_fn(a_ctx);
				return true;
			} __except (*a_code = GetExceptionCode(), 1) {
				return false;
			}
		}
		// s4 fix lane 2 (REVIEW DefendJump): his controller is the proven RB controller NOW (the runtime's DoBlink 'ctlClass'
		// test), read in a shell; false while it is the proxy (about 1.3 % of the time) or gone
		bool RbNowImpl(RE::Actor* a_me, std::uintptr_t a_vt)
		{
			auto* con = sp::fo4::GetController(a_me);
			return con && a_vt && *reinterpret_cast<const std::uintptr_t*>(con) == a_vt;
		}
		bool SehRbNow(RE::Actor* a_me, std::uintptr_t a_vt, bool* a_ok) noexcept
		{
			__try {
				*a_ok = RbNowImpl(a_me, a_vt);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehRead(std::uintptr_t a_at, std::uint8_t* a_out, int a_n) noexcept
		{
			__try {
				std::memcpy(a_out, reinterpret_cast<const void*>(a_at), static_cast<std::size_t>(a_n));
				return true;
			} __except (1) {
				return false;
			}
		}

		// ------------------------------------------------------------------ the render camera (HOOKS s3 B8)
		struct CamRead
		{
			bool  ok{ false };
			int   state{ -1 };
			float pos[3]{};
			float root[3]{};
			bool  haveRoot{ false };
			float m[4][4]{};
			float port[4]{};  // l r t b
		};
		void CamImpl(CamRead* a_out)
		{
			a_out->ok = false;
			if (auto* pc = RE::PlayerCamera::GetSingleton()) {
				const auto* st = pc->currentState.get();
				a_out->state = st ? static_cast<int>(st->id.underlying()) : -1;
				if (const auto* r = pc->cameraRoot.get()) {
					a_out->root[0] = r->world.translate.x;
					a_out->root[1] = r->world.translate.y;
					a_out->root[2] = r->world.translate.z;
					a_out->haveRoot = true;
				}
			}
			const RE::NiCamera* nc = RE::Main::WorldRootCamera();
			if (!nc) return;
			a_out->pos[0] = nc->world.translate.x;
			a_out->pos[1] = nc->world.translate.y;
			a_out->pos[2] = nc->world.translate.z;
			std::memcpy(a_out->m, nc->worldToCam, sizeof(a_out->m));
			a_out->port[0] = nc->port.left;
			a_out->port[1] = nc->port.right;
			a_out->port[2] = nc->port.top;
			a_out->port[3] = nc->port.bottom;
			a_out->ok = true;
		}
		bool SehCam(CamRead* a_out) noexcept
		{
			__try {
				CamImpl(a_out);
				return true;
			} __except (1) {
				return false;
			}
		}
		// on screen: the projection returns true AND the point is in front (z > 0) AND inside the port (+-0.02)
		bool OnScreen(const CamRead& c, const Vec3& p, float* a_z)
		{
			float                   x = 0, y = 0, z = 0;
			const RE::NiRect<float> port{ c.port[0], c.port[1], c.port[2], c.port[3] };
			const RE::NiPoint3      pt{ static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z) };
			const bool              r = RE::NiCamera::WorldPtToScreenPt3(c.m, port, pt, x, y, z, 1e-5f);
			if (a_z) *a_z = z;
			const float lo = std::min(c.port[0], c.port[1]) - 0.02f, hi = std::max(c.port[0], c.port[1]) + 0.02f;
			const float bo = std::min(c.port[2], c.port[3]) - 0.02f, to = std::max(c.port[2], c.port[3]) + 0.02f;
			return r && z > 0 && x >= lo && x <= hi && y >= bo && y <= to;
		}
		struct ProjCtx
		{
			const CamRead* c;
			Vec3           p;
			float          z;
			bool           on;
		};
		void ProjImpl(void* a_ctx)
		{
			auto& x = *static_cast<ProjCtx*>(a_ctx);
			x.on = OnScreen(*x.c, x.p, &x.z);
		}

		// ------------------------------------------------------------------ state
		struct World6 final : sp::router::IWorld6
		{
			bool          LineClear(const Vec3& a_from, const Vec3& a_to, std::uint32_t a_targetId, double a_eps) override;
			std::uint64_t rays{ 0 };
		};

		struct State
		{
			int                                        mode{ 0 };
			double                                     modeAt{ -1e9 };
			bool                                       off{ false };
			sp::r6check::Features                      feat{};
			W::Warden                                  warden;
			W::Params                                  wp{};
			sp::router::Router                         router;
			sp::router::Params                         rp{};
			double                                     pAt{ -1e9 };
			sp::percept::HitCounter                    hits;
			sp::percept::SightMemo                     sight;
			std::unordered_map<std::uint32_t, AskMemo> asks;
			std::unordered_map<std::uint32_t, double>  lastHitGroup;
			std::vector<scan::ActorFacts>              merged;
			std::vector<int>                           sel;
			std::vector<W::Cand>                       cands;
			std::vector<W::ReachEst>                   reach;
			std::unordered_set<std::uint32_t>          keptIds;
			W::Decision                                d{};
			W::ShadowReport                            report;
			std::size_t                                rr{ 0 };
			World6                                     world;
			sp::events::RateLimiter                    lim{ 8 };
			// cost of the shadow tick (5 s windows)
			std::uint64_t                              frames{ 0 }, framesWin{ 0 };
			double                                     usSum{ 0 }, usMax{ 0 };
			std::vector<float>                         usWin;
			double                                     statAt{ -1e9 };
			// B9: Percept rays after loads / cell changes / continuity events, refused picks
			int                                        quietFrames{ 10 };
			double                                     quietUntil{ -1e9 };
			std::uint32_t                              meCell{ 0 }, plCell{ 0 };
			bool                                       raysStopped{ false };
			std::uint64_t                              sightRays{ 0 }, sightRefused{ 0 }, skippedAfterLoad{ 0 }, stoppedByRefusal{ 0 };
			double                                     sightLineAt{ -1e9 };
			// P-R6-5
			std::uint64_t                              asksN{ 0 }, askHostile{ 0 }, askFriendly{ 0 }, askFails{ 0 }, askLateTrue{ 0 }, askLines{ 0 };
			int                                        askFrameMax{ 0 };
			double                                     askUsSum{ 0 }, askUsMax{ 0 };
			std::vector<float>                         askUs;
			// hp
			double                                     hpLineAt{ -1e9 }, hpLast{ -2 }, hpNow{ 1 };
			bool                                       hpOff{ false };
			// P-R6-4 / camera
			std::uint64_t                              relocs{ 0 }, relocSeenCam{ 0 }, relocSeenEye{ 0 }, relocDisagree{ 0 };
			std::uint64_t                              relocEyeUnknown{ 0 };  // s3 fix lane 3: eye verdicts that rest on "no ray allowed"
			double                                     camLineAt{ -1e9 };
			// hit never-set
			std::uint64_t                              hitNever{ 0 }, hitRefused{ 0 };
			std::unordered_map<std::uint32_t, double>  hitNeverLogAt;
			double                                     lastNow{ 0 }, lastWall{ 0 };
			// integrator (step 3 tune): the shadow tick's cost by part (5 s windows): keep / ask / sight / cand / hp+facts /
			// router / warden / shadow lines
			double                                     partUs[8]{};
			double                                     partMax[8]{};  // R6 step 4 (step-3 REVIEW 3): the worst single tick per part in the window
			// integrator (step 3 tune): [Tabs] warden.tickDt - the shadow ticks at most every tickDt s (0 = every brain frame);
			// the skipped frames' dt is carried into the next tick, a change of the D2f target ticks at once
			double                                     dtAcc{ 0 };
			std::uint32_t                              lastOld{ 0 };
			bool                                       ticked{ false };
			std::uint64_t                              ticks{ 0 }, ticksWin{ 0 };
			// integrator (cost): the merged keep list is rebuilt only when the scan's serial or the kept ids change
			std::uint64_t                              mergeSerial{ ~0ull };
			std::size_t                                keptN{ 0 };
			// fix lane 1 (cost, step-3 gate dt 1080u 0.357 ms): the cost bank ([Tabs] warden.capUs) and the sight plan
			// (core/shadow_cost.h): reused D2f traces, band F / none skipped beyond warden.sightFarD, rays the bank refused
			sp::shadow_cost::Params                    cp{};
			sp::shadow_cost::Bank                      bank;
			std::uint64_t                              sightReused{ 0 }, sightFarSkip{ 0 }, sightBudget{ 0 }, lineBudget{ 0 };
			// R6 step 4 (Warden 2: the Warden picks): the directive of this frame, the lost-transfer clock, the near reach
			// failures (the defend jump's "close-range law failed twice"), the act comparison (the Warden vs the acting target)
			sp::brain::WardenExt                       ext{};
			double                                     lostFor{ 0 }, otherFor{ 0 }, lostBest{ -1 }, lostProgAt{ -1e9 };
			std::unordered_map<std::uint32_t, std::array<double, 2>> nearFails;  // brain clock, newest first
			std::uint32_t                              actBrain{ ~0u }, actWarden{ ~0u };
			std::uint64_t                              actFrames{ 0 }, actAgree{ 0 }, actMissing{ 0 }, actFails{ 0 }, actNearFails{ 0 }, defendJumps{ 0 }, pinMiss{ 0 };
			double                                     actDisagreeS{ 0 }, actLastNow{ -1e9 };
			bool                                       bbWarden{ true };  // [Tabs] bb.warden: the Warden's frames in the black box
			std::uintptr_t                             rbVtbl{ 0 };       // HOOKS s4 C: VTABLE_bhkCharRigidBodyController (0 = not proven)
			double                                     spaceLineAt{ -1e9 };
			bool                                       teleJumpOff{ false }, teleLostOff{ false };  // crash shields (HOOKS s4 D)
			std::uint64_t                              djGated{ 0 }, djGatedProxy{ 0 };            // s4 fix lane 2: jump permissions withheld (tele.jump off / proxy)
			bool                                       rbNowOff{ false };                            // s4 fix lane 2: the controller read's crash shield fired
			// step 4 integrator (TESTPLAN R1): the runtime's inside-solid / snap-loop put-back while acting = the ARCH 4.11 wedged
			// relocation (his take-off out of the player's eye and camera view, at most one per tele.wedgedEvery s)
			double                                     wedgedAt{ -1e9 }, wedgedLineAt{ -1e9 };
			std::uint64_t                              wedgedN{ 0 }, wedgedRefused{ 0 };
			// step 4 integrator (cost, [Tabs] warden.actTickDt): the acting Warden decides at most every actTickDt s of brain
			// clock, and at once on any event that can change its decision (ActForce); the frames in between hand the brain the
			// last directive (its target re-pinned). Signatures of the scan facts that matter for an event
			bool                                       forceTick{ false };
			std::uint64_t                              keptSig{ 0 }, knownSig{ 0 }, sigSerial{ ~0ull };
			std::uint64_t                              actSkips{ 0 }, actForced{ 0 }, actTicks{ 0 };
			std::unordered_map<std::uint32_t, std::pair<double, bool>> kindAtHit;  // step 4 integrator: the hostile kind at the player's last hit
		};
		State g;

		double WallNow() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

		void Shield(const char* a_where, unsigned long a_code)
		{
			g.off = true;
			REX::ERROR("SPN|R6|off feat=warden why=seh where={} code=0x{:08X} (the shadow Warden is off for this session; the D2f brain is untouched)", a_where, a_code);
			sp::runtime::RequestDump("warden crash shield");
		}

		// ------------------------------------------------------------------ HOOKS s3 C: load-time proofs
		void RunChecks()
		{
			if (g.feat.ran) return;
			auto read = [](std::uint64_t a_id, std::uint32_t& a_rva, std::uint32_t a_off, std::uint8_t* a_out, int a_n) -> bool {
				std::uintptr_t addr = 0;
				try {
					const REL::ID id(a_id);
					a_rva = static_cast<std::uint32_t>(id.offset());
					addr = id.address();
				} catch (...) {
					return false;
				}
				if (a_n <= 0) return true;
				return SehRead(addr + a_off, a_out, a_n);
			};
			auto log = [](const sp::r6check::Proof& p, bool ok, const std::string& why) {
				if (ok) REX::INFO("SPN|R6|check {} id={} rva={:X}+{:X}: ok", p.name, p.id, p.rva, p.off);
				else REX::WARN("SPN|R6|check {} id={} rva={:X}+{:X}: REFUSED why={} -> feat {} off", p.name, p.id, p.rva, p.off, why, p.feat);
			};
			g.feat = sp::r6check::RunAll(read, log);
			g.feat.ran = true;
			// R6 step 4 (HOOKS s4 C): the RB controller's vtable slots 0x33 / 0x34 = GetPositionImpl / SetPositionImpl (addresses
			// at run time); the RB vtable is also the class test of every defend jump (DoBlink)
			auto slot = [&](const char* a_name, std::uint32_t a_off, std::uint64_t a_fn) {
				std::string why;
				bool        ok = false;
				try {
					const std::uintptr_t vt = REL::ID(152147).address();
					const std::uintptr_t fn = REL::ID(a_fn).address();
					std::uintptr_t       q = 0;
					if (!SehRead(vt + a_off, reinterpret_cast<std::uint8_t*>(&q), 8)) why = "unreadable";
					else if (q != fn) why = "slot";
					else {
						ok = true;
						g.rbVtbl = vt;
					}
				} catch (...) {
					why = "id";
				}
				if (ok) REX::INFO("SPN|R6|check {} id=152147 +{:X}: ok", a_name, a_off);
				else {
					g.feat.teleJump = false;
					REX::WARN("SPN|R6|check {} id=152147 +{:X}: REFUSED why={} -> feat tele.jump off", a_name, a_off, why);
				}
			};
			slot("RB vtable slot 0x34 (SetPositionImpl)", 0x1A0, 2278671);
			slot("RB vtable slot 0x33 (GetPositionImpl)", 0x198, 2278672);
			if (!g.feat.teleJump) g.rbVtbl = 0;
			scan::SetR6Checks(g.feat.killRole, g.feat.workshop, true);
			REX::INFO("SPN|R6|checks hitAsk={} cam={} workshop={} killRole={} immobile={} teleJump={} teleLost={} probeKill={}", g.feat.hitAsk, g.feat.cam, g.feat.workshop, g.feat.killRole,
				g.feat.immobile, g.feat.teleJump, g.feat.teleLost, g.feat.probeKill);
		}

		void LoadParams(double a_now)
		{
			if (a_now - g.pAt < 1.0 && a_now >= g.pAt) return;
			g.pAt = a_now;
			g.wp = W::Params::Load();
			g.rp = sp::router::Params::Load();
			g.lim.SetPerSecond(static_cast<int>(std::clamp(Tunable("warden.linesPerSec", 8.0), 1.0, 60.0)));
		}

		void Line(const char* a_kind, std::uint32_t a_id, const std::string& a_body)
		{
			int supp = 0;
			if (!g.lim.Allow(sp::events::KindId(a_kind), a_id, g.lastNow, supp)) return;
			if (supp) REX::INFO("SPN|WARDEN|{} {} supp={}", a_kind, a_body, supp);
			else REX::INFO("SPN|WARDEN|{} {}", a_kind, a_body);
		}
		void DrainLines()
		{
			auto& ls = g.warden.Lines();
			for (const auto& l : ls) {
				const std::string k = std::string("w") + l.kind;
				Line(k.c_str(), l.id, l.body);
			}
			ls.clear();
		}

		// the player's eye / view (as brain_glue builds them for the D2f brain)
		void PlayerView(Vec3& a_eye, Vec3& a_view, const Vec3& a_feet)
		{
			a_eye = a_feet + Vec3{ 0, 0, Tunable("brain.playerEyeZ", 120.0) };
			a_view = { 0, 1, 0 };
			if (auto* pl = RE::PlayerCharacter::GetSingleton()) {
				const double h = pl->data.angle.z, pt = pl->data.angle.x;
				a_view = Vec3{ std::sin(h) * std::cos(pt), std::cos(h) * std::cos(pt), -std::sin(pt) };
				if (!sp::math::IsFinite(a_view) || Length(a_view) < 0.5) a_view = { 0, 1, 0 };
			}
		}

		// ------------------------------------------------------------------ B9: may the Percept cast a ray now?
		bool RaysOk() { return !g.raysStopped && g.quietFrames <= 0 && g.lastWall >= g.quietUntil; }
		void Quiet()
		{
			g.quietFrames = 10;
			g.quietUntil = WallNow() + 0.2;
		}
		// one Percept ray: false when not allowed (a_out untouched); a refused pick stops this frame's rays
		bool Ray(const Vec3& a_from, const Vec3& a_to, std::uint32_t a_target, double a_eps, bool& a_out)
		{
			if (!RaysOk()) return false;
			const auto r0 = sp::fo4::rays::GetStats().refused;
			a_out = scan::GetBrainWorld().ClearTo(a_from, a_to, a_target, a_eps);
			if (sp::fo4::rays::GetStats().refused != r0) {
				g.raysStopped = true;
				++g.stoppedByRefusal;
				++g.sightRefused;
				a_out = false;  // fix c: a refused pick is blocked
			}
			return true;
		}

		bool World6::LineClear(const Vec3& a_from, const Vec3& a_to, std::uint32_t a_targetId, double a_eps)
		{
			bool clear = false;
			if (!Ray(a_from, a_to, a_targetId, a_eps, clear)) return false;
			++rays;
			return clear;
		}

		// keep:ask:sight:cand:hp:router:warden:shadow (us per frame in this window)
		std::string PartsText()
		{
			const double n = g.framesWin ? static_cast<double>(g.framesWin) : 1.0;
			return std::format("keep:{:.1f},ask:{:.1f},sight:{:.1f},cand:{:.1f},hp:{:.1f},router:{:.1f},warden:{:.1f},shadow:{:.1f}", g.partUs[0] / n, g.partUs[1] / n,
				g.partUs[2] / n, g.partUs[3] / n, g.partUs[4] / n, g.partUs[5] / n, g.partUs[6] / n, g.partUs[7] / n) +
				std::format(" partsMax={:.0f},{:.0f},{:.0f},{:.0f},{:.0f},{:.0f},{:.0f},{:.0f}", g.partMax[0], g.partMax[1], g.partMax[2], g.partMax[3], g.partMax[4], g.partMax[5], g.partMax[6], g.partMax[7]);
		}

		double P99(std::vector<float>& v)
		{
			if (v.empty()) return 0.0;
			const std::size_t k = std::min(v.size() - 1, static_cast<std::size_t>(0.99 * static_cast<double>(v.size())));
			std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
			return static_cast<double>(v[k]);
		}

		// ------------------------------------------------------------------ the shadow tick (inside the SEH shell)
		void TickImpl(void* a_ctx)
		{
			const ShadowIn& in = *static_cast<const ShadowIn*>(a_ctx);
			const auto      t0 = Clock::now();
			auto            tp = t0;
			auto            part = [&](int a_i) {
				const auto n = Clock::now();
				const double us = std::chrono::duration<double, std::micro>(n - tp).count();
				g.partUs[a_i] += us;
				g.partMax[a_i] = std::max(g.partMax[a_i], us);
				tp = n;
			};
			const double    now = in.now;
			g.lastNow = now;
			g.lastWall = in.wall;
			g.raysStopped = false;
			if (g.quietFrames > 0) --g.quietFrames;
			LoadParams(now);
			const auto& pl = scan::GetPlayerFacts();
			const Vec3  torso = in.haveSim ? in.simTorso : in.meFeet + Vec3{ 0, 0, 80 };
			// B9: his or the player's cell changed -> no Percept rays for 10 frames / 0.2 s
			if (in.meCell != g.meCell || pl.cellId != g.plCell) {
				if (g.meCell || g.plCell) Quiet();
				g.meCell = in.meCell;
				g.plCell = pl.cellId;
			}

			// ---- the R6 keep list: kept facts (refreshed every frame) first, then every other relevant known actor
			// integrator (cost, step 3 tune): the known facts change only at a full scan (ScanSerial, 0.1 s), so the merged list
			// and the selection are rebuilt then or when the kept ids change; in between only the kept facts (refreshed every
			// frame) are copied into their slots (the same values a full rebuild would copy)
			{
				const auto          kept = scan::Kept();
				const std::uint64_t serial = scan::ScanSerial();
				bool                same = serial == g.mergeSerial && kept.size() == g.keptN && g.merged.size() >= kept.size();
				// the kept list is re-sorted by distance every frame: match by id (any order) among the first keptN slots
				for (std::size_t i = 0; same && i < kept.size(); ++i) {
					std::size_t j = 0;
					while (j < g.keptN && g.merged[j].id != kept[i].id) ++j;
					if (j < g.keptN) g.merged[j] = kept[i];
					else same = false;
				}
				if (!same) {
					g.merged.clear();
					g.keptIds.clear();
					for (const auto& k : kept) {
						g.merged.push_back(k);
						g.keptIds.insert(k.id);
					}
					for (const auto& k : scan::KnownFacts())
						if (!g.keptIds.contains(k.id) && sp::percept::Relevant(k, now)) g.merged.push_back(k);
					const int cap = static_cast<int>(std::clamp(g.wp.keepCap, 1.0, static_cast<double>(W::kMaxCands)));
					sp::percept::SelectKeep(g.merged, now, cap, g.sel);
					g.mergeSerial = serial;
					g.keptN = kept.size();
				}
			}

			part(0);
			// ---- live hit-cause asks (N08 b, probe P-R6-5): a new hit on the group by an actor not known hostile; a
			// "false" is asked again at +0.1 s and +0.3 s while the hit is younger than 1 s (HOOKS s3 B1)
			int       asksFrame = 0;
			const int askCap = g.feat.hitAsk ? static_cast<int>(std::clamp(Tunable("warden.hitAsksPerFrame", 4.0), 0.0, 16.0)) : 0;
			for (int i : g.sel) {
				if (asksFrame >= askCap) break;
				const auto&  f = g.merged[static_cast<std::size_t>(i)];
				const double lh = f.LastHitGroupAt();
				double&      prev = g.lastHitGroup[f.id];
				const bool   edge = lh > prev + 1e-9 && lh > -1e8;
				prev = std::max(prev, lh);
				AskMemo* m = nullptr;
				if (auto it = g.asks.find(f.id); it != g.asks.end()) m = &it->second;
				const bool reask = !edge && m && !m->hostile && now - m->hitAt < 1.0 && m->reasks < 2 && now - m->at >= (m->reasks == 0 ? 0.1 : 0.2);
				if (!(edge || reask)) continue;
				if (edge && f.hostileKnown && f.hostile) continue;  // already known hostile: nothing to ask
				bool         h = false;
				const auto   a0 = Clock::now();
				const bool   ok = scan::AskHostileR6(f.id, &h);
				const double us = std::chrono::duration<double, std::micro>(Clock::now() - a0).count();
				++asksFrame;
				if (!ok) {
					++g.askFails;
					continue;
				}
				++g.asksN;
				(h ? g.askHostile : g.askFriendly)++;
				g.askUsSum += us;
				g.askUsMax = std::max(g.askUsMax, us);
				if (g.askUs.size() < 4096) g.askUs.push_back(static_cast<float>(us));
				const char* why = (now - f.lastHitPlayerAt < 1.0) ? "hitP" : (now - f.lastHitMeAt < 1.0 ? "hitMe" : "hitMate");
				const int   prevAns = m ? (m->hostile ? 1 : 0) : -1;
				const bool  flip = reask && h;
				if (flip) ++g.askLateTrue;
				if (edge) g.asks[f.id] = AskMemo{ h, now, lh, 0 };
				else if (m) {
					m->hostile = h;
					m->at = now;
					++m->reasks;
				}
				if (g.askLines < 300 || flip) {
					++g.askLines;
					REX::INFO("SPN|R6|PROBE|P5 ask id={:08X} why={} age={:.0f} ans={} prev={} us={:.2f} nFrame={} flip={}", f.id, why, (now - lh) * 1000.0, h ? 1 : 0,
						prevAns < 0 ? std::string("-") : std::to_string(prevAns), us, asksFrame, flip ? 1 : 0);
				}
			}
			g.askFrameMax = std::max(g.askFrameMax, asksFrame);

			part(1);
			// ---- sight round robin (P4 rays, B9 rules): the oldest checks first, each candidate at most every sightEvery s
			// fix lane 1 (cost): a due candidate first takes the D2f brain's own trace of it (no ray), a band F / none one
			// beyond warden.sightFarD gets none, and a ray is cast only while the cost bank can pay its running price
			const double every = std::clamp(Tunable("warden.sightEvery", 0.25), 0.02, 5.0);
			auto         elapsedUs = [&]() { return std::chrono::duration<double, std::micro>(Clock::now() - t0).count(); };
			{
				const int         rays = static_cast<int>(std::clamp(Tunable("warden.sightRays", 2.0), 0.0, 8.0));
				const double      maxD = Tunable("warden.sightMaxD", 6000.0);
				const std::size_t n = g.sel.size();
				int               cast = 0;
				if (!RaysOk() && n) ++g.skippedAfterLoad;
				// pass 1 (no rays): every due candidate - the D2f's trace or the far skip at once; the ones that need a ray are
				// ranked oldest check first (a fixed list order starved the last of several near foes: FIX1-COST-3)
				std::pair<double, std::size_t> want[8];
				int                            nWant = 0;
				for (std::size_t k = 0; k < n; ++k) {
					const std::size_t j = (g.rr + k) % n;
					const auto&       f = g.merged[static_cast<std::size_t>(g.sel[j])];
					const double      checkedAt = g.sight.CheckedAt(f.id);
					if (now - checkedAt < every) continue;
					// no sight ray for an actor the Warden never targets (last frame: a never-set row other than range) or
					// beyond warden.sightMaxD (a direct dash from that far is never the estimate that matters)
					const W::Score* ls = g.warden.LastScore(f.id);
					if (ls && ls->never != W::Never::kOk && ls->never != W::Never::kRange) continue;
					const double dMe = Length(f.feet - in.meFeet);
					if (dMe > maxD) continue;
					double     bAt = -1e9;
					bool       bClear = false;
					const bool haveB = in.brain && in.brain->LastSight(f.id, bAt, bClear);
					const int  band = ls ? static_cast<int>(ls->band) : sp::percept::RoughBand(f, now);
					const auto plan = sp::shadow_cost::PlanSight(now, every, haveB ? bAt : -1e9, band, dMe, g.cp);
					if (plan == sp::shadow_cost::Sight::kReuse) {
						g.sight.Note(f.id, bClear, bAt);
						++g.sightReused;
						continue;
					}
					if (plan == sp::shadow_cost::Sight::kFarSkip) {
						g.sight.Note(f.id, false, now);  // not seen: its reach is the unknown estimate
						++g.sightFarSkip;
						continue;
					}
					// keep the `rays` oldest (insertion into a small sorted list)
					const int cap = std::min(rays, 8);
					if (nWant < cap || (cap > 0 && checkedAt < want[nWant - 1].first)) {
						int at = nWant < cap ? nWant++ : nWant - 1;
						while (at > 0 && want[at - 1].first > checkedAt) {
							want[at] = want[at - 1];
							--at;
						}
						want[at] = { checkedAt, j };
					}
				}
				// pass 2: the rays, oldest first, while the B9 rules allow rays and the cost bank's ray credit pays
				for (int w = 0; w < nWant && RaysOk(); ++w) {
					const auto& f = g.merged[static_cast<std::size_t>(g.sel[want[w].second])];
					if (!g.bank.CanRay(elapsedUs(), g.cp)) {
						++g.sightBudget;
						break;
					}
					const Vec3 chest = (std::abs(f.chest.x) + std::abs(f.chest.y) + std::abs(f.chest.z) > 0) ? f.chest : f.feet + Vec3{ 0, 0, 90 };
					bool       clear = false;
					const auto r0 = Clock::now();
					if (!Ray(torso, chest, f.id, 40.0, clear)) break;
					g.bank.NoteRay(std::chrono::duration<double, std::micro>(Clock::now() - r0).count());
					g.sight.Note(f.id, clear, now);
					++cast;
					++g.sightRays;
				}
				if (n) g.rr = (g.rr + 1) % n;
				if (in.oldTarget && in.oldVisible) g.sight.Note(in.oldTarget, true, now);  // the D2f brain's own sight of its target
			}

			part(2);
			// ---- candidates
			g.cands.clear();
			for (int i : g.sel) {
				const auto&        f = g.merged[static_cast<std::size_t>(i)];
				sp::percept::Extra x;
				x.killPending = brain_glue::KillPending(f.id);
				if (in.brain) {
					x.denied = in.brain->Denied(f.id);
					x.cleared = in.brain->Cleared(f.id, now);
				}
				x.visible = g.sight.VisibleNow(f.id, now, every * 1.5);
				x.seenAt = g.sight.SeenAt(f.id);
				x.hitsOnPlayer3s = g.hits.Note(f.id, f.lastHitPlayerAt, now);
				x.self = (f.veto & scan::kV01Self) != 0 && !f.child;
				x.useImmobile = g.feat.immobile;
				x.useKillRole = g.feat.killRole;
				x.useWorkshop = g.feat.workshop;
				x.playerId = kPlayerId;
				auto c = sp::percept::ToCand(f, pl, x, torso, in.centre, now);
				if (auto it = g.asks.find(f.id); it != g.asks.end() && now - it->second.at <= 1.0 && it->second.at >= f.hostileAt) {
					c.ev.hostile = it->second.hostile;
					c.ev.hostileAsked = it->second.at == now;
				}
				// step 4 integrator ([Tabs] warden.kindAtHit; in game hf HF1 14:58:57-58: the player shot the dormant Swan, the
				// scan said "hostile kind" at the shot and "not" 0.35 s later -> N14 dropped it, it was re-picked 0.2 s later and
				// killed in 10 ms, so the Runner never saw it targeted): the hostile-kind fact seen at the player's hit holds for
				// warden.playerFightFor s after that hit (the scan's own rule: "the player's hit makes it a foe")
				if (f.playerHitAt > -1e8 && Tunable("warden.kindAtHit", 0.0) >= 0.5) {
					auto& k = g.kindAtHit[f.id];
					if (k.first != f.playerHitAt) k = { f.playerHitAt, f.hostileKind };
					else if (f.hostileKind) k.second = true;
					if (k.second && now - f.playerHitAt <= g.wp.playerFightFor && now >= f.playerHitAt) c.ev.hostileKind = true;
				}
				g.cands.push_back(c);
			}

			part(3);
			// ---- the player's health: native every frame (HOOKS s3 B2); the Papyrus value when the read fails
			double hp = -1;
			{
				float cur = 1, mx = 1;
				// R6 step 4 (step-3 REVIEW 7): a bad read (a load, the player's death) is the Papyrus value for THIS tick only; the
				// native read is never switched off for the session (SehPlayerHp is its own crash shield)
				if (sp::runtime::PlayerHpRaw(&cur, &mx)) hp = std::clamp(static_cast<double>(cur) / static_cast<double>(mx), 0.0, 1.0);
				else {
					if (!g.hpOff) {
						g.hpOff = true;  // (the first one is logged)
						REX::WARN("SPN|R6|percept.hp read failed once (the Papyrus SetVerdicts health is used for such ticks)");
					}
					hp = brain_glue::LastVerdictHp();
				}
				g.hpNow = hp;
				const bool changed = std::abs(hp - g.hpLast) > 0.05;
				if ((in.wall - g.hpLineAt >= 1.0 || in.wall < g.hpLineAt) || (changed && in.wall - g.hpLineAt >= 0.2)) {
					g.hpLineAt = in.wall;
					g.hpLast = hp;
					REX::INFO("SPN|R6|PROBE|hp t={:.3f} cur={:.1f} max={:.1f} frac={:.3f} papyrus={:.2f} lagFrames=-1", now, cur, mx, hp, brain_glue::LastVerdictHp());
				}
			}

			// ---- facts for the Warden
			W::WFacts wf;
			wf.now = now;
			wf.dt = in.dt;
			wf.wall = in.wall;
			wf.me.feet = in.meFeet;
			wf.me.chest = torso;
			wf.me.eye = in.meEye;
			wf.me.simTorso = torso;
			wf.me.vel = in.meVel;
			wf.me.fist = torso;
			wf.me.down = in.meDown;
			wf.me.interiorCell = in.interior;
			// R6 step 4: the lost-transfer clock (ARCH §4.11, HOOKS s4 T5): the player in another space - another interior cell,
			// or another worldspace outdoors (an exterior cell change is no separation; his cell unknown while loading = never) -
			// for tele.lostSeconds; or his guard distance beyond tele.lostDist 8000 u with no 300 u of progress toward the player
			// for tele.lostNoProg 30 s. Me::lostFor = the seconds the condition has held.
			{
				const bool known = in.meCellKnown && pl.valid && pl.cellId != 0;
				const bool other = known && ((in.interior || pl.interior) ? in.meCell != pl.cellId : (in.meWorld != 0 && pl.worldId != 0 && in.meWorld != pl.worldId));
				g.otherFor = other ? g.otherFor + std::max(0.0, in.dt) : 0.0;
				const double dP = pl.valid ? W::GuardDist(in.meFeet, pl.feet) : 0.0;
				if (pl.valid && dP > Tunable("tele.lostDist", 8000.0)) {
					if (g.lostBest < 0 || dP < g.lostBest - 300.0) {
						g.lostBest = dP;
						g.lostProgAt = now;
					}
				} else {
					g.lostBest = -1;
					g.lostProgAt = now;
				}
				const double noProg = (g.lostBest >= 0 && now >= g.lostProgAt) ? now - g.lostProgAt : 0.0;
				const double noProgNeed = std::clamp(Tunable("tele.lostNoProg", 30.0), 1.0, 600.0);
				g.lostFor = std::max(g.otherFor, noProg >= noProgNeed ? noProg - noProgNeed + g.wp.lostSeconds : 0.0);
				// HOOKS s4 E: SPN|R6|PROBE|space (1 Hz while in another space or beyond 4000 u)
				if ((other || dP > 4000.0) && (in.wall - g.spaceLineAt >= 1.0 || in.wall < g.spaceLineAt)) {
					g.spaceLineAt = in.wall;
					auto spaceName = [](bool a_known, bool a_int, std::uint32_t a_cell, std::uint32_t a_ws) {
						return !a_known ? std::string("?") : a_int ? std::format("I:{:08X}", a_cell) : std::format("E:{:08X}", a_ws);
					};
					REX::INFO("SPN|R6|PROBE|space t={:.3f} me={} pl={} same={} sepFor={:.2f} guard={:.0f} lostFor={:.2f} noProg={:.1f}", now, spaceName(in.meCellKnown, in.interior, in.meCell, in.meWorld),
						spaceName(pl.valid && pl.cellId != 0, pl.interior, pl.cellId, pl.worldId), other ? 0 : 1, g.otherFor, dP, g.lostFor, noProg);
				}
			}
			wf.me.lostFor = g.lostFor;
			wf.player.feet = pl.valid ? pl.feet : in.centre;
			PlayerView(wf.player.eye, wf.player.view, wf.player.feet);
			wf.player.camera = wf.player.eye - wf.player.view * Tunable("tele.camBack", 250.0) + Vec3{ 0, 0, Tunable("tele.camUp", 40.0) };
			wf.player.vel = pl.vel;
			wf.player.health = hp;
			wf.player.sneaking = pl.sneaking;
			wf.player.inCombat = pl.inCombat;
			wf.player.dialogue = pl.inDialogue;
			wf.player.scene = pl.inScene;
			wf.player.mounted = pl.mounted;
			wf.player.workshop = pl.workshop;
			wf.gates.paused = (in.gate & 16) != 0;
			wf.gates.sentAway = (in.gate & 32) != 0;
			wf.gates.carrying = in.carrying;
			wf.gates.escorting = in.escorting;
			wf.gates.waiting = in.waiting;
			wf.gates.waitSpot = in.centre;
			wf.gates.playerScene = (in.gate & 8) != 0;
			wf.gates.rangeMode = static_cast<int>(Tunable("brain.range", 1.0));
			wf.gates.requireCombat = Tunable("brain.requireCombat", 0.0) != 0.0;
			wf.cands = g.cands;

			part(4);
			// ---- the Router's ETA table (the live navmesh snapshot; never a build request)
			g.reach.assign(g.cands.size(), W::ReachEst{});
			{
				sp::router::RouterIn ri;
				ri.now = now;
				ri.meFeet = in.meFeet;
				ri.meTorso = torso;
				ri.interior = in.interior;
				ri.graph = Tunable("warden.nav", 1.0) != 0.0 ? sp::fo4::navmesh::Peek(&ri.graphSerial) : nullptr;
				ri.target = g.warden.Target();
				ri.challenger = g.d.second;
				// fix lane 1 (cost): the field / plan slices and the router's own line checks (<= router.directRays rays)
				// run only while the cost bank can pay for them
				const auto&  rs = g.router.GetStats();
				const double sliceEst = rs.frames > rs.sliceSkips ? rs.sliceUsSum / static_cast<double>(rs.frames - rs.sliceSkips) : 5.0;
				ri.slices = g.bank.CanPay(elapsedUs(), sliceEst, g.cp);
				const bool lineOk = g.bank.CanPay(elapsedUs(), std::max(0.0, g.rp.directRays) * g.bank.RayUs(g.cp), g.cp);
				if (!lineOk) ++g.lineBudget;
				g.router.UpdateEta(ri, g.cands, (RaysOk() && lineOk) ? &g.world : nullptr, g.rp, g.reach);
			}
			// R6 step 4: the close-range law "failed twice" for the defend jump (ARCH §3.9.6 / §4.11) - in step 4 the old
			// movement's near give-ups stand in for it: two reach failures within warden.cnlNearR u of it inside warden.cnlWindow s
			if (in.acting) {
				const double win = std::clamp(Tunable("warden.cnlWindow", 10.0), 0.0, 60.0);
				// step 4 integrator ([Tabs] warden.cnlOnceHitting; in game hf CZ.b 14:50:33-39: three caged raiders, one
				// failure each while he switched between them, the player 1.0 -> 0.25): ONE failure counts when that foe hit
				// the player after it (it is hurting the player and he could not get there)
				const bool once = Tunable("warden.cnlOnceHitting", 0.0) >= 0.5;
				for (std::size_t i = 0; i < g.cands.size() && i < g.reach.size(); ++i)
					if (auto it = g.nearFails.find(g.cands[i].id); it != g.nearFails.end()) {
						const bool twice = it->second[1] > -1e8 && now - it->second[1] <= win && now >= it->second[1];
						const bool hitting = once && it->second[0] > -1e8 && now - it->second[0] <= win && now >= it->second[0] &&
						                     g.cands[i].ev.hitPlayerAt > it->second[0];
						if (twice || hitting) g.reach[i].cnlFailedTwice = true;
					}
			}
			wf.reach = g.reach;

			part(5);
			// ---- the Warden (R6 step 4, acting: its frame rides in the black box's brain record - bit-exact replay)
			if (in.acting && g.bbWarden) brain_glue::BbRecorder().WardenBegin(g.warden, now);
			g.warden.Update(wf, g.wp, g.d);
			if (in.acting && g.bbWarden) brain_glue::BbRecorder().WardenEnd(wf, g.wp, g.d);

			part(6);
			if (in.acting) {
				// ---- R6 step 4: the directive for the old movement + the target pinned into the scan's kept list
				g.ext = W::ExtFromDecision(g.d, Tunable("tele.oldReachBlinks", 0.0) != 0.0, std::clamp(Tunable("warden.failGap", 1.0), 0.0, 10.0));
				g.ext.otherSpace = g.otherFor > 0;  // the lost transfer's take-off is not judged then (another space)
				if (g.d.target && !scan::PinKeep(g.d.target, now)) ++g.pinMiss;
			} else {
				// ---- shadow comparison
				std::string detail;
				const auto  diff = W::Classify(in.oldTarget, g.d, g.warden, &detail);
				double      prevDur = 0;
				// fix lane 2 (step 3 gate S3-IG02): a pair change (a decision of either brain) and a D2f hunt edge are always
				// logged, never rate-limited; a class change of the same pair once it held warden.shadowClassHold s (a hold <->
				// utility flicker is no line), and those still go through the limiter
				const double classHold = std::clamp(Tunable("warden.shadowClassHold", 0.25), 0.0, 2.0);
				const auto   due = g.report.Observe(in.oldTarget, g.d.target, diff, now, in.dt, &prevDur, classHold, in.oldEdge);
				if (due != W::ShadowReport::Due::kNone) {
					const std::string body = std::format("t={:.3f} old={} oldTier={} new={} band={} why={} agree={} class={} detail={} prevDur={:.2f} intent={} hp={:.2f}", now,
						sp::events::Hex(in.oldTarget), in.oldTier ? in.oldTier : "-", sp::events::Hex(g.d.target), W::BandName(g.d.band), g.d.why, diff == W::Diff::kAgree ? 1 : 0,
						W::DiffName(diff), detail.empty() ? "-" : detail, prevDur, W::IntentName(g.d.intent), hp);
					if (due == W::ShadowReport::Due::kClass) Line("shadow", g.d.target ^ in.oldTarget, body);
					else REX::INFO("SPN|WARDEN|shadow {}", body);
				}
			}
			DrainLines();

			part(7);
			// ---- cost and the 5 s / 1 s lines
			const double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
			g.bank.Spend(us, g.cp);  // fix lane 1 (cost)
			++g.frames;
			++g.framesWin;
			g.usSum += us;
			g.usMax = std::max(g.usMax, us);
			if (g.usWin.size() < 4096) g.usWin.push_back(static_cast<float>(us));
			if (in.wall - g.statAt >= 5.0 || in.wall < g.statAt) {
				g.statAt = in.wall;
				const double p99 = P99(g.usWin);
				const auto&  ws = g.warden.GetStats();
				const auto& bs = g.bank.GetStats();
				REX::INFO("SPN|WARDEN|wstat t={:.3f} frames={} costAvgUs={:.1f} costP99Us={:.1f} costMaxUs={:.1f} cands={} sightRays={} lineRays={} picks={} switches={} band={} same={} urgent={} "
				          "holds={} pairRef={} rangeDrops={} idle={} shields={} turns={} cageKeeps={} cageHurting={} essStalls={} hurtKeeps={} parts={} ticks={} capUs={:.0f} bank={:.1f} capSkips={} capForced={} sightReuse={} sightFarSkip={} "
				          "sightBudget={} lineBudget={} rayUs={:.1f} rayBank={:.1f} | {} | {}",
					now, g.frames, g.framesWin ? g.usSum / static_cast<double>(g.framesWin) : 0.0, p99, g.usMax, g.cands.size(), g.sightRays, g.world.rays, ws.picks, ws.switches,
					ws.bandSwitches, ws.sameSwitches, ws.urgent, ws.holds, ws.pairRefusals, ws.rangeDrops, ws.idleFrames, ws.shields, ws.turns, ws.cageKeeps, ws.cageHurting, ws.essStalls, ws.hurtKeeps, PartsText(), g.ticksWin, g.cp.capUs,
					g.bank.Credit(), bs.skips, bs.forced, g.sightReused, g.sightFarSkip, g.sightBudget, g.lineBudget, g.bank.RayUs(g.cp), g.bank.RayCredit(), g.report.Summary(), g.router.Describe());
				REX::INFO("SPN|R6|PROBE|P5 sum asks={} perFrameMax={} usAvg={:.2f} usP99={:.2f} usMax={:.2f} true={} false={} lateTrue={} fails={} off={}", g.asksN, g.askFrameMax,
					g.asksN ? g.askUsSum / static_cast<double>(g.asksN) : 0.0, P99(g.askUs), g.askUsMax, g.askHostile, g.askFriendly, g.askLateTrue, g.askFails, scan::R6AskFaults());
				g.framesWin = 0;
				g.ticksWin = 0;
				for (double& x : g.partUs) x = 0;
				for (double& x : g.partMax) x = 0;
				g.usSum = 0;
				g.usMax = 0;
				g.usWin.clear();
			}
			if (in.wall - g.sightLineAt >= 1.0 || in.wall < g.sightLineAt) {
				g.sightLineAt = in.wall;
				REX::INFO("SPN|R6|PROBE|sight rays={} refused={} skippedAfterLoad={} stoppedByRefusal={}", g.sightRays + g.world.rays, g.sightRefused, g.skippedAfterLoad,
					g.stoppedByRefusal);
			}
			// the camera probe line (1 Hz): the camera root against the render camera, and the D2 eye guess
			if (g.feat.cam && (in.wall - g.camLineAt >= 1.0 || in.wall < g.camLineAt)) {
				g.camLineAt = in.wall;
				CamRead c;
				if (!SehCam(&c)) {
					g.feat.cam = false;
					REX::ERROR("SPN|R6|off feat=percept.cam why=seh n=1 (the D2 eye test is used)");
				} else if (c.ok) {
					const Vec3 nc{ c.pos[0], c.pos[1], c.pos[2] }, root{ c.root[0], c.root[1], c.root[2] };
					const Vec3 guess = wf.player.camera;
					REX::INFO("SPN|R6|PROBE|cam state={} root={:.0f},{:.0f},{:.0f} nc={:.0f},{:.0f},{:.0f} dRootNc={:.0f} port={:.2f},{:.2f},{:.2f},{:.2f} eyeGuess={:.0f},{:.0f},{:.0f} dGuess={:.0f}",
						c.state, root.x, root.y, root.z, nc.x, nc.y, nc.z, c.haveRoot ? Length(root - nc) : -1.0, c.port[0], c.port[1], c.port[2], c.port[3], guess.x, guess.y, guess.z,
						Length(guess - nc));
				}
			}
			// forget old per-id memories
			static double s_pruneAt = 0;
			if (now - s_pruneAt > 2.0 || now < s_pruneAt) {
				s_pruneAt = now;
				g.hits.Prune(now);
				g.sight.Prune(now);
				std::erase_if(g.asks, [&](const auto& kv) { return now - kv.second.at > 5.0; });
				std::erase_if(g.lastHitGroup, [&](const auto& kv) { return now - kv.second > 30.0 && kv.second > -1e8; });
				std::erase_if(g.hitNeverLogAt, [&](const auto& kv) { return now - kv.second > 30.0; });
				std::erase_if(g.kindAtHit, [&](const auto& kv) { return now - kv.second.first > 30.0; });  // step 4 integrator
			}
		}
	}

	bool Enabled()
	{
		if (g.off) return false;
		return g.mode >= 1;
	}

	namespace
	{
		// [R6] Warden (0 off, 1 shadow, 2 the Warden picks) and the cost cap keys, re-read once a second
		void ModeRefresh(double a_wall)
		{
			if (!(a_wall - g.modeAt >= 1.0 || a_wall < g.modeAt)) return;
			g.modeAt = a_wall;
			const int m = std::clamp(static_cast<int>(Tunable("R6.warden", 0.0)), 0, 2);
			if (m != g.mode) {
				REX::INFO("SPN|R6|warden mode {} -> {} ({})", g.mode, m,
					m >= 2 ? "acting: the Warden picks the targets, the old movement moves him" : m >= 1 ? "shadow: the D2f brain acts, the Warden logs" : "off");
				if (m < 1 || (m >= 2) != (g.mode >= 2)) {  // R6 step 4: acting <-> shadow starts the Warden afresh too
					g.warden.Reset();
					if (g.bbWarden) brain_glue::BbRecorder().WardenReset();
					g.router.Reset();
					g.report.Reset();
					g.bank.Reset();
					g.ext = sp::brain::WardenExt{};
					g.nearFails.clear();
					g.lostFor = g.otherFor = 0; g.lostBest = -1; g.lostProgAt = -1e9;
					g.actBrain = g.actWarden = ~0u;
				}
				g.mode = m;
			}
			// fix lane 1 (cost): the cap's keys (core/shadow_cost.h), re-read once a second like the mode. R6 step 4: acting,
			// the tick runs every brain frame and the cap (warden.capUsAct) only limits rays and router slices
			g.cp.capUs = g.mode >= 2 ? Tunable("warden.capUsAct", 40.0) : Tunable("warden.capUs", 20.0);
			g.cp.bankFrames = std::clamp(Tunable("warden.capBankFrames", 4.0), 1.0, 50.0);
			g.cp.debtFrames = std::clamp(Tunable("warden.capDebtFrames", 8.0), 1.0, 50.0);
			g.cp.sightFarD = Tunable("warden.sightFarD", 2000.0);
			g.cp.rayShare = std::clamp(Tunable("warden.capRayShare", 0.25), 0.0, 1.0);
			g.cp.reuse = Tunable("warden.sightReuse", 1.0) != 0.0;
			g.bbWarden = Tunable("bb.warden", 1.0) != 0.0;  // R6 step 4
			scan::SetKnownView(g.mode >= 1);  // s4 fix lane 3 (cost): the scan's copy of every known actor only for a Warden
		}

		struct AfterCtx
		{
			const AfterIn* a;
		};
		void AfterImpl(void* a_ctx)
		{
			const AfterIn& a = *static_cast<AfterCtx*>(a_ctx)->a;
			const double   now = a.now;
			// reach failures of the old movement: an ETA penalty, never an ignore (ARCH C6)
			if (a.reachFail && a.reachFailId) {
				g.warden.NoteReachFail(a.reachFailId, now, a.reachFailWhy);
				g.forceTick = true;  // step 4 integrator (warden.actTickDt): decide again next frame
				if (g.bbWarden) brain_glue::BbRecorder().WardenNoteReachFail(a.reachFailId, now, a.reachFailWhy);
				++g.actFails;
				// near the foe: counts toward "the close-range law failed twice" (the defend jump rule)
				if (a.reachFailId == a.brainTarget && a.dist <= Tunable("warden.cnlNearR", 300.0)) {
					auto& nf = g.nearFails[a.reachFailId];
					if (nf[0] == 0 && nf[1] == 0) nf = { -1e9, -1e9 };
					nf[1] = nf[0];
					nf[0] = now;
					++g.actNearFails;
				}
			}
			// the act comparison: the Warden's decision vs the acting target (they differ only when the target could not be
			// handed over: missing from the candidates / pin refused)
			++g.actFrames;
			const std::uint32_t wt = g.d.target, bt = a.brainTarget;
			const double        dt = (g.actLastNow > -1e8 && now >= g.actLastNow) ? std::min(0.1, now - g.actLastNow) : 0.0;
			g.actLastNow = now;
			if (wt == bt) ++g.actAgree;
			else {
				g.actDisagreeS += dt;
				if (wt && !bt) ++g.actMissing;
			}
			if (wt != g.actWarden || bt != g.actBrain || a.edge) {
				g.actWarden = wt;
				g.actBrain = bt;
				REX::INFO("SPN|WARDEN|act t={:.3f} target={} brain={} agree={} band={} why={} intent={} reach={} eta={:.2f} pen={:.2f} U={:.3f} ctl={} dj={} view={} shield={} tier={} vis={} move={} "
				          "d={:.0f} 2nd={} hp={:.2f}",
					now, sp::events::Hex(wt), sp::events::Hex(bt), wt == bt ? 1 : 0, W::BandName(g.d.band), g.d.why, W::IntentName(g.d.intent), W::ReachName(g.d.s.reach),
					g.d.s.eta >= W::kInf ? -1.0 : g.d.s.eta, g.d.s.penalty, g.d.s.U, g.d.controlOnly ? 1 : 0, g.d.mayDefendJump ? 1 : 0, g.d.jumpInViewOk ? 1 : 0,
					g.ext.shield ? 1 : 0, a.brainTier ? a.brainTier : "-", a.brainVisible ? 1 : 0, a.move ? a.move : "-", a.dist, sp::events::Hex(g.d.second), g.hpNow);
			}
			DrainLines();
			static double s_prune = -1e9;
			if (now - s_prune > 5.0 || now < s_prune) {
				s_prune = now;
				const double win = std::clamp(Tunable("warden.cnlWindow", 10.0), 0.0, 60.0);
				std::erase_if(g.nearFails, [&](const auto& kv) { return now - kv.second[0] > win || now < kv.second[0]; });
			}
		}
	}

	namespace
	{
		// step 4 integrator (cost): an order-independent signature of the facts whose change can change the Warden's decision
		// at once (hits on the group / by the player, hostility, combat target, down) - positions are left to the tick rate
		std::uint64_t Mix64(std::uint64_t x)
		{
			x ^= x >> 30;
			x *= 0xbf58476d1ce4e5b9ull;
			x ^= x >> 27;
			x *= 0x94d049bb133111ebull;
			x ^= x >> 31;
			return x;
		}
		std::uint64_t FactSig(std::span<const scan::ActorFacts> a_fs)
		{
			std::uint64_t h = 0;
			for (const auto& f : a_fs) {
				std::uint64_t e = Mix64(f.id);
				e = Mix64(e ^ std::bit_cast<std::uint64_t>(f.lastHitPlayerAt));
				e = Mix64(e ^ std::bit_cast<std::uint64_t>(f.lastHitMeAt));
				e = Mix64(e ^ std::bit_cast<std::uint64_t>(f.lastHitTeammateAt));
				e = Mix64(e ^ std::bit_cast<std::uint64_t>(f.playerHitAt));
				e = Mix64(e ^ ((f.hostile ? 1ull : 0ull) | (f.hostileKnown ? 2ull : 0ull) | (f.down ? 4ull : 0ull) | (f.inCombat ? 8ull : 0ull) |
				                  (static_cast<std::uint64_t>(f.combatKind) << 8)));
				h += e;  // a sum: the kept list is re-sorted every frame
			}
			return h;
		}
		// true = the Warden must decide this frame (also keeps the signatures current: call it every frame)
		bool ActForce(const ShadowIn& a_in)
		{
			bool       f = g.forceTick || !g.ticked;
			const auto kept = scan::Kept();
			if (const std::uint64_t ks = FactSig(kept); ks != g.keptSig) {
				g.keptSig = ks;
				f = true;
			}
			if (const std::uint64_t serial = scan::ScanSerial(); serial != g.sigSerial) {
				g.sigSerial = serial;
				if (const std::uint64_t s = FactSig(scan::KnownFacts()); s != g.knownSig) {
					g.knownSig = s;
					f = true;
				}
			}
			if (a_in.oldTarget != g.d.target) f = true;  // the old movement did not take the Warden's target last frame
			if (g.d.target) {
				bool found = false;
				for (const auto& k : kept)
					if (k.id == g.d.target) {
						found = true;
						if (k.down) f = true;
						break;
					}
				if (!found || brain_glue::KillPending(g.d.target)) f = true;
			}
			if (g.bbWarden && brain_glue::BbRecorder().WouldStartSegment(a_in.now)) f = true;  // the segment's Warden keyframe
			return f;
		}
	}

	// s4 fix lane 2 (REVIEW: DefendJump vs DoBlink refusals): the old mover gets the defend-jump permission only when the
	// runtime can make the jump now - tele.jump proven at load and never faulted (TeleJumpOk), his controller the RB one (not
	// the proxy). Else DoBlink would refuse it ('teleJumpOff' / 'ctlClass') every blinkGap s, for good when the checks failed.
	static void GateJump(sp::brain::WardenExt& a_ext, RE::Actor* a_me)
	{
		if (!a_ext.mayDefendJump) return;
		const bool jumpOk = TeleJumpOk();
		bool       rbNow = false;
		if (jumpOk && !g.rbNowOff && a_me && !SehRbNow(a_me, g.rbVtbl, &rbNow)) {
			g.rbNowOff = true;  // crash shield: this read is off for the session (then no defend jump: the shield goes on)
			rbNow = false;
			REX::ERROR("SPN|R6|off feat=tele.jumpGate why=seh n=1 (his controller could not be read: no defend jump this session; the shield goes on)");
			sp::runtime::RequestDump("tele.jumpGate crash shield");
		}
		sp::warden::GateDefendJump(a_ext, jumpOk, rbNow && !g.rbNowOff);
		if (!a_ext.mayDefendJump) {
			++g.djGated;
			if (jumpOk) ++g.djGatedProxy;
		}
	}

	bool Decide(const ShadowIn& a_in, sp::brain::WardenExt& a_ext)
	{
		a_ext = sp::brain::WardenExt{};
		if (g.off || !sp::fo4::threads::InPump()) return false;
		ModeRefresh(a_in.wall);
		if (g.mode < 2) return false;
		if (!g.feat.ran) RunChecks();
		g.bank.Credit(g.cp);  // the bank still limits rays / router slices (warden.capUsAct)
		// step 4 integrator (cost): [Tabs] warden.actTickDt (0 = every brain frame, the CORE lane's rule)
		if (const double actDt = std::clamp(Tunable("warden.actTickDt", 0.0), 0.0, 0.1); actDt > 0) {
			const bool force = ActForce(a_in);
			if (!force && g.dtAcc + std::max(0.0, a_in.dt) + 1e-9 < actDt) {
				g.dtAcc += std::max(0.0, a_in.dt);
				++g.framesWin;  // a frame without a decision: cost 0 in the per-frame averages
				++g.actSkips;
				if (g.d.target && !scan::PinKeep(g.d.target, a_in.now)) ++g.pinMiss;
				a_ext = g.ext;
				GateJump(a_ext, a_in.me);  // s4 fix lane 2
				return true;
			}
			if (force) ++g.actForced;
		}
		g.forceTick = false;
		++g.actTicks;
		ShadowIn in = a_in;
		in.acting = true;
		in.dt = g.ticked ? g.dtAcc + std::max(0.0, a_in.dt) : a_in.dt;
		g.dtAcc = 0;
		g.ticked = true;
		g.lastOld = a_in.oldTarget;
		++g.ticks;
		++g.ticksWin;
		unsigned long code = 0;
		if (!SehRun(&TickImpl, &in, &code)) {
			Shield("decide", code);
			return false;
		}
		a_ext = g.ext;
		GateJump(a_ext, a_in.me);  // s4 fix lane 2
		return true;
	}

	void AfterBrain(const AfterIn& a_in)
	{
		if (g.off || g.mode < 2 || !sp::fo4::threads::InPump()) return;
		AfterCtx      c{ &a_in };
		unsigned long code = 0;
		if (!SehRun(&AfterImpl, &c, &code)) Shield("after", code);
	}

	bool Acting() { return !g.off && g.mode >= 2; }
	int  Mode() { return g.off ? 0 : g.mode; }
	bool TurnedFoe(std::uint32_t a_id) { return a_id && Acting() && g.warden.Turned(a_id); }
	bool EssCapped(std::uint32_t a_id) { return a_id && Acting() && g.warden.EssCapped(a_id, WallNow(), g.wp); }
	bool EssHold(std::uint32_t a_id)
	{
		// control hits (fling, no damage, no new knock-down): the capped essential the Warden fights (ARCH §3.8 controlOnly:
		// it is the only foe attacking the group) and a turned companion that is not essential (companionLethal 0)
		if (!a_id || !Acting()) return false;
		if (a_id == g.d.target && (g.d.controlOnly || g.warden.EssCapped(a_id, WallNow(), g.wp))) return true;
		return g.warden.Turned(a_id) && g.wp.companionLethal < 0.5;
	}
	void NoteDefendJump()
	{
		if (!Acting()) return;
		++g.defendJumps;
		g.forceTick = true;  // step 4 integrator (warden.actTickDt)
		const double wall = WallNow();
		g.warden.NoteDefendJump(wall);
		if (g.bbWarden) brain_glue::BbRecorder().WardenNoteDefendJump(wall);
		REX::INFO("SPN|TELE|defend t={:.3f} id={} n={} hp={:.2f} view={} (the Warden's permitted defend jump, ARCH 4.11)", g.lastNow, sp::events::Hex(g.d.target), g.defendJumps,
			g.hpNow, g.d.jumpInViewOk ? 1 : 0);
	}

	void Tick(const ShadowIn& a_in)
	{
		if (g.off || !sp::fo4::threads::InPump()) return;
		ModeRefresh(a_in.wall);
		if (g.mode < 1 || g.mode >= 2) return;  // (mode 2: Decide / AfterBrain)
		if (!g.feat.ran) RunChecks();
		g.dtAcc += std::max(0.0, a_in.dt);
		g.bank.Credit(g.cp);  // fix lane 1 (cost): every brain frame brings capUs of credit
		// a change of the D2f target, a D2f hunt / pick / drop edge (fix lane 2) or the first tick after a load always ticks
		// (the shadow line on every decision): never skipped by warden.tickDt nor by the cost bank (fix lane 1)
		const bool   force = !g.ticked || a_in.oldTarget != g.lastOld || a_in.oldEdge;
		const double tickDt = std::clamp(Tunable("warden.tickDt", 0.0), 0.0, 0.1);
		if (tickDt > 0 && !force && g.dtAcc + 1e-9 < tickDt) {
			++g.framesWin;  // a skipped brain frame: cost 0 in the per-frame averages
			return;
		}
		// fix lane 1 (cost): the bank is empty - this brain frame skips the shadow (its dt goes to the next tick); a change
		// of the D2f target or the first tick after a load runs anyway (the shadow line on every decision) and is debited
		if (!g.bank.MayTick(g.cp, force)) {
			++g.framesWin;
			return;
		}
		unsigned long code = 0;
		ShadowIn      in = a_in;
		in.dt = g.ticked ? g.dtAcc : a_in.dt;
		g.dtAcc = 0;
		g.ticked = true;
		g.lastOld = a_in.oldTarget;
		++g.ticks;
		++g.ticksWin;
		if (!SehRun(&TickImpl, &in, &code)) Shield("tick", code);
	}

	void OnLoad()
	{
		g.warden.OnLoad();
		if (Acting() && g.bbWarden) brain_glue::BbRecorder().WardenOnLoad();  // R6 step 4
		g.router.Reset();
		g.hits.Clear();
		g.sight.Clear();
		g.asks.clear();
		g.lastHitGroup.clear();
		g.kindAtHit.clear();  // step 4 integrator
		g.hitNeverLogAt.clear();
		g.d = W::Decision{};
		g.ticked = false;  // the first brain frame after a load ticks (warden.tickDt)
		g.mergeSerial = ~0ull;
		g.dtAcc = 0;
		g.bank.Reset();  // fix lane 1 (cost)
		g.ext = sp::brain::WardenExt{};  // R6 step 4
		g.nearFails.clear();
		g.lostFor = g.otherFor = 0; g.lostBest = -1; g.lostProgAt = -1e9;
		g.actBrain = g.actWarden = ~0u;
		g.actLastNow = -1e9;
		Quiet();
	}
	// R6 step 4: every call that changes the acting Warden is also noted in the black box (bit-exact replay)
	void OnTeleport()
	{
		Quiet();
		if (!Enabled()) return;
		g.warden.OnJump();
		g.router.OnJump();
		g.forceTick = true;  // step 4 integrator (warden.actTickDt)
		if (Acting() && g.bbWarden) brain_glue::BbRecorder().WardenOnJump();
	}
	void Recall(bool a_fly, double a_now)
	{
		if (!Enabled()) return;
		g.warden.Recall(a_fly, a_now);
		g.forceTick = true;  // step 4 integrator (warden.actTickDt)
		if (Acting() && g.bbWarden) brain_glue::BbRecorder().WardenRecall(a_fly, a_now);
	}
	void NoteSpared(std::uint32_t a_id)
	{
		if (!Enabled()) return;
		const double wall = WallNow();
		g.warden.NoteSpared(a_id, wall);
		g.forceTick = true;  // step 4 integrator (warden.actTickDt)
		if (Acting() && g.bbWarden) brain_glue::BbRecorder().WardenNoteSpared(a_id, wall);
	}
	void NoteKill(std::uint32_t a_id)
	{
		if (!Enabled()) return;
		g.warden.NoteKill(a_id);
		g.forceTick = true;  // step 4 integrator (warden.actTickDt)
		if (Acting() && g.bbWarden) brain_glue::BbRecorder().WardenNoteKill(a_id);
	}
	void NoteHitLanded(std::uint32_t a_id)
	{
		if (!Enabled()) return;
		g.warden.NoteHit(a_id, g.lastNow);
		if (Acting() && g.bbWarden) brain_glue::BbRecorder().WardenNoteHit(a_id, g.lastNow);
	}

	// R6 step 7 (ARCH §4.9 the close-range no-hit law): pilot.cnlFail s at close range without a hit on the target = a reach
	// failure "cnl" (its ETA +1 s, max 3 s, decays; never an ignore list, ARCH C6). Only while a Warden runs.
	void NoteCnlFail(std::uint32_t a_id)
	{
		if (!a_id || !Enabled()) return;
		g.warden.NoteReachFail(a_id, g.lastNow, "cnl");
		if (Acting() && g.bbWarden) brain_glue::BbRecorder().WardenNoteReachFail(a_id, g.lastNow, "cnl");
	}

	namespace
	{
		struct HitCtx
		{
			std::uint32_t id;
			const char*   stage;
			const char*   out;
		};
		void HitImpl(void* a_ctx)
		{
			auto&     h = *static_cast<HitCtx*>(a_ctx);
			const int mode = static_cast<int>(Tunable("warden.hitNever", 0.0));
			if (mode <= 0) return;
			const auto* f = scan::Find(h.id);
			if (!f) return;  // unknown to the scan: the old checks (FoeVeto, LiveVeto) decide alone
			sp::percept::Extra x;
			x.self = (f->veto & scan::kV01Self) != 0 && !f->child;
			x.useImmobile = g.feat.immobile;
			x.useKillRole = g.feat.killRole;
			x.useWorkshop = g.feat.workshop;
			const auto& pl = scan::GetPlayerFacts();
			auto        c = sp::percept::ToCand(*f, pl, x, f->feet, pl.feet, g.lastNow);
			if (auto it = g.asks.find(h.id); it != g.asks.end() && g.lastNow - it->second.at <= 1.0 && it->second.at >= f->hostileAt) c.ev.hostile = it->second.hostile;
			const W::Score* s = g.warden.LastScore(h.id);
			const W::Never  nv = W::NeverRows(c, g.lastNow, g.wp, g.warden.Turned(h.id), s ? s->band : W::Band::kNone);
			if (nv == W::Never::kOk) return;
			++g.hitNever;
			if (mode >= 2) {
				++g.hitRefused;
				h.out = W::NeverName(nv);
			}
			double& at = g.hitNeverLogAt[h.id];
			if (g.lastNow - at >= 1.0 || g.lastNow < at) {
				at = g.lastNow;
				REX::INFO("SPN|WARDEN|hitNever t={:.3f} id={:08X} code={} ({}) stage={} enforce={} hostile={} kind={} ct={} n={}", g.lastNow, h.id, W::NeverCode(nv), W::NeverName(nv),
					h.stage ? h.stage : "?", mode >= 2 ? 1 : 0, c.ev.hostile ? 1 : 0, c.ev.hostileKind ? 1 : 0, static_cast<int>(f->combatKind), g.hitNever);
			}
		}
	}

	const char* HitCheck(std::uint32_t a_id, const char* a_stage)
	{
		if (!Enabled() || !a_id) return nullptr;
		HitCtx        h{ a_id, a_stage, nullptr };
		unsigned long code = 0;
		if (!SehRun(&HitImpl, &h, &code)) {
			Shield("hit", code);
			return nullptr;
		}
		return h.out;
	}

	namespace
	{
		struct RelocCtx
		{
			const char* kind;
			Vec3        from, to;
			bool        fromSameSpace;
		};
		void RelocImpl(void* a_ctx)
		{
			auto&       r = *static_cast<RelocCtx*>(a_ctx);
			const auto& pl = scan::GetPlayerFacts();
			if (!pl.valid) return;
			Vec3 eye{}, view{};
			PlayerView(eye, view, pl.feet);
			const Vec3 up{ 0, 0, 60 };
			const Vec3 pf = r.from + up, pt = r.to + up;
			// s3 fix lane 3 (REVIEW s3 finding 2): the D2 eye test exactly as Brain::PlayerSees applies it (brain.navViewDeg 60,
			// the camera guess brain.d2CamBack / d2CamUp, sight rays eps 16, "no ray allowed" = SEEN; it was a 30 deg cone with
			// "no ray" folded into hidden, so every jump / warp read "eye hidden" by construction). The take-off end of a jump
			// that changed space is skipped (it lies in another cell / worldspace). eyeKnown 0 = a verdict that rests on an end
			// with no ray allowed (D2's "unknown = seen"); agree / disagreeN use measured verdicts only.
			const sp::p4::EyeParams ep{ Tunable("brain.navViewDeg", 60.0), Tunable("brain.d2CamBack", 0.0), Tunable("brain.d2CamUp", 40.0) };
			auto                    eyeRay = [](const Vec3& a_a, const Vec3& a_b, bool& a_clear) { return Ray(a_a, a_b, 0, 16.0, a_clear); };
			const sp::p4::EyeEnd    eF = r.fromSameSpace ? sp::p4::EyeEndOf(eye, view, pf, ep, eyeRay) : sp::p4::EyeEnd::kSkipped;
			const sp::p4::EyeEnd    eT = sp::p4::EyeEndOf(eye, view, pt, ep, eyeRay);
			const bool              seenEye = sp::p4::D2Sees(eF) || sp::p4::D2Sees(eT);
			const bool              eyeKnown = eF != sp::p4::EyeEnd::kUnknown && eT != sp::p4::EyeEnd::kUnknown;
			// the real render camera (HOOKS s3 B8): on screen (z > 0, inside the port) AND a clear ray from it
			CamRead c;
			int     onF = -1, onT = -1, rayF = -1, rayT = -1, seenCam = -1;
			float   zF = 0, zT = 0;
			if (g.feat.cam) {
				if (!SehCam(&c)) {
					g.feat.cam = false;
					REX::ERROR("SPN|R6|off feat=percept.cam why=seh n=1 (the D2 eye test is used)");
				} else if (c.ok) {
					const Vec3    cp{ c.pos[0], c.pos[1], c.pos[2] };
					ProjCtx       a{ &c, pf, 0, false }, b{ &c, pt, 0, false };
					unsigned long code = 0;
					if (!SehRun(&ProjImpl, &a, &code) || !SehRun(&ProjImpl, &b, &code)) {
						g.feat.cam = false;
						REX::ERROR("SPN|R6|off feat=percept.cam why=seh code=0x{:08X} (WorldPtToScreenPt3)", code);
					} else {
						onF = r.fromSameSpace ? (a.on ? 1 : 0) : -1;  // s3 fix lane 3: a take-off in another space is never on this screen
						onT = b.on ? 1 : 0;
						zF = a.z;
						zT = b.z;
						bool clr = false;
						if (onF == 1) rayF = Ray(cp, pf, 0, 40.0, clr) ? (clr ? 1 : 0) : -1;
						if (onT == 1) rayT = Ray(cp, pt, 0, 40.0, clr) ? (clr ? 1 : 0) : -1;
						if (rayF == 1 || rayT == 1) seenCam = 1;
						else if ((onF == 1 && rayF < 0) || (onT == 1 && rayT < 0)) seenCam = -1;
						else seenCam = 0;
					}
				}
			}
			++g.relocs;
			if (seenEye) ++g.relocSeenEye;
			if (!eyeKnown) ++g.relocEyeUnknown;
			if (seenCam == 1) ++g.relocSeenCam;
			const int agree = (seenCam < 0 || !eyeKnown) ? -1 : ((seenCam == 1) == seenEye ? 1 : 0);
			if (agree == 0) ++g.relocDisagree;
			REX::INFO("SPN|R6|PROBE|P4 reloc kind={} t={:.3f} cam={} camPos={:.0f},{:.0f},{:.0f} eye={:.0f},{:.0f},{:.0f} from={:.0f},{:.0f},{:.0f} to={:.0f},{:.0f},{:.0f} onFrom={} onTo={} "
			          "zFrom={:.3f} zTo={:.3f} rayFrom={} rayTo={} seenCam={} seenEye={} agree={} hp={:.2f} n={} seenCamN={} seenEyeN={} disagreeN={} eyeFrom={} eyeTo={} eyeKnown={} "
			          "fromSpace={} view={:.3f},{:.3f},{:.3f} eyeRule=d2:{:.0f}/{:.0f}/{:.0f} eyeUnknownN={}",
				r.kind ? r.kind : "?", g.lastNow, c.ok ? c.state : -1, c.pos[0], c.pos[1], c.pos[2], eye.x, eye.y, eye.z, r.from.x, r.from.y, r.from.z, r.to.x, r.to.y, r.to.z, onF, onT, zF,
				zT, rayF, rayT, seenCam, seenEye ? 1 : 0, agree, sp::runtime::PlayerHealthNative(), g.relocs, g.relocSeenCam, g.relocSeenEye, g.relocDisagree,
				sp::p4::EyeEndName(eF), sp::p4::EyeEndName(eT), eyeKnown ? 1 : 0, r.fromSameSpace ? "same" : "other", view.x, view.y, view.z, ep.viewDeg, ep.camBack, ep.camUp,
				g.relocEyeUnknown);
		}
	}

	void ProbeRelocation(const char* a_kind, const Vec3& a_from, const Vec3& a_to, bool a_fromSameSpace)
	{
		if (!Enabled() || !sp::fo4::threads::InPump()) return;
		// HOOKS s3 B9: an engine relocation (door, fast travel, MoveTo) may come with a cell load: no rays now (the camera
		// projection needs none; the ray parts say -1 = unknown)
		// R6 step 4 (step-3 REVIEW 4): his own blink too - no probe rays in the blink frame (B9)
		if (a_kind && (std::string_view(a_kind) == "jump" || std::string_view(a_kind) == "blink")) Quiet();
		RelocCtx      r{ a_kind, a_from, a_to, a_fromSameSpace };
		unsigned long code = 0;
		if (!SehRun(&RelocImpl, &r, &code)) Shield("probe P-R6-4", code);
	}

	namespace
	{
		struct ViewCtx
		{
			Vec3 from, to;
			bool seen;
			int  onScreen;
		};
		void ViewImpl(void* a_ctx)
		{
			auto& v = *static_cast<ViewCtx*>(a_ctx);
			v.seen = false;
			v.onScreen = 0;
			CamRead c;
			if (!SehCam(&c)) {
				g.feat.cam = false;
				REX::ERROR("SPN|R6|off feat=percept.cam why=seh n=1 (the brain's eye test decides the defend jump's view)");
				return;
			}
			if (!c.ok) return;
			const Vec3 cp{ c.pos[0], c.pos[1], c.pos[2] };
			const Vec3 up{ 0, 0, 60 };
			for (const Vec3& p : { v.from + up, v.to + up }) {
				ProjCtx       pc{ &c, p, 0, false };
				unsigned long code = 0;
				if (!SehRun(&ProjImpl, &pc, &code)) {
					g.feat.cam = false;
					REX::ERROR("SPN|R6|off feat=percept.cam why=seh code=0x{:08X} (WorldPtToScreenPt3, defend jump)", code);
					return;
				}
				if (!pc.on) continue;
				++v.onScreen;
				bool clear = false;
				if (!Ray(cp, p, 0, 40.0, clear) || clear) {  // no ray allowed (B9) = seen; a clear ray = seen
					v.seen = true;
					return;
				}
			}
		}
	}

	// HOOKS s4 C / D: the defend jump's controller write and the lost transfer's space reads
	bool           TeleJumpOk() { return Acting() && g.feat.ran && g.feat.teleJump && !g.teleJumpOff && g.rbVtbl != 0; }
	std::uintptr_t RbControllerVtbl() { return g.rbVtbl; }
	void           TeleJumpFault(unsigned long a_code)
	{
		if (g.teleJumpOff) return;
		g.teleJumpOff = true;
		REX::ERROR("SPN|R6|off feat=tele.jump why=seh code=0x{:08X} n=1 (no defend jump / blink this session; the shield goes on)", a_code);
		sp::runtime::RequestDump("tele.jump crash shield");
	}
	bool TeleLostOk() { return g.feat.ran && g.feat.teleLost && !g.teleLostOff; }
	void TeleLostFault(unsigned long a_code)
	{
		if (g.teleLostOff) return;
		g.teleLostOff = true;
		REX::ERROR("SPN|R6|off feat=tele.lost why=seh code=0x{:08X} n=1 (no 'other space' lost transfer this session; Papyrus TravelCheck still transfers)", a_code);
		sp::runtime::RequestDump("tele.lost crash shield");
	}

	namespace
	{
		// step 4 integrator (HOOKS s4 E djump probe + the view rule at execution for every blink while acting): the D2 eye
		// test (60 deg cone + camera guess + ray) AND the real render camera (on screen + clear ray) for both ends, BEFORE the
		// move (rays allowed; B9 quiet frames = unknown = seen)
		struct JumpCtx
		{
			Vec3 from, to;
			int  eyeFrom, eyeTo, camFrom, camTo;  // 1 seen, 0 not, -1 unknown
		};
		void JumpImpl(void* a_ctx)
		{
			auto& j = *static_cast<JumpCtx*>(a_ctx);
			j.eyeFrom = j.eyeTo = j.camFrom = j.camTo = -1;
			const auto& pl = scan::GetPlayerFacts();
			if (!pl.valid) return;
			Vec3 eye{}, view{};
			PlayerView(eye, view, pl.feet);
			const Vec3              up{ 0, 0, 60 };
			const Vec3              pf = j.from + up, pt = j.to + up;
			const sp::p4::EyeParams ep{ Tunable("brain.navViewDeg", 60.0), Tunable("brain.d2CamBack", 0.0), Tunable("brain.d2CamUp", 40.0) };
			auto                    eyeRay = [](const Vec3& a_a, const Vec3& a_b, bool& a_clear) { return Ray(a_a, a_b, 0, 16.0, a_clear); };
			auto                    eyeOf = [&](const Vec3& p) {
                const sp::p4::EyeEnd e = sp::p4::EyeEndOf(eye, view, p, ep, eyeRay);
                return e == sp::p4::EyeEnd::kSeen ? 1 : e == sp::p4::EyeEnd::kUnknown ? -1 : 0;
			};
			j.eyeFrom = eyeOf(pf);
			j.eyeTo = eyeOf(pt);
			if (!g.feat.cam) return;
			CamRead c;
			if (!SehCam(&c)) {
				g.feat.cam = false;
				REX::ERROR("SPN|R6|off feat=percept.cam why=seh n=1 (the brain's eye test decides the jump's view)");
				return;
			}
			if (!c.ok) return;
			const Vec3 cp{ c.pos[0], c.pos[1], c.pos[2] };
			auto       camOf = [&](const Vec3& p) {
                ProjCtx       pc{ &c, p, 0, false };
                unsigned long code = 0;
                if (!SehRun(&ProjImpl, &pc, &code)) {
                    g.feat.cam = false;
                    REX::ERROR("SPN|R6|off feat=percept.cam why=seh code=0x{:08X} (WorldPtToScreenPt3, jump view)", code);
                    return -1;
                }
                if (!pc.on) return 0;
                bool clear = false;
                if (!Ray(cp, p, 0, 40.0, clear)) return -1;
                return clear ? 1 : 0;
			};
			j.camFrom = camOf(pf);
			j.camTo = camOf(pt);
		}
	}

	bool JumpViewOk(const char* a_kind, std::uint32_t a_id, const Vec3& a_from, const Vec3& a_to, bool a_inViewOk, double a_waited)
	{
		if (!Acting() || !sp::fo4::threads::InPump()) return true;
		JumpCtx       j{ a_from, a_to, -1, -1, -1, -1 };
		unsigned long code = 0;
		if (!SehRun(&JumpImpl, &j, &code)) {
			Shield("jump view", code);
			return false;
		}
		// seen = the eye test or the camera says seen; unknown on an end = seen (never relocate on "unknown"); no camera
		// at all (menus, loads, cam off) = the eye test decides
		auto       seen = [](int e, int c) { return e != 0 || c == 1 || c < 0; };
		const bool camKnown = g.feat.cam;
		const bool seenFrom = camKnown ? seen(j.eyeFrom, j.camFrom) : j.eyeFrom != 0;
		const bool seenTo = camKnown ? seen(j.eyeTo, j.camTo) : j.eyeTo != 0;
		const bool ok = a_inViewOk || (!seenFrom && !seenTo);
		auto       v = [](int x) { return x < 0 ? std::string("-") : std::to_string(x); };
		// the executed decision: SPN|R6|PROBE|djump (the analyzer's 'seen' source); a refusal: djump-refused (<= 2 / s)
		static double s_refLineAt = -1e9;
		if (ok)
			REX::INFO("SPN|R6|PROBE|djump t={:.3f} id={} kind={} d={:.0f} hp={:.2f} inViewOk={} eyeFrom={} camFrom={} eyeTo={} camTo={} waited={:.2f}", g.lastNow, sp::events::Hex(a_id),
				a_kind ? a_kind : "?", Length(a_to - a_from), g.hpNow, a_inViewOk ? 1 : 0, v(j.eyeFrom), v(j.camFrom), v(j.eyeTo), v(j.camTo), a_waited);
		else if (g.lastWall - s_refLineAt >= 0.5 || g.lastWall < s_refLineAt) {
			s_refLineAt = g.lastWall;
			REX::INFO("SPN|R6|PROBE|djump-refused t={:.3f} id={} kind={} why=seen{} eyeFrom={} camFrom={} eyeTo={} camTo={} hp={:.2f}", g.lastNow, sp::events::Hex(a_id), a_kind ? a_kind : "?",
				seenFrom ? "From" : "To", v(j.eyeFrom), v(j.camFrom), v(j.eyeTo), v(j.camTo), g.hpNow);
		}
		return ok;
	}

	// R6 step 10 ([R6] Tele 1): the same decision-time view test for the Pilot's execution gate, in any Warden mode
	bool ViewEnds(const Vec3& a_from, const Vec3& a_to, ViewAnswer& a_out)
	{
		a_out = ViewAnswer{};
		if (!sp::fo4::threads::InPump()) return false;
		if (!g.feat.ran) RunChecks();
		JumpCtx       j{ a_from, a_to, -1, -1, -1, -1 };
		unsigned long code = 0;
		if (!SehRun(&JumpImpl, &j, &code)) {
			// (the Warden's own shell switches the Warden off; here only this answer is lost: unknown = seen)
			REX::ERROR("SPN|R6|off feat=tele.view why=seh code=0x{:08X} (this relocation is refused as seen)", code);
			sp::runtime::RequestDump("tele.view crash shield");
			return false;
		}
		a_out.eyeFrom = j.eyeFrom;
		a_out.eyeTo = j.eyeTo;
		a_out.camFrom = j.camFrom;
		a_out.camTo = j.camTo;
		a_out.camKnown = g.feat.cam;
		auto seen = [](int e, int c) { return e != 0 || c == 1 || c < 0; };
		a_out.seenFrom = a_out.camKnown ? seen(j.eyeFrom, j.camFrom) : j.eyeFrom != 0;
		a_out.seenTo = a_out.camKnown ? seen(j.eyeTo, j.camTo) : j.eyeTo != 0;
		a_out.ok = true;
		return true;
	}
	bool TeleJumpReady()
	{
		if (!g.feat.ran && sp::fo4::threads::InPump()) RunChecks();
		return g.feat.ran && g.feat.teleJump && !g.teleJumpOff && g.rbVtbl != 0;
	}
	double HpNow() { return g.hpNow; }
	void   TeleFrame(double a_now, double a_wall, std::uint32_t a_meCell, std::uint32_t a_plCell, double a_hp)
	{
		// (only while the Warden does not tick: its own tick does exactly this - B9 ray quiet after a load / cell change)
		if (!g.off && g.mode >= 1) return;
		g.lastNow = a_now;
		g.lastWall = a_wall;
		g.raysStopped = false;
		if (g.quietFrames > 0) --g.quietFrames;
		if (a_meCell != g.meCell || a_plCell != g.plCell) {
			if (g.meCell || g.plCell) Quiet();
			g.meCell = a_meCell;
			g.plCell = a_plCell;
		}
		if (a_hp >= 0) g.hpNow = a_hp;
	}

	bool DefendJumpViewOk(const Vec3& a_from, const Vec3& a_to)
	{
		if (!Acting() || !sp::fo4::threads::InPump()) return true;
		// step 4 integrator: the shared decision-time check (eye test + real camera, both ends, unknown = seen) and the djump
		// probe line; below tele.defendInViewHp (jumpInViewOk) a seen jump may go (logged with inViewOk=1)
		const bool ok = JumpViewOk("defend", g.d.target, a_from, a_to, g.d.jumpInViewOk, 0.0);
		if (!ok) {
			static double s_lineAt = -1e9;
			if (g.lastWall - s_lineAt >= 0.5 || g.lastWall < s_lineAt) {
				s_lineAt = g.lastWall;
				REX::INFO("SPN|TELE|defendRefused t={:.3f} id={} why=seen hp={:.2f} (the player's eye or camera sees an end; he keeps shielding)", g.lastNow, sp::events::Hex(g.d.target),
					g.hpNow);
			}
		}
		return ok;
	}

	namespace
	{
		struct WedgeCtx
		{
			Vec3 feet;
			bool seen;
			int  eye;       // p4::EyeEnd of his chest
			int  onScreen;  // real camera: ends on screen
		};
		void WedgeImpl(void* a_ctx)
		{
			auto& w = *static_cast<WedgeCtx*>(a_ctx);
			w.seen = true;  // unknown = seen (never relocate on "unknown")
			const auto& pl = scan::GetPlayerFacts();
			if (!pl.valid) return;
			Vec3 eye{}, view{};
			PlayerView(eye, view, pl.feet);
			const sp::p4::EyeParams ep{ Tunable("brain.navViewDeg", 60.0), Tunable("brain.d2CamBack", 0.0), Tunable("brain.d2CamUp", 40.0) };
			auto                    eyeRay = [](const Vec3& a_a, const Vec3& a_b, bool& a_clear) { return Ray(a_a, a_b, 0, 16.0, a_clear); };
			const sp::p4::EyeEnd    e = sp::p4::EyeEndOf(eye, view, w.feet + Vec3{ 0, 0, 60 }, ep, eyeRay);
			w.eye = static_cast<int>(e);
			if (sp::p4::D2Sees(e)) return;
			if (g.feat.cam) {
				ViewCtx v{ w.feet, w.feet, false, 0 };
				ViewImpl(&v);
				w.onScreen = v.onScreen;
				if (v.seen) return;
			}
			w.seen = false;
		}
	}

	bool WedgedOk(const Vec3& a_meFeet, const char* a_kind, Vec3* a_land, bool a_force)
	{
		if (!Acting() && !a_force) return true;  // Warden 0 / 1: the old put-back as before (R6 step 10: a_force = the Pilot's policy)
		if (!g.feat.ran && sp::fo4::threads::InPump()) RunChecks();
		const double wall = WallNow();
		const double every = std::max(0.0, Tunable("tele.wedgedEvery", 60.0));
		const char*  why = nullptr;
		WedgeCtx     w{ a_meFeet, true, -9, -1 };
		const bool   native = a_land && (a_force || Tunable("tele.wedgedBlink", 0.0) >= 0.5);  // step 4 fix lane 1 (R6 step 10: the Pilot always lands natively)
		Vec3         land{};
		if (Tunable("tele.wedged", 1.0) < 0.5) why = "off";
		else if (wall - g.wedgedAt < every && wall >= g.wedgedAt) why = "budget";
		else if (!sp::fo4::threads::InPump()) why = "thread";
		else {
			unsigned long code = 0;
			if (!SehRun(&WedgeImpl, &w, &code)) {
				Shield("wedged view", code);
				why = "fault";
			} else if (w.seen) why = "seen";
		}
		// step 4 fix lane 1 (REVIEW F1): the landing - the nearest floor of the live navmesh snapshot within tele.wedgedR u
		// (a pure copy of the engine's navmeshes: no engine call), pulled up to 48 u into its triangle so it is not on the
		// wall edge he is stuck in, with a clear headroom ray; then BOTH ends with the eye test and the real camera
		// (JumpViewOk, unknown = seen). Any doubt = refused: the pulls go on and the budget is kept.
		if (!why && native) {
			std::uint64_t serial = 0;
			const sp::nav::Graph* gph = Tunable("warden.nav", 1.0) != 0.0 ? sp::fo4::navmesh::Peek(&serial) : nullptr;
			const double          R = std::clamp(Tunable("tele.wedgedR", 400.0), 64.0, 1000.0);
			Vec3                  at{};
			const std::uint32_t   tri = gph ? gph->LocateNear(a_meFeet, R, std::min(R, 200.0), R, &at) : sp::nav::kNone;
			if (!gph) why = "noNav";
			else if (tri == sp::nav::kNone || !IsFinite(at)) why = "noFloor";
			else {
				const Vec3 c = gph->Centroid(tri);
				Vec3       in = c - at;
				in.z = 0;
				const double L = Length(in);
				land = L > 1e-6 ? at + in * (std::min(48.0, L) / L) : at;
				land.z = gph->PlaneZ(tri, land.x, land.y);
				if (!IsFinite(land) || Length(land - a_meFeet) > R + 64.0) why = "noFloor";
				else {
					bool clear = false;
					if (!RaysOk() || !Ray(land + Vec3{ 0, 0, 20 }, land + Vec3{ 0, 0, 150 }, 0, 4.0, clear)) why = "noRay";
					else if (!clear) why = "headroom";
					else if (Acting() && !JumpViewOk("wedged", 0, a_meFeet, land, false, 0.0)) why = "seenEnd";
					else if (!Acting()) {  // R6 step 10 (the Pilot's policy in any Warden mode): both ends, unknown = seen
						ViewAnswer va;
						if (!ViewEnds(a_meFeet, land, va) || va.seenFrom || va.seenTo) why = "seenEnd";
					}
				}
			}
		}
		if (why) {
			++g.wedgedRefused;
			if (wall - g.wedgedLineAt >= 1.0 || wall < g.wedgedLineAt) {
				g.wedgedLineAt = wall;
				REX::INFO("SPN|TELE|wedgedRefused t={:.3f} kind={} why={} eye={} onScreen={} n={} (ARCH 4.11: a wedged relocation only out of view, <= 1 per {:.0f} s; the pulls go on)",
					g.lastNow, a_kind ? a_kind : "?", why, sp::p4::EyeEndName(static_cast<sp::p4::EyeEnd>(w.eye)), w.onScreen, g.wedgedRefused, every);
			}
			return false;
		}
		g.wedgedAt = wall;
		++g.wedgedN;
		if (native) {
			*a_land = land;
			REX::INFO("SPN|TELE|wedged t={:.3f} kind={} seen=0 eye={} onScreen={} n={} land={:.0f},{:.0f},{:.0f} d={:.0f} (ARCH 4.11 wedged relocation: the nearest navmesh floor, both ends out of view; a native blink)",
				g.lastNow, a_kind ? a_kind : "?", sp::p4::EyeEndName(static_cast<sp::p4::EyeEnd>(w.eye)), w.onScreen, g.wedgedN, land.x, land.y, land.z, Length(land - a_meFeet));
			return true;
		}
		REX::INFO("SPN|TELE|wedged t={:.3f} kind={} seen=0 eye={} onScreen={} n={} (ARCH 4.11 wedged relocation: his take-off out of view; Papyrus places him)", g.lastNow,
			a_kind ? a_kind : "?", sp::p4::EyeEndName(static_cast<sp::p4::EyeEnd>(w.eye)), w.onScreen, g.wedgedN);
		return true;
	}

	std::string Describe()
	{
		if (g.mode < 1) return " | WARDEN off";
		if (g.mode >= 2)  // R6 step 4: acting
			return std::format(" | WARDEN mode=2{} frames={} act agree={:.1f}% disagreeS={:.2f} missing={} fails={} nearFails={} defJumps={} djGated={}/{} pinMiss={} {} hitNever={} refused={} relocs={} wedged={}/{} decide={}/skip={}/forced={}",
				g.off ? " OFF(shield)" : "", g.frames, g.actFrames ? 100.0 * static_cast<double>(g.actAgree) / static_cast<double>(g.actFrames) : 100.0, g.actDisagreeS, g.actMissing,
				g.actFails, g.actNearFails, g.defendJumps, g.djGated, g.djGatedProxy, g.pinMiss, scan::PinDescribe(), g.hitNever, g.hitRefused, g.relocs, g.wedgedN, g.wedgedRefused, g.actTicks,
				g.actSkips, g.actForced);
		return std::format(" | WARDEN mode={}{} frames={} {} hitNever={} refused={} relocs={} seenCam={} seenEye={}", g.mode, g.off ? " OFF(shield)" : "", g.frames, g.report.Summary(),
			g.hitNever, g.hitRefused, g.relocs, g.relocSeenCam, g.relocSeenEye);
	}
}
