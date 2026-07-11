#include "transmit_manager.hpp"

#include "game_config.hpp"
#include "globals.hpp"
#include "sdk/entity/cbaseentity.h"
#include "sdk/helpers.hpp"

#include <entity2/entitysystem.h>

TransmitManager TransmitManager::instance;

polyhook::ResultType TransmitManager::OnSetTransmit(polyhook::HookHandle, polyhook::ParametersHandle params, int, polyhook::ReturnHandle, polyhook::CallbackType) {
	auto& self = Instance();
	if (self.m_playerHiddenEntities.empty()) {
		return polyhook::ResultType::Ignored;
	}

	auto* entity = polyhook::GetArgument<CBaseEntity*>(params, 0);
	auto* info = polyhook::GetArgument<CCheckTransmitInfo*>(params, 1);
	if (!entity || !info) {
		return polyhook::ResultType::Ignored;
	}

	auto it = self.m_playerHiddenEntities.find(info->m_nPlayerSlot.Get());
	if (it == self.m_playerHiddenEntities.end()) {
		return polyhook::ResultType::Ignored;
	}

	if (!it->second.contains(entity->GetRefEHandle().ToInt())) {
		return polyhook::ResultType::Ignored;
	}

	// Keeping a player pawn hidden through death crashes nearby clients, so let
	// dead pawns transmit normally.
	if (entity->m_lifeState != LIFE_ALIVE && entity->IsPlayerPawn()) {
		return polyhook::ResultType::Ignored;
	}

	// Skip the original SetTransmit: the entity never adds itself to this
	// client's transmit list.
	return polyhook::ResultType::Supercede;
}

void TransmitManager::InstallHook(CEntityInstance* entity) {
	if (!entity) {
		return;
	}

	void* vtable = *reinterpret_cast<void* const*>(entity);
	if (m_setTransmitHooks.contains(vtable)) {
		return;
	}

	static int32_t index = GetOrLog(g_pGameConfig->GetOffset("CBaseEntity::SetTransmit"));
	if (index <= 0) {
		return;
	}

	static constexpr polyhook::DataType kArgs[] = {
		polyhook::DataType::Pointer,// this (CBaseEntity*)
		polyhook::DataType::Pointer,// CCheckTransmitInfo*
		polyhook::DataType::Bool,   // bAlways
	};

	// Hook against a stable stand-in object (entry->vtable) rather than the
	// entity, so unhooking stays valid after the entity is freed.
	auto entry = std::make_unique<SetTransmitHook>();
	entry->vtable = vtable;

	auto hook = polyhook::VTableHook::Create(&entry->vtable, index, polyhook::DataType::Void, kArgs, -1, "CBaseEntity::SetTransmit");
	if (!hook) {
		return;
	}

	hook->AddCallback(polyhook::CallbackType::Pre, &TransmitManager::OnSetTransmit);
	entry->hook = std::move(hook);
	m_setTransmitHooks.emplace(vtable, std::move(entry));
}

void TransmitManager::EnsureSetTransmitHook(CEntityInstance* entity) {
	if (m_active) {
		InstallHook(entity);
	}
}

void TransmitManager::UpdateActiveState() {
	const bool shouldBeActive = !m_playerHiddenEntities.empty();
	if (shouldBeActive == m_active) {
		return;
	}

	m_active = shouldBeActive;
	if (!m_active) {
		// Nothing hidden anymore: unpatch every class vtable.
		m_setTransmitHooks.clear();
		return;
	}

	// Just became active: retroactively hook the classes of all live entities.
	if (g_pGameEntitySystem) {
		for (CEntityIdentity* id = g_pGameEntitySystem->m_EntityList.m_pFirstActiveEntity; id != nullptr; id = id->m_pNext) {
			InstallHook(id->m_pInstance);
		}
	}
}

void TransmitManager::RoundStart() {
	m_playerHiddenEntities.clear();
	UpdateActiveState();
}

void TransmitManager::HideEntities(int32_t playerSlot, std::span<const int32_t> entHandles) {
	auto& hidden = m_playerHiddenEntities[playerSlot];
	for (const int32_t handle : entHandles) {
		hidden.insert(handle);
	}
	UpdateActiveState();
}

void TransmitManager::ShowEntities(int32_t playerSlot, std::span<const int32_t> entHandles) {
	auto it = m_playerHiddenEntities.find(playerSlot);
	if (it == m_playerHiddenEntities.end()) {
		return;
	}

	auto& hidden = it->second;
	for (const int32_t handle : entHandles) {
		hidden.erase(handle);
	}

	if (hidden.empty()) {
		m_playerHiddenEntities.erase(it);
	}
	UpdateActiveState();
}

plg::vector<int32_t> TransmitManager::GetHiddenEntities(int32_t playerSlot) {
	auto it = m_playerHiddenEntities.find(playerSlot);
	if (it == m_playerHiddenEntities.end()) {
		return {};
	}

	return plg::vector<int32_t>(it->second.begin(), it->second.end());
}

void TransmitManager::HideEntityFromOtherPlayers(int32_t playerSlot, int32_t entHandle) {
	for (int32_t slot = 0; slot < MaxPlayers; ++slot) {
		if (slot == playerSlot) {
			continue;
		}

		m_playerHiddenEntities[slot].insert(entHandle);
	}
	UpdateActiveState();
}

void TransmitManager::ShowEntityToOtherPlayers(int32_t playerSlot, int32_t entHandle) {
	for (int32_t slot = 0; slot < MaxPlayers; ++slot) {
		if (slot == playerSlot) {
			continue;
		}

		auto it = m_playerHiddenEntities.find(slot);
		if (it == m_playerHiddenEntities.end()) {
			continue;
		}

		it->second.erase(entHandle);
		if (it->second.empty()) {
			m_playerHiddenEntities.erase(it);
		}
	}
	UpdateActiveState();
}
