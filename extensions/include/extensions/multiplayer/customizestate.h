#pragma once

#include <cstdint>

namespace Multiplayer
{

struct CustomizeState {
	uint8_t colorIndices[10]; // m_nameIndex per body part (matching LegoActorInfo::Part::m_nameIndex)
	uint8_t hatVariantIndex;  // m_partNameIndex for infohat part
	uint8_t bodyVariantIndex; // m_partNameIndex for body part
	uint8_t sound;            // 0 to 8
	uint8_t move;             // 0 to 3
	uint8_t mood;             // 0 to 3

	void InitFromActorInfo(uint8_t actorInfoIndex);
	void Pack(uint8_t out[5]) const;
	void Unpack(const uint8_t in[5]);
	bool operator==(const CustomizeState& other) const;
	bool operator!=(const CustomizeState& other) const { return !(*this == other); }
};

} // namespace Multiplayer
