#pragma once
#include <iservernetworkable.h>
#include <polyhook/polyhook.hpp>

#include <memory>
#include <span>

class CEntityInstance;

// Hides entities from specific players by hooking the per-entity virtual
// CBaseEntity::SetTransmit(CCheckTransmitInfo*, bool). When a hidden entity is
// asked to add itself to a recipient's transmit list, the hook supercedes the
// original call so the entity is never networked to that client.
//
// The vtable hook is installed per entity class (keyed by vtable pointer) and
// only while at least one entity is hidden, so servers that never hide anything
// pay no cost.
class TransmitManager {
	TransmitManager() = default;
	~TransmitManager() = default;
	NONCOPYABLE(TransmitManager)

	static TransmitManager instance;
public:
	static auto& Instance() noexcept {
		return instance;
	}

	// Installs the SetTransmit hook for this entity's class if hiding is active.
	// Called for every created entity.
	void EnsureSetTransmitHook(CEntityInstance* entity);

	void HideEntities(int32_t playerSlot, std::span<const int32_t> entHandles);
	void ShowEntities(int32_t playerSlot, std::span<const int32_t> entHandles);

	void HideEntityFromOtherPlayers(int32_t playerSlot, int32_t entHandle);
	void ShowEntityToOtherPlayers(int32_t playerSlot, int32_t entHandle);

	void RoundStart();
	plg::vector<int32_t> GetHiddenEntities(int32_t playerSlot);

	// Pre-callback for the hooked CBaseEntity::SetTransmit.
	static polyhook::ResultType OnSetTransmit(polyhook::HookHandle hook, polyhook::ParametersHandle params, int count, polyhook::ReturnHandle ret, polyhook::CallbackType type);

private:
	// Recomputes whether hooking should be active based on the hidden set and
	// installs/removes hooks accordingly.
	void UpdateActiveState();
	void InstallHook(CEntityInstance* entity);

	// Owns a vtable hook plus a stable stand-in object whose first pointer is the
	// hooked class vtable. polyhook re-reads that pointer when unhooking, so it
	// must outlive any real entity instance (which may be freed by then).
	struct SetTransmitHook {
		void* vtable = nullptr;
		std::unique_ptr<polyhook::IHook> hook;
	};

	plg::flat_hash_map<int32_t, plg::flat_hash_set<int32_t>> m_playerHiddenEntities;
	plg::flat_hash_map<void*, std::unique_ptr<SetTransmitHook>> m_setTransmitHooks;
	bool m_active = false;
	//std::mutex m_mutex;
};
inline TransmitManager& g_TransmitManager = TransmitManager::Instance();
