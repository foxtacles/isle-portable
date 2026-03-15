#include "extensions/common/npcanimcatalog.h"

#include "legoanimationmanager.h"
#include "legocharactermanager.h"
#include "misc.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

using namespace Extensions::Common;

// Mirrors g_unk0x100d8b28 from legoanimationmanager.cpp.
// Maps main actor IDs (0-5) to eligibility bitmask values.
// actorId 0 = none, 1 = pepper (bit0), 2 = mama (bit1), 3 = papa (bit2),
// 4 = nick (bit3), 5 = laura (bit4).
static const MxU8 g_actorIdToBitmask[] = {0, 1, 2, 4, 8, 16};

// All 5 main actors have bits set
static const MxU8 ALL_MAIN_ACTORS_MASK = 0x1F;

void NpcAnimCatalog::Refresh(LegoAnimationManager* p_am)
{
	m_npcAnims.clear();
	m_camAnims.clear();

	if (!p_am) {
		return;
	}

	MxU16 animCount = p_am->m_animCount;
	AnimInfo* anims = p_am->m_anims;

	if (!anims || animCount == 0) {
		return;
	}

	int skippedNoName = 0, skippedNoChar = 0, skippedNoModel = 0;

	for (MxU16 i = 0; i < animCount; i++) {
		if (!anims[i].m_name || anims[i].m_objectId == 0) {
			SDL_Log("NpcAnimCatalog:   Skipped [%u]: no name or objectId=0", i);
			skippedNoName++;
			continue;
		}

		if (anims[i].m_characterIndex < 0) {
			skippedNoChar++;
			continue;
		}

		if (anims[i].m_modelCount == 0) {
			SDL_Log(
				"NpcAnimCatalog:   Skipped '%s' (objId=%u): modelCount=0",
				anims[i].m_name,
				anims[i].m_objectId
			);
			skippedNoModel++;
			continue;
		}

		NpcAnimEntry entry;
		entry.animIndex = i;
		entry.objectId = anims[i].m_objectId;
		entry.name = anims[i].m_name;
		entry.location = anims[i].m_location;
		entry.characterIndex = anims[i].m_characterIndex;
		entry.eligibilityMask = anims[i].m_unk0x0c;
		entry.modelCount = anims[i].m_modelCount;

		if (entry.location == -1) {
			m_npcAnims.push_back(entry);
		}
		else {
			m_camAnims.push_back(entry);
		}
	}

	SDL_Log(
		"NpcAnimCatalog: Refreshed with %zu NPC anims + %zu cam anims from %u total "
		"(skipped: %d no name/id, %d no character, %d no models)",
		m_npcAnims.size(),
		m_camAnims.size(),
		animCount,
		skippedNoName,
		skippedNoChar,
		skippedNoModel
	);
}

int8_t NpcAnimCatalog::DisplayActorToCharacterIndex(uint8_t p_displayActorIndex)
{
	const char* actorName = CharacterManager()->GetActorName(p_displayActorIndex);
	if (!actorName) {
		return -1;
	}

	// GetCharacterIndex matches first 2 chars of name against g_characters[].m_name
	int8_t idx = AnimationManager()->GetCharacterIndex(actorName);
	SDL_Log(
		"NpcAnimCatalog: DisplayActor %u -> name='%s' -> characterIndex=%d",
		p_displayActorIndex,
		actorName,
		idx
	);
	return idx;
}

static std::vector<const NpcAnimEntry*> FilterEligible(
	const std::vector<NpcAnimEntry>& p_entries,
	int8_t p_characterIndex,
	const char* p_bucketName
)
{
	std::vector<const NpcAnimEntry*> result;
	int skippedChar = 0;
	int skippedMask = 0;

	for (const auto& entry : p_entries) {
		// Animation must be for the player's character
		if (entry.characterIndex != p_characterIndex) {
			skippedChar++;
			continue;
		}

		// Check eligibility mask. If the mask doesn't cover all 5 main actors,
		// a counterpart is needed. For now we pretend no counterpart is available,
		// so we only include animations where ALL main actors can trigger
		// (mask has all 5 bits set = no specific counterpart needed).
		if ((entry.eligibilityMask & ALL_MAIN_ACTORS_MASK) != ALL_MAIN_ACTORS_MASK) {
			skippedMask++;
			continue;
		}

		result.push_back(&entry);
	}

	SDL_Log(
		"NpcAnimCatalog: %s filter for charIndex=%d: %zu eligible, %d skipped (wrong char), %d skipped (needs counterpart)",
		p_bucketName,
		p_characterIndex,
		result.size(),
		skippedChar,
		skippedMask
	);

	return result;
}

std::vector<const NpcAnimEntry*> NpcAnimCatalog::GetEligibleNpcAnimations(uint8_t p_displayActorIndex) const
{
	int8_t charIndex = DisplayActorToCharacterIndex(p_displayActorIndex);
	if (charIndex < 0) {
		return {};
	}
	return FilterEligible(m_npcAnims, charIndex, "NPC");
}

std::vector<const NpcAnimEntry*> NpcAnimCatalog::GetEligibleCamAnimations(uint8_t p_displayActorIndex) const
{
	int8_t charIndex = DisplayActorToCharacterIndex(p_displayActorIndex);
	if (charIndex < 0) {
		return {};
	}
	return FilterEligible(m_camAnims, charIndex, "Cam");
}

bool NpcAnimCatalog::NeedsCounterpart(uint16_t p_index) const
{
	for (const auto& entry : m_npcAnims) {
		if (entry.animIndex == p_index) {
			return entry.modelCount >= 2;
		}
	}
	for (const auto& entry : m_camAnims) {
		if (entry.animIndex == p_index) {
			return entry.modelCount >= 2;
		}
	}
	return false;
}
