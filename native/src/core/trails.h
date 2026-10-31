#pragma once
// OWNER: STEP 7.5 (TABS motion trails).  Pure C++ (no game headers, desktop-testable).
//
// What TABS draws: the Super Peasant's bracers each carry a Unity TrailRenderer (sharedassets0.assets #57285, both arms:
// the right bracer is a mirrored copy), emitting from bracer-local x -0.244 m = the forearm 83 % of the way from the
// elbow to the wrist (native\spec_model.md 2.3 + 5, native\model_src\superpeasant_model.json effects[0]; the TESTS lane's
// full typetree: round6\testsuite\scenarios\s7.5\tabs_trail_truth.json; emitting always, alignment View):
//   time 1.0 s, minVertexDistance 0.1 m, widthMultiplier 0.4 m x a 16-key width curve (thin at the head, widest at 6 %,
//   then thinner and thinner, 0 at the tail), a colour gradient over the trail's length: gold (1, 0.77, 0) at the head ->
//   pale yellow -> WHITE from 55 % of the length on, material "MultiplyBoxLight" = Particles/Standard Unlit, OPAQUE
//   (_Mode 0, ZWrite on: the alpha keys do nothing), view-aligned (the strip always faces the camera).
// So the "long white wind trails behind his body" and the "glowing yellow fist trails" of the TABS frames are the SAME
// two ribbons: yellow near his fists, the older half white and thin. There is no other trail on the unit.
//
// This file is the TrailRenderer itself: a history of world points per emitter (a new point once the emitter moved
// minVertexDist from the last one, every point dies `time` s after it was laid, the head always sits on the emitter),
// and the ribbon geometry built from it each frame: `sections` cross-sections at fixed fractions u of the trail's
// LENGTH (head u = 0 .. tail u = 1), each section 2 vertices (left, right) across the view direction, width = the TABS
// width curve at u. Because every section sits at a fixed u, its colour never changes: the colours / UVs / triangles
// are baked ONCE (Bake*), only the 2 x sections positions are rewritten per frame (the engine's dynamic tri-shape).
// Sampling the history on a centripetal Catmull-Rom curve keeps the sliding sections on one smooth path (no shimmer
// at loops). Units: FO4 game units, seconds; the glue maps TABS metres with [Body] Upm (64.18 u per TABS metre).
#include "core/math.h"

#include <array>
#include <cstdint>

namespace sp::trails
{
	using sp::math::Real;
	using sp::math::Vec3;

	inline constexpr int kMaxPoints = 1024;   // history points per trail (ring; the oldest is dropped when full)
	inline constexpr int kMinSections = 4;
	inline constexpr int kMaxSections = 128;  // per trail (2 x 128 vertices)
	inline constexpr int kTrailCount = 2;     // 0 = left fist, 1 = right fist (TABS: one per bracer)
	inline constexpr int kMaxSegments = 8;    // shapes per ribbon (SegmentPlan)

	// ---- TABS truth (TrailRenderer #57285) ----------------------------------------------------------------------------
	inline constexpr Real kTabsTime = 1.0;              // s
	inline constexpr Real kTabsMinVertexDistM = 0.1;    // TABS metres
	inline constexpr Real kTabsWidthM = 0.4;            // TABS metres (widthMultiplier)
	// bracer-local x -0.244 m on M_Elbow: 0.244 / 0.293 = 83 % of the way from the elbow joint (x 0.483) to the wrist
	// (x 0.776), 3 u before the wrist at Upm 64.18. FO4: ForeArm1 (elbow) -> Hand (the wrist joint).
	inline constexpr Real kTabsEmitAlong = 0.83;
	// widthCurve keys {time, value, inTangent, outTangent} (Unity AnimationCurve, Hermite)
	inline constexpr std::array<std::array<double, 4>, 16> kWidthCurve{ { { 0.0, 0.17293930053710938, 0.0, 0.0 },
		{ 0.0629405751824379, 0.5028647780418396, -0.03957358002662659, -0.03957358002662659 },
		{ 0.11368079483509064, 0.30909085273742676, -0.10304480791091919, -0.10304480791091919 },
		{ 0.17685189843177795, 0.367794394493103, -0.1324871927499771, -0.1324871927499771 },
		{ 0.21230269968509674, 0.1994371861219406, -0.1593816727399826, -0.1593816727399826 },
		{ 0.31416207551956177, 0.1318366527557373, -0.20473822951316833, -0.20473822951316833 },
		{ 0.3908899426460266, 0.1837230920791626, -0.22519713640213013, -0.22519713640213013 },
		{ 0.47855260968208313, 0.09112924337387085, -0.23647324740886688, -0.23647324740886688 },
		{ 0.5581640005111694, 0.11787638813257217, -0.23363621532917023, -0.23363621532917023 },
		{ 0.6241413950920105, 0.1639084815979004, -0.22676774859428406, -0.22676774859428406 },
		{ 0.6497585773468018, 0.06822672486305237, -0.21705736219882965, -0.21705736219882965 },
		{ 0.7369410395622253, 0.09699883311986923, -0.18900717794895172, -0.18900717794895172 },
		{ 0.7900692224502563, 0.07435048371553421, -0.15712976455688477, -0.15712976455688477 },
		{ 0.8506991863250732, 0.10477082431316376, -0.12719622254371643, -0.12719622254371643 },
		{ 0.9086629152297974, 0.03151160851120949, -0.091899074614048, -0.091899074614048 },
		{ 1.0, 0.0, 0.0, 0.0 } } };
	struct GradKey
	{
		double t, r, g, b;
	};
	// colorGradient (Blend mode), times = ctime / 65535; colours as authored (gamma) = what the TABS screen shows (unlit)
	inline constexpr std::array<GradKey, 4> kColorKeys{ { { 4433.0 / 65535.0, 1.0, 0.7678804993629456, 0.0 },
		{ 14071.0 / 65535.0, 1.0, 0.9713963270187378, 0.37254905700683594 },
		{ 24287.0 / 65535.0, 0.9978355169296265, 1.0, 0.8078431487083435 },
		{ 35852.0 / 65535.0, 1.0, 1.0, 1.0 } } };
	// alpha keys {time, alpha}; TABS renders the trail OPAQUE, so they only matter with Params::alphaKeys (non-TABS option)
	inline constexpr std::array<std::array<double, 2>, 3> kAlphaKeys{ { { 0.0, 1.0 }, { 36044.0 / 65535.0, 1.0 }, { 1.0, 0.5098039507865906 } } };

	struct Rgba
	{
		float r{ 1 }, g{ 1 }, b{ 1 }, a{ 1 };
	};
	Real EvalWidth(Real a_u);                     // the TABS width curve at u (0 head .. 1 tail), without the multiplier
	Rgba EvalColor(Real a_u, bool a_alphaKeys);   // the TABS gradient at u (Unity Gradient, Blend mode, clamped ends)
	inline Real SectionU(int a_k, int a_n) { return a_n > 1 ? static_cast<Real>(a_k) / static_cast<Real>(a_n - 1) : 0.0; }

	struct Params
	{
		Real time{ kTabsTime };                    // s a point lives
		Real minVertexDist{ 6.418 };               // u: 0.1 TABS m x Upm 64.18 (a new point once the emitter moved this far)
		Real width{ 25.672 };                      // u: 0.4 TABS m x Upm (the curve's 1.0); full width (vertices at +-w/2)
		int  sections{ 64 };                       // cross-sections per ribbon (clamped kMinSections..kMaxSections)
		// a jump of the emitter above max(resetJump, resetSpeed x dt) in one update (blink, put-back, door, load: nothing he can
		// fly; his mover caps at 14 000 u/s = 233 u per 1/60 s) restarts the trail there instead of drawing a streak
		Real resetJump{ 300 };                     // u
		Real resetSpeed{ 15000 };                  // u/s (0 = only resetJump)
		Real minDrawLength{ 1.0 };                 // u: a shorter trail draws nothing
		bool smooth{ true };                       // centripetal Catmull-Rom through the history (false: straight segments, Unity-exact)
	};

	// One built cross-section (for the tests / diagnostics)
	struct Section
	{
		Vec3 c{};     // centre (on the trail)
		Vec3 side{};  // unit vector across the ribbon (left vertex = c + side * w / 2)
		Real u{ 0 };  // fraction of the trail length (0 head)
		Real w{ 0 };  // full width (u)
	};

	struct Bound
	{
		Vec3 center{};
		Real radius{ 0 };
	};

	class Trail
	{
	public:
		void Reset();
		// One update: the emitter's world position at game time a_t (s, non-decreasing). a_emit = false: no new points and
		// the head stays on the last point (the trail detaches and ages out; TrailRenderer.emitting = false).
		// Returns false (nothing changed) on a non-finite position.
		bool Update(const Vec3& a_pos, Real a_t, bool a_emit, const Params& a_p);
		// Builds the ribbon: a_n sections (clamped), 2 vertices each into a_verts[2k] (left) / a_verts[2k + 1] (right),
		// head first. Returns the sections built, 0 when the trail is too short to see (a_verts untouched). a_sec (optional,
		// a_n entries) gets the sections. a_eye = the camera's world position (view-aligned ribbon).
		int Build(const Vec3& a_eye, const Params& a_p, int a_n, Vec3* a_verts, Section* a_sec = nullptr, Bound* a_bound = nullptr) const;

		int  Points() const { return _count; }     // committed history points (the head is extra)
		bool HeadLive() const { return _headLive; }
		const Vec3& Head() const { return _head; }
		Real Length() const;                       // head -> tail arc length (straight segments), u
		Real OldestAge(Real a_now) const;          // s, 0 when empty
		std::uint64_t Resets() const { return _resets; }
		std::uint64_t Commits() const { return _commits; }

	private:
		struct Pt
		{
			Vec3 p{};
			Real t{ 0 };
		};
		const Pt& At(int a_i) const { return _pts[(_first + a_i) % kMaxPoints]; }  // 0 = oldest
		void      Push(const Vec3& a_p, Real a_t);
		void      Expire(Real a_now, Real a_time);
		int       Polyline(Vec3* a_out, int a_max) const;  // head first, then newest -> oldest; returns the count

		std::array<Pt, kMaxPoints> _pts{};
		int           _first{ 0 }, _count{ 0 };
		Vec3          _head{};
		Real          _headT{ 0 };
		bool          _headLive{ false };
		bool          _haveHead{ false };
		std::uint64_t _resets{ 0 }, _commits{ 0 };
	};

	// ---- the engine's dynamic vertex format (HOOKS.md D): 4 x IEEE half per vertex {x, y, z, 0}, relative to an anchor --
	std::uint16_t ToHalf(float a_f);                  // round to nearest even; +-65504 clamp; NaN -> 0
	float         FromHalf(std::uint16_t a_h);
	// Packs a_nv world vertices as halves relative to a_anchor into a_out (4 x a_nv); a_bound (optional) gets the bound
	// in ANCHOR space (centre = AABB centre - anchor, radius = half diagonal + a_pad). Returns the largest |coordinate|.
	Real PackHalves(const Vec3* a_verts, int a_nv, const Vec3& a_anchor, std::uint16_t* a_out, Bound* a_bound = nullptr, Real a_pad = 0);
	// Load-check helper: a_bytes against "48 8B C4 xx 89 ..." (hex bytes, xx = any). False on a bad pattern.
	bool MatchPattern(const std::uint8_t* a_bytes, int a_len, const char* a_pattern);

	// ---- static ribbon data, baked once per shape (n sections = 2n vertices, head first, left then right) -------------
	int  IndexCount(int a_n, bool a_twoSided);                            // 6 (n-1), x2 two-sided
	void BakeIndices(int a_n, bool a_twoSided, std::uint16_t* a_out);     // IndexCount entries
	void BakeColors(int a_n, bool a_alphaKeys, Rgba* a_out);              // 2n entries (the TABS gradient at each section's u)
	void BakeUVs(int a_n, float* a_out);                                  // 4n floats: (u, 0) left, (u, 1) right
	// the same for sections a_first .. a_first + a_count - 1 of an a_total-section ribbon (one segment shape of it)
	void BakeColorsRange(int a_first, int a_count, int a_total, bool a_alphaKeys, Rgba* a_out);  // 2 a_count entries
	void BakeUVsRange(int a_first, int a_count, int a_total, float* a_out);                       // 4 a_count floats
	// A ribbon drawn as o_segments shapes (each with its own half-float anchor: the error of a half grows with the distance
	// to the anchor, HOOKS.md D / TESTS TR-17). o_segments = a_segments clamped to 1..kMaxSegments and to at least
	// kMinSections sections per segment; every segment has o_perSeg sections, neighbours share their end section, the
	// ribbon has o_total = o_segments (o_perSeg - 1) + 1 sections (<= kMaxSections; about a_sections). Segment j = sections
	// j (perSeg - 1) .. j (perSeg - 1) + perSeg - 1.
	void SegmentPlan(int a_sections, int a_segments, int& o_segments, int& o_perSeg, int& o_total);

	// ---- R6 step 7.5 FIX lane 1 (review: trails_glue emit had no Carry gate): when the glue lays new trail points ---------
	// His TABS body drawn (mode >= 2, or trails.anyMode), alive, not settled on the ground outside a fight
	// (trails.groundStop), and not while Carry me runs (trails.carryOff 1, the code default): during GRAB / CARRY / LAND /
	// GUARD his hands grip you, so one second of his path (15-30 m) would trail straight back through the third-person
	// camera, and Carry me keeps its look (user rule 3: no carry work). Off = the trail detaches and fades within
	// trails.time like a stopped TABS trail; points resume the frame the carry ends.
	struct EmitIn
	{
		int  mode{ 0 };          // sp::cfg mode (2 = body sim, 3 = native movement)
		bool anyMode{ false };   // trails.anyMode (TEST)
		bool alive{ false };     // Actor::lifeState == 0
		bool onGround{ false };  // trails.groundStop: on the ground that long with no target
		int  carryState{ 0 };    // runtime Snapshot::carryState (0 OFF, 1 GRAB, 2 CARRY, 3 LAND, 4 GUARD)
		bool carryOff{ true };   // trails.carryOff
	};
	inline bool EmitOn(const EmitIn& a_e)
	{
		return (a_e.mode >= 2 || a_e.anyMode) && a_e.alive && !a_e.onGround && !(a_e.carryOff && a_e.carryState != 0);
	}
}
