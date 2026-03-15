#pragma once

#include <cstdint>
#include <vector>

class LegoAnimationManager;

namespace Extensions
{
namespace Common
{

struct NpcAnimEntry {
	uint16_t animIndex;      // Index into LegoAnimationManager::m_anims[]
	uint32_t objectId;       // AnimInfo::m_objectId
	const char* name;        // Pointer into existing AnimInfo data
	int16_t location;        // AnimInfo::m_location (-1 = NPC anim, >= 0 = cam anim)
	int8_t characterIndex;   // g_characters[] index (which NPC performs the animation)
	uint8_t eligibilityMask; // AnimInfo::m_unk0x0c (bitmask of which player actors can trigger)
	uint8_t modelCount;      // AnimInfo::m_modelCount
};

class NpcAnimCatalog {
public:
	void Refresh(LegoAnimationManager* p_am);

	const std::vector<NpcAnimEntry>& GetNpcEntries() const { return m_npcAnims; }
	const std::vector<NpcAnimEntry>& GetCamEntries() const { return m_camAnims; }

	// Get NPC animations (location == -1) for a given display actor index.
	// Filters by characterIndex matching the display actor's character,
	// and by eligibility mask (pretends no counterpart available, so only
	// animations where the mask includes all 5 main actors are returned).
	std::vector<const NpcAnimEntry*> GetEligibleNpcAnimations(uint8_t p_displayActorIndex) const;

	// Get cam animations (location >= 0) for a given display actor index.
	std::vector<const NpcAnimEntry*> GetEligibleCamAnimations(uint8_t p_displayActorIndex) const;

	bool NeedsCounterpart(uint16_t p_index) const;

	// Convert a display actor index to the g_characters[] index used by animations.
	// Returns -1 if no match.
	static int8_t DisplayActorToCharacterIndex(uint8_t p_displayActorIndex);

private:
	std::vector<NpcAnimEntry> m_npcAnims; // location == -1
	std::vector<NpcAnimEntry> m_camAnims; // location >= 0
};

} // namespace Common
} // namespace Extensions
