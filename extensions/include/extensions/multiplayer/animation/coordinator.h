#pragma once

#include <cstdint>
#include <vector>

namespace Multiplayer::Animation
{

class Catalog;

enum class CoordinationState : uint8_t {
	e_idle,
	e_interested,
	e_countdown,
	e_playing,
	e_completed
};

struct EligibilityInfo {
	uint16_t animIndex;
	bool eligible;          // All requirements met: at location and all roles (spectator + performers) filled
	bool atLocation;        // At the right location (or location == -1)
	bool needsOtherPlayers; // Player has a role but other player slots are unfilled
};

class Coordinator {
public:
	Coordinator();

	void SetCatalog(const Catalog* p_catalog);

	CoordinationState GetState() const { return m_state; }
	uint16_t GetCurrentAnimIndex() const { return m_currentAnimIndex; }

	void SetInterest(uint16_t p_animIndex);
	void ClearInterest();

	// Compute eligibility for animations at a location given all available players.
	// p_charIndices[0] is the local player; the rest are remote players at this location.
	std::vector<EligibilityInfo> ComputeEligibility(
		int16_t p_location, const int8_t* p_charIndices, uint8_t p_count) const;

	void Tick(uint32_t p_now);
	void Reset();

	// Networking hooks (stubs, wired up in Part 4)
	void OnRemoteInterest(uint32_t p_peerId, uint16_t p_animIndex);
	void OnRemoteInterestClear(uint32_t p_peerId);
	void OnAnimationStart(uint16_t p_animIndex, uint32_t p_startTimeDelta);
	void OnAnimationCancel(uint16_t p_animIndex);

private:
	const Catalog* m_catalog;
	CoordinationState m_state;
	uint16_t m_currentAnimIndex;
};

} // namespace Multiplayer::Animation
