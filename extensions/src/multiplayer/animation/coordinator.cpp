#include "extensions/multiplayer/animation/coordinator.h"

#include "extensions/multiplayer/animation/catalog.h"
#include "legoanimationmanager.h"

using namespace Multiplayer::Animation;

// Defined in legoanimationmanager.cpp
extern LegoAnimationManager::Character g_characters[47];

Coordinator::Coordinator() : m_catalog(nullptr), m_state(CoordinationState::e_idle), m_currentAnimIndex(ANIM_INDEX_NONE)
{
}

void Coordinator::SetCatalog(const Catalog* p_catalog)
{
	m_catalog = p_catalog;
}

void Coordinator::SetInterest(uint16_t p_animIndex)
{
	if (m_state != CoordinationState::e_idle && m_state != CoordinationState::e_interested) {
		return;
	}

	m_currentAnimIndex = p_animIndex;
	m_state = CoordinationState::e_interested;
}

void Coordinator::ClearInterest()
{
	if (m_state == CoordinationState::e_interested) {
		m_state = CoordinationState::e_idle;
		m_currentAnimIndex = ANIM_INDEX_NONE;
	}
}

// Build the unified slots vector from CanTriggerDetailed results.
// Each bit in performerMask becomes one slot; the spectator becomes one slot at the end.
static void BuildSlots(
	const CatalogEntry* p_entry,
	uint64_t p_filledPerformers,
	bool p_spectatorFilled,
	std::vector<SlotInfo>& p_slots)
{
	// One slot per performer bit in performerMask
	for (int8_t i = 0; i < 64; i++) {
		uint64_t bit = uint64_t(1) << i;
		if (!(p_entry->performerMask & bit)) {
			continue;
		}

		SlotInfo slot;
		if (i < (int8_t) sizeOfArray(g_characters)) {
			slot.names.push_back(g_characters[i].m_name);
		}
		slot.filled = (p_filledPerformers & bit) != 0;
		p_slots.push_back(std::move(slot));
	}

	// One spectator slot
	SlotInfo spectatorSlot;
	if (p_entry->spectatorMask == ALL_CORE_ACTORS_MASK) {
		spectatorSlot.names.push_back("any");
	}
	else {
		for (int8_t i = 0; i < CORE_CHARACTER_COUNT; i++) {
			if ((p_entry->spectatorMask >> i) & 1) {
				spectatorSlot.names.push_back(g_characters[i].m_name);
			}
		}
	}
	spectatorSlot.filled = p_spectatorFilled;
	p_slots.push_back(std::move(spectatorSlot));
}

std::vector<EligibilityInfo> Coordinator::ComputeEligibility(
	int16_t p_location, const int8_t* p_charIndices, uint8_t p_count) const
{
	std::vector<EligibilityInfo> result;

	if (!m_catalog || p_count == 0) {
		return result;
	}

	auto anims = m_catalog->GetAnimationsAtLocation(p_location);

	for (const CatalogEntry* entry : anims) {
		// Skip animations where the local player has no role (performer or spectator)
		if (!Catalog::CanParticipateChar(entry, p_charIndices[0])) {
			continue;
		}

		EligibilityInfo info;
		info.animIndex = entry->animIndex;
		info.entry = entry;

		bool atLoc = (entry->location == -1) || (entry->location == p_location);
		info.atLocation = atLoc;

		uint64_t filledPerformers = 0;
		bool spectatorFilled = false;

		if (atLoc) {
			info.eligible =
				m_catalog->CanTriggerDetailed(entry, p_charIndices, p_count, &filledPerformers, &spectatorFilled);
		}
		else {
			info.eligible = false;
		}

		BuildSlots(entry, filledPerformers, spectatorFilled, info.slots);

		result.push_back(std::move(info));
	}

	return result;
}

void Coordinator::OnLocationChanged(int16_t p_location, const Catalog* p_catalog)
{
	if (m_state != CoordinationState::e_interested || !p_catalog) {
		return;
	}

	auto anims = p_catalog->GetAnimationsAtLocation(p_location);
	for (const auto* e : anims) {
		if (e->animIndex == m_currentAnimIndex) {
			return; // still available
		}
	}

	// Animation not at new location — clear interest
	m_state = CoordinationState::e_idle;
	m_currentAnimIndex = ANIM_INDEX_NONE;
}

void Coordinator::Tick(uint32_t p_now)
{
	// State machine transitions will be implemented in Part 4 (networking)
	(void) p_now;
}

void Coordinator::Reset()
{
	m_state = CoordinationState::e_idle;
	m_currentAnimIndex = ANIM_INDEX_NONE;
}

void Coordinator::OnRemoteInterest(uint32_t p_peerId, uint16_t p_animIndex)
{
	// Stub — wired up in Part 4
	(void) p_peerId;
	(void) p_animIndex;
}

void Coordinator::OnRemoteInterestClear(uint32_t p_peerId)
{
	// Stub — wired up in Part 4
	(void) p_peerId;
}

void Coordinator::OnAnimationStart(uint16_t p_animIndex, uint32_t p_startTimeDelta)
{
	// Stub — wired up in Part 4
	(void) p_animIndex;
	(void) p_startTimeDelta;
}

void Coordinator::OnAnimationCancel(uint16_t p_animIndex)
{
	// Stub — wired up in Part 4
	(void) p_animIndex;
}
