// OWNER: R6 step 13 (CORE lane). See fling_now.h.
#include "fo4/fling_now.h"

#include "config.h"
#include "fo4/actors.h"
#include "fo4/look_hooks.h"  // B2 codex review (FL-01): CkdExecutorHooked
#include "runtime.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <format>

namespace sp::fo4::flingnow
{
	namespace
	{
		namespace FL = sp::flinglook;

		// ---- features (load checks + crash shield)
		enum Feat : int { fNow = 0, fSpin, fGravity, kFeatN };
		const char*       kFeatName[kFeatN] = { "fling.now", "fling.spin", "fling.gravity" };
		std::atomic<bool> g_off[kFeatN]{};
		std::atomic<int>  g_faults[kFeatN]{};
		const char*       g_offWhy[kFeatN]{};
		bool              g_offLogged[kFeatN]{};
		void Trip(int f, const char* a_why)
		{
			g_faults[f].fetch_add(1, std::memory_order_relaxed);
			if (!g_offWhy[f]) g_offWhy[f] = a_why;
			g_off[f].store(true, std::memory_order_relaxed);
		}
		bool Off(int f) { return g_off[f].load(std::memory_order_relaxed); }

		bool g_checked{ false };
		FL::Features g_ok{ false, false, false };

		using ExecFn = void(RE::AIProcess*, RE::Actor*, const RE::NiPoint3*, float);
		using GraphFn = void(RE::TESObjectREFR*, float);
		using SetMotionFn = bool(RE::NiAVObject*, std::uint32_t, bool, bool, bool);
		using AddEntryFn = std::uint16_t*(void*, std::uint16_t*, const void*);
		using SetMpFn = void(std::uintptr_t, int, short);
		ExecFn*      g_exec{ nullptr };
		GraphFn*     g_graph{ nullptr };
		SetMotionFn* g_setMotion{ nullptr };
		AddEntryFn*  g_addEntry{ nullptr };
		SetMpFn*     g_setMp{ nullptr };

		// ---- the TABS entry cache: one per (physics world, source entry, factor); re-validated before every use (HOOKS T9)
		struct GravSlot
		{
			std::uintptr_t world{ 0 };
			std::uint16_t  src{ 0xFFFF }, tabs{ 0xFFFF };
			float          factor{ 0 }, gWritten{ 0 }, maxLin{ 0 };
		};
		std::array<GravSlot, 32> g_gc{};
		int                      g_gcNext{ 0 };
		std::uint64_t            g_mpLines{ 0 };
		bool                     g_capOff{ false };  // HOOKS T8: more than 8 TABS entries wanted in one world -> gravity off (logged once)

		// ---- stats (P5 only)
		std::uint64_t g_knocks{ 0 }, g_graphs{ 0 }, g_setMotions{ 0 }, g_angWrites{ 0 }, g_gravWrites{ 0 }, g_gravRestores{ 0 }, g_entriesAdded{ 0 };
		std::uint64_t g_lockBusy{ 0 }, g_gravRefused{ 0 };

		// ---------------------------------------------------------------- SEH shells (no C++ objects with destructors inside)
		bool SehReadBytes(std::uintptr_t a, std::uint8_t* out, int n) noexcept
		{
			__try {
				for (int i = 0; i < n; ++i) out[i] = reinterpret_cast<const std::uint8_t*>(a)[i];
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehReadPtr(std::uintptr_t a_at, std::uintptr_t* out) noexcept
		{
			__try {
				*out = *reinterpret_cast<const std::uintptr_t*>(a_at);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehExec(RE::AIProcess* p, RE::Actor* a, const RE::NiPoint3* from, float mag) noexcept
		{
			__try {
				g_exec(p, a, from, mag);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehGraph(RE::Actor* a) noexcept
		{
			__try {
				g_graph(a, 0.0001f);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehSetMotion(RE::NiAVObject* a_root) noexcept
		{
			__try {
				g_setMotion(a_root, 1, true, true, true);  // 1 = DYNAMIC (the game's own KillImpl / KillDying call)
				return true;
			} __except (1) {
				return false;
			}
		}
		void AngImpl(RE::hknpBSWorld* np, std::uint32_t id, const RE::hkVector4f* w)
		{
			RE::hknpBodyId b;
			b.m_value = id;
			np->SetBodyAngularVelocity(b, *w);
			np->ActivateBody(b);
		}
		bool SehAng(RE::hknpBSWorld* np, std::uint32_t id, const RE::hkVector4f* w) noexcept
		{
			__try {
				AngImpl(np, id, w);
				return true;
			} __except (1) {
				return false;
			}
		}
		std::int32_t BodyCountImpl(RE::hknpBSWorld* np, std::uintptr_t* a_base)
		{
			const auto& bodies = np->m_bodyManager.m_bodies;
			*a_base = reinterpret_cast<std::uintptr_t>(bodies.data());
			return static_cast<std::int32_t>(bodies.size());
		}
		std::int32_t SehBodyCount(RE::hknpBSWorld* np, std::uintptr_t* a_base) noexcept
		{
			__try {
				return BodyCountImpl(np, a_base);
			} __except (1) {
				return -1;
			}
		}
		bool SehGravity(std::uintptr_t world, float out[3]) noexcept
		{
			__try {
				const float* g = reinterpret_cast<const float*>(world + FL::kWorldGravity);
				out[0] = g[0];
				out[1] = g[1];
				out[2] = g[2];
				return true;
			} __except (1) {
				return false;
			}
		}
		FL::MpEntry ReadEntry(std::uintptr_t a_e)
		{
			FL::MpEntry e;
			e.exclusive = *reinterpret_cast<const std::uint32_t*>(a_e + FL::kMpExclusive);
			e.gravity = *reinterpret_cast<const float*>(a_e + FL::kMpGravity);
			e.maxLin = *reinterpret_cast<const float*>(a_e + FL::kMpMaxLin);
			e.maxAng = *reinterpret_cast<const float*>(a_e + FL::kMpMaxAng);
			e.linDamp = *reinterpret_cast<const float*>(a_e + FL::kMpLinDamp);
			e.angDamp = *reinterpret_cast<const float*>(a_e + FL::kMpAngDamp);
			return e;
		}
		// the motion of a body: false = not a simulated (dynamic, not keyframed) body with its own motion
		bool MotionOf(std::uintptr_t a_bodies, std::int32_t a_nBodies, std::uintptr_t a_motions, std::uint32_t a_id, bool a_needSim, std::uintptr_t* a_motion)
		{
			if (!a_bodies || !a_motions || a_id >= static_cast<std::uint32_t>(a_nBodies)) return false;
			const std::uintptr_t b = a_bodies + static_cast<std::uintptr_t>(a_id) * FL::kBodyStride;
			const std::uint32_t  fl = *reinterpret_cast<const std::uint32_t*>(b + FL::kBodyFlags);
			if (a_needSim && !((fl & 0x2u) && !(fl & 0x4u))) return false;
			const std::int32_t mi = *reinterpret_cast<const std::int32_t*>(b + FL::kBodyMotionId);
			if (mi <= 0 || mi >= (1 << 20)) return false;  // motion 0 = the shared static motion
			*a_motion = a_motions + static_cast<std::uintptr_t>(mi) * FL::kMotionStride;
			return true;
		}

		struct GravIo
		{
			std::uintptr_t world{ 0 }, bodies{ 0 };
			std::int32_t   nBodies{ 0 };
			float          factor{ 0 };
			// this world's TABS entries (in: from the cache; out: + the ones added now). fresh = validated in this call.
			int            nPairs{ 0 };
			std::uint16_t  src[FL::kMaxTabsEntriesPerWorld]{}, tabs[FL::kMaxTabsEntriesPerWorld]{};
			float          gW[FL::kMaxTabsEntriesPerWorld]{}, maxLin[FL::kMaxTabsEntriesPerWorld]{};
			std::uint8_t   fresh[FL::kMaxTabsEntriesPerWorld]{}, bad[FL::kMaxTabsEntriesPerWorld]{};
			int            added{ 0 }, capHit{ 0 }, skipped{ 0 };  // out: entries added now; a 9th source seen; bodies on a non-plain entry
			FL::MpEntry    e1{}, lastSrcE{};                       // out: probe
			std::uint16_t  lastSrc{ 0xFFFF }, lastTabs{ 0xFFFF };
		};
		// is `id` one of this world's (valid) TABS entries? (index or -1)
		int OurIndex(const GravIo* io, std::uint16_t id)
		{
			for (int k = 0; k < io->nPairs; ++k)
				if (!io->bad[k] && io->tabs[k] == id) return k;
			return -1;
		}
		// returns bodies written >= 0, -1 refused (layout / an added entry is not ours), -3 fault
		int SehGravApply(GravIo* io, const std::uint32_t* ids, int n, std::uint16_t* old, std::uint16_t* nw) noexcept
		{
			__try {
				const std::uintptr_t lib = *reinterpret_cast<const std::uintptr_t*>(io->world + FL::kWorldMpLib);
				const std::uintptr_t motions = *reinterpret_cast<const std::uintptr_t*>(io->world + FL::kWorldMotions);
				if (!lib || !motions) return -1;
				std::uintptr_t ent = *reinterpret_cast<const std::uintptr_t*>(lib + FL::kLibEntries);
				std::int32_t   cnt = *reinterpret_cast<const std::int32_t*>(lib + FL::kLibCount);
				if (!ent || cnt < 3 || cnt > 4096) return -1;
				io->e1 = ReadEntry(ent + 1 * FL::kMpStride);
				if (!FL::LibLayoutOk(io->e1)) return -1;
				// the cached entries of this world still ours? (HOOKS T9)
				for (int k = 0; k < io->nPairs; ++k) {
					io->bad[k] = !(io->tabs[k] < cnt && FL::IsOurEntry(ReadEntry(ent + static_cast<std::uintptr_t>(io->tabs[k]) * FL::kMpStride), io->gW[k], io->maxLin[k]));
					io->fresh[k] = io->bad[k] ? 0 : 1;
				}
				int written = 0;
				for (int i = 0; i < n; ++i) {
					old[i] = 0xFFFF;
					nw[i] = 0xFFFF;
					std::uintptr_t mo = 0;
					if (!MotionOf(io->bodies, io->nBodies, motions, ids[i], true, &mo)) continue;  // only simulated bodies (HOOKS T7)
					const std::uint16_t cur = *reinterpret_cast<const std::uint16_t*>(mo + FL::kMotionMpId);
					if (cur >= cnt || OurIndex(io, cur) >= 0) continue;  // already ours
					const FL::MpEntry se = ReadEntry(ent + static_cast<std::uintptr_t>(cur) * FL::kMpStride);
					if (!FL::TemplateOk(se)) {
						++io->skipped;
						continue;
					}
					int k = -1;
					for (int j = 0; j < io->nPairs && k < 0; ++j)
						if (!io->bad[j] && io->src[j] == cur) k = j;
					if (k < 0) {  // a TABS copy of this body's own entry
						int slot = -1;
						for (int j = 0; j < io->nPairs && slot < 0; ++j)
							if (io->bad[j]) slot = j;
						if (slot < 0) {
							if (io->nPairs >= FL::kMaxTabsEntriesPerWorld) {
								io->capHit = 1;
								continue;
							}
							slot = io->nPairs++;
						}
						const FL::MpEntry want = FL::MakeTabsEntry(se, io->factor);
						alignas(16) std::uint8_t buf[FL::kMpStride];
						std::memcpy(buf, reinterpret_cast<const void*>(ent + static_cast<std::uintptr_t>(cur) * FL::kMpStride), FL::kMpStride);
						std::memcpy(buf + FL::kMpExclusive, &want.exclusive, 4);
						std::memcpy(buf + FL::kMpGravity, &want.gravity, 4);
						std::memcpy(buf + FL::kMpLinDamp, &want.linDamp, 4);
						std::memcpy(buf + FL::kMpAngDamp, &want.angDamp, 4);
						std::uint16_t outId = 0xFFFF;
						g_addEntry(reinterpret_cast<void*>(lib), &outId, buf);
						++io->added;
						ent = *reinterpret_cast<const std::uintptr_t*>(lib + FL::kLibEntries);  // HOOKS T8: the table may have moved
						cnt = *reinterpret_cast<const std::int32_t*>(lib + FL::kLibCount);
						if (!ent || outId == 0xFFFF || outId >= cnt || !FL::IsOurEntry(ReadEntry(ent + static_cast<std::uintptr_t>(outId) * FL::kMpStride), want.gravity, se.maxLin)) return -1;
						io->src[slot] = cur;
						io->tabs[slot] = outId;
						io->gW[slot] = want.gravity;
						io->maxLin[slot] = se.maxLin;
						io->bad[slot] = 0;
						io->fresh[slot] = 1;
						io->lastSrc = cur;
						io->lastTabs = outId;
						io->lastSrcE = se;
						k = slot;
					}
					old[i] = cur;
					nw[i] = io->tabs[k];
					g_setMp(io->world, static_cast<int>(ids[i]), static_cast<short>(io->tabs[k]));
					++written;
				}
				return written;
			} __except (1) {
				return -3;
			}
		}
		int SehGravRestore(std::uintptr_t world, std::uintptr_t bodies, std::int32_t nBodies, const std::uint32_t* ids, int n, const std::uint32_t* saved,
			const std::uint16_t* old, const std::uint16_t* written, int nSaved) noexcept
		{
			__try {
				const std::uintptr_t motions = *reinterpret_cast<const std::uintptr_t*>(world + FL::kWorldMotions);
				if (!motions) return -1;
				int restored = 0;
				for (int i = 0; i < n; ++i) {
					int k = -1;
					for (int j = 0; j < nSaved && k < 0; ++j)
						if (saved[j] == ids[i] && old[j] != 0xFFFF && written[j] != 0xFFFF) k = j;
					if (k < 0) continue;
					std::uintptr_t mo = 0;
					if (!MotionOf(bodies, nBodies, motions, ids[i], false, &mo)) continue;
					if (*reinterpret_cast<const std::uint16_t*>(mo + FL::kMotionMpId) != written[k]) continue;  // the engine changed it: leave it
					g_setMp(world, static_cast<int>(ids[i]), static_cast<short>(old[k]));
					++restored;
				}
				return restored;
			} __except (1) {
				return -3;
			}
		}
		bool SehReadMotion(std::uintptr_t world, std::uintptr_t bodies, std::int32_t nBodies, std::uint32_t id, float lin[3], float ang[3]) noexcept
		{
			__try {
				const std::uintptr_t motions = *reinterpret_cast<const std::uintptr_t*>(world + FL::kWorldMotions);
				std::uintptr_t       mo = 0;
				if (!MotionOf(bodies, nBodies, motions, id, false, &mo)) return false;
				const float* l = reinterpret_cast<const float*>(mo + FL::kMotionLinVel);
				const float* a = reinterpret_cast<const float*>(mo + FL::kMotionAngVel);
				for (int i = 0; i < 3; ++i) {
					lin[i] = l[i];
					ang[i] = a[i];
				}
				return true;
			} __except (1) {
				return false;
			}
		}

		// ---------------------------------------------------------------- load checks
		void RunChecks()
		{
			if (g_checked) return;
			g_checked = true;
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
			g_ok = FL::RunAll(reader, [](const sp::r6check::Proof& p, bool ok, const std::string& why) {
				REX::INFO("SPN|S13|check {} id={} rva={:X}: {}", p.name, p.id, p.rva, ok ? std::string("ok") : std::format("REFUSED why={} -> feat {} off", why, p.feat));
			});
			// the executor's CanKnockDown call (+0x36): the original call, or a 5-byte call another hook (the step-5 CKD fallback) wrote
			{
				std::uint8_t b[5]{};
				bool         ok = false;
				try {
					const REL::ID r(FL::kCkdSite.id);
					ok = r.offset() == FL::kCkdSite.rva && SehReadBytes(r.address() + FL::kCkdSite.off, b, 5) && FL::CkdSiteOk(b, sp::fo4::look::CkdExecutorHooked());  // B2 codex (FL-01): was "true"
				} catch (...) {
					ok = false;
				}
				FL::Apply(g_ok, FL::kCkdSite.feat, ok);
				REX::INFO("SPN|S13|check {} id={} +0x{:X}: {} (bytes {:02X} {:02X} {:02X} {:02X} {:02X})", FL::kCkdSite.name, FL::kCkdSite.id, FL::kCkdSite.off,
					ok ? "ok" : "REFUSED -> feat fling.now off", b[0], b[1], b[2], b[3], b[4]);
			}
			// KillImpl's vtable slot (logged only: the virtual call is the game's own dispatch either way)
			try {
				std::uintptr_t v = 0;
				const auto     vt = REL::Relocation<std::uintptr_t>(RE::VTABLE::Actor[0]).address();
				const bool     same = SehReadPtr(vt + 8 * FL::kKillImplSlot, &v) && v == REL::ID(2230339).address();
				REX::INFO("SPN|S13|check Actor::KillImpl vtable slot 0x{:X}: {}", FL::kKillImplSlot, same ? "ok" : "another function owns the slot (logged only; the virtual is called)");
			} catch (...) {
			}
			try {
				if (g_ok.now) {
					g_exec = reinterpret_cast<ExecFn*>(REL::ID(2232584).address());
					g_graph = reinterpret_cast<GraphFn*>(REL::ID(2201508).address());
					g_setMotion = reinterpret_cast<SetMotionFn*>(REL::ID(2277723).address());
				}
				if (g_ok.gravity) {
					g_addEntry = reinterpret_cast<AddEntryFn*>(REL::ID(2257785).address());
					g_setMp = reinterpret_cast<SetMpFn*>(REL::ID(2255630).address());
				}
			} catch (...) {
				g_ok = { false, false, false };
			}
			REX::INFO("SPN|S13|checks now={} spin={} gravity={}", g_ok.now, g_ok.spin, g_ok.gravity);
		}

		// the world write lock (= hknpBSWorld::markForWrite's lock at +0x6D8, recursive for the owner): never blocks the pump
		bool TryLock(RE::hknpBSWorld* np)
		{
			if (np->m_worldLock.try_lock_write()) return true;
			++g_lockBusy;
			return false;
		}
	}

	void OnDataLoaded() { RunChecks(); }

	bool NowReady()
	{
		if (!g_checked) RunChecks();
		return g_ok.now && g_exec && g_graph && g_setMotion && !Off(fNow);
	}
	bool SpinReady()
	{
		if (!g_checked) RunChecks();
		return g_ok.spin && !Off(fSpin);
	}
	bool GravityReady()
	{
		if (!g_checked) RunChecks();
		return g_ok.gravity && g_addEntry && g_setMp && !Off(fGravity);
	}

	bool KnockNow(RE::Actor* a_t, const RE::NiPoint3& a_from, float a_mag, int& a_ksAfter)
	{
		a_ksAfter = -1;
		if (!a_t || !NowReady()) return false;
		if (a_t == sp::fo4::PeekPeasant() || a_t->IsPlayerRef()) return false;  // HOOKS T6: never him (the L4 veto sits on the queue site only)
		auto* p = a_t->currentProcess;
		if (!p || !p->middleHigh || !a_t->Get3D() || !a_t->parentCell || !a_t->parentCell->IsAttached()) return false;  // HOOKS T4 / E15
		if (!std::isfinite(a_mag) || !std::isfinite(a_from.x) || !std::isfinite(a_from.y) || !std::isfinite(a_from.z)) return false;
		const RE::NiPoint3 from = a_from;
		if (!SehExec(p, a_t, &from, a_mag)) {
			Trip(fNow, "knock executor fault");
			return false;
		}
		++g_knocks;
		a_ksAfter = static_cast<int>(a_t->knockState);
		return true;
	}

	bool GraphNow(RE::Actor* a_t)
	{
		if (!a_t || !NowReady() || !a_t->Get3D()) return false;
		if (!SehGraph(a_t)) {
			Trip(fNow, "graph update fault");
			return false;
		}
		++g_graphs;
		return true;
	}

	int SetMotionDynamic(RE::NiAVObject* a_root, RE::hknpBSWorld* a_np)
	{
		if (!a_root || !a_np || !NowReady()) return -1;
		if (!TryLock(a_np)) return 0;
		const bool ok = SehSetMotion(a_root);
		a_np->m_worldLock.unlock_write();
		if (!ok) {
			Trip(fNow, "SetMotion fault");
			return -1;
		}
		++g_setMotions;
		return 1;
	}

	bool SetAngVel(RE::hknpBSWorld* a_np, std::uint32_t a_id, const Vec3& a_w)
	{
		if (!a_np || !SpinReady() || !sp::math::IsFinite(a_w)) return false;
		const RE::hkVector4f w{ static_cast<float>(a_w.x), static_cast<float>(a_w.y), static_cast<float>(a_w.z), 0.0f };
		if (!SehAng(a_np, a_id, &w)) {
			Trip(fSpin, "SetBodyAngularVelocity fault");
			return false;
		}
		++g_angWrites;
		return true;
	}

	bool WorldGravity(RE::hknpBSWorld* a_np, double& a_gHavok)
	{
		a_gHavok = 0;
		if (!a_np) return false;
		float g[3]{};
		if (!SehGravity(reinterpret_cast<std::uintptr_t>(a_np), g)) return false;
		const double l = std::sqrt(static_cast<double>(g[0]) * g[0] + static_cast<double>(g[1]) * g[1] + static_cast<double>(g[2]) * g[2]);
		if (!std::isfinite(l) || l < 9.0 || l > 10.5 || !(g[2] < 0.0f)) return false;  // HOOKS B: |g| 9.0 .. 10.5, pointing down
		a_gHavok = l;
		return true;
	}

	int GravityApply(RE::hknpBSWorld* a_np, const std::uint32_t* a_ids, int a_n, std::uint16_t* a_old, std::uint16_t* a_new, double a_key, float& a_factor)
	{
		if (!a_np || !a_ids || !a_old || !a_new || a_n <= 0 || !GravityReady() || g_capOff) return -1;
		double g = 0;
		if (!WorldGravity(a_np, g)) {
			if (++g_gravRefused <= 5) REX::WARN("SPN|S13|mp world={:X}: world gravity unreadable or outside 9.0..10.5 Havok/s2 -> no TABS gravity here", reinterpret_cast<std::uintptr_t>(a_np));
			return -1;
		}
		const double fac = FL::GravityFactor(a_key, g);
		if (!(fac > 0)) return -1;
		GravIo io;
		io.world = reinterpret_cast<std::uintptr_t>(a_np);
		io.nBodies = SehBodyCount(a_np, &io.bodies);
		if (io.nBodies <= 0 || !io.bodies) return -1;
		io.factor = static_cast<float>(fac);
		// this world's cached TABS entries (validated inside before use; a recycled world just re-adds - de-duplicated)
		std::array<int, FL::kMaxTabsEntriesPerWorld> slotOf{};
		for (auto& s : slotOf) s = -1;
		for (int i = 0; i < static_cast<int>(g_gc.size()) && io.nPairs < FL::kMaxTabsEntriesPerWorld; ++i) {
			const auto& e = g_gc[static_cast<std::size_t>(i)];
			if (e.world != io.world || e.factor != io.factor || e.tabs == 0xFFFF) continue;
			slotOf[static_cast<std::size_t>(io.nPairs)] = i;
			io.src[io.nPairs] = e.src;
			io.tabs[io.nPairs] = e.tabs;
			io.gW[io.nPairs] = e.gWritten;
			io.maxLin[io.nPairs] = e.maxLin;
			++io.nPairs;
		}
		if (!TryLock(a_np)) return -2;
		const int r = SehGravApply(&io, a_ids, a_n, a_old, a_new);
		a_np->m_worldLock.unlock_write();
		if (r == -3) {
			Trip(fGravity, "motion properties fault");
			return -3;
		}
		if (r < 0) {
			if (++g_gravRefused <= 5)
				REX::WARN("SPN|S13|mp world={:X}: refused (entry1 g={} v={}, or the added entry is not ours) -> no TABS gravity", io.world, io.e1.gravity, io.e1.maxLin);
			return -1;
		}
		// the cache back: the slots of this world updated in place, the new pairs in free / the oldest slots
		for (int k = 0; k < io.nPairs; ++k) {
			int i = slotOf[static_cast<std::size_t>(k)];
			if (io.bad[k] && i >= 0) {
				g_gc[static_cast<std::size_t>(i)] = {};
				continue;
			}
			if (io.bad[k]) continue;
			if (i < 0) {
				for (int j = 0; j < static_cast<int>(g_gc.size()) && i < 0; ++j)
					if (g_gc[static_cast<std::size_t>(j)].tabs == 0xFFFF) i = j;
				if (i < 0) {
					i = g_gcNext;
					g_gcNext = (g_gcNext + 1) % static_cast<int>(g_gc.size());
				}
			}
			g_gc[static_cast<std::size_t>(i)] = { io.world, io.src[k], io.tabs[k], io.factor, io.gW[k], io.maxLin[k] };
		}
		if (io.added > 0) {
			g_entriesAdded += static_cast<std::uint64_t>(io.added);
			if (++g_mpLines <= 40)
				REX::INFO("SPN|S13|mp world={:X} g={:.3f} fac={:.4f} e1g={:.2f} e1v={:.0f} src={} srcG={:.2f} srcVmax={:.0f} srcWmax={:.0f} srcDl={:.3f} srcDa={:.3f} tabs={} added={} pairs={} skipped={}",
					io.world, g, io.factor, io.e1.gravity, io.e1.maxLin, io.lastSrc, io.lastSrcE.gravity, io.lastSrcE.maxLin, io.lastSrcE.maxAng, io.lastSrcE.linDamp,
					io.lastSrcE.angDamp, io.lastTabs, io.added, io.nPairs, io.skipped);
		}
		if (io.capHit && !g_capOff) {  // HOOKS T8: more than 8 TABS entries wanted in one world
			g_capOff = true;
			REX::WARN("SPN|S13|off feat=fling.gravity why=entries (more than {} TABS motion-properties entries wanted in world {:X}); the flung bodies keep Fallout gravity from now on",
				FL::kMaxTabsEntriesPerWorld, io.world);
		}
		g_gravWrites += static_cast<std::uint64_t>(r);
		a_factor = io.factor;
		return r;
	}

	int GravityRestore(RE::hknpBSWorld* a_np, const std::uint32_t* a_ids, int a_n, const std::uint32_t* a_savedIds, const std::uint16_t* a_old,
		const std::uint16_t* a_written, int a_savedN)
	{
		if (!a_np || !a_ids || a_n <= 0 || !a_savedIds || !a_old || !a_written || a_savedN <= 0 || !g_setMp || Off(fGravity)) return -1;
		std::uintptr_t bodies = 0;
		const auto     nb = SehBodyCount(a_np, &bodies);
		if (nb <= 0 || !bodies) return -1;
		if (!TryLock(a_np)) return -2;
		const int r = SehGravRestore(reinterpret_cast<std::uintptr_t>(a_np), bodies, nb, a_ids, a_n, a_savedIds, a_old, a_written, a_savedN);
		a_np->m_worldLock.unlock_write();
		if (r == -3) {
			Trip(fGravity, "motion properties restore fault");
			return -3;
		}
		if (r > 0) g_gravRestores += static_cast<std::uint64_t>(r);
		return r;
	}

	void OnLoad()
	{
		g_gc = {};
		g_gcNext = 0;
	}

	bool ReadMotion(RE::hknpBSWorld* a_np, std::uint32_t a_id, Vec3& a_linU, double& a_angRad)
	{
		if (!a_np) return false;
		std::uintptr_t bodies = 0;
		const auto     nb = SehBodyCount(a_np, &bodies);
		float          l[3]{}, a[3]{};
		if (nb <= 0 || !SehReadMotion(reinterpret_cast<std::uintptr_t>(a_np), bodies, nb, a_id, l, a)) return false;
		a_linU = Vec3{ l[0], l[1], l[2] } * FL::kHavokToGame;
		a_angRad = std::sqrt(static_cast<double>(a[0]) * a[0] + static_cast<double>(a[1]) * a[1] + static_cast<double>(a[2]) * a[2]);  // |w| (local = world length)
		return sp::math::IsFinite(a_linU) && std::isfinite(a_angRad);
	}

	void OnP5()
	{
		for (int f = 0; f < kFeatN; ++f) {
			if (Off(f) && !g_offLogged[f]) {
				g_offLogged[f] = true;
				REX::ERROR("SPN|S13|off feat={} why=fault ({}) faults={} (switched off for this session; the old path runs, the game goes on)", kFeatName[f],
					g_offWhy[f] ? g_offWhy[f] : "?", g_faults[f].load());
				sp::runtime::RequestDump(std::format("s13 crash shield {}", kFeatName[f]).c_str());
			}
		}
	}

	std::string Describe()
	{
		return std::format("s13 ok={}{}{} off={}{}{} knocks={} graphs={} setMotion={} ang={} grav={} restored={} entries={} busy={} gravRefused={}", g_ok.now ? 1 : 0,
			g_ok.spin ? 1 : 0, g_ok.gravity ? 1 : 0, Off(fNow) ? 1 : 0, Off(fSpin) ? 1 : 0, Off(fGravity) ? 1 : 0, g_knocks, g_graphs, g_setMotions, g_angWrites, g_gravWrites,
			g_gravRestores, g_entriesAdded, g_lockBusy, g_gravRefused);
	}
}
