#include "extensions/multiplayer/animation/locationproximity.h"

#include "decomp.h"
#include "legolocations.h"

#include <cmath>

using namespace Multiplayer::Animation;

static const float DEFAULT_RADIUS = 15.0f;

// Location 0 is the camera origin, and the last location is overhead — skip both
static const int FIRST_VALID_LOCATION = 1;
static const int LAST_VALID_LOCATION = sizeOfArray(g_locations) - 2;

LocationProximity::LocationProximity() : m_nearestLocation(-1), m_nearestDistance(0.0f), m_radius(DEFAULT_RADIUS)
{
}

bool LocationProximity::Update(float p_x, float p_z)
{
	int16_t prev = m_nearestLocation;
	int16_t nearest = ComputeNearest(p_x, p_z, m_radius);

	if (nearest != prev) {
		m_nearestLocation = nearest;
		// Recompute distance for the new nearest
		if (nearest >= 0) {
			float dx = p_x - g_locations[nearest].m_position[0];
			float dz = p_z - g_locations[nearest].m_position[2];
			m_nearestDistance = std::sqrt(dx * dx + dz * dz);
		}
		else {
			m_nearestDistance = 0.0f;
		}
		return true;
	}

	// Update distance even when location didn't change
	if (nearest >= 0) {
		float dx = p_x - g_locations[nearest].m_position[0];
		float dz = p_z - g_locations[nearest].m_position[2];
		m_nearestDistance = std::sqrt(dx * dx + dz * dz);
	}

	return false;
}

void LocationProximity::Reset()
{
	m_nearestLocation = -1;
	m_nearestDistance = 0.0f;
}

int16_t LocationProximity::ComputeNearest(float p_x, float p_z, float p_radius)
{
	float bestDist = p_radius;
	int16_t bestLocation = -1;

	for (int i = FIRST_VALID_LOCATION; i <= LAST_VALID_LOCATION; i++) {
		float dx = p_x - g_locations[i].m_position[0];
		float dz = p_z - g_locations[i].m_position[2];
		float dist = std::sqrt(dx * dx + dz * dz);

		if (dist < bestDist) {
			bestDist = dist;
			bestLocation = static_cast<int16_t>(i);
		}
	}

	return bestLocation;
}
