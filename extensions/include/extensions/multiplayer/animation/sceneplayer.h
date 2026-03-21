#pragma once

#include "extensions/multiplayer/animation/audioplayer.h"
#include "extensions/multiplayer/animation/catalog.h"
#include "extensions/multiplayer/animation/loader.h"
#include "extensions/multiplayer/animation/phonemeplayer.h"
#include "mxgeometry/mxmatrix.h"
#include "mxtypes.h"

#include <cstdint>
#include <string>
#include <vector>

class LegoROI;
struct AnimInfo;

namespace Multiplayer::Animation
{

// A participant (local or remote player) whose ROI is borrowed during animation
struct ParticipantROI {
	LegoROI* roi;
	MxMatrix savedTransform;
	std::string savedName;
	int8_t charIndex; // g_characters[] index this slot requires (-1 for spectator)
	bool isSpectator;
};

class ScenePlayer {
public:
	ScenePlayer();
	~ScenePlayer();

	void Play(
		const AnimInfo* p_animInfo,
		AnimCategory p_category,
		LegoROI* p_localROI,
		LegoROI* p_vehicleROI,
		const ParticipantROI* p_participants,
		uint8_t p_participantCount
	);
	void Tick(float p_deltaTime);
	void Stop();
	bool IsPlaying() const { return m_playing; }

private:
	void ComputeRebaseMatrix();
	void SetupROIs(const AnimInfo* p_animInfo, LegoROI* p_localROI, LegoROI* p_vehicleROI);
	void ResolvePtAtCamROIs();
	void ApplyPtAtCam();
	void CleanupProps();
	void RestoreVehicleROI();

	// Sub-components
	Loader m_loader;
	AudioPlayer m_audioPlayer;
	PhonemePlayer m_phonemePlayer;

	// Playback state
	bool m_playing;
	bool m_rebaseComputed;
	uint64_t m_startTime;
	SceneAnimData* m_currentData;
	AnimCategory m_category;
	MxMatrix m_animPose0;
	MxMatrix m_rebaseMatrix;

	// Participants (local player at index 0, remote players after)
	std::vector<ParticipantROI> m_participants;

	// The root performer ROI for the animation (used for rebase in npc_anims)
	LegoROI* m_animRootROI;

	// Vehicle ROI (borrowed, renamed during playback)
	LegoROI* m_vehicleROI;
	std::string m_savedVehicleName;

	// Player's ride vehicle ROI hidden during cam_anim (not borrowed, just hidden)
	LegoROI* m_hiddenVehicleROI;

	// ROI map for skeletal animation
	LegoROI** m_roiMap;
	MxU32 m_roiMapSize;

	// Extra ROIs created for the animation (props and unmatched characters)
	LegoROI** m_propROIs;
	uint8_t m_propCount;


	// Camera animation (cam_anim only)
	bool m_hasCamAnim;

	// PTATCAM
	std::vector<LegoROI*> m_ptAtCamROIs;

	// HIDE_ON_STOP
	bool m_hideOnStop;

	// Debug
	bool m_debugFirstTickLogged;
};

} // namespace Multiplayer::Animation
