#pragma once

#include "extensions/multiplayer/animation/audioplayer.h"
#include "extensions/multiplayer/animation/loader.h"
#include "extensions/multiplayer/animation/phonemeplayer.h"
#include "mxgeometry/mxmatrix.h"
#include "mxtypes.h"

#include <cstdint>
#include <string>

class LegoROI;
struct AnimInfo;

namespace Multiplayer::Animation
{

class Controller {
public:
	Controller();
	~Controller();

	void Play(const AnimInfo* p_animInfo, LegoROI* p_executingROI, LegoROI* p_vehicleROI = nullptr);
	void Tick(float p_deltaTime);
	void Stop();
	bool IsPlaying() const { return m_playing; }

private:
	void ComputeRebaseMatrix();
	void CreateExtraROIs(const AnimInfo* p_animInfo, LegoROI* p_executingROI, LegoROI* p_vehicleROI);
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
	AnimData* m_currentData;
	LegoROI* m_executingROI;
	MxMatrix m_savedTransform;
	MxMatrix m_animPose0;
	MxMatrix m_rebaseMatrix;

	// Vehicle ROI (borrowed, renamed during playback)
	LegoROI* m_vehicleROI;
	std::string m_savedVehicleName;

	// ROI map for skeletal animation
	LegoROI** m_roiMap;
	MxU32 m_roiMapSize;

	// Extra ROIs created for the animation
	LegoROI** m_propROIs;
	uint8_t m_propCount;
};

} // namespace Multiplayer::Animation
