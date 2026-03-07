#include "extensions/multiplayer/charactercustomizer.h"

#include "extensions/multiplayer/charactercloner.h"
#include "extensions/multiplayer/customizestate.h"

#include "3dmanager/lego3dmanager.h"
#include "3dmanager/lego3dview.h"
#include "legoactors.h"
#include "legocharactermanager.h"
#include "legovideomanager.h"
#include "misc.h"
#include "mxatom.h"
#include "mxdsaction.h"
#include "mxmisc.h"
#include "roi/legolod.h"
#include "roi/legoroi.h"
#include "viewmanager/viewlodlist.h"
#include "viewmanager/viewmanager.h"

#include <SDL3/SDL_stdinc.h>
#include <cstdio>

using namespace Multiplayer;

static const MxU32 g_characterSoundIdOffset = 50;
static const MxU32 g_characterSoundIdMoodOffset = 66;
static const MxU32 g_characterAnimationId = 10;
static const MxU32 g_maxSound = 9;
static const MxU32 g_maxMove = 4;

static uint32_t s_variantCounter = 10000;

// MARK: Private helpers

LegoROI* CharacterCustomizer::FindChildROI(LegoROI* rootROI, const char* name)
{
	const CompoundObject* comp = rootROI->GetComp();

	for (CompoundObject::const_iterator it = comp->begin(); it != comp->end(); it++) {
		LegoROI* roi = (LegoROI*) *it;

		if (!SDL_strcasecmp(name, roi->GetName())) {
			return roi;
		}
	}

	return NULL;
}

// MARK: Public API

uint8_t CharacterCustomizer::ResolveActorInfoIndex(uint8_t displayActorIndex, uint8_t actorId)
{
	if (IsValidDisplayActorIndex(displayActorIndex)) {
		return displayActorIndex;
	}

	if (actorId >= 1 && actorId <= 5) {
		return actorId - 1;
	}

	return 0;
}

bool CharacterCustomizer::SwitchColor(
	LegoROI* rootROI,
	uint8_t actorInfoIndex,
	CustomizeState& state,
	int partIndex
)
{
	// Remap derived parts to independent parts
	if (partIndex == c_clawlftPart) {
		partIndex = c_armlftPart;
	}
	else if (partIndex == c_clawrtPart) {
		partIndex = c_armrtPart;
	}
	else if (partIndex == c_headPart) {
		partIndex = c_infohatPart;
	}
	else if (partIndex == c_bodyPart) {
		partIndex = c_infogronPart;
	}

	if (!(g_actorLODs[partIndex + 1].m_flags & LegoActorLOD::c_useColor)) {
		return false;
	}

	if (actorInfoIndex >= sizeOfArray(g_actorInfoInit)) {
		return false;
	}

	const LegoActorInfo::Part& part = g_actorInfoInit[actorInfoIndex].m_parts[partIndex];

	state.colorIndices[partIndex]++;
	if (part.m_nameIndices[state.colorIndices[partIndex]] == 0xff) {
		state.colorIndices[partIndex] = 0;
	}

	LegoROI* targetROI = FindChildROI(rootROI, g_actorLODs[partIndex + 1].m_name);
	if (!targetROI) {
		return false;
	}

	LegoFloat red, green, blue, alpha;
	LegoROI::GetRGBAColor(part.m_names[part.m_nameIndices[state.colorIndices[partIndex]]], red, green, blue, alpha);
	targetROI->SetLodColor(red, green, blue, alpha);
	return true;
}

bool CharacterCustomizer::SwitchVariant(LegoROI* rootROI, uint8_t actorInfoIndex, CustomizeState& state)
{
	if (actorInfoIndex >= sizeOfArray(g_actorInfoInit)) {
		return false;
	}

	const LegoActorInfo::Part& part = g_actorInfoInit[actorInfoIndex].m_parts[c_infohatPart];

	state.hatVariantIndex++;
	MxU8 partNameIndex = part.m_partNameIndices[state.hatVariantIndex];

	if (partNameIndex == 0xff) {
		state.hatVariantIndex = 0;
		partNameIndex = part.m_partNameIndices[state.hatVariantIndex];
	}

	LegoROI* childROI = FindChildROI(rootROI, g_actorLODs[c_infohatLOD].m_name);

	if (childROI != NULL) {
		char lodName[256];

		ViewLODList* lodList = GetViewLODListManager()->Lookup(part.m_partName[partNameIndex]);
		MxS32 lodSize = lodList->Size();
		sprintf(lodName, "%s_cv%u", rootROI->GetName(), s_variantCounter++);
		ViewLODList* dupLodList = GetViewLODListManager()->Create(lodName, lodSize);

		Tgl::Renderer* renderer = VideoManager()->GetRenderer();
		LegoFloat red, green, blue, alpha;
		LegoROI::GetRGBAColor(
			part.m_names[part.m_nameIndices[state.colorIndices[c_infohatPart]]],
			red,
			green,
			blue,
			alpha
		);

		for (MxS32 i = 0; i < lodSize; i++) {
			LegoLOD* lod = (LegoLOD*) (*lodList)[i];
			LegoLOD* clone = lod->Clone(renderer);
			clone->SetColor(red, green, blue, alpha);
			dupLodList->PushBack(clone);
		}

		lodList->Release();
		lodList = dupLodList;

		if (childROI->GetLodLevel() >= 0) {
			VideoManager()->Get3DManager()->GetLego3DView()->GetViewManager()->RemoveROIDetailFromScene(childROI);
		}

		childROI->SetLODList(lodList);
		lodList->Release();
	}

	return true;
}

bool CharacterCustomizer::SwitchSound(CustomizeState& state)
{
	state.sound++;
	if (state.sound >= g_maxSound) {
		state.sound = 0;
	}
	return true;
}

bool CharacterCustomizer::SwitchMove(CustomizeState& state)
{
	state.move++;
	if (state.move >= g_maxMove) {
		state.move = 0;
	}
	return true;
}

bool CharacterCustomizer::SwitchMood(CustomizeState& state)
{
	state.mood++;
	if (state.mood > 3) {
		state.mood = 0;
	}
	return true;
}

int CharacterCustomizer::MapClickedPartIndex(const char* partName)
{
	for (int i = 0; i < 10; i++) {
		if (!SDL_strcasecmp(partName, g_actorLODs[i + 1].m_name)) {
			return i;
		}
	}
	return -1;
}

void CharacterCustomizer::ApplyFullState(
	LegoROI* rootROI,
	uint8_t actorInfoIndex,
	const CustomizeState& state
)
{
	if (actorInfoIndex >= sizeOfArray(g_actorInfoInit)) {
		return;
	}

	// Apply colors for the 6 independent colorable parts
	static const int colorableParts[] = {
		c_infohatPart, c_infogronPart, c_armlftPart, c_armrtPart, c_leglftPart, c_legrtPart
	};

	for (int i = 0; i < (int) sizeOfArray(colorableParts); i++) {
		int partIndex = colorableParts[i];

		if (!(g_actorLODs[partIndex + 1].m_flags & LegoActorLOD::c_useColor)) {
			continue;
		}

		LegoROI* childROI = FindChildROI(rootROI, g_actorLODs[partIndex + 1].m_name);
		if (!childROI) {
			continue;
		}

		const LegoActorInfo::Part& part = g_actorInfoInit[actorInfoIndex].m_parts[partIndex];

		LegoFloat red, green, blue, alpha;
		LegoROI::GetRGBAColor(
			part.m_names[part.m_nameIndices[state.colorIndices[partIndex]]],
			red,
			green,
			blue,
			alpha
		);
		childROI->SetLodColor(red, green, blue, alpha);
	}

	// Apply hat variant if different from default
	const LegoActorInfo::Part& hatPart = g_actorInfoInit[actorInfoIndex].m_parts[c_infohatPart];
	if (state.hatVariantIndex != hatPart.m_partNameIndex) {
		ApplyHatVariant(rootROI, actorInfoIndex, state);
	}
}

void CharacterCustomizer::ApplyHatVariant(
	LegoROI* rootROI,
	uint8_t actorInfoIndex,
	const CustomizeState& state
)
{
	if (actorInfoIndex >= sizeOfArray(g_actorInfoInit)) {
		return;
	}

	const LegoActorInfo::Part& part = g_actorInfoInit[actorInfoIndex].m_parts[c_infohatPart];

	MxU8 partNameIndex = part.m_partNameIndices[state.hatVariantIndex];
	if (partNameIndex == 0xff) {
		return;
	}

	LegoROI* childROI = FindChildROI(rootROI, g_actorLODs[c_infohatLOD].m_name);

	if (childROI != NULL) {
		char lodName[256];

		ViewLODList* lodList = GetViewLODListManager()->Lookup(part.m_partName[partNameIndex]);
		MxS32 lodSize = lodList->Size();
		sprintf(lodName, "%s_cv%u", rootROI->GetName(), s_variantCounter++);
		ViewLODList* dupLodList = GetViewLODListManager()->Create(lodName, lodSize);

		Tgl::Renderer* renderer = VideoManager()->GetRenderer();
		LegoFloat red, green, blue, alpha;
		LegoROI::GetRGBAColor(
			part.m_names[part.m_nameIndices[state.colorIndices[c_infohatPart]]],
			red,
			green,
			blue,
			alpha
		);

		for (MxS32 i = 0; i < lodSize; i++) {
			LegoLOD* lod = (LegoLOD*) (*lodList)[i];
			LegoLOD* clone = lod->Clone(renderer);
			clone->SetColor(red, green, blue, alpha);
			dupLodList->PushBack(clone);
		}

		lodList->Release();
		lodList = dupLodList;

		if (childROI->GetLodLevel() >= 0) {
			VideoManager()->Get3DManager()->GetLego3DView()->GetViewManager()->RemoveROIDetailFromScene(childROI);
		}

		childROI->SetLODList(lodList);
		lodList->Release();
	}
}

void CharacterCustomizer::PlayClickSound(LegoROI* roi, const CustomizeState& state, bool basedOnMood)
{
	MxU32 objectId = basedOnMood ? (state.mood + g_characterSoundIdMoodOffset)
	                             : (state.sound + g_characterSoundIdOffset);

	if (objectId) {
		MxDSAction action;
		action.SetAtomId(MxAtomId(LegoCharacterManager::GetCustomizeAnimFile(), e_lowerCase2));
		action.SetObjectId(objectId);

		const char* name = roi->GetName();
		action.AppendExtra(SDL_strlen(name) + 1, name);
		Start(&action);
	}
}

MxU32 CharacterCustomizer::PlayClickAnimation(LegoROI* roi, const CustomizeState& state)
{
	MxU32 objectId = state.move + g_characterAnimationId;

	MxDSAction action;
	action.SetAtomId(MxAtomId(LegoCharacterManager::GetCustomizeAnimFile(), e_lowerCase2));
	action.SetObjectId(objectId);

	char extra[1024];
	SDL_snprintf(extra, sizeof(extra), "SUBST:actor_01:%s", roi->GetName());
	action.AppendExtra(SDL_strlen(extra) + 1, extra);
	StartActionIfInitialized(action);

	return objectId;
}

void CharacterCustomizer::StopClickAnimation(MxU32 objectId)
{
	MxDSAction action;
	action.SetAtomId(MxAtomId(LegoCharacterManager::GetCustomizeAnimFile(), e_lowerCase2));
	action.SetObjectId(objectId);
	DeleteObject(action);
}
