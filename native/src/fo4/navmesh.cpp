// OWNER: REACH (round 3, new file). See navmesh.h.
#include "fo4/navmesh.h"

#include "config.h"
#include "core/door_checks.h"  // R6 step 9: the navmesh door list layout
#include "fo4/doors.h"         // R6 step 9: the PathingDoor vtable check (doors::Ready / IsPathingDoor)
#include "fo4/threads.h"
#include "runtime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sp::fo4::navmesh
{
	using sp::cfg::Tunable;
	using sp::math::Vec3;

	namespace
	{
		struct RawArr
		{
			void*         data;
			std::uint32_t cap;
			std::uint32_t pad;
			std::uint32_t size;
			std::uint32_t pad2;
		};
		static_assert(sizeof(RawArr) == 0x18);
		struct RawTri
		{
			std::uint16_t v[3];
			std::uint16_t t[3];
			float         h;
			std::uint32_t flags;
			std::int8_t   trav;
			std::uint8_t  pad[3];
		};
		static_assert(sizeof(RawTri) == 0x18 && sizeof(RawTri) == sizeof(RE::BSNavmeshTriangle));
		struct RawEdge
		{
			std::int32_t  type;
			std::uint32_t otherMesh;
			std::uint16_t tri;
			std::int8_t   edge;
			std::uint8_t  pad;
		};
		static_assert(sizeof(RawEdge) == 0xC && sizeof(RawEdge) == sizeof(RE::BSNavmeshEdgeExtraInfo));
		static_assert(sizeof(RE::BSNavmeshVertex) == 12);
		static_assert(offsetof(RE::BSNavmesh, vertices) == 0x10 && offsetof(RE::BSNavmesh, triangles) == 0x28 && offsetof(RE::BSNavmesh, extraEdgeInfo) == 0x40);
		static_assert(offsetof(RE::BSNavmesh, obstacles) == 0x100 && offsetof(RE::BSNavmesh, disabledLinks) == 0x1C8);
		// R6 step 9 (steps\s9\HOOKS.md A1 / C1): the FULL door list is doorPortals (+0x58 / size +0x68); +0x70 is closedDoors (a lagging
		// runtime list of closed doors only, never read)
		static_assert(offsetof(RE::BSNavmesh, doorPortals) == 0x58 && offsetof(RE::BSNavmesh, closedDoors) == 0x70);
		static_assert(sizeof(RE::BSNavmeshTriangleDoorPortal) == 0x10);

		constexpr int kMaxSeed = 64;
		using clock = std::chrono::steady_clock;
		double Ms(clock::time_point a) { return std::chrono::duration<double, std::milli>(clock::now() - a).count(); }
		double NowS() { return std::chrono::duration<double>(clock::now().time_since_epoch()).count(); }

		// ROUND3 fix 1: one rejected mesh (index out of range, non-finite vertex) of a build, for the logs
		struct BadMesh
		{
			std::uint32_t id{ 0 };
			std::uint32_t nt{ 0 };
			int           range{ 0 };
			const char*   why{ "" };
		};

		// ROUND3 fix 4: one snapshot build, run in slices by Tick
		struct Job
		{
			bool                                                   active{ false };
			int                                                    phase{ 0 };  // 0 collect (copy / reuse meshes), 1 build
			std::uint32_t                                          seedA{ 0 }, seedB{ 0 };
			double                                                 bx0{ 0 }, by0{ 0 }, bx1{ 0 }, by1{ 0 };
			std::vector<std::uint32_t>                             queue;  // navmesh form ids (never engine pointers across frames)
			std::size_t                                            qi{ 0 };
			std::vector<std::uint32_t>                             seen, cells;
			std::vector<std::shared_ptr<const sp::nav::MeshData>> meshes;
			std::size_t                                            tris{ 0 };
			int                                                    copied{ 0 }, reused{ 0 }, frames{ 0 }, range{ 0 };
			double                                                 msCopy{ 0 }, msTotal{ 0 }, worst{ 0 }, startedAt{ 0 };
			bool                                                   capped{ false };
			bool                                                   prebuild{ false };  // started by Tick, not by a plan request
			std::vector<BadMesh>                                   rejected;
			sp::nav::Builder                                       builder;
		};

		struct Request
		{
			bool          want{ false };
			std::uint32_t a{ 0 }, b{ 0 };
			Vec3          me{}, t{};
		};

		struct Cached
		{
			std::shared_ptr<const sp::nav::MeshData> m;
			std::uint64_t                            lastUse{ 0 };
		};

		struct State
		{
			sp::nav::Graph g;
			bool           have{ false };
			bool           off{ false };
			const char*    offWhy{ "" };
			std::uint32_t  seedA{ 0 }, seedB{ 0 };
			std::vector<std::uint32_t> cells;  // parent cells of the meshes in the snapshot (a pair of seeds inside it reuses it)
			double         builtAt{ -1e9 }, triedAt{ -1e9 };
			int            misses{ 0 };
			bool           frameOk{ false };  // ROUND3 fix 1: one plan start was found on a snapshot: the coordinate frame is right
			int            emptyFails{ 0 };  // builds in a row that found nothing usable
			std::uint64_t  builds{ 0 }, fails{ 0 }, lines{ 0 }, timeCaps{ 0 };
			std::uint64_t  skipped{ 0 }, badLines{ 0 }, rejectedMeshes{ 0 };  // ROUND3 fix 1
			int            meshes{ 0 }, bad{ 0 }, asym{ 0 }, oneWay{ 0 };
			double         ms{ 0 }, msMax{ 0 }, sliceMax{ 0 };
			// ROUND3 fix 1 / 7: the last build (kept also when it was skipped, for the off / snap / STAT lines)
			const char*            lastWhy{ "" };
			sp::nav::RepairStats   lastRepair{};
			std::vector<BadMesh>   lastRejected;
			int                    lastRange{ 0 }, lastMeshes{ 0 };
			bool                   builtNow{ false };  // the job ran this frame (scan_world defers that frame's plan when long)
			double                 builtNowMs{ 0 };
			// ROUND3 fix 4
			Job                                          job;
			Request                                      req;
			std::unordered_map<std::uint32_t, Cached>    cache;
			std::uint64_t                                serial{ 0 };
			std::uint32_t                                failA{ 0 }, failB{ 0 };  // the seeds of the last failed job (not asked again for a while)
			double                                       failAt{ -1e9 }, prebuildFailAt{ -1e9 };
			std::uint64_t                                copiedTotal{ 0 }, reusedTotal{ 0 };
			int                                          lastFrames{ 0 }, lastCopied{ 0 }, lastReused{ 0 };
			double                                       lastWorst{ 0 };
			std::uint64_t                                d2Capped{ 0 };  // D2: build frames whose slice was capped (low fps / dense)
			std::uint64_t                                doorEntries{ 0 }, doorBad{ 0 };  // R6 step 9: navmesh door entries copied / refused
		};
		State s;
		std::uint64_t g_r6Gen{ 1 };  // R6 step 3: changes whenever the live snapshot is replaced / dropped (Peek)

		std::string Ids(const std::vector<BadMesh>& a_v)
		{
			std::string o;
			for (std::size_t i = 0; i < a_v.size() && i < 6; ++i) o += std::format("{}{:08X}:{}:{}", i ? "," : "", a_v[i].id, a_v[i].why, a_v[i].range);
			if (a_v.size() > 6) o += std::format(",+{}", a_v.size() - 6);
			return o.empty() ? "-" : o;
		}

		// ---- SEH shells: plain pointers / PODs only inside __try ----
		// The cell's navmesh list: every entry must be a real form (GetFormByID(id) == it) of type kNAVM.
		bool SehCellMeshes(RE::TESObjectCELL* c, std::uint32_t* out, int cap, int* n) noexcept
		{
			*n = 0;
			__try {
				const auto* arr = reinterpret_cast<const RawArr*>(c->navMeshes);
				if (!arr) return true;
				if (arr->size > static_cast<std::uint32_t>(cap) || (arr->size && !arr->data)) return false;
				auto** d = static_cast<RE::NavMesh**>(arr->data);
				for (std::uint32_t i = 0; i < arr->size; ++i) {
					RE::NavMesh* m = d[i];
					if (!m) continue;
					const std::uint32_t id = m->GetFormID();
					if (RE::TESForm::GetFormByID(id) != m || m->GetFormType() != RE::ENUM_FORM_ID::kNAVM) return false;
					out[(*n)++] = id;
				}
				return true;
			} __except (1) {
				return false;
			}
		}
		// A form id named by an edge portal: the NavMesh, or nullptr
		bool SehMeshById(std::uint32_t id, RE::NavMesh** out) noexcept
		{
			*out = nullptr;
			__try {
				RE::TESForm* f = RE::TESForm::GetFormByID(id);
				if (f && f->GetFormType() == RE::ENUM_FORM_ID::kNAVM) *out = static_cast<RE::NavMesh*>(f);
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehCell(std::uint32_t id, RE::TESObjectCELL** out) noexcept
		{
			*out = nullptr;
			__try {
				RE::TESForm* f = RE::TESForm::GetFormByID(id);
				if (f && f->GetFormType() == RE::ENUM_FORM_ID::kCELL) {
					auto* c = static_cast<RE::TESObjectCELL*>(f);
					if (c->IsAttached()) *out = c;
				}
				return true;
			} __except (1) {
				return false;
			}
		}
		bool SehCounts(RE::NavMesh* m, std::uint32_t* nv, std::uint32_t* nt, std::uint32_t* ne, std::uint32_t* formId, bool* attached, std::uint32_t* cellId) noexcept
		{
			__try {
				const RE::BSNavmesh* b = m;
				*nv = b->vertices.size();
				*nt = b->triangles.size();
				*ne = b->extraEdgeInfo.size();
				*formId = m->GetFormID();
				const RE::TESObjectCELL* c = static_cast<const RE::TESChildCell*>(m)->GetSaveParentCell();
				*attached = c && c->IsAttached();
				*cellId = c ? c->GetFormID() : 0;
				return *nv < 65535 && *nt < 65535 && *ne < 65535 && (*nv == 0 || b->vertices.data()) && (*nt == 0 || b->triangles.data()) &&
				       (*ne == 0 || b->extraEdgeInfo.data());
			} __except (1) {
				return false;
			}
		}
		inline std::uint64_t Mix(std::uint64_t h, std::uint64_t v)
		{
			h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
			return h * 0xFF51AFD7ED558CCDull;
		}
		// ROUND3 fix 4: the content stamp of one mesh: counts, array pointers, the cut state (obstacles, disabled links) and
		// every triangle's links + flags and every portal (a run-time cut rewrites those in place)
		bool SehStamp(RE::NavMesh* m, std::uint32_t nv, std::uint32_t nt, std::uint32_t ne, std::uint64_t* out, bool a_doors) noexcept
		{
			__try {
				const RE::BSNavmesh* b = m;
				if (b->vertices.size() != nv || b->triangles.size() != nt || b->extraEdgeInfo.size() != ne) return false;
				std::uint64_t h = 0xCBF29CE484222325ull;
				// R6 step 9 (HOOKS C7): the door list is part of the content (only while the navmesh door names are on)
				if (a_doors) {
					const auto* d = reinterpret_cast<const std::uint8_t*>(b->doorPortals.data());
					const std::uint32_t nd = b->doorPortals.size();
					h = Mix(h, 0xD0025ull ^ nd);
					for (std::uint32_t k = 0; k < nd && k < sp::r6check::kNavDoorsMax && d; ++k) {
						const void* obj = *reinterpret_cast<void* const*>(d + k * sp::r6check::kNavDoorEntry);
						const std::uint16_t tri = *reinterpret_cast<const std::uint16_t*>(d + k * sp::r6check::kNavDoorEntry + sp::r6check::kNavDoorTri);
						const std::uint32_t fid = (obj && sp::fo4::doors::IsPathingDoor(obj)) ? *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(obj) + sp::r6check::kPathingDoorFormId) : 0u;
						h = Mix(h, (static_cast<std::uint64_t>(fid) << 16) ^ tri);
					}
				}
				h = Mix(h, nv);
				h = Mix(h, nt);
				h = Mix(h, ne);
				h = Mix(h, reinterpret_cast<std::uintptr_t>(b->vertices.data()));
				h = Mix(h, reinterpret_cast<std::uintptr_t>(b->triangles.data()));
				h = Mix(h, reinterpret_cast<std::uintptr_t>(b->extraEdgeInfo.data()));
				h = Mix(h, b->obstacles.size());
				h = Mix(h, b->disabledLinks.size());
				const auto* t = reinterpret_cast<const RawTri*>(b->triangles.data());
				for (std::uint32_t k = 0; k < nt; ++k) {
					std::uint64_t a = 0;
					std::memcpy(&a, t[k].t, 6);
					h = Mix(h, a ^ (static_cast<std::uint64_t>(t[k].flags) << 48) ^ (static_cast<std::uint64_t>(t[k].v[0]) << 32));
				}
				const auto* e = reinterpret_cast<const RawEdge*>(b->extraEdgeInfo.data());
				for (std::uint32_t k = 0; k < ne; ++k) h = Mix(h, (static_cast<std::uint64_t>(e[k].otherMesh) << 20) ^ e[k].tri ^ (static_cast<std::uint64_t>(e[k].type) << 60));
				*out = h;
				return true;
			} __except (1) {
				return false;
			}
		}
		// R6 step 9 (HOOKS D1): one navmesh door entry copied as plain data (no engine pointer is kept)
		struct RawDoor
		{
			std::uint32_t tri;
			std::uint32_t ref;
		};
		bool SehCopy(RE::NavMesh* m, float* v3, RawTri* t, RawEdge* e, std::uint32_t nv, std::uint32_t nt, std::uint32_t ne, RawDoor* a_doors, int* a_nDoors,
			int* a_badDoors) noexcept
		{
			__try {
				const RE::BSNavmesh* b = m;
				if (b->vertices.size() != nv || b->triangles.size() != nt || b->extraEdgeInfo.size() != ne) return false;  // changed meanwhile
				std::memcpy(v3, b->vertices.data(), static_cast<std::size_t>(nv) * 12);
				std::memcpy(t, b->triangles.data(), static_cast<std::size_t>(nt) * sizeof(RawTri));
				if (ne) std::memcpy(e, b->extraEdgeInfo.data(), static_cast<std::size_t>(ne) * sizeof(RawEdge));
				// R6 step 9: the door list in the SAME copy (HOOKS C7): {PathingDoor* (vtable checked), u16 triangle}, PathingDoor
				// +0x10 = the door ref's FormID; at most kNavDoorsMax entries (more = the list is refused, the mesh is kept)
				if (a_doors && a_nDoors) {
					*a_nDoors = 0;
					const auto*         d = reinterpret_cast<const std::uint8_t*>(b->doorPortals.data());
					const std::uint32_t nd = b->doorPortals.size();
					if (nd > sp::r6check::kNavDoorsMax || (nd && !d)) {
						*a_badDoors += static_cast<int>(nd);
						return true;
					}
					for (std::uint32_t k = 0; k < nd; ++k) {
						const void*         obj = *reinterpret_cast<void* const*>(d + k * sp::r6check::kNavDoorEntry);
						const std::uint16_t tri = *reinterpret_cast<const std::uint16_t*>(d + k * sp::r6check::kNavDoorEntry + sp::r6check::kNavDoorTri);
						if (!obj || !sp::fo4::doors::IsPathingDoor(obj) || tri >= nt) {
							++*a_badDoors;
							continue;
						}
						const std::uint32_t fid = *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const std::uint8_t*>(obj) + sp::r6check::kPathingDoorFormId);
						if (!fid) continue;
						a_doors[*a_nDoors] = { tri, fid };
						++*a_nDoors;
					}
				}
				return true;
			} __except (1) {
				return false;
			}
		}

		void Off(const char* a_why)
		{
			if (s.off) return;
			s.off = true;
			s.offWhy = a_why;
			s.have = false;
			s.g.Clear();
			++g_r6Gen;  // R6 step 3
			s.job.active = false;
			s.job.builder.Cancel();
			const auto& r = s.lastRepair;
			REX::WARN("SPN|NAV|off why={} (the brain keeps its non-nav law for this session; builds={} fails={} skipped={} | last build why={} meshes={} range={} "
			          "asym={} bogus={} relinked={} cut={} oneWay={} rejected={})",
				a_why, s.builds, s.fails, s.skipped, s.lastWhy, s.lastMeshes, s.lastRange, r.asymBefore, r.bogus, r.relinked, r.cut, r.oneWay, Ids(s.lastRejected));
		}

		// A job failed (nothing usable): the previous snapshot stays; nav switches off only when none ever worked.
		void JobFailed(const char* a_why, bool a_bad)
		{
			Job& j = s.job;
			j.active = false;
			if (j.prebuild) {
				// a snapshot round him that found nothing (no navmesh under him: water, a gap): tried again in 5 s; never
				// counted toward switching nav off (only a plan's own builds are)
				s.prebuildFailAt = NowS();
				j.meshes.clear();
				return;
			}
			s.failA = j.seedA;
			s.failB = j.seedB;
			s.failAt = NowS();
			s.lastWhy = a_why;
			s.lastRejected = j.rejected;
			s.lastMeshes = static_cast<int>(j.meshes.size());
			s.lastRange = j.range;
			s.rejectedMeshes += j.rejected.size();
			++s.fails;
			if (a_bad) ++s.skipped;
			if (a_bad && sp::runtime::LogDue(s.badLines, 30.0, 20.0))
				REX::WARN("SPN|NAV|skip why={} meshes={} range={} rejected={} (snapshot {}; builds={} skipped={})", a_why, j.meshes.size(), j.range, Ids(j.rejected),
					s.have ? "kept" : "none", s.builds, s.skipped);
			// never one usable snapshot after brain.navEmptyOff tries: the layout is not what we read
			if (s.builds == 0 && ++s.emptyFails >= static_cast<int>(std::max(3.0, Tunable("brain.navEmptyOff", 10.0)))) Off(a_bad ? "layout" : "noMeshes");
			j.meshes.clear();
		}

		// Start a job for these seed cells (his, the target's). false = no seed meshes (counted as a failed build).
		bool StartJob(RE::TESObjectCELL* a_meCell, RE::TESObjectCELL* a_tCell, const Vec3& a_me, const Vec3& a_t, bool a_prebuild)
		{
			Job& j = s.job;
			j.builder.Cancel();
			j = Job{};
			j.prebuild = a_prebuild;
			const double pad = std::max(500.0, Tunable("brain.navRegionPad", 6000.0));
			j.bx0 = std::min(a_me.x, a_t.x) - pad;
			j.bx1 = std::max(a_me.x, a_t.x) + pad;
			j.by0 = std::min(a_me.y, a_t.y) - pad;
			j.by1 = std::max(a_me.y, a_t.y) + pad;
			j.seedA = a_meCell ? a_meCell->GetFormID() : 0;
			j.seedB = a_tCell ? a_tCell->GetFormID() : j.seedA;
			j.startedAt = NowS();
			s.triedAt = j.startedAt;
			int                cellFails = 0;
			std::uint32_t      seed[kMaxSeed];
			RE::TESObjectCELL* cells[2] = { a_meCell, a_tCell != a_meCell ? a_tCell : nullptr };
			for (RE::TESObjectCELL* c : cells) {
				if (!c) continue;
				int n = 0;
				if (!SehCellMeshes(c, seed, kMaxSeed, &n)) {  // that cell's list only (was Off("cellMeshes"))
					++cellFails;
					continue;
				}
				for (int i = 0; i < n; ++i)
					if (std::find(j.queue.begin(), j.queue.end(), seed[i]) == j.queue.end()) j.queue.push_back(seed[i]);
			}
			if (j.queue.empty()) {
				j.active = true;
				JobFailed(cellFails ? "cellMeshes" : "noSeed", false);
				return false;
			}
			j.active = true;
			j.phase = 0;
			return true;
		}

		// Collect: copy (or reuse) the meshes of the BFS round the seeds until the slice is spent. true = done.
		bool Collect(clock::time_point a_t0, double a_sliceMs)
		{
			Job&       j = s.job;
			const int  maxMeshes = static_cast<int>(std::clamp(Tunable("brain.navMaxMeshes", 32.0), 1.0, 128.0));
			const auto maxTris = static_cast<std::size_t>(std::clamp(Tunable("brain.navMaxTris", 40000.0), 1000.0, 400000.0));
			const bool repair = Tunable("brain.navRepair", 1.0) >= 0.5;
			const double tol = std::clamp(Tunable("brain.navRepairTol", 6.0), 0.5, 64.0), dz = std::clamp(Tunable("brain.navRepairDz", 200.0), 16.0, 1000.0);
			std::vector<float>   v3;
			std::vector<RawTri>  rt;
			std::vector<RawEdge> re;
			std::vector<RawDoor> rd;  // R6 step 9
			while (j.qi < j.queue.size() && static_cast<int>(j.meshes.size()) < maxMeshes) {
				if (!j.meshes.empty() && Ms(a_t0) >= a_sliceMs) return false;  // resume next frame
				const std::uint32_t qid = j.queue[j.qi++];
				if (std::find(j.seen.begin(), j.seen.end(), qid) != j.seen.end()) continue;
				j.seen.push_back(qid);
				RE::NavMesh* m = nullptr;
				if (!SehMeshById(qid, &m) || !m) continue;
				std::uint32_t nv = 0, nt = 0, ne = 0, id = 0, cellId = 0;
				bool          attached = false;
				if (!SehCounts(m, &nv, &nt, &ne, &id, &attached, &cellId)) {  // was Off("counts"): this mesh only
					if (j.rejected.size() < 32) j.rejected.push_back({ qid, nt, 0, "counts" });
					continue;
				}
				if (!attached || nv == 0 || nt == 0) continue;
				if (j.tris + nt > maxTris) {
					j.capped = true;
					break;
				}
				std::uint64_t stamp = 0;
				const bool    doorsOn = sp::fo4::doors::Ready();  // R6 step 9: router.doorFromNavmesh + the door load checks
				if (!SehStamp(m, nv, nt, ne, &stamp, doorsOn)) continue;  // changed while read: skip this one
				std::shared_ptr<const sp::nav::MeshData> md;
				auto                                     it = s.cache.find(id);
				if (it != s.cache.end() && it->second.m && it->second.m->stamp == stamp && (repair || !it->second.m->repaired)) {
					md = it->second.m;
					it->second.lastUse = s.serial;
					++j.reused;
				} else {
					const auto c0 = clock::now();
					v3.assign(static_cast<std::size_t>(nv) * 3, 0.0f);
					rt.assign(nt, RawTri{});
					re.assign(ne, RawEdge{});
					int  nDoors = 0, badDoors = 0;
					if (doorsOn) rd.assign(sp::r6check::kNavDoorsMax, RawDoor{});
					if (!SehCopy(m, v3.data(), rt.data(), re.data(), nv, nt, ne, doorsOn ? rd.data() : nullptr, doorsOn ? &nDoors : nullptr, &badDoors)) continue;
					auto d = std::make_shared<sp::nav::MeshData>();
					for (int k = 0; k < nDoors; ++k) d->doors.push_back({ rd[static_cast<std::size_t>(k)].tri, rd[static_cast<std::size_t>(k)].ref });  // R6 step 9
					s.doorEntries += static_cast<std::uint64_t>(nDoors);
					s.doorBad += static_cast<std::uint64_t>(badDoors);
					d->id = id;
					d->cellId = cellId;
					d->stamp = stamp;
					d->verts.resize(nv);
					for (std::uint32_t k = 0; k < nv; ++k) d->verts[k] = { v3[k * 3], v3[k * 3 + 1], v3[k * 3 + 2] };
					d->tris.resize(nt);
					for (std::uint32_t k = 0; k < nt; ++k) {
						sp::nav::MeshTri& T = d->tris[k];
						for (int e = 0; e < 3; ++e) {
							T.v[e] = rt[k].v[e];
							T.t[e] = rt[k].t[e];
						}
						T.flags = rt[k].flags;
					}
					d->edges.resize(ne);
					for (std::uint32_t k = 0; k < ne; ++k) d->edges[k] = { re[k].otherMesh, re[k].tri, static_cast<std::uint8_t>(std::clamp(re[k].type, 0, 255)) };
					d->Finish();
					// ROUND3 fix 1: index ranges first, on the raw records: a mesh with any broken index or a non-finite vertex
					// is left out of the snapshot (never cached)
					if (!d->finite || d->range) {
						if (!d->finite) j.rejected.push_back({ id, nt, 1, "coords" });
						else {
							j.range += d->range;
							j.rejected.push_back({ id, nt, d->range, "range" });
						}
						j.msCopy += Ms(c0);
						continue;
					}
					if (repair) d->RepairLocal(tol, dz);  // its own links, once per content (the snapshot repairs the links between meshes)
					md = d;
					s.cache[id] = { md, s.serial };
					++j.copied;
					j.msCopy += Ms(c0);
				}
				// region: the vertex box must touch the him-target box (+pad); the seed cells always count
				if (!j.meshes.empty() && (md->x1 < j.bx0 || md->x0 > j.bx1 || md->y1 < j.by0 || md->y0 > j.by1)) continue;
				j.meshes.push_back(md);
				j.tris += nt;
				if (cellId && std::find(j.cells.begin(), j.cells.end(), cellId) == j.cells.end()) j.cells.push_back(cellId);
				// BFS: the meshes this one links to (by form id; resolved when their turn comes)
				for (const auto& e : md->edges) {
					const std::uint32_t o = e.otherMesh;
					if (o == 0 || o == id || j.queue.size() >= 512) continue;
					if (std::find(j.queue.begin() + static_cast<std::ptrdiff_t>(j.qi), j.queue.end(), o) != j.queue.end()) continue;
					if (std::find(j.seen.begin(), j.seen.end(), o) != j.seen.end()) continue;
					j.queue.push_back(o);
				}
			}
			return true;
		}

		void Install()
		{
			Job&                       j = s.job;
			const sp::nav::BuildStats& st = j.builder.Stats();
			s.g = j.builder.Take();
			++g_r6Gen;  // R6 step 3
			s.cells = j.cells;
			s.seedA = j.seedA;
			s.seedB = j.seedB;
			s.have = true;
			s.builtAt = NowS();
			s.emptyFails = 0;
			s.meshes = static_cast<int>(j.meshes.size());
			s.bad = 0;
			s.lastRepair = st.repair;
			s.asym = st.repair.asymBefore;
			s.oneWay = st.repair.oneWay;
			s.lastRejected = j.rejected;
			s.lastMeshes = s.meshes;
			s.lastRange = j.range;
			s.rejectedMeshes += j.rejected.size();
			s.lastWhy = "ok";
			s.lastFrames = j.frames;
			s.lastCopied = j.copied;
			s.lastReused = j.reused;
			s.lastWorst = j.worst;
			s.copiedTotal += static_cast<std::uint64_t>(j.copied);
			s.reusedTotal += static_cast<std::uint64_t>(j.reused);
			if (j.capped) ++s.timeCaps;
			++s.builds;
			s.ms = j.msTotal;
			s.msMax = std::max(s.msMax, s.ms);
			if (sp::runtime::LogDue(s.lines, 50.0, 20.0) || (!j.rejected.empty() && sp::runtime::LogDue(s.badLines, 30.0, 20.0)))
				REX::INFO("SPN|NAV|snap meshes={} tris={} verts={} links={} asym={} bogus={} relinked={} cut={} sym={} oneWay={} dead={} ledges={} comps={} rejected={} "
				          "ms={:.2f} over {} frames (worst {:.2f}; copy {:.2f} copied={} reused={} | assemble {:.2f} index {:.2f} repair {:.2f} comps {:.2f}) "
				          "idx cell={:.0f} items={} cutTris={} giant={} capped={} n={}",
					s.meshes, s.g.tris.size(), s.g.verts.size(), st.links, st.repair.asymBefore, st.repair.bogus, st.repair.relinked, st.repair.cut,
					st.repair.symmetrised, st.repair.oneWay, st.repair.dead, st.ledges, st.comps, Ids(j.rejected), j.msTotal, j.frames, j.worst, j.msCopy, j.copied, j.reused,
					st.msAssemble, st.msIndex, st.msRepair, st.msComp, st.cell, st.items, st.idxDeleted, st.idxHuge, j.capped, s.builds);
			j.active = false;
			j.meshes.clear();
			// the cache keeps the meshes used by the last few builds (and at most 256)
			++s.serial;
			for (auto it = s.cache.begin(); it != s.cache.end();) {
				if (s.serial - it->second.lastUse > 3 || s.cache.size() > 256) it = s.cache.erase(it);
				else ++it;
			}
		}

		bool Covers(std::uint32_t a, std::uint32_t b)
		{
			if (!s.have) return false;
			auto covered = [](std::uint32_t c) { return std::find(s.cells.begin(), s.cells.end(), c) != s.cells.end(); };
			return (a == s.seedA && b == s.seedB) || (a == s.seedB && b == s.seedA) || (covered(a) && covered(b));
		}
	}

	// D2 (user 03:50, downtown Boston: 'NAV ms=11.15 over 4 frames (worst 3.32)', fps 28-43): in an EXTERIOR the snapshot is
	// rebuilt on a cell change (it no longer covers him / the target), not on the 15 s timer: brain.navRebuildEveryExt s
	// (0 = brain.navRebuildEvery, D1H). Interiors keep navRebuildEvery.
	double RebuildEvery(RE::TESObjectCELL* a_cell)
	{
		const double every = std::max(1.0, Tunable("brain.navRebuildEvery", 15.0));
		const double ext = Tunable("brain.navRebuildEveryExt", 0.0);
		if (ext > 0 && a_cell && !a_cell->IsInterior()) return std::max(every, ext);
		return every;
	}

	const sp::nav::Graph* Get(RE::TESObjectCELL* a_meCell, RE::TESObjectCELL* a_tCell, const Vec3& a_me, const Vec3& a_t, double a_now)
	{
		if (s.off || Tunable("brain.nav", 1.0) == 0) return nullptr;
		if (!sp::fo4::threads::InPump()) return nullptr;
		if (!a_meCell) return nullptr;
		const std::uint32_t a = a_meCell->GetFormID(), b = a_tCell ? a_tCell->GetFormID() : a;
		const bool          fresh = NowS() - s.builtAt < RebuildEvery(a_meCell);
		(void)a_now;
		if (!(Covers(a, b) && fresh)) {
			const bool running = s.job.active && ((s.job.seedA == a && s.job.seedB == b) || (s.job.seedA == b && s.job.seedB == a));
			const bool failed = ((s.failA == a && s.failB == b) || (s.failA == b && s.failB == a)) && NowS() - s.failAt < std::max(1.0, Tunable("brain.navRebuildEvery", 15.0));  // (a failure is retried on the plain timer)
			if (!running && !failed) {
				s.req.want = true;
				s.req.a = a;
				s.req.b = b;
				s.req.me = a_me;
				s.req.t = a_t;
			}
		}
		return s.have ? &s.g : nullptr;
	}

	void Tick(RE::TESObjectCELL* a_meCell, const Vec3& a_me)
	{
		s.builtNow = false;
		s.builtNowMs = 0;
		// D2: the frame interval (EMA) for the low-fps slice cap below; measured here, between two pump ticks
		static clock::time_point s_lastTick{};
		static double            s_frameEma = 1.0 / 60.0;
		{
			const auto tn = clock::now();
			if (s_lastTick.time_since_epoch().count() != 0) {
				const double dt = std::chrono::duration<double>(tn - s_lastTick).count();
				if (dt > 0 && dt < 0.5) s_frameEma += (dt - s_frameEma) * 0.1;
			}
			s_lastTick = tn;
		}
		if (s.off || Tunable("brain.nav", 1.0) == 0 || !sp::fo4::threads::InPump() || !a_meCell) return;
		const double now = NowS();
		const double gap = std::max(0.1, Tunable("brain.navRebuildGap", 0.5));
		const auto   t0 = clock::now();
		const double rebuildEvery = RebuildEvery(a_meCell);  // D2
		// a plan's request replaces a snapshot round him that is still being built (its BFS starts at his cell too)
		if (s.job.active && s.job.prebuild && s.req.want) {
			s.job.builder.Cancel();
			s.job.active = false;
		}
		if (!s.job.active) {
			const std::uint32_t meId = a_meCell->GetFormID();
			if (s.req.want && now - s.failAt >= gap) {
				// the brain asked for these cells (a hidden target): resolve them again (forms, attached)
				s.req.want = false;
				RE::TESObjectCELL* ca = nullptr;
				RE::TESObjectCELL* cb = nullptr;
				SehCell(s.req.a, &ca);
				SehCell(s.req.b, &cb);
				if (!ca) ca = a_meCell;
				if (!cb) cb = ca;
				if (!Covers(ca->GetFormID(), cb->GetFormID()) || now - s.builtAt >= rebuildEvery)
					StartJob(ca, cb, s.req.me, s.req.t, false);
			} else if (!s.req.want && Tunable("brain.navPrebuild", 1.0) >= 0.5 && now - s.triedAt >= gap && now - s.prebuildFailAt >= 5.0 &&
			           (!Covers(meId, meId) || now - s.builtAt >= rebuildEvery)) {
				// ROUND3 fix 4: a snapshot round him before a hidden target needs one (the live seeds when they still cover him)
				RE::TESObjectCELL* cb = nullptr;
				if (Covers(meId, meId) && s.seedB != meId) SehCell(s.seedB, &cb);
				StartJob(a_meCell, cb ? cb : a_meCell, a_me, a_me, true);
			}
		}
		Job& j = s.job;
		if (!j.active) return;
		// the slice: brain.navBuildSliceMs (1.5) per frame, brain.navFirstSliceMs (3) while no snapshot exists yet
		double slice = s.have ? std::clamp(Tunable("brain.navBuildSliceMs", 1.5), 0.2, 20.0) : std::clamp(Tunable("brain.navFirstSliceMs", 3.0), 0.2, 50.0);
		// D2 (downtown: worst slice 3.32 ms at 28-43 fps): below brain.navLowFps fps, or with a dense snapshot (more than
		// brain.navDenseTris triangles), every slice - the first one too - is capped at brain.navSliceLowFps ms (spread over
		// more frames). 0 = D1H.
		if (const double cap = Tunable("brain.navSliceLowFps", 0.0); cap > 0) {
			const bool lowFps = s_frameEma > 1.0 / std::max(1.0, Tunable("brain.navLowFps", 50.0));
			const bool dense = s.g.tris.size() > static_cast<size_t>(std::max(0.0, Tunable("brain.navDenseTris", 8000.0)));
			// D2 tuner (d2a-dt60 Boston Common: the FIRST exterior build ran at navFirstSliceMs, slice 2.14 ms, d2cap=0 - no
			// snapshot yet to call dense, 61 fps): brain.navCapExterior 1 caps every exterior build too. 0 = the D2 desktop rule.
			const bool ext = Tunable("brain.navCapExterior", 0.0) >= 0.5 && !a_meCell->IsInterior();
			if (lowFps || dense || ext) {
				slice = std::min(slice, std::max(0.2, cap));
				++s.d2Capped;
			}
		}
		++j.frames;
		if (j.phase == 0) {
			if (Collect(t0, slice)) {
				if (j.meshes.empty()) {
					JobFailed(j.rejected.empty() ? "noMeshes" : "rejected", !j.rejected.empty());
				} else {
					sp::nav::BuildParams bp;
					bp.repair = Tunable("brain.navRepair", 1.0) >= 0.5;
					bp.repairTol = std::clamp(Tunable("brain.navRepairTol", 6.0), 0.5, 64.0);
					bp.repairDz = std::clamp(Tunable("brain.navRepairDz", 200.0), 16.0, 1000.0);
					bp.maxTriSpan = std::clamp(Tunable("brain.navMaxTriSpan", 8192.0), 512.0, 1e6);
					bp.meshRepaired = bp.repair;
					j.builder.Begin(j.meshes, bp);
					j.phase = 1;
				}
			}
		}
		if (j.active && j.phase == 1) {
			const double left = slice - Ms(t0);
			if (left > 0.05 && j.builder.Step(left)) {
				if (j.builder.Stats().layout > 0) {
					j.range += j.builder.Stats().layout;
					j.builder.Cancel();
					JobFailed("range", true);
				} else {
					const double ms = Ms(t0);
					j.msTotal += ms;
					j.worst = std::max(j.worst, ms);
					Install();
					s.builtNow = true;
					s.builtNowMs = ms;
					s.sliceMax = std::max(s.sliceMax, ms);
					return;
				}
			}
		}
		const double ms = Ms(t0);
		j.msTotal += ms;
		j.worst = std::max(j.worst, ms);
		s.builtNow = ms > 0.0;
		s.builtNowMs = ms;
		s.sliceMax = std::max(s.sliceMax, ms);
	}

	bool Building() { return !s.off && (s.job.active || s.req.want); }

	bool Covering(RE::TESObjectCELL* a_meCell, RE::TESObjectCELL* a_tCell)
	{
		if (!a_meCell) return false;
		const std::uint32_t a = a_meCell->GetFormID(), b = a_tCell ? a_tCell->GetFormID() : a;
		return Covers(a, b);
	}

	// R6 step 3: the live snapshot without asking for a build (the shadow Warden's ETA table must not change what the D2f
	// brain's navmesh requests do)
	const sp::nav::Graph* Peek(std::uint64_t* a_serial)
	{
		if (a_serial) *a_serial = g_r6Gen;
		if (s.off || Tunable("brain.nav", 1.0) == 0 || !sp::fo4::threads::InPump()) return nullptr;
		return s.have ? &s.g : nullptr;
	}

	bool Available() { return !s.off && Tunable("brain.nav", 1.0) != 0; }

	void SnapshotCells(std::vector<RE::TESObjectCELL*>& a_out)
	{
		a_out.clear();
		if (s.off || !s.have) return;
		for (const std::uint32_t id : s.cells) {
			RE::TESObjectCELL* c = nullptr;
			if (SehCell(id, &c) && c && a_out.size() < 32) a_out.push_back(c);
		}
	}

	bool JustBuilt(double* a_ms)
	{
		if (a_ms) *a_ms = s.builtNowMs;
		return s.builtNow;
	}

	void NoteLocate(bool a_found)
	{
		if (a_found) {
			s.misses = 0;
			s.frameOk = true;
			return;
		}
		// ROUND3 fix 1: once one plan start was found the coordinate frame is proven; later misses are his position
		// (flying high, over a gap or water), not the layout
		if (s.frameOk) return;
		if (++s.misses >= static_cast<int>(std::max(5.0, Tunable("brain.navLocateMisses", 20.0)))) Off("frame");
	}

	void Reset()
	{
		s.g.Clear();
		++g_r6Gen;  // R6 step 3
		s.cells.clear();
		s.have = false;
		s.seedA = s.seedB = 0;
		s.builtAt = s.triedAt = -1e9;
		s.misses = 0;
		s.job.builder.Cancel();
		s.job = Job{};
		s.req = Request{};
		s.failA = s.failB = 0;
		s.failAt = s.prebuildFailAt = -1e9;
		s.cache.clear();  // the forms of the old session may hold other data now
	}

	std::string Describe()
	{
		return std::format(" | NAV snaps={} fails={} skipped={} rejMeshes={} meshes={} tris={} bad={} asym={} oneWay={} ms={:.2f}/{:.2f} slice={:.2f} frames={} copied={}/{} "
		                   "reused={}/{} cache={} job={} tcap={} d2cap={} off={} doors={}/{}/bad{}",
			s.builds, s.fails, s.skipped, s.rejectedMeshes, s.meshes, s.g.tris.size(), s.bad, s.asym, s.oneWay, s.ms, s.msMax, s.sliceMax, s.lastFrames, s.lastCopied,
			s.copiedTotal, s.lastReused, s.reusedTotal, s.cache.size(), s.job.active ? (s.job.phase == 0 ? "copy" : "build") : "-", s.timeCaps, s.d2Capped, s.off ? s.offWhy : "-",
			s.g.doors.size(), s.doorEntries, s.doorBad);
	}
}
