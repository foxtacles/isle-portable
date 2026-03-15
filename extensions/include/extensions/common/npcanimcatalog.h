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
	int8_t characterIndex;   // g_characters[] index
	uint8_t eligibilityMask; // AnimInfo::m_unk0x0c
	uint8_t modelCount;      // AnimInfo::m_modelCount
};

class NpcAnimCatalog {
public:
	void Refresh(LegoAnimationManager* p_am);

	const std::vector<NpcAnimEntry>& GetEntries() const { return m_entries; }

	// Get animations eligible for a given actor ID (1-based, e.g. pepper=1).
	// Filters by eligibility mask per the FUN_10062110 pattern.
	std::vector<const NpcAnimEntry*> GetEligibleAnimations(uint8_t p_actorId) const;

	bool NeedsCounterpart(uint16_t p_index) const;

private:
	std::vector<NpcAnimEntry> m_entries;
};

} // namespace Common
} // namespace Extensions
