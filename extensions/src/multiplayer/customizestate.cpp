#include "extensions/multiplayer/customizestate.h"

#include "legoactors.h"
#include "misc.h"

#include <SDL3/SDL_stdinc.h>

using namespace Multiplayer;

void CustomizeState::InitFromActorInfo(uint8_t actorInfoIndex)
{
	if (actorInfoIndex >= sizeOfArray(g_actorInfoInit)) {
		return;
	}

	const LegoActorInfo& info = g_actorInfoInit[actorInfoIndex];

	for (int i = 0; i < 10; i++) {
		colorIndices[i] = info.m_parts[i].m_nameIndex;
	}

	hatVariantIndex = info.m_parts[c_infohatPart].m_partNameIndex;
	bodyVariantIndex = info.m_parts[c_bodyPart].m_partNameIndex;
	sound = (uint8_t) info.m_sound;
	move = (uint8_t) info.m_move;
	mood = info.m_mood;
}

void CustomizeState::Pack(uint8_t out[5]) const
{
	// byte 0: hatVariantIndex(5 bits) | bodyVariantIndex(3 bits)
	out[0] = (hatVariantIndex & 0x1F) | ((bodyVariantIndex & 0x07) << 5);

	// byte 1: sound(4 bits) | move(2 bits) | mood(2 bits)
	out[1] = (sound & 0x0F) | ((move & 0x03) << 4) | ((mood & 0x03) << 6);

	// byte 2: infohatColor(4 bits) | infogronColor(4 bits)
	out[2] = (colorIndices[c_infohatPart] & 0x0F) | ((colorIndices[c_infogronPart] & 0x0F) << 4);

	// byte 3: armlftColor(4 bits) | armrtColor(4 bits)
	out[3] = (colorIndices[c_armlftPart] & 0x0F) | ((colorIndices[c_armrtPart] & 0x0F) << 4);

	// byte 4: leglftColor(4 bits) | legrtColor(4 bits)
	out[4] = (colorIndices[c_leglftPart] & 0x0F) | ((colorIndices[c_legrtPart] & 0x0F) << 4);
}

void CustomizeState::Unpack(const uint8_t in[5])
{
	// byte 0: hatVariantIndex(5 bits) | bodyVariantIndex(3 bits)
	hatVariantIndex = in[0] & 0x1F;
	bodyVariantIndex = (in[0] >> 5) & 0x07;

	// byte 1: sound(4 bits) | move(2 bits) | mood(2 bits)
	sound = in[1] & 0x0F;
	move = (in[1] >> 4) & 0x03;
	mood = (in[1] >> 6) & 0x03;

	// byte 2: infohatColor(4 bits) | infogronColor(4 bits)
	colorIndices[c_infohatPart] = in[2] & 0x0F;
	colorIndices[c_infogronPart] = (in[2] >> 4) & 0x0F;

	// byte 3: armlftColor(4 bits) | armrtColor(4 bits)
	colorIndices[c_armlftPart] = in[3] & 0x0F;
	colorIndices[c_armrtPart] = (in[3] >> 4) & 0x0F;

	// byte 4: leglftColor(4 bits) | legrtColor(4 bits)
	colorIndices[c_leglftPart] = in[4] & 0x0F;
	colorIndices[c_legrtPart] = (in[4] >> 4) & 0x0F;

	// Derive non-independent parts
	colorIndices[c_bodyPart] = colorIndices[c_infogronPart];
	colorIndices[c_headPart] = colorIndices[c_infohatPart];
	colorIndices[c_clawlftPart] = colorIndices[c_armlftPart];
	colorIndices[c_clawrtPart] = colorIndices[c_armrtPart];
}

bool CustomizeState::operator==(const CustomizeState& other) const
{
	return SDL_memcmp(this, &other, sizeof(CustomizeState)) == 0;
}
