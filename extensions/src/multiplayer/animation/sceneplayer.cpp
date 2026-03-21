#include "extensions/multiplayer/animation/sceneplayer.h"

#include "3dmanager/lego3dmanager.h"
#include "anim/legoanim.h"
#include "extensions/common/animutils.h"
#include "extensions/common/charactercloner.h"
#include "legoanimationmanager.h"
#include "legocameracontroller.h"
#include "legocharactermanager.h"
#include "legovideomanager.h"
#include "legoworld.h"
#include "misc.h"
#include "misc/legotree.h"
#include "mxgeometry/mxgeometry3d.h"
#include "realtime/realtime.h"
#include "roi/legoroi.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

using namespace Multiplayer::Animation;
namespace AnimUtils = Extensions::Common::AnimUtils;
using Extensions::Common::CharacterCloner;

// Defined in legoanimationmanager.cpp
extern LegoAnimationManager::Character g_characters[47];

ScenePlayer::ScenePlayer()
	: m_playing(false), m_rebaseComputed(false), m_startTime(0), m_currentData(nullptr), m_category(e_npcAnim),
	  m_animRootROI(nullptr), m_vehicleROI(nullptr), m_hiddenVehicleROI(nullptr), m_roiMap(nullptr), m_roiMapSize(0), m_propROIs(nullptr),
	  m_propCount(0), m_hasCamAnim(false), m_hideOnStop(false), m_debugFirstTickLogged(false)
{
}

ScenePlayer::~ScenePlayer()
{
	if (m_playing) {
		Stop();
	}
}

// Match an animation actor name (lowercased, * stripped) against g_characters[charIndex].m_name
static bool MatchesCharacter(const std::string& p_actorName, int8_t p_charIndex)
{
	if (p_charIndex < 0 || p_charIndex >= (int8_t) sizeOfArray(g_characters)) {
		return false;
	}
	return !SDL_strcasecmp(p_actorName.c_str(), g_characters[p_charIndex].m_name);
}

void ScenePlayer::SetupROIs(const AnimInfo* p_animInfo, LegoROI* p_localROI, LegoROI* p_vehicleROI)
{
	LegoU32 numActors = m_currentData->anim->GetNumActors();
	std::vector<LegoROI*> createdROIs;
	std::vector<AnimUtils::ROIAlias> aliases;    // participant animation name → ROI mappings
	std::vector<std::string> aliasNames;         // storage for alias name strings

	SDL_Log(
		"[ScenePlayer] SetupROIs: anim='%s' category=%d numActors=%u participants=%zu",
		p_animInfo->m_name,
		(int) m_category,
		numActors,
		m_participants.size()
	);

	for (size_t p = 0; p < m_participants.size(); p++) {
		SDL_Log(
			"[ScenePlayer]   participant[%zu]: roi='%s' charIndex=%d isSpectator=%d charName='%s'",
			p,
			m_participants[p].roi->GetName(),
			(int) m_participants[p].charIndex,
			(int) m_participants[p].isSpectator,
			(m_participants[p].charIndex >= 0 && m_participants[p].charIndex < (int8_t) sizeOfArray(g_characters))
				? g_characters[m_participants[p].charIndex].m_name
				: "(spectator)"
		);
	}

	// Track which participants have been matched to animation actors
	std::vector<bool> participantMatched(m_participants.size(), false);

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

		SDL_Log(
			"[ScenePlayer]   actor[%u]: raw='%s' lowered='%s' type=%u",
			i,
			actorName,
			lowered.c_str(),
			actorType
		);

		// For character actors, check if any participant fills this role
		if (actorType == LegoAnimActorEntry::e_managedLegoActor) {
			bool matched = false;

			for (size_t p = 0; p < m_participants.size(); p++) {
				if (participantMatched[p] || m_participants[p].isSpectator) {
					continue;
				}

				const char* charName =
					(m_participants[p].charIndex >= 0 &&
					 m_participants[p].charIndex < (int8_t) sizeOfArray(g_characters))
					? g_characters[m_participants[p].charIndex].m_name
					: "(none)";

				SDL_Log(
					"[ScenePlayer]     trying participant[%zu] charIndex=%d charName='%s' vs actor='%s' => %s",
					p,
					(int) m_participants[p].charIndex,
					charName,
					lowered.c_str(),
					MatchesCharacter(lowered, m_participants[p].charIndex) ? "MATCH" : "no match"
				);

				if (MatchesCharacter(lowered, m_participants[p].charIndex)) {
					participantMatched[p] = true;
					matched = true;
					// Map animation name → participant ROI via alias (no rename needed)
					aliasNames.push_back(lowered);
					AnimUtils::ROIAlias alias;
					alias.animName = aliasNames.back().c_str();
					alias.roi = m_participants[p].roi;
					aliases.push_back(alias);
					SDL_Log("[ScenePlayer]     => matched participant[%zu] to '%s' (alias)", p, lowered.c_str());
					break;
				}
			}

			if (matched) {
				continue;
			}

			SDL_Log("[ScenePlayer]     => no participant match, creating clone for '%s'", lowered.c_str());

			// No participant matched — create a clone for this character
			char uniqueName[64];
			SDL_snprintf(uniqueName, sizeof(uniqueName), "npc_char_%s", lowered.c_str());

			LegoROI* roi = CharacterCloner::Clone(CharacterManager(), uniqueName, lowered.c_str());
			if (roi) {
				roi->SetName(lowered.c_str());
				VideoManager()->Get3DManager()->Add(*roi);
				createdROIs.push_back(roi);
			}
		}
		else if (
			actorType == LegoAnimActorEntry::e_managedInvisibleRoiTrimmed ||
			actorType == LegoAnimActorEntry::e_sceneRoi1 ||
			actorType == LegoAnimActorEntry::e_sceneRoi2
		) {
			// Prop with digit-trimmed LOD name
			char uniqueName[64];
			SDL_snprintf(uniqueName, sizeof(uniqueName), "npc_prop_%s", lowered.c_str());
			LegoROI* roi =
				CharacterManager()->CreateAutoROI(uniqueName, AnimUtils::TrimLODSuffix(lowered).c_str(), FALSE);
			if (roi) {
				roi->SetName(lowered.c_str());
				createdROIs.push_back(roi);
			}
		}
		else if (actorType == LegoAnimActorEntry::e_managedInvisibleRoi) {
			// Prop with exact name
			char uniqueName[64];
			SDL_snprintf(uniqueName, sizeof(uniqueName), "npc_prop_%s", lowered.c_str());
			LegoROI* roi = CharacterManager()->CreateAutoROI(uniqueName, lowered.c_str(), FALSE);
			if (roi) {
				roi->SetName(lowered.c_str());
				createdROIs.push_back(roi);
			}
		}
		else {
			// Type 0/1: create as prop. If creation fails, reuse the vehicle ROI.
			char uniqueName[64];
			SDL_snprintf(uniqueName, sizeof(uniqueName), "npc_prop_%s", lowered.c_str());
			LegoROI* roi =
				CharacterManager()->CreateAutoROI(uniqueName, AnimUtils::TrimLODSuffix(lowered).c_str(), FALSE);
			if (roi) {
				roi->SetName(lowered.c_str());
				createdROIs.push_back(roi);
			}
			else if (p_vehicleROI && !m_vehicleROI) {
				m_vehicleROI = p_vehicleROI;
				m_savedVehicleName = p_vehicleROI->GetName();
				p_vehicleROI->SetName(lowered.c_str());
			}
		}
	}

	if (!createdROIs.empty()) {
		m_propCount = (uint8_t) createdROIs.size();
		m_propROIs = new LegoROI*[m_propCount];
		for (uint8_t i = 0; i < m_propCount; i++) {
			m_propROIs[i] = createdROIs[i];
		}
	}

	// Build ROI map with all participant ROIs and props as extras.
	// Find the root ROI: the first non-spectator participant that was matched.
	LegoROI* rootROI = nullptr;
	size_t rootParticipantIdx = SIZE_MAX;
	for (size_t p = 0; p < m_participants.size(); p++) {
		if (!m_participants[p].isSpectator && participantMatched[p]) {
			rootROI = m_participants[p].roi;
			rootParticipantIdx = p;
			break;
		}
	}

	if (!rootROI && !m_participants.empty()) {
		rootROI = m_participants[0].roi;
		rootParticipantIdx = 0;
	}

	if (!rootROI) {
		return;
	}

	m_animRootROI = rootROI;

	// Collect all extra ROIs (other participants + props + vehicle)
	std::vector<LegoROI*> extras;
	for (size_t p = 0; p < m_participants.size(); p++) {
		if (m_participants[p].roi != rootROI && participantMatched[p]) {
			extras.push_back(m_participants[p].roi);
		}
	}
	for (uint8_t i = 0; i < m_propCount; i++) {
		extras.push_back(m_propROIs[i]);
	}
	if (m_vehicleROI) {
		extras.push_back(m_vehicleROI);
	}

	delete[] m_roiMap;
	m_roiMap = nullptr;
	m_roiMapSize = 0;

	SDL_Log(
		"[ScenePlayer] BuildROIMap: rootROI='%s' extraCount=%zu",
		rootROI->GetName(),
		extras.size()
	);
	for (size_t e = 0; e < extras.size(); e++) {
		SDL_Log("[ScenePlayer]   extra[%zu]: '%s'", e, extras[e]->GetName());
	}

	AnimUtils::BuildROIMap(
		m_currentData->anim,
		rootROI,
		extras.empty() ? nullptr : extras.data(),
		(int) extras.size(),
		m_roiMap,
		m_roiMapSize,
		aliases.empty() ? nullptr : aliases.data(),
		(int) aliases.size()
	);

	SDL_Log("[ScenePlayer] roiMap built: size=%u", m_roiMapSize);
	if (m_roiMap) {
		bool foundRoot = false;
		for (MxU32 i = 1; i < m_roiMapSize; i++) {
			SDL_Log(
				"[ScenePlayer]   roiMap[%u]: %s%s",
				i,
				m_roiMap[i] ? m_roiMap[i]->GetName() : "(null)",
				m_roiMap[i] == rootROI ? " [ROOT]" : ""
			);
			if (m_roiMap[i] == rootROI) {
				foundRoot = true;
			}
		}
		if (!foundRoot) {
			SDL_Log("[ScenePlayer]   WARNING: rootROI '%s' NOT found in roiMap!", rootROI->GetName());
		}
	}
}

void ScenePlayer::Play(
	const AnimInfo* p_animInfo,
	AnimCategory p_category,
	LegoROI* p_localROI,
	LegoROI* p_vehicleROI,
	const ParticipantROI* p_participants,
	uint8_t p_participantCount
)
{
	if (m_playing) {
		Stop();
	}

	if (!p_localROI || !p_animInfo) {
		return;
	}

	SceneAnimData* data = m_loader.EnsureCached(p_animInfo->m_objectId);
	if (!data || !data->anim) {
		return;
	}

	m_currentData = data;
	m_category = p_category;
	m_hideOnStop = data->hideOnStop;

	// Build participant list: local player first, then remotes
	// Find which participant entry is the local player (or create one)
	bool localIsSpectator = true;
	int8_t localCharIndex = -1;

	// Check if local player appears in the participant list
	for (uint8_t i = 0; i < p_participantCount; i++) {
		if (p_participants[i].roi == p_localROI) {
			localCharIndex = p_participants[i].charIndex;
			localIsSpectator = p_participants[i].isSpectator;
			break;
		}
	}

	// Add local player as first participant
	{
		ParticipantROI local;
		local.roi = p_localROI;
		local.savedTransform = p_localROI->GetLocal2World();
		local.savedName = p_localROI->GetName();
		local.charIndex = localCharIndex;
		local.isSpectator = localIsSpectator;
		m_participants.push_back(local);
	}

	// Add remote participants
	for (uint8_t i = 0; i < p_participantCount; i++) {
		if (p_participants[i].roi == p_localROI) {
			continue; // Already added above
		}

		ParticipantROI remote;
		remote.roi = p_participants[i].roi;
		remote.savedTransform = p_participants[i].roi->GetLocal2World();
		remote.savedName = p_participants[i].roi->GetName();
		remote.charIndex = p_participants[i].charIndex;
		remote.isSpectator = p_participants[i].isSpectator;
		m_participants.push_back(remote);
	}

	// Set up ROIs: match participants to animation actors, create props
	SetupROIs(p_animInfo, p_localROI, p_vehicleROI);

	if (!m_roiMap) {
		m_currentData = nullptr;
		m_participants.clear();
		return;
	}

	// Resolve PTATCAM ROIs from names
	ResolvePtAtCamROIs();

	// Init phoneme state — resolves target ROIs from track roiName via roiMap
	m_phonemePlayer.Init(data->phonemeTracks, m_roiMap, m_roiMapSize);

	// Create sounds upfront
	m_audioPlayer.Init(data->audioTracks);

	// Camera setup for cam_anims
	m_hasCamAnim = (m_category == e_camAnim && m_currentData->anim->GetCamAnim() != nullptr);

	if (m_category == e_camAnim) {
		// Hide spectator ROI during cam_anim playback
		for (auto& p : m_participants) {
			if (p.isSpectator) {
				p.roi->SetVisibility(FALSE);
			}
		}

		// Hide the player's ride vehicle if present — it would otherwise
		// remain visible at the pre-animation position while the player
		// is teleported to the animation location
		if (p_vehicleROI && p_vehicleROI != m_vehicleROI) {
			p_vehicleROI->SetVisibility(FALSE);
			m_hiddenVehicleROI = p_vehicleROI;
		}
	}

	m_startTime = 0;
	m_playing = true;
}

void ScenePlayer::ComputeRebaseMatrix()
{
	// Use the animation root performer (not the spectator) as the rebase anchor.
	// The root ROI is the first non-spectator participant that was matched to an
	// animation actor — its saved position becomes the origin for the animation.
	if (!m_animRootROI) {
		m_rebaseMatrix.SetIdentity();
		m_rebaseComputed = true;
		return;
	}

	// NPC anims: rebase from root performer's saved position.
	// Matches the original game passing roi->GetLocal2World() to FUN_100605e0.
	MxMatrix targetTransform;
	targetTransform.SetIdentity();
	for (const auto& p : m_participants) {
		if (p.roi == m_animRootROI) {
			targetTransform = p.savedTransform;
			break;
		}
	}

	// Find the root ROI's node in the animation tree and compute its WORLD
	// transform at time 0 by accumulating parent transforms.
	std::function<bool(LegoTreeNode*, MxMatrix&)> findOrigin = [&](LegoTreeNode* node,
																   MxMatrix& parentWorld) -> bool {
		LegoAnimNodeData* data = (LegoAnimNodeData*) node->GetData();
		MxU32 roiIdx = data ? data->GetROIIndex() : 0;

		MxMatrix localMat;
		LegoROI::CreateLocalTransform(data, 0, localMat);
		MxMatrix worldMat;
		worldMat.Product(localMat, parentWorld);

		if (roiIdx != 0 && m_roiMap[roiIdx] == m_animRootROI) {
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
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 3; c++) {
			invAnimPose0[r][c] = m_animPose0[c][r];
		}
	}
	for (int r = 0; r < 3; r++) {
		invAnimPose0[3][r] =
			-(invAnimPose0[0][r] * m_animPose0[3][0] + invAnimPose0[1][r] * m_animPose0[3][1] +
			  invAnimPose0[2][r] * m_animPose0[3][2]);
	}

	// rebaseMatrix = targetTransform * inverse(animPose0)
	m_rebaseMatrix.Product(invAnimPose0, targetTransform);
	m_rebaseComputed = true;
}

void ScenePlayer::ResolvePtAtCamROIs()
{
	m_ptAtCamROIs.clear();
	if (!m_currentData || m_currentData->ptAtCamNames.empty() || !m_roiMap) {
		return;
	}

	for (const auto& name : m_currentData->ptAtCamNames) {
		for (MxU32 i = 1; i < m_roiMapSize; i++) {
			if (m_roiMap[i] && m_roiMap[i]->GetName() &&
				!SDL_strcasecmp(name.c_str(), m_roiMap[i]->GetName())) {
				m_ptAtCamROIs.push_back(m_roiMap[i]);
				break;
			}
		}
	}
}

void ScenePlayer::ApplyPtAtCam()
{
	if (m_ptAtCamROIs.empty()) {
		return;
	}

	LegoWorld* world = CurrentWorld();
	if (!world || !world->GetCameraController()) {
		return;
	}

	// Same math as LegoAnimPresenter::PutFrame (legoanimpresenter.cpp:645-674)
	for (LegoROI* roi : m_ptAtCamROIs) {
		if (!roi) {
			continue;
		}

		MxMatrix mat(roi->GetLocal2World());

		Vector3 pos(mat[0]);
		Vector3 dir(mat[1]);
		Vector3 up(mat[2]);
		Vector3 und(mat[3]);

		float possqr = sqrt(pos.LenSquared());
		float dirsqr = sqrt(dir.LenSquared());
		float upsqr = sqrt(up.LenSquared());

		up = und;
		up -= world->GetCameraController()->GetWorldLocation();
		dir /= dirsqr;
		pos.EqualsCross(dir, up);
		pos.Unitize();
		up.EqualsCross(pos, dir);
		pos *= possqr;
		dir *= dirsqr;
		up *= upsqr;

		roi->SetLocal2World(mat);
		roi->WrappedUpdateWorldData();
	}
}

void ScenePlayer::Tick(float p_deltaTime)
{
	if (!m_playing || !m_currentData || m_participants.empty()) {
		return;
	}

	if (m_startTime == 0) {
		m_startTime = SDL_GetTicks();
	}

	// For NPC anims, force all mapped ROIs visible (existing behavior).
	// For cam anims, visibility is driven by animation morph keys
	// via ApplyAnimationTransformation.
	if (m_category == e_npcAnim && m_roiMap) {
		AnimUtils::EnsureROIMapVisibility(m_roiMap, m_roiMapSize);
	}

	float elapsed = (float) (SDL_GetTicks() - m_startTime);

	if (elapsed >= m_currentData->duration) {
		Stop();
		return;
	}

	// 1. Skeletal animation
	if (m_currentData->anim && m_roiMap) {
		if (!m_rebaseComputed) {
			if (m_category == e_camAnim) {
				// cam_anims: pass the action transform DIRECTLY as the parent (no rebase).
				// Mirrors the original game's ApplyTransformWithVisibilityAndCam(anim, time, m_transform)
				// where m_transform is computed from CalcLocalTransform(action loc/dir/up).
				// Animation keyframes are already in world space; the parent transform is
				// typically identity (loc=0, dir=forward, up=up).
				if (m_currentData->hasActionTransform) {
					Mx3DPointFloat loc(m_currentData->actionLocation[0], m_currentData->actionLocation[1], m_currentData->actionLocation[2]);
					Mx3DPointFloat dir(m_currentData->actionDirection[0], m_currentData->actionDirection[1], m_currentData->actionDirection[2]);
					Mx3DPointFloat up(m_currentData->actionUp[0], m_currentData->actionUp[1], m_currentData->actionUp[2]);
					CalcLocalTransform(loc, dir, up, m_rebaseMatrix);
				}
				else {
					m_rebaseMatrix.SetIdentity();
				}
				m_rebaseComputed = true;
			}
			else {
				// NPC anims: rebase from root performer's saved position
				ComputeRebaseMatrix();
			}
		}

		AnimUtils::ApplyTree(m_currentData->anim, m_rebaseMatrix, (LegoTime) elapsed, m_roiMap);

		// Debug: log each ROI's position after first ApplyTree
		if (!m_debugFirstTickLogged) {
			m_debugFirstTickLogged = true;
			SDL_Log("[ScenePlayer] First tick applied at elapsed=%.0f:", elapsed);
			for (MxU32 i = 1; i < m_roiMapSize; i++) {
				if (m_roiMap[i]) {
					const MxMatrix& l2w = m_roiMap[i]->GetLocal2World();
					SDL_Log(
						"[ScenePlayer]   roiMap[%u] '%s': pos=(%.1f, %.1f, %.1f) vis=%d",
						i,
						m_roiMap[i]->GetName(),
						l2w[3][0], l2w[3][1], l2w[3][2],
						(int) m_roiMap[i]->GetVisibility()
					);
				}
			}
		}
	}

	// 2. Camera animation (cam_anim only)
	// Mirrors original ApplyTransformWithVisibilityAndCam: camera transform
	// uses the same parent transform (m_rebaseMatrix) as skeletal animation.
	if (m_hasCamAnim) {
		MxMatrix camTransform(m_rebaseMatrix);
		m_currentData->anim->GetCamAnim()->CalculateCameraTransform((LegoFloat) elapsed, camTransform);

		LegoWorld* world = CurrentWorld();
		if (world && world->GetCameraController()) {
			world->GetCameraController()->TransformPointOfView(camTransform, FALSE);
		}
	}

	// 3. PTATCAM post-processing
	ApplyPtAtCam();

	// 4. Audio — use the root performer ROI's real name for 3D spatialization.
	// Since we no longer rename participant ROIs, names like "tp_display" or
	// "pepper_mp_12" won't trigger IsActor() in Lego3DSound and will correctly
	// resolve via FindROI to the ROI at the animation position.
	const char* audioROIName = m_animRootROI ? m_animRootROI->GetName() : nullptr;
	m_audioPlayer.Tick(elapsed, audioROIName);

	// 5. Phoneme frames
	m_phonemePlayer.Tick(elapsed, m_currentData->phonemeTracks);
}

void ScenePlayer::Stop()
{
	if (!m_playing) {
		return;
	}

	m_audioPlayer.Cleanup();
	m_phonemePlayer.Cleanup();

	// HIDE_ON_STOP: hide all ROIs in the map (matches original game behavior)
	if (m_hideOnStop && m_roiMap) {
		for (MxU32 i = 1; i < m_roiMapSize; i++) {
			if (m_roiMap[i]) {
				m_roiMap[i]->SetVisibility(FALSE);
			}
		}
	}

	// Restore hidden vehicle ROI visibility
	if (m_hiddenVehicleROI) {
		m_hiddenVehicleROI->SetVisibility(TRUE);
		m_hiddenVehicleROI = nullptr;
	}

	CleanupProps();
	RestoreVehicleROI();

	delete[] m_roiMap;
	m_roiMap = nullptr;
	m_roiMapSize = 0;

	// Restore all participant ROIs
	for (auto& p : m_participants) {
		p.roi->WrappedSetLocal2WorldWithWorldDataUpdate(p.savedTransform);
		p.roi->SetVisibility(TRUE);
	}
	m_participants.clear();

	m_ptAtCamROIs.clear();
	m_playing = false;
	m_rebaseComputed = false;
	m_currentData = nullptr;
	m_animRootROI = nullptr;
	m_hasCamAnim = false;
	m_startTime = 0;
	m_hideOnStop = false;
	m_debugFirstTickLogged = false;
}

void ScenePlayer::CleanupProps()
{
	for (uint8_t i = 0; i < m_propCount; i++) {
		if (m_propROIs[i]) {
			// Skip borrowed vehicle ROI — it belongs to the ride animation system
			if (m_propROIs[i] == m_vehicleROI) {
				continue;
			}

			// Use ReleaseAutoROI (not ReleaseActor) because these are
			// independent clones that must not touch g_actorInfo[].
			CharacterManager()->ReleaseAutoROI(m_propROIs[i]);
		}
	}
	delete[] m_propROIs;
	m_propROIs = nullptr;
	m_propCount = 0;
}

void ScenePlayer::RestoreVehicleROI()
{
	if (m_vehicleROI && !m_savedVehicleName.empty()) {
		m_vehicleROI->SetName(m_savedVehicleName.c_str());
		m_vehicleROI = nullptr;
		m_savedVehicleName.clear();
	}
}
