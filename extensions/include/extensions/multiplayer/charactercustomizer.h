#pragma once

#include "mxtypes.h"

#include <cstdint>

class LegoROI;

namespace Multiplayer
{

struct CustomizeState;

class CharacterCustomizer {
public:
	static uint8_t ResolveActorInfoIndex(uint8_t displayActorIndex, uint8_t actorId);

	static bool SwitchColor(LegoROI* rootROI, uint8_t actorInfoIndex,
	                        CustomizeState& state, int partIndex);
	static bool SwitchVariant(LegoROI* rootROI, uint8_t actorInfoIndex,
	                          CustomizeState& state);
	static bool SwitchSound(CustomizeState& state);
	static bool SwitchMove(CustomizeState& state);
	static bool SwitchMood(CustomizeState& state);
	static void ApplyFullState(LegoROI* rootROI, uint8_t actorInfoIndex,
	                           const CustomizeState& state);
	static int MapClickedPartIndex(const char* partName);
	static void PlayClickSound(LegoROI* roi, const CustomizeState& state, bool basedOnMood);
	static MxU32 PlayClickAnimation(LegoROI* roi, const CustomizeState& state);
	static void StopClickAnimation(MxU32 objectId);

private:
	static LegoROI* FindChildROI(LegoROI* rootROI, const char* name);
	static void ApplyHatVariant(LegoROI* rootROI, uint8_t actorInfoIndex,
	                            const CustomizeState& state);
};

} // namespace Multiplayer
