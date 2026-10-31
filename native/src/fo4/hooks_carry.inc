// OWNER: SKELETON (CARRY patch: "Carry me", CARRY_SPEC 8.7).  Included by hooks.cpp INSIDE its anonymous namespace
// (after Touch / VtblOffset / Install), so it shares g_pt and the stats.
//
//   P7 PlayerCharacter::Update            VTABLE::PlayerCharacter[0] vfunc 0xCF, after the original: the player's own
//      controller (carry velocity / kFlying / fall guard), threads::InPlayerUpdate(). RTTI: COL offset 0 and the type
//      name ".?AVPlayerCharacter@@".
//   P8 IMovementPlayerControlsFilter::FilterControllerOutput  PlayerCharacter[k: COL offset 0x4C0] vfunc 0x01: plan A
//      (carry.inputMode 1) zeroes the engine's own walk while carried.
//   P9 PostUpdateAnimationGraphManager    PlayerCharacter[k: COL offset 0x48] vfunc 0x17, this - 0x48 = the player:
//      the arms "holding on" (fo4/player_arms).
// Never through RE::VTABLE::PlayerControls[0] (its id duplicates PlayerInputHandler, CARRY_ENGINE K1).
// Each is constructed only if its [Tabs] carry.hooks bit is set (1 P7, 2 P8, 4 P9; separate from [Hooks] InstallMask
// so a shipped ini with 126 keeps working) and its table check passes.

// MSVC x64 RTTI: the CompleteObjectLocator at vtbl[-1]: +0x0C TypeDescriptor RVA, +0x14 the locator's own RVA (so the
// module base = col - selfRVA); the TypeDescriptor's decorated name starts at +0x10.
bool VtblTypeNameImpl(std::uintptr_t a_vtbl, char* a_out, std::size_t a_cap)
{
	const auto col = *reinterpret_cast<const std::uintptr_t*>(a_vtbl - sizeof(void*));
	if (!col) return false;
	const auto tdRva = *reinterpret_cast<const std::uint32_t*>(col + 0x0C);
	const auto selfRva = *reinterpret_cast<const std::uint32_t*>(col + 0x14);
	const auto base = col - selfRva;
	const char* name = reinterpret_cast<const char*>(base + tdRva + 0x10);
	std::size_t i = 0;
	for (; i + 1 < a_cap && name[i]; ++i) a_out[i] = name[i];
	a_out[i] = 0;
	return i > 0;
}
bool SehVtblTypeName(std::uintptr_t a_vtbl, char* a_out, std::size_t a_cap) noexcept
{
	__try {
		return VtblTypeNameImpl(a_vtbl, a_out, a_cap);
	} __except (1) {
		return false;
	}
}
std::string VtblTypeName(REL::ID a_id)
{
	char buf[96]{};
	const auto vtbl = a_id.address();
	if (!vtbl || !SehVtblTypeName(vtbl, buf, sizeof(buf))) return "?";
	return buf;
}
// PlayerCharacter's table whose sub-object sits at a_off (COL offset), or -1.
int FindPlayerTable(std::uint32_t a_off)
{
	for (std::size_t k = 0; k < RE::VTABLE::PlayerCharacter.size(); ++k)
		if (VtblOffset(RE::VTABLE::PlayerCharacter[k]) == a_off) return static_cast<int>(k);
	return -1;
}

// ---------------- P7 ----------------
using P7Fn = void(RE::PlayerCharacter*, float);
std::unique_ptr<REL::THookVFT<P7Fn>> g_p7;
void P7Hook(RE::PlayerCharacter* a, float dt)
{
	(*g_p7)(a, dt);
	SP_COST_SCOPE(P7);
	SP_COST_NOTE(2);
	Touch(kP7, true);
	threads::OnPlayerUpdateBegin();
	sp::runtime::OnPlayerUpdate(a, dt);
	threads::OnPlayerUpdateEnd();
}

// ---------------- P8 (plan A) ----------------
using P8Fn = void(const RE::IMovementPlayerControlsFilter*, const RE::MovementData&, RE::MovementData&, float);
std::unique_ptr<REL::THookVFT<P8Fn>> g_p8;
void P8Hook(const RE::IMovementPlayerControlsFilter* a_this, const RE::MovementData& a_prev, RE::MovementData& a_io, float a_dt)
{
	(*g_p8)(a_this, a_prev, a_io, a_dt);
	SP_COST_SCOPE(P8);
	Touch(kP8, false);
	sp::runtime::FilterPlayerMovement(a_io);
}

// ---------------- P9 (the player's arms) ----------------
std::unique_ptr<REL::THookVFT<P1Fn>> g_p9;
void P9Hook(RE::IAnimationGraphManagerHolder* a_this, const RE::BSTSmartPointer<RE::BSAnimationGraphManager>& a_mgr)
{
	(*g_p9)(a_this, a_mgr);
	SP_COST_SCOPE(P9);
	auto* a = reinterpret_cast<RE::Actor*>(reinterpret_cast<std::uintptr_t>(a_this) - 0x48);
	Touch(kP9, false);
	if (sp::fo4::player_arms::Write(a)) g_pt[kP9].writes.fetch_add(1, std::memory_order_relaxed);
}

template <class Fn>
void InstallCarryPoint(std::unique_ptr<REL::THookVFT<Fn>>& a_hook, int a_point, const char* a_name, REL::ID a_vtbl, std::size_t a_idx, Fn* a_fn)
{
	a_hook = std::make_unique<REL::THookVFT<Fn>>(a_name, a_vtbl, a_idx, a_fn);
	g_pt[a_point].installed = a_hook->Enable();
	REX::INFO("SPN|I|hook P{} {} vtbl id {} idx 0x{:X}: {}", a_point, a_name, a_vtbl.id(), a_idx, g_pt[a_point].installed ? "enabled" : "FAILED");
}

void InstallCarryHooks()
{
	const int bits = static_cast<int>(sp::cfg::Tunable("carry.hooks", 7.0));
	if (sp::cfg::Tunable("carry.enabled", 1.0) == 0.0 || bits == 0) {
		REX::INFO("SPN|I|carry hooks P7-P9 not installed (carry.enabled 0 / carry.hooks 0)");
		return;
	}
	const auto        off0 = VtblOffset(RE::VTABLE::PlayerCharacter[0]);
	const std::string n0 = VtblTypeName(RE::VTABLE::PlayerCharacter[0]);
	const int         k48 = FindPlayerTable(0x48);
	const int         k4c0 = FindPlayerTable(0x4C0);
	const bool        ok7 = off0 == 0 && n0.find("PlayerCharacter") != std::string::npos;
	g_pt[kP7].rttiOk = ok7;
	g_pt[kP8].rttiOk = k4c0 >= 0;
	g_pt[kP9].rttiOk = k48 >= 0;
	REX::INFO("SPN|PCARRY|C-S1 PlayerCharacter[0] COL offset 0x{:X} type {} -> P7 {}; table with offset 0x48 (IAnimationGraphManagerHolder) = [{}] {}; 0x4C0 "
	          "(IMovementPlayerControlsFilter) = [{}] {}",
		off0, n0, ok7 ? "ok" : "MISMATCH (off)", k48, k48 >= 0 ? VtblTypeName(RE::VTABLE::PlayerCharacter[k48]) : "-", k4c0,
		k4c0 >= 0 ? VtblTypeName(RE::VTABLE::PlayerCharacter[k4c0]) : "-");
	if ((bits & 1) && ok7) InstallCarryPoint<P7Fn>(g_p7, kP7, "P7 PlayerCharacter::Update", RE::VTABLE::PlayerCharacter[0], 0xCF, &P7Hook);
	if ((bits & 2) && k4c0 >= 0) InstallCarryPoint<P8Fn>(g_p8, kP8, "P8 FilterControllerOutput", RE::VTABLE::PlayerCharacter[k4c0], 0x01, &P8Hook);
	if ((bits & 4) && k48 >= 0) InstallCarryPoint<P1Fn>(g_p9, kP9, "P9 player PostUpdateAnimationGraphManager", RE::VTABLE::PlayerCharacter[k48], 0x17, &P9Hook);
}
