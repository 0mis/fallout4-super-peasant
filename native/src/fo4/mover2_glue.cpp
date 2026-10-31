// OWNER: R6 step 6. See mover2_glue.h.
#include "fo4/mover2_glue.h"

#include "config.h"
#include "core/r6_checks.h"
#include "fo4/actors.h"
#include "fo4/look_hooks.h"
#include "runtime.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <format>
#include <mutex>

namespace sp::fo4::mv2
{
	namespace
	{
		using sp::cfg::Tunable;
		namespace M = sp::mover2;

		double NowS()
		{
			return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) * 1e-6;
		}

		// ---- features (load checks + crash shield)
		enum Feat : int { fClock = 0, fCapsule, fMotion, fVats, fWarp, kFeatN };
		const char*       kFeatName[kFeatN] = { "clock", "capsule", "motionProps", "vatsClock", "noPathWarp" };
		std::atomic<bool> g_off[kFeatN]{};
		std::atomic<int>  g_faults[kFeatN]{};
		bool              g_offLogged[kFeatN]{};
		void Trip(int f)
		{
			g_faults[f].fetch_add(1, std::memory_order_relaxed);
			g_off[f].store(true, std::memory_order_relaxed);
		}
		bool Off(int f) { return g_off[f].load(std::memory_order_relaxed); }

		bool              g_checked{ false };
		bool              g_okMotion{ false }, g_okVats{ false }, g_okWarp{ false }, g_okClock{ false };
		std::uintptr_t    g_gN{ 0 }, g_gSub{ 0 }, g_gAvail{ 0 };
		std::uintptr_t    g_vtRB{ 0 };
		using SetMpFn = void(std::uintptr_t, int, short);
		using MarkFn = void(std::uintptr_t);
		using VatsFn = float(void*, RE::TESObjectREFR*);
		using WarpFn = bool(RE::Actor*);
		SetMpFn* g_setMp{ nullptr };
		MarkFn*  g_mark{ nullptr };
		MarkFn*  g_unmark{ nullptr };
		VatsFn*  g_vats{ nullptr };
		WarpFn*  g_canWarp{ nullptr };
		std::uintptr_t g_doAddr{ 0 };
		std::string    g_noPathState{ "off" };

		// ---- the owner flag / veto key (atomics only: read by the L2 hook)
		std::atomic<std::int64_t> g_ownUs{ 0 };
		std::atomic<float>        g_ownHold{ 0.25f };
		std::atomic<bool>         g_vetoKey{ false };
		std::atomic<bool>         g_vatsKey{ false };
		std::atomic<bool>         g_moverKey{ false };  // look.mover
		std::atomic<bool>         g_vetoRelease{ false };     // R6 step 6 FIX: the runtime released the veto (AI leg / fight stall)
		std::atomic<std::uint64_t> g_vetoRelFrames{ 0 };      // frames released (1 Hz line: rel=)
		std::atomic<std::uint64_t> g_vetoRelEvents{ 0 };      // stall releases (session)
		std::atomic<bool>         g_vetoTurn{ false };  // look.aiVetoTurn  // look.vatsClock (cached once a second: VatsMult runs twice a frame)

		// ---- metrics (P6 thread writes; the 1 Hz line is written from P6 too)
		struct Win
		{
			sp::mover2::ErrMeter err, errOpen, step;
			std::uint64_t writes{ 0 }, idle{ 0 }, capped{ 0 }, zeroN{ 0 };
		};
		Win               g_win{}, g_ses{};
		double            g_lineAt{ -1e9 };
		std::uint64_t     g_lines{ 0 };
		// motion properties
		sp::mover2::MpRead g_mp{};
		int               g_mpVerdict{ -1 };
		std::uint64_t     g_mpBinds{ 0 }, g_mpReads{ 0 }, g_mpBad{ 0 };
		double            g_mpAt{ -1e9 };
		const void*       g_mpCon{ nullptr };
		int               g_warpArmed{ -1 };
		double            g_warpAt{ -1e9 };
		// the P5 capsule snapshot
		std::mutex        g_p5Lock;
		Vec3              g_p5Cap{};
		bool              g_p5Have{ false };
		double            g_keysAt{ -1e9 };

		// ---- SEH bodies (no C++ objects with destructors inside)
		bool SehClock(std::uint32_t* n, float* sub, float* avail) noexcept
		{
			__try {
				*n = *reinterpret_cast<const std::uint32_t*>(g_gN);
				*sub = *reinterpret_cast<const float*>(g_gSub);
				*avail = *reinterpret_cast<const float*>(g_gAvail);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehCapsule(RE::bhkCharacterController* con, float out[3]) noexcept
		{
			__try {
				RE::hkVector4f p;
				con->GetPositionImpl(p, false);
				out[0] = p[0];
				out[1] = p[1];
				out[2] = p[2];
				return true;
			} __except (1) {
				return false;
			}
		}
		__declspec(noinline) std::uint32_t BodyIdOf(RE::bhkCharacterController* con)
		{
			const auto    bid = con->GetBodyIdImpl();  // vf 0x41 (2278664)
			std::uint32_t id = 0;
			std::memcpy(&id, &bid, sizeof(id));
			return id;
		}
		// reads the entries; a_bind = rebind to 2 under markForWrite when the verdict is kWrongId
		int SehMotion(RE::Actor* me, RE::bhkCharacterController* con, bool a_bind, sp::mover2::MpRead* r, int* bound) noexcept
		{
			__try {
				*bound = 0;
				auto* c = me->parentCell;
				if (!c || !c->IsAttached()) return -1;
				const auto bw = reinterpret_cast<std::uintptr_t>(c->GetbhkWorld());
				if (!bw) return -1;
				const auto world = *reinterpret_cast<const std::uintptr_t*>(bw + 0x60);
				const std::uint32_t id = BodyIdOf(con);
				if (!world || id >= (1u << 20)) return -1;
				for (int pass = 0; pass < 2; ++pass) {
					const auto bodies = *reinterpret_cast<const std::uintptr_t*>(world + 0x20);
					const auto motions = *reinterpret_cast<const std::uintptr_t*>(world + 0xE0);
					const auto lib = *reinterpret_cast<const std::uintptr_t*>(world + 0x5D0);
					if (!bodies || !motions || !lib) return -1;
					const auto mpTbl = *reinterpret_cast<const std::uintptr_t*>(lib + 0x28);
					if (!mpTbl) return -1;
					const std::int32_t mi = *reinterpret_cast<const std::int32_t*>(bodies + static_cast<std::uintptr_t>(id) * 0x90 + 0x68);
					if (mi < 0) return -1;
					const std::uint16_t mpId = *reinterpret_cast<const std::uint16_t*>(motions + static_cast<std::uintptr_t>(static_cast<std::uint32_t>(mi)) * 0x80 + 0x38);
					r->id = mpId < 4096 ? mpId : -1;
					r->g1 = *reinterpret_cast<const float*>(mpTbl + 1 * 0x40 + 0x08);
					r->v1 = *reinterpret_cast<const float*>(mpTbl + 1 * 0x40 + 0x10);
					r->g2 = *reinterpret_cast<const float*>(mpTbl + 2 * 0x40 + 0x08);
					r->v2 = *reinterpret_cast<const float*>(mpTbl + 2 * 0x40 + 0x10);
					if (r->id >= 0) {
						r->gMe = *reinterpret_cast<const float*>(mpTbl + static_cast<std::uintptr_t>(r->id) * 0x40 + 0x08);
						r->vMe = *reinterpret_cast<const float*>(mpTbl + static_cast<std::uintptr_t>(r->id) * 0x40 + 0x10);
					}
					const auto v = sp::mover2::JudgeMp(*r);
					if (pass == 0 && a_bind && v == sp::mover2::MpVerdict::kWrongId && g_setMp && g_mark && g_unmark) {
						g_mark(world);
						// B2 codex review (P9-M1): mark/unmark are the world's lock_write / unlock_write (+0x6D8); a fault in the set
						// must still release the lock, else every other Havok writer waits on it forever (a hang, not a shielded fault)
						__try {
							g_setMp(world, static_cast<int>(id), 2);
						} __finally {
							g_unmark(world);
						}
						*bound = 1;
						continue;  // read back
					}
					return static_cast<int>(v);
				}
				return static_cast<int>(sp::mover2::JudgeMp(*r));
			} __except (1) {
				return -2;
			}
		}
		bool SehVats(void* vats, RE::Actor* me, float* out) noexcept
		{
			__try {
				*out = g_vats(vats, me);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehWarp(RE::Actor* me, bool* out) noexcept
		{
			__try {
				*out = g_canWarp(me);
				return true;
			} __except (1) {
				return false;
			}
		}
		// the DO: type +0x28 == KYWD, editor id +0x30 == "NoPathFailureWarping_DO"; returns its form (+0x20) or -1 / -2
		int SehDoState(std::uintptr_t a_do, void** a_form) noexcept
		{
			__try {
				auto* d = reinterpret_cast<RE::BGSDefaultObject*>(a_do);
				if (d->type != RE::ENUM_FORM_ID::kKYWD) return -1;
				const char* n = d->formEditorID.c_str();
				if (!n || std::strcmp(n, "NoPathFailureWarping_DO") != 0) return -1;
				*a_form = d->form;
				return 0;
			} __except (1) {
				return -2;
			}
		}
		bool SehDoWrite(std::uintptr_t a_do, void* a_kw) noexcept
		{
			__try {
				reinterpret_cast<RE::BGSDefaultObject*>(a_do)->form = static_cast<RE::TESForm*>(a_kw);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehReadBytes(std::uintptr_t a, std::uint8_t* out, int n) noexcept
		{
			__try {
				for (int i = 0; i < n; ++i) out[i] = reinterpret_cast<const std::uint8_t*>(a)[i];
				return true;
			} __except (1) {
				return false;
			}
		}

		void RunChecks()
		{
			if (g_checked) return;
			g_checked = true;
			bool okMotion = true, okVats = true, okWarp = true;
			auto reader = [](std::uint64_t id, std::uint32_t& rva, std::uint32_t off, std::uint8_t* out, int n) -> bool {
				try {
					const REL::ID r(id);
					rva = static_cast<std::uint32_t>(r.offset());
					if (n <= 0) return true;
					return SehReadBytes(r.address() + off, out, n);
				} catch (...) {
					return false;
				}
			};
			for (const auto& p : M::kProofs) {
				std::string why;
				const bool  ok = sp::r6check::CheckProof(p, reader, why);
				const std::string_view f = p.feat;
				if (!ok) {
					if (f == "motionProps") okMotion = false;
					else if (f == "vatsClock") okVats = false;
					else if (f == "noPathWarp") okWarp = false;
				}
				REX::INFO("SPN|MOVER2|check {} id={} rva={:X}: {}", p.name, p.id, p.rva, ok ? std::string("ok") : std::format("REFUSED why={} -> feat {} off", why, p.feat));
			}
			// CanWarpOnPathFailure must read the DO's form pointer (rip-relative at +0x0F)
			if (okWarp) {
				try {
					const REL::ID fn(2229636), dobj(4800127), dform(4800128);
					std::uint8_t  b[4]{};
					std::int32_t  disp = 0;
					if (SehReadBytes(fn.address() + M::kCanWarpDispOff, b, 4)) std::memcpy(&disp, b, 4);
					okWarp = M::CanWarpReadsDo(static_cast<std::uint32_t>(fn.offset()), disp, static_cast<std::uint32_t>(dobj.offset())) &&
					         dform.offset() == dobj.offset() + M::kDoFormOff;
					if (okWarp) {
						g_canWarp = reinterpret_cast<WarpFn*>(fn.address());
						g_doAddr = dobj.address();
					}
					REX::INFO("SPN|MOVER2|check CanWarpOnPathFailure reads the DO form pointer: {}", okWarp ? "ok" : "REFUSED -> feat noPathWarp off");
				} catch (...) {
					okWarp = false;
				}
			}
			try {
				if (okMotion) {
					g_setMp = reinterpret_cast<SetMpFn*>(REL::ID(2255630).address());
					g_mark = reinterpret_cast<MarkFn*>(REL::ID(2277691).address());
					g_unmark = reinterpret_cast<MarkFn*>(REL::ID(2277692).address());
				}
				if (okVats) g_vats = reinterpret_cast<VatsFn*>(REL::ID(2237204).address());
			} catch (...) {
				okMotion = okVats = false;
			}
			// the physics clock globals (step 2 probe ids, RVAs on the exe copy) and the RB controller vtable
			bool okClock = true;
			try {
				struct G { std::uint64_t id; std::uint32_t rva; std::uintptr_t* out; };
				const G gs[] = { { 2704764, 0x3D87EA4, &g_gN }, { 2704758, 0x3D87E8C, &g_gSub }, { 2704762, 0x3D87E9C, &g_gAvail } };
				for (const auto& g : gs) {
					const REL::ID id(g.id);
					if (id.offset() != g.rva) {
						okClock = false;
						REX::WARN("SPN|MOVER2|check phys global id {} rva {:X} != {:X}: REFUSED -> the mover v2 stays off", g.id, id.offset(), g.rva);
						break;
					}
					*g.out = id.address();
				}
				const REL::ID rb(152147);
				g_vtRB = rb.offset() == 0x26B35E8 ? rb.address() : 0;
				if (!g_vtRB) okMotion = false;
			} catch (...) {
				okClock = false;
			}
			g_okMotion = okMotion;
			g_okVats = okVats;
			g_okWarp = okWarp;
			g_okClock = okClock;
			REX::INFO("SPN|MOVER2|checks clock={} motionProps={} vatsClock={} noPathWarp={}", okClock, okMotion, okVats, okWarp);
		}

		void Refresh()
		{
			const double t = NowS();
			if (t - g_keysAt < 1.0) return;
			g_keysAt = t;
			g_ownHold.store(static_cast<float>(Tunable("look.moverOwnHold", 0.25)), std::memory_order_relaxed);
			g_vetoKey.store(Tunable("look.aiVeto", 0.0) != 0.0, std::memory_order_relaxed);
			g_vatsKey.store(Tunable("look.vatsClock", 0.0) != 0.0, std::memory_order_relaxed);
			g_moverKey.store(Tunable("look.mover", 0.0) != 0.0, std::memory_order_relaxed);
			g_vetoTurn.store(Tunable("look.aiVetoTurn", 0.0) != 0.0, std::memory_order_relaxed);
		}

		void LogOff()
		{
			for (int f = 0; f < kFeatN; ++f) {
				if (Off(f) && !g_offLogged[f] && g_faults[f].load() > 0) {
					g_offLogged[f] = true;
					REX::ERROR("SPN|MOVER2|off feat={} why=seh faults={} (switched off for this session; the game goes on)", kFeatName[f], g_faults[f].load());
					sp::runtime::RequestDump("mover2 crash shield");
				}
			}
		}
	}

	void OnDataLoaded()
	{
		RunChecks();
		Refresh();
		if (Tunable("look.noPathWarp", 0.0) == 0.0) {
			// the step-6 esp's DFOB binds the DO by its editor ID at data load: with the key at 0 the plugin takes OUR keyword
			// back out (vanilla: no form), so the path-failure warp is armed exactly as before step 6 (one key away)
			g_noPathState = "off (look.noPathWarp 0)";
			if (g_okWarp && g_doAddr) {
				RE::TESForm* kw0 = nullptr;
				if (auto* dh = RE::TESDataHandler::GetSingleton()) kw0 = dh->LookupForm(0x822, "SuperPeasant.esp");
				void* cur0 = nullptr;
				if (kw0 && SehDoState(g_doAddr, &cur0) == 0 && cur0 == kw0) {
					if (SehDoWrite(g_doAddr, nullptr)) {
						g_noPathState = "off (look.noPathWarp 0: the esp's binding cleared)";
						REX::INFO("SPN|MOVER2|noPathWarp 0: NoPathFailureWarping_DO cleared (vanilla: his path-failure warp stays armed)");
					} else {
						Trip(fWarp);
					}
				}
			}
			return;
		}
		if (!g_okWarp || !g_doAddr) {
			g_noPathState = "refused (load check)";
			REX::WARN("SPN|MOVER2|noPathWarp refused (load check): the engine's path-failure warp stays armed for him");
			return;
		}
		RE::TESForm* kw = nullptr;
		if (auto* dh = RE::TESDataHandler::GetSingleton()) kw = dh->LookupForm(0x822, "SuperPeasant.esp");
		if (!kw || kw->GetFormType() != RE::ENUM_FORM_ID::kKYWD) {
			g_noPathState = "no keyword (SuperPeasant.esp 0x822)";
			REX::WARN("SPN|MOVER2|noPathWarp: keyword SP_NoPathWarpKW (SuperPeasant.esp 0x822) not found: warp stays armed");
			return;
		}
		void*     cur = nullptr;
		const int st = SehDoState(g_doAddr, &cur);
		if (st == -2) {
			Trip(fWarp);
			g_noPathState = "fault";
			return;
		}
		if (st != 0) {
			g_noPathState = "refused (the DO is not NoPathFailureWarping_DO / KYWD)";
			REX::WARN("SPN|MOVER2|noPathWarp refused: the default object at id 4800127 is not NoPathFailureWarping_DO (KYWD)");
			return;
		}
		if (cur == kw) {
			g_noPathState = "bound by the esp (DFOB)";
		} else if (cur != nullptr) {
			g_noPathState = "refused (the DO already points at another form)";
			REX::WARN("SPN|MOVER2|noPathWarp refused: NoPathFailureWarping_DO already points at {:08X} (another mod?)", reinterpret_cast<RE::TESForm*>(cur)->GetFormID());
			return;
		} else if (SehDoWrite(g_doAddr, kw)) {
			g_noPathState = "written (plugin fallback)";
		} else {
			Trip(fWarp);
			g_noPathState = "fault";
			return;
		}
		REX::INFO("SPN|MOVER2|noPathWarp: NoPathFailureWarping_DO -> {:08X} ({}): his path-failure warp agent disarms at its Init / LoadGame", kw->GetFormID(), g_noPathState);
	}

	bool ReadClock(double a_dt, sp::mover2::PhysClock& a_out)
	{
		a_out = {};
		if (!g_checked) RunChecks();
		if (!g_okClock || Off(fClock)) return false;
		std::uint32_t n = 0;
		float         sub = 0, avail = 0;
		if (!SehClock(&n, &sub, &avail)) {
			Trip(fClock);
			return false;
		}
		a_out.dt = a_dt;
		a_out.n = static_cast<int>(std::min<std::uint32_t>(n, 100));
		a_out.substep = sub;
		a_out.avail = avail;
		a_out.valid = true;
		return sp::mover2::ClockSane(a_out);
	}

	bool CapsulePos(RE::bhkCharacterController* a_con, Vec3& a_out)
	{
		if (!a_con || Off(fCapsule)) return false;
		float p[3]{};
		if (!SehCapsule(a_con, p)) {
			Trip(fCapsule);
			return false;
		}
		const double k = sp::cfg::K::kHavokToGame;
		a_out = { p[0] * k, p[1] * k, p[2] * k };
		return sp::math::IsFinite(a_out);
	}

	float VatsMult(RE::Actor* a_me)
	{
		if (!a_me || !g_vatsKey.load(std::memory_order_relaxed) || !g_okVats || !g_vats || Off(fVats)) return 1.0f;
		void* vats = RE::VATS::GetSingleton();
		if (!vats) return 1.0f;
		float m = 1.0f;
		if (!SehVats(vats, a_me, &m)) {
			Trip(fVats);
			return 1.0f;
		}
		return (std::isfinite(m) && m > 0.0f && m <= 1.0f) ? m : 1.0f;
	}

	void MotionProps(RE::Actor* a_me, RE::bhkCharacterController* a_con, bool a_force)
	{
		if (!a_me || !a_con || Off(fMotion)) return;
		const double t = NowS();
		const bool   newCon = a_con != g_mpCon;
		if (!a_force && !newCon && t - g_mpAt < 1.0) return;
		g_mpAt = t;
		g_mpCon = a_con;
		if (!g_checked) RunChecks();
		if (!g_vtRB || *reinterpret_cast<const std::uintptr_t*>(a_con) != g_vtRB) {
			g_mpVerdict = -3;  // not the RB controller: nothing to bind (proxy path)
			return;
		}
		const bool bind = g_okMotion && Tunable("look.motionProps", 0.0) != 0.0;
		sp::mover2::MpRead r;
		int                bound = 0;
		const int          v = SehMotion(a_me, a_con, bind, &r, &bound);
		if (v == -2) {
			Trip(fMotion);
			return;
		}
		++g_mpReads;
		g_mp = r;
		g_mpVerdict = v;
		if (bound) {
			++g_mpBinds;
			REX::INFO("SPN|MOVER2|motionProps bound: id -> {} (g {:.2f} max {:.0f} Havok/s) verdict {} n={}", r.id, r.gMe, r.vMe,
				v >= 0 ? sp::mover2::MpVerdictName(static_cast<sp::mover2::MpVerdict>(v)) : "?", g_mpBinds);
		}
		if (v != static_cast<int>(sp::mover2::MpVerdict::kOk)) ++g_mpBad;
		if (v == static_cast<int>(sp::mover2::MpVerdict::kLayout) && g_mpBad <= 3)
			REX::WARN("SPN|MOVER2|motionProps layout check failed (entry1 g {} v {}, entry2 g {} v {}): no write", r.g1, r.v1, r.g2, r.v2);
	}

	void NoteOwned(bool a_owned)
	{
		if (a_owned) g_ownUs.store(static_cast<std::int64_t>(NowS() * 1e6), std::memory_order_relaxed);
	}
	bool Owns()
	{
		const auto at = g_ownUs.load(std::memory_order_relaxed);
		return at != 0 && NowS() * 1e6 - static_cast<double>(at) <= g_ownHold.load(std::memory_order_relaxed) * 1e6;
	}
	bool VetoReady() { return g_vetoKey.load(std::memory_order_relaxed); }
	bool VetoTurn() { return g_vetoTurn.load(std::memory_order_relaxed); }
	void SetVetoRelease(bool a_release, bool a_newEvent)
	{
		g_vetoRelease.store(a_release, std::memory_order_relaxed);
		if (a_release) g_vetoRelFrames.fetch_add(1, std::memory_order_relaxed);
		if (a_newEvent) g_vetoRelEvents.fetch_add(1, std::memory_order_relaxed);
	}
	bool VetoReleased() { return g_vetoRelease.load(std::memory_order_relaxed); }

	void NoteErr(double a_u, bool a_open)
	{
		g_win.err.Add(a_u);
		g_ses.err.Add(a_u);
		if (a_open) {
			g_win.errOpen.Add(a_u);
			g_ses.errOpen.Add(a_u);
		}
	}
	void NoteStep(double a_u)
	{
		g_win.step.Add(a_u);
		g_ses.step.Add(a_u);
	}
	void NoteWrite(bool a_idle, bool a_capped, int a_n)
	{
		++g_win.writes;
		++g_ses.writes;
		if (a_idle) ++g_win.idle, ++g_ses.idle;
		if (a_capped) ++g_win.capped, ++g_ses.capped;
		if (a_n == 0) ++g_win.zeroN, ++g_ses.zeroN;
	}

	void OnP5(RE::Actor* a_me)
	{
		Refresh();
		LogOff();
		if (!a_me || !g_moverKey.load(std::memory_order_relaxed)) return;
		auto* con = sp::fo4::GetController(a_me);
		Vec3  p;
		if (con && a_me->parentCell && a_me->parentCell->IsAttached() && CapsulePos(con, p)) {
			std::scoped_lock l{ g_p5Lock };
			g_p5Cap = p;
			g_p5Have = true;
		}
	}
	bool TakeP5Capsule(Vec3& a_out)
	{
		std::scoped_lock l{ g_p5Lock };
		if (!g_p5Have) return false;
		g_p5Have = false;
		a_out = g_p5Cap;
		return true;
	}

	int WarpArmed(RE::Actor* a_me)
	{
		if (!a_me || !g_canWarp || Off(fWarp)) return -1;
		bool w = false;
		if (!SehWarp(a_me, &w)) {
			Trip(fWarp);
			return -1;
		}
		return w ? 1 : 0;
	}

	void LineP6(RE::Actor* a_me)
	{
		const double t = NowS();
		if (t - g_lineAt < 1.0) return;
		const bool first = g_lineAt < -1e8;
		g_lineAt = t;
		if (t - g_warpAt >= 1.0) {
			g_warpAt = t;
			g_warpArmed = WarpArmed(a_me);
		}
		++g_lines;
		static std::uint64_t s_vetoPrev = 0;
		const std::uint64_t  veto = sp::fo4::look::VetoCount();
		const std::uint64_t  vetoWin = veto - s_vetoPrev;
		s_vetoPrev = veto;
		static std::uint64_t s_relPrev = 0;
		const std::uint64_t  rel = g_vetoRelFrames.load(std::memory_order_relaxed);
		const std::uint64_t  relWin = rel - s_relPrev;
		s_relPrev = rel;
		if (first || g_win.writes > 0 || (g_lines % 10) == 0) {
			REX::INFO("SPN|MOVER2|s veto={} vetoSes={} w={} idle={} cap={} n0={} err n={} p50={:.0f} p99={:.0f} max={:.0f} open p99={:.0f} step n={} p99={:.0f} max={:.1f} | ses err p99={:.0f} open p99={:.0f} step max={:.1f} | mp={} g={:.2f} v={:.0f} verdict={} binds={} warpArmed={} noPath={} rel={} relEv={}",
				vetoWin, veto, g_win.writes, g_win.idle, g_win.capped, g_win.zeroN, g_win.err.n, g_win.err.Pct(0.5), g_win.err.Pct(0.99), g_win.err.max, g_win.errOpen.Pct(0.99), g_win.step.n,
				g_win.step.Pct(0.99), g_win.step.max, g_ses.err.Pct(0.99), g_ses.errOpen.Pct(0.99), g_ses.step.max, g_mp.id, g_mp.gMe, g_mp.vMe, g_mpVerdict, g_mpBinds, g_warpArmed,
				g_noPathState, relWin, g_vetoRelEvents.load(std::memory_order_relaxed));
		}
		g_win = {};
	}

	std::string Describe()
	{
		return std::format("mv2 w={} errP99={:.0f} openP99={:.0f} stepMax={:.1f} mp={} verdict={} binds={} warpArmed={}", g_ses.writes, g_ses.err.Pct(0.99), g_ses.errOpen.Pct(0.99),
			g_ses.step.max, g_mp.id, g_mpVerdict, g_mpBinds, g_warpArmed);
	}
}
