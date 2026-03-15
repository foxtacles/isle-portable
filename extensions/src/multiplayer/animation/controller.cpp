#include "extensions/multiplayer/animation/controller.h"

#include "3dmanager/lego3dmanager.h"
#include "anim/legoanim.h"
#include "extensions/common/animutils.h"
#include "extensions/common/charactercloner.h"
#include "legoanimationmanager.h"
#include "legocharactermanager.h"
#include "legovideomanager.h"
#include "misc.h"
#include "misc/legotree.h"
#include "roi/legoroi.h"

#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <functional>
#include <vector>

using namespace Multiplayer::Animation;
namespace AnimUtils = Extensions::Common::AnimUtils;
using Extensions::Common::CharacterCloner;

Controller::Controller()
	: m_playing(false), m_rebaseComputed(false), m_startTime(0), m_currentData(nullptr), m_executingROI(nullptr),
	  m_vehicleROI(nullptr), m_roiMap(nullptr), m_roiMapSize(0), m_propROIs(nullptr), m_propCount(0)
{
}

Controller::~Controller()
{
	if (m_playing) {
		Stop();
	}
}

void Controller::CreateExtraROIs(const AnimInfo* p_animInfo, LegoROI* p_executingROI, LegoROI* p_vehicleROI)
{
	// Determine the player character's 2-letter prefix for matching.
	// The last 2 chars of the animation name encode the character
	// (same convention used by GetCharacterIndex).
	char playerPrefix[3] = {0};
	size_t nameLen = SDL_strlen(p_animInfo->m_name);
	if (nameLen >= 2) {
		playerPrefix[0] = p_animInfo->m_name[nameLen - 2];
		playerPrefix[1] = p_animInfo->m_name[nameLen - 1];
	}

	LegoU32 numActors = m_currentData->anim->GetNumActors();
	std::vector<LegoROI*> createdROIs;

	for (LegoU32 i = 0; i < numActors; i++) {
		const char* actorName = m_currentData->anim->GetActorName(i);
		LegoU32 actorType = m_currentData->anim->GetActorType(i);

		if (!actorName || *actorName == '\0') {
			continue;
		}

		// Strip '*' prefix for lookup
		const char* lookupName = (*actorName == '*') ? actorName + 1 : actorName;
		std::string lowered(lookupName);
		std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::tolower);

		// Skip the player character — already have p_executingROI
		if (playerPrefix[0] && !SDL_strncasecmp(lookupName, playerPrefix, 2)) {
			continue;
		}

		LegoROI* roi = nullptr;

		if (actorType == LegoAnimActorEntry::e_managedLegoActor) {
			// Character — use CharacterCloner for full compound ROI.
			// Intentional divergence from original's GetActorROI():
			// we clone instead of borrowing the global actor, to avoid
			// mutating global actor state in multiplayer.
			char uniqueName[64];
			SDL_snprintf(uniqueName, sizeof(uniqueName), "npc_char_%s", lowered.c_str());

			roi = CharacterCloner::Clone(CharacterManager(), uniqueName, lowered.c_str());
			if (roi) {
				roi->SetName(lowered.c_str());
				VideoManager()->Get3DManager()->Add(*roi);
			}
		}
		else if (actorType == LegoAnimActorEntry::e_managedInvisibleRoiTrimmed || actorType == LegoAnimActorEntry::e_sceneRoi1 || actorType == LegoAnimActorEntry::e_sceneRoi2) {
			// Prop with digit-trimmed LOD name (matches original exactly)
			std::string lodName(lowered);
			while (lodName.size() > 1) {
				char c = lodName.back();
				if ((c >= '0' && c <= '9') || c == '_') {
					lodName.pop_back();
				}
				else {
					break;
				}
			}

			char uniqueName[64];
			SDL_snprintf(uniqueName, sizeof(uniqueName), "npc_prop_%s", lowered.c_str());
			roi = CharacterManager()->CreateAutoROI(uniqueName, lodName.c_str(), FALSE);
			if (roi) {
				roi->SetName(lowered.c_str());
			}
		}
		else if (actorType == LegoAnimActorEntry::e_managedInvisibleRoi) {
			// Prop with exact name (matches original exactly)
			char uniqueName[64];
			SDL_snprintf(uniqueName, sizeof(uniqueName), "npc_prop_%s", lowered.c_str());
			roi = CharacterManager()->CreateAutoROI(uniqueName, lowered.c_str(), FALSE);
			if (roi) {
				roi->SetName(lowered.c_str());
			}
		}
		else {
			// Type 0/1: "scene" actors expected to already exist in the world.
			// Try to create as prop first. If that fails and we have a vehicle
			// ROI, reuse it (the actor is likely the player's vehicle).
			std::string lodName(lowered);
			while (lodName.size() > 1) {
				char c = lodName.back();
				if ((c >= '0' && c <= '9') || c == '_') {
					lodName.pop_back();
				}
				else {
					break;
				}
			}

			char uniqueName[64];
			SDL_snprintf(uniqueName, sizeof(uniqueName), "npc_prop_%s", lowered.c_str());
			roi = CharacterManager()->CreateAutoROI(uniqueName, lodName.c_str(), FALSE);
			if (roi) {
				roi->SetName(lowered.c_str());
			}
			else if (p_vehicleROI && !m_vehicleROI) {
				// Prop creation failed — reuse the existing ride vehicle ROI
				m_vehicleROI = p_vehicleROI;
				m_savedVehicleName = p_vehicleROI->GetName();
				p_vehicleROI->SetName(lowered.c_str());
				roi = p_vehicleROI;
			}
		}

		if (roi) {
			createdROIs.push_back(roi);
		}
	}

	if (!createdROIs.empty()) {
		m_propCount = (uint8_t) createdROIs.size();
		m_propROIs = new LegoROI*[m_propCount];
		for (uint8_t i = 0; i < m_propCount; i++) {
			m_propROIs[i] = createdROIs[i];
		}

		// Rebuild ROI map with extra ROIs
		delete[] m_roiMap;
		m_roiMap = nullptr;
		m_roiMapSize = 0;
		AnimUtils::BuildROIMap(m_currentData->anim, p_executingROI, m_propROIs, m_propCount, m_roiMap, m_roiMapSize);
	}
}

void Controller::Play(const AnimInfo* p_animInfo, LegoROI* p_executingROI, LegoROI* p_vehicleROI)
{
	if (m_playing) {
		Stop();
	}

	if (!p_executingROI || !p_animInfo) {
		return;
	}

	AnimData* data = m_loader.EnsureCached(p_animInfo->m_objectId);
	if (!data || !data->anim) {
		return;
	}

	m_currentData = data;
	m_executingROI = p_executingROI;

	// Build ROI map -- only the executing player's ROI.
	// Target character's *-prefixed nodes get index 0 (NULL) -> transforms skipped.
	AnimUtils::BuildROIMap(data->anim, p_executingROI, nullptr, 0, m_roiMap, m_roiMapSize);

	if (!m_roiMap) {
		m_currentData = nullptr;
		m_executingROI = nullptr;
		return;
	}

	// Create ROIs for extra actors and props
	CreateExtraROIs(p_animInfo, p_executingROI, p_vehicleROI);

	// Save initial transform
	m_savedTransform = p_executingROI->GetLocal2World();

	// Init phoneme state
	m_phonemePlayer.Init(data->phonemeTracks, p_executingROI);

	// Create sounds upfront (during the same freeze as EnsureCached)
	m_audioPlayer.Init(data->audioTracks);

	// Clock starts on first Tick(), not here. All setup (file I/O,
	// parsing, sound buffer creation) is complete by the time Play()
	// returns, so the first Tick() starts from a clean state.
	m_startTime = 0;
	m_playing = true;
}

void Controller::ComputeRebaseMatrix()
{
	// Find the player character node and compute its WORLD transform
	// at time 0 by accumulating parent transforms (handles nested
	// '-' nodes like -SBA001BU -> -TILT -> BU).
	std::function<bool(LegoTreeNode*, MxMatrix&)> findOrigin = [&](LegoTreeNode* node,
																   MxMatrix& parentWorld) -> bool {
		LegoAnimNodeData* data = (LegoAnimNodeData*) node->GetData();
		MxU32 roiIdx = data ? data->GetROIIndex() : 0;

		// Compute this node's world transform
		MxMatrix localMat;
		LegoROI::CreateLocalTransform(data, 0, localMat);
		MxMatrix worldMat;
		worldMat.Product(localMat, parentWorld);

		if (roiIdx != 0 && m_roiMap[roiIdx] == m_executingROI) {
			m_animPose0 = worldMat;
			return true;
		}
		for (LegoU32 i = 0; i < node->GetNumChildren(); i++) {
			if (findOrigin(node->GetChild(i), worldMat)) {
				return true;
			}
		}
		return false;
	};
	MxMatrix identity;
	identity.SetIdentity();
	findOrigin(m_currentData->anim->GetRoot(), identity);

	// Compute inverse of animPose0 (rigid body: transpose rotation, negate translated position)
	MxMatrix invAnimPose0;
	invAnimPose0.SetIdentity();
	// Transpose the 3x3 rotation
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 3; c++) {
			invAnimPose0[r][c] = m_animPose0[c][r];
		}
	}
	// Translation: -R^T * t
	for (int r = 0; r < 3; r++) {
		invAnimPose0[3][r] =
			-(invAnimPose0[0][r] * m_animPose0[3][0] + invAnimPose0[1][r] * m_animPose0[3][1] +
			  invAnimPose0[2][r] * m_animPose0[3][2]);
	}

	// rebaseMatrix = savedTransform * inverse(animPose0)
	m_rebaseMatrix.Product(invAnimPose0, m_savedTransform);
	m_rebaseComputed = true;
}

void Controller::Tick(float p_deltaTime)
{
	if (!m_playing || !m_currentData || !m_executingROI) {
		return;
	}

	// Start the clock on the first tick. All setup (file I/O, parsing,
	// sound buffer creation) completed in Play(), so this tick is clean.
	if (m_startTime == 0) {
		m_startTime = SDL_GetTicks();
	}

	// Ensure all mapped ROIs (including props) stay visible
	if (m_roiMap) {
		AnimUtils::EnsureROIMapVisibility(m_roiMap, m_roiMapSize);
	}

	// Use wall-clock time (SDL_GetTicks) instead of game timer
	// (Timer()->GetTime()) because audio plays via miniaudio on
	// a real-time audio thread. The game timer can stall during
	// freezes, causing it to diverge from the audio clock.
	float elapsed = (float) (SDL_GetTicks() - m_startTime);

	if (elapsed >= m_currentData->duration) {
		Stop();
		return;
	}

	// 1. Skeletal animation
	if (m_currentData->anim && m_roiMap) {
		// Compute the rebase transform once: the full rotation+translation delta
		// between the animation's designed player pose (at time 0) and the
		// player's actual pose. This delta is applied as a parent transform to
		// the entire animation tree, preserving all relative motion (turns,
		// walks, extra actor positions) while anchoring everything to the
		// player's current position and facing direction.
		//
		// Math: rebaseMatrix = savedTransform * inverse(animPose0)
		// So: world = rebaseMatrix * animPose(t) = savedTransform * inverse(animPose0) * animPose(t)
		// At t=0 this gives savedTransform (player stays put).
		// At t>0 the delta animPose0->animPose(t) is applied in the player's frame.
		if (!m_rebaseComputed) {
			ComputeRebaseMatrix();
		}

		// Apply the entire animation tree with the rebase matrix.
		// This correctly transforms all characters and props from animation
		// world-space to the player's local frame.
		LegoTreeNode* root = m_currentData->anim->GetRoot();
		for (LegoU32 i = 0; i < root->GetNumChildren(); i++) {
			LegoROI::ApplyAnimationTransformation(root->GetChild(i), m_rebaseMatrix, (LegoTime) elapsed, m_roiMap);
		}
	}

	// 2. Audio
	m_audioPlayer.Tick(elapsed, m_executingROI->GetName());

	// 3. Phoneme frames
	m_phonemePlayer.Tick(elapsed, m_currentData->phonemeTracks);
}

void Controller::Stop()
{
	if (!m_playing) {
		return;
	}

	m_audioPlayer.Cleanup();
	m_phonemePlayer.Cleanup(m_executingROI);
	CleanupProps();
	RestoreVehicleROI();

	delete[] m_roiMap;
	m_roiMap = nullptr;
	m_roiMapSize = 0;

	if (m_executingROI) {
		m_executingROI->WrappedSetLocal2WorldWithWorldDataUpdate(m_savedTransform);
	}

	m_playing = false;
	m_rebaseComputed = false;
	m_currentData = nullptr;
	m_executingROI = nullptr;
	m_startTime = 0;
}

void Controller::CleanupProps()
{
	for (uint8_t i = 0; i < m_propCount; i++) {
		if (m_propROIs[i]) {
			// Skip borrowed vehicle ROI — it belongs to the ride animation system
			if (m_propROIs[i] == m_vehicleROI) {
				continue;
			}

			VideoManager()->Get3DManager()->Remove(*m_propROIs[i]);
			if (CharacterManager()->GetRefCount(m_propROIs[i]) > 0) {
				CharacterManager()->ReleaseActor(m_propROIs[i]);
			}
			else {
				CharacterManager()->ReleaseAutoROI(m_propROIs[i]);
			}
		}
	}
	delete[] m_propROIs;
	m_propROIs = nullptr;
	m_propCount = 0;
}

void Controller::RestoreVehicleROI()
{
	if (m_vehicleROI && !m_savedVehicleName.empty()) {
		m_vehicleROI->SetName(m_savedVehicleName.c_str());
		m_vehicleROI = nullptr;
		m_savedVehicleName.clear();
	}
}
