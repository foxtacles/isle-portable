#include "extensions/multiplayer/animation/coordinator.h"

#include "extensions/multiplayer/animation/catalog.h"

using namespace Multiplayer::Animation;

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

		bool atLoc = (entry->location == -1) || (entry->location == p_location);
		bool allRolesFilled = atLoc && m_catalog->CanTrigger(entry, p_charIndices, p_count);

		info.atLocation = atLoc;
		info.eligible = allRolesFilled;
		info.needsOtherPlayers = atLoc && !allRolesFilled;

		result.push_back(info);
	}

	return result;
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
