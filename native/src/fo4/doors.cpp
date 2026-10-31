// OWNER: R6 (round 6 step 9, CORE lane). See doors.h and steps\s9\HOOKS.md (A, B, C).
#include "fo4/doors.h"

#include "config.h"
#include "core/door_checks.h"
#include "fo4/threads.h"
#include "runtime.h"

#include <array>
#include <atomic>
#include <chrono>
#include <format>
#include <string_view>

namespace sp::fo4::doors
{
	namespace
	{
		struct State
		{
			bool               checked{ false };
			bool               off{ false };
			const char*        offWhy{ "" };
			std::uintptr_t     base{ 0 };
			std::uint64_t      queries{ 0 }, unknown{ 0 }, loads{ 0 }, locked{ 0 }, quest{ 0 }, opens{ 0 }, openRefused{ 0 }, faults{ 0 }, notPump{ 0 };
			double             usMax{ 0 }, usSum{ 0 };
		};
		State s;
		using CanOpenFn = bool (*)(RE::TESObjectREFR*, RE::Actor*, bool&);

		bool SehRead(std::uintptr_t a, std::uint8_t* out, int n) noexcept
		{
			__try {
				for (int i = 0; i < n; ++i) out[i] = reinterpret_cast<const std::uint8_t*>(a)[i];
				return true;
			} __except (1) {
				return false;
			}
		}

		void TurnOff(const char* a_why, bool a_dump)
		{
			if (s.off) return;
			s.off = true;
			s.offWhy = a_why;
			REX::WARN("SPN|LOOK|off feat=doors why={} (navmesh door names and the engine door calls are off for this session; the Router treats every door "
			          "portal as unnamed: only its portal sweep decides)",
				a_why);
			if (a_dump) sp::runtime::RequestDump("look off doors");
		}

		void RunChecks()
		{
			if (s.checked) return;
			s.checked = true;
			auto reader = [](std::uint64_t id, std::uint32_t& rva, std::uint32_t off, std::uint8_t* out, int n) -> bool {
				try {
					const REL::ID r(id);
					rva = static_cast<std::uint32_t>(r.offset());
					if (n <= 0) return true;
					return SehRead(r.address() + off, out, n);
				} catch (...) {
					return false;
				}
			};
			std::string firstBad;
			const bool  ok = sp::r6check::RunDoorChecks(reader, [&](const sp::r6check::Proof& p, bool a_ok, const std::string& a_why) {
				if (!a_ok && firstBad.empty()) firstBad = std::format("bytes id={} {} ({})", p.id, a_why, p.name);
				REX::INFO("SPN|ROUTE|check {} id={} rva={:X}: {}", p.name, p.id, p.rva, a_ok ? std::string("ok") : std::format("REFUSED why={} -> feat doors off", a_why));
			});
			try {
				const REL::ID vt(447258);
				s.base = vt.address() - vt.offset();
			} catch (...) {
				s.base = 0;
			}
			if (!ok || !s.base) {
				static std::string why;
				why = firstBad.empty() ? std::string("base") : firstBad;
				TurnOff(why.c_str(), false);
			}
		}

		// ---- SEH shells (plain pointers / PODs only inside __try) ----
		struct Raw
		{
			int   refType{ 0 }, baseType{ 0 };
			bool  del{ false }, dis{ false }, has3d{ false }, attached{ false }, load{ false };
			int   open{ 0 };
			bool  hasLock{ false }, lockOn{ false };
			int   level{ -1 };
			bool  may{ false }, pick{ false };
			float x{ 0 }, y{ 0 }, z{ 0 };
		};
		// a_stage: 0 resolve + flags, 1 + state, 2 + lock, 3 + may-open (the fault reports the stage)
		bool SehFacts(std::uint32_t a_id, RE::Actor* a_me, CanOpenFn a_can, Raw* o, int* a_stage) noexcept
		{
			__try {
				*a_stage = 0;
				RE::TESForm* f = RE::TESForm::GetFormByID(a_id);
				if (!f) return false;
				o->refType = static_cast<int>(f->GetFormType());
				if (f->GetFormType() != RE::ENUM_FORM_ID::kREFR) return false;
				auto* r = static_cast<RE::TESObjectREFR*>(f);
				auto* b = r->GetObjectReference();
				o->baseType = b ? static_cast<int>(b->GetFormType()) : 0;
				if (!b || b->GetFormType() != RE::ENUM_FORM_ID::kDOOR) return false;
				o->del = r->IsDeleted();
				o->dis = r->IsDisabled();
				o->has3d = r->Get3D() != nullptr;
				o->attached = r->parentCell && r->parentCell->IsAttached();
				o->load = r->extraList && r->extraList->HasType(RE::EXTRA_DATA_TYPE::kTeleport);
				const auto p = r->GetPosition();
				o->x = p.x;
				o->y = p.y;
				o->z = p.z;
				if (o->del || o->dis || !o->has3d || !o->attached) return true;  // known to exist, but not a door we may judge (unknown)
				*a_stage = 1;
				o->open = static_cast<int>(RE::BGSOpenCloseForm::GetOpenState(r));
				if (o->load) return true;  // C5: a load door is never opened / never his way (no lock / may-open asks)
				*a_stage = 2;
				RE::REFR_LOCK* lk = r->GetLock();
				o->hasLock = lk != nullptr;
				o->lockOn = lk && (lk->flags & 0x1) != 0;
				if (o->lockOn) o->level = static_cast<int>(lk->GetLockLevel(r));  // C16: the door ref is the owner
				*a_stage = 3;
				bool pick = false;
				o->may = a_can && a_me ? a_can(r, a_me, pick) : false;
				o->pick = pick;
				return true;
			} __except (1) {
				*a_stage += 10;
				return false;
			}
		}
		// 1 = asked, 0 = refused (no 3D / detached), -1 = a fault (s9 integrator: the crash shield switches the doors off, C18)
		int SehOpen(std::uint32_t a_id) noexcept
		{
			__try {
				RE::TESForm* f = RE::TESForm::GetFormByID(a_id);
				if (!f || f->GetFormType() != RE::ENUM_FORM_ID::kREFR) return 0;
				auto* r = static_cast<RE::TESObjectREFR*>(f);
				if (!r->Get3D() || !r->parentCell || !r->parentCell->IsAttached()) return 0;  // C2
				RE::BGSOpenCloseForm::SetOpenState(r, true, false);  // C11: never snap; the return value is not "success" (C2)
				return 1;
			} __except (1) {
				return -1;
			}
		}
		CanOpenFn CanOpen()
		{
			static CanOpenFn fn = nullptr;
			static bool      looked = false;
			if (!looked) {
				looked = true;
				try {
					fn = reinterpret_cast<CanOpenFn>(REL::ID(2198701).address());
				} catch (...) {
					fn = nullptr;
				}
			}
			return fn;
		}
		bool Enabled() { return sp::cfg::Tunable("router.doorFromNavmesh", 0.0) != 0; }

		// s9 integrator: HOOKS.md E probes (TEST only, [Tabs] look.probe 1; our own answers, no extra engine read).
		// P4 doorq: the first answer for a door and every change of it (state / lock / level / may / pick / load) with the call cost;
		// P5 dooropen: each open ask (the following doorq lines show the engine's 3 -> 2 -> 1 timing, HOOKS C2 / B)
		struct Seen
		{
			std::uint32_t id{ 0 };
			int           open{ -1 }, level{ -2 };
			bool          lockOn{ false }, may{ false }, pick{ false }, load{ false };
		};
		std::array<Seen, 64> g_seen{};
		int                  g_seenN = 0;
		std::uint64_t        g_probeLines = 0;
		void Probe(const char* a_kind, std::uint32_t a_id, const sp::route::DoorFacts& f, bool a_pick = false, double a_us = 0)
		{
			if (sp::cfg::Tunable("look.probe", 0.0) == 0.0) return;
			const bool open = std::string_view(a_kind) == "dooropen";
			if (!open) {
				Seen* e = nullptr;
				for (int i = 0; i < g_seenN; ++i)
					if (g_seen[static_cast<std::size_t>(i)].id == a_id) e = &g_seen[static_cast<std::size_t>(i)];
				if (!e) {
					e = &g_seen[static_cast<std::size_t>(g_seenN % static_cast<int>(g_seen.size()))];
					if (g_seenN < static_cast<int>(g_seen.size())) ++g_seenN;
					*e = {};
					e->id = a_id;
				}
				const int lvl = f.locked ? f.lockLevel : -1;
				if (e->open == f.open && e->level == lvl && e->lockOn == f.locked && e->may == f.canOpen && e->pick == a_pick && e->load == f.load) return;
				e->open = f.open;
				e->level = lvl;
				e->lockOn = f.locked;
				e->may = f.canOpen;
				e->pick = a_pick;
				e->load = f.load;
			}
			if (!sp::runtime::LogDue(g_probeLines, 3000.0, 0.0)) return;
			REX::INFO("SPN|LOOK|PROBE|{} id={:08X} st={} lock={} lvl={} quest={} load={} may={} pick={} us={:.1f} pump={}", a_kind, a_id, f.open, f.locked ? 1 : 0, f.locked ? f.lockLevel : -1,
				f.quest ? 1 : 0, f.load ? 1 : 0, f.canOpen ? 1 : 0, a_pick ? 1 : 0, a_us, sp::fo4::threads::InPump() ? 1 : 0);
		}
	}

	bool Ready()
	{
		if (s.off || !Enabled()) return false;
		if (!s.checked) RunChecks();
		return !s.off;
	}

	bool IsPathingDoor(const void* a_obj) noexcept
	{
		if (!a_obj || !s.base || s.off) return false;
		const std::uintptr_t vt = *reinterpret_cast<const std::uintptr_t*>(a_obj);  // (inside the caller's SEH shell)
		if (sp::r6check::PathingDoorVt(s.base, vt, 0)) return true;
		const std::uintptr_t slot1 = vt ? reinterpret_cast<const std::uintptr_t*>(vt)[1] : 0;
		return sp::r6check::PathingDoorVt(s.base, vt, slot1);
	}

	bool Facts(RE::Actor* a_me, std::uint32_t a_ref, sp::route::DoorFacts& a_out)
	{
		a_out = {};
		a_out.ref = a_ref;
		if (!a_ref || !Ready()) return false;
		if (!sp::fo4::threads::InPump()) {  // C12: P5 only
			++s.notPump;
			return false;
		}
		const auto t0 = std::chrono::steady_clock::now();
		Raw        r;
		int        stage = 0;
		const bool ok = SehFacts(a_ref, a_me, CanOpen(), &r, &stage);
		const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
		s.usMax = std::max(s.usMax, us);
		s.usSum += us;
		++s.queries;
		if (!ok && stage >= 10) {
			++s.faults;
			static std::string why;
			why = std::format("fault fn={} door={:08X}", stage == 10 ? "resolve" : stage == 11 ? "D1" : stage == 12 ? "D3/D4" : "D5", a_ref);
			TurnOff(why.c_str(), true);
			return false;
		}
		if (!ok || r.del || r.dis || !r.has3d || !r.attached) {  // C6 / C8: unknown (never "open")
			++s.unknown;
			return false;
		}
		a_out.known = true;
		a_out.at = { r.x, r.y, r.z };
		a_out.open = r.open;
		a_out.load = r.load;
		a_out.locked = r.lockOn;
		a_out.lockLevel = r.lockOn ? r.level : -1;
		a_out.quest = r.lockOn && sp::r6check::QuestLock(r.level);
		a_out.canOpen = !r.load && r.may && !r.pick;  // C3: may only without bobby pins
		if (r.load) ++s.loads;
		if (a_out.locked) ++(a_out.quest ? s.quest : s.locked);
		Probe("doorq", a_ref, a_out, r.pick, us);
		return true;
	}

	bool Open(RE::Actor* a_me, std::uint32_t a_ref)
	{
		sp::route::DoorFacts f;
		if (!Facts(a_me, a_ref, f)) return false;
		// C5 / C3 / C2: never a load door, only when he may open it, only closed / closing, with 3D in an attached cell
		if (f.load || !f.canOpen || !(f.open == sp::route::kDoorClosed || f.open == sp::route::kDoorClosing)) {
			++s.openRefused;
			return false;
		}
		const int asked = SehOpen(a_ref);
		if (asked < 0) {
			++s.faults;
			static std::string why;
			why = std::format("fault fn=D2 door={:08X}", a_ref);
			TurnOff(why.c_str(), true);
			return false;
		}
		if (asked == 0) {
			++s.openRefused;
			return false;
		}
		++s.opens;
		Probe("dooropen", a_ref, f);
		return true;
	}

	void OnLoad() {}

	std::string Describe()
	{
		const double n = static_cast<double>(std::max<std::uint64_t>(1, s.queries));
		return std::format("doors on={} q={} unknown={} load={} locked={} quest={} opens={} refused={} faults={} notPump={} usAvg={:.1f} usMax={:.0f}{}", !s.off && Enabled(),
			s.queries, s.unknown, s.loads, s.locked, s.quest, s.opens, s.openRefused, s.faults, s.notPump, s.usSum / n, s.usMax, s.off ? std::format(" off={}", s.offWhy) : std::string());
	}
}
