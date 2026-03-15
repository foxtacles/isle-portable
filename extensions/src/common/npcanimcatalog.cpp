#include "extensions/common/npcanimcatalog.h"

#include "legoanimationmanager.h"

#include <SDL3/SDL_log.h>

using namespace Extensions::Common;

void NpcAnimCatalog::Refresh(LegoAnimationManager* p_am)
{
	m_entries.clear();

	if (!p_am) {
		return;
	}

	MxU16 animCount = p_am->m_animCount;
	AnimInfo* anims = p_am->m_anims;

	if (!anims || animCount == 0) {
		return;
	}

	for (MxU16 i = 0; i < animCount; i++) {
		if (!anims[i].m_name || anims[i].m_objectId == 0) {
			continue;
		}

		// Only include animations that have a character association
		if (anims[i].m_characterIndex < 0) {
			continue;
		}

		// Only include animations with at least one model
		if (anims[i].m_modelCount == 0) {
			continue;
		}

		NpcAnimEntry entry;
		entry.animIndex = i;
		entry.objectId = anims[i].m_objectId;
		entry.name = anims[i].m_name;
		entry.characterIndex = anims[i].m_characterIndex;
		entry.eligibilityMask = anims[i].m_unk0x0c;
		entry.modelCount = anims[i].m_modelCount;
		m_entries.push_back(entry);
	}

	SDL_Log("NpcAnimCatalog: Refreshed with %zu entries from %u anims", m_entries.size(), animCount);
}

std::vector<const NpcAnimEntry*> NpcAnimCatalog::GetEligibleAnimations(uint8_t p_actorId) const
{
	std::vector<const NpcAnimEntry*> result;

	// Actor IDs are 1-based (pepper=1, mama=2, papa=3, nick=4, laura=5).
	// The eligibility mask (m_unk0x0c) uses bit positions where bit N
	// corresponds to some character category. The FUN_10062110 pattern
	// filters by (m_unk0x0c & p_unk0x0c), so we use the actorId
	// as the mask value for filtering.
	for (const auto& entry : m_entries) {
		if (entry.eligibilityMask & p_actorId) {
			result.push_back(&entry);
		}
	}

	return result;
}

bool NpcAnimCatalog::NeedsCounterpart(uint16_t p_index) const
{
	for (const auto& entry : m_entries) {
		if (entry.animIndex == p_index) {
			return entry.modelCount >= 2;
		}
	}
	return false;
}
