#pragma once

#include <cstdint>
#include <vector>

class LegoAnimationManager;
struct AnimInfo;

namespace Multiplayer::Animation
{

enum AnimCategory : uint8_t {
	e_npcAnim,  // characterIndex >= 0 && location == -1
	e_camAnim,  // characterIndex >= 0 && location >= 0
	e_otherAnim // characterIndex < 0 (ambient, non-character)
};

struct CatalogEntry {
	uint16_t animIndex; // Index into LegoAnimationManager::m_anims[]
	AnimCategory category;
};

class Catalog {
public:
	void Refresh(LegoAnimationManager* p_am);

	const AnimInfo* GetAnimInfo(uint16_t p_animIndex) const;

	std::vector<const CatalogEntry*> GetEligibleNpcAnimations(uint8_t p_displayActorIndex) const;
	std::vector<const CatalogEntry*> GetEligibleCamAnimations(uint8_t p_displayActorIndex) const;

	bool NeedsCounterpart(uint16_t p_animIndex) const;

	// Convert a display actor index to the g_characters[] index used by animations.
	// Returns -1 if no match.
	static int8_t DisplayActorToCharacterIndex(uint8_t p_displayActorIndex);

private:
	std::vector<const CatalogEntry*> FilterEligible(AnimCategory p_category, int8_t p_characterIndex) const;

	std::vector<CatalogEntry> m_entries;
	AnimInfo* m_animsBase;
	uint16_t m_animCount;
};

} // namespace Multiplayer::Animation
