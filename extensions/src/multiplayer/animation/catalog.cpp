#include "extensions/multiplayer/animation/catalog.h"

#include "legoanimationmanager.h"
#include "legocharactermanager.h"
#include "misc.h"

using namespace Multiplayer::Animation;

// All 5 main actors have bits set
static const MxU8 ALL_MAIN_ACTORS_MASK = 0x1F;

void Catalog::Refresh(LegoAnimationManager* p_am)
{
	m_entries.clear();
	m_animsBase = nullptr;
	m_animCount = 0;

	if (!p_am) {
		return;
	}

	m_animCount = p_am->m_animCount;
	m_animsBase = p_am->m_anims;

	if (!m_animsBase || m_animCount == 0) {
		return;
	}

	for (uint16_t i = 0; i < m_animCount; i++) {
		if (!m_animsBase[i].m_name || m_animsBase[i].m_objectId == 0) {
			continue;
		}

		CatalogEntry entry;
		entry.animIndex = i;

		if (m_animsBase[i].m_characterIndex < 0) {
			entry.category = e_otherAnim;
		}
		else if (m_animsBase[i].m_location == -1) {
			entry.category = e_npcAnim;
		}
		else {
			entry.category = e_camAnim;
		}

		m_entries.push_back(entry);
	}
}

const AnimInfo* Catalog::GetAnimInfo(uint16_t p_animIndex) const
{
	if (!m_animsBase || p_animIndex >= m_animCount) {
		return nullptr;
	}
	return &m_animsBase[p_animIndex];
}

int8_t Catalog::DisplayActorToCharacterIndex(uint8_t p_displayActorIndex)
{
	const char* actorName = CharacterManager()->GetActorName(p_displayActorIndex);
	if (!actorName) {
		return -1;
	}

	// GetCharacterIndex matches first 2 chars of name against g_characters[].m_name
	return AnimationManager()->GetCharacterIndex(actorName);
}

std::vector<const CatalogEntry*> Catalog::FilterEligible(AnimCategory p_category, int8_t p_characterIndex) const
{
	std::vector<const CatalogEntry*> result;

	for (const auto& entry : m_entries) {
		if (entry.category != p_category) {
			continue;
		}

		const AnimInfo& info = m_animsBase[entry.animIndex];

		// Animation must be for the player's character
		if (info.m_characterIndex != p_characterIndex) {
			continue;
		}

		// Must have at least one model
		if (info.m_modelCount == 0) {
			continue;
		}

		// Check eligibility mask. If the mask doesn't cover all 5 main actors,
		// a counterpart is needed. For now we pretend no counterpart is available,
		// so we only include animations where ALL main actors can trigger
		// (mask has all 5 bits set = no specific counterpart needed).
		if ((info.m_unk0x0c & ALL_MAIN_ACTORS_MASK) != ALL_MAIN_ACTORS_MASK) {
			continue;
		}

		result.push_back(&entry);
	}

	return result;
}

std::vector<const CatalogEntry*> Catalog::GetEligibleNpcAnimations(uint8_t p_displayActorIndex) const
{
	int8_t charIndex = DisplayActorToCharacterIndex(p_displayActorIndex);
	if (charIndex < 0) {
		return {};
	}
	return FilterEligible(e_npcAnim, charIndex);
}

std::vector<const CatalogEntry*> Catalog::GetEligibleCamAnimations(uint8_t p_displayActorIndex) const
{
	int8_t charIndex = DisplayActorToCharacterIndex(p_displayActorIndex);
	if (charIndex < 0) {
		return {};
	}
	return FilterEligible(e_camAnim, charIndex);
}

bool Catalog::NeedsCounterpart(uint16_t p_animIndex) const
{
	if (!m_animsBase || p_animIndex >= m_animCount) {
		return false;
	}
	return m_animsBase[p_animIndex].m_modelCount >= 2;
}
