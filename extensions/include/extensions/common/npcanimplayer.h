#pragma once

#include "extensions/common/npcanimcatalog.h"
#include "mxgeometry/mxmatrix.h"
#include "mxtypes.h"
#include "mxwavepresenter.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct FLIC_HEADER;
class LegoAnim;
class LegoROI;
class LegoCacheSound;
class LegoTextureInfo;
class MxBitmap;

namespace si
{
class File;
class Object;
} // namespace si

namespace Extensions
{
namespace Common
{

// Extracted NPC animation data from ISLE.SI
struct NpcAnimData {
	LegoAnim* anim;
	float duration;
	float boundingRadius;
	float centerPoint[3];

	struct AudioTrack {
		MxU8* pcmData;
		MxU32 pcmDataSize;
		MxWavePresenter::WaveFormat format;
		std::string mediaSrcPath;
		int32_t volume;
		uint32_t timeOffset;
	};
	std::vector<AudioTrack> audioTracks;

	struct PhonemeTrack {
		FLIC_HEADER* flcHeader;
		std::vector<std::vector<char>> frameData; // data_[1..N] raw (includes rect prefix)
		uint32_t timeOffset;
		std::string roiName;
		uint16_t width, height;
	};
	std::vector<PhonemeTrack> phonemeTracks;

	NpcAnimData();
	~NpcAnimData();

	NpcAnimData(const NpcAnimData&) = delete;
	NpcAnimData& operator=(const NpcAnimData&) = delete;
	NpcAnimData(NpcAnimData&& p_other) noexcept;
	NpcAnimData& operator=(NpcAnimData&& p_other) noexcept;
};

// Phoneme playback state for a single track
struct PhonemeState {
	LegoTextureInfo* originalTexture;
	LegoTextureInfo* cachedTexture;
	MxBitmap* bitmap;
	int32_t currentFrame;
};

// Plays NPC interaction animations directly on a player's ROI using data
// extracted from ISLE.SI via libweaver. Bypasses the streaming pipeline
// and its singleplayer side effects.
//
// SI reading strategy: reads only the RIFF header + MxOf offset table
// (contiguous at file start, one fetch). Then for each requested object,
// seeks to its MxSt offset and reads just that one object tree + data
// (one more fetch per object). This avoids the ObjectsOnly scan which
// would touch hundreds of offsets across the file.
class NpcAnimPlayer {
public:
	NpcAnimPlayer();
	~NpcAnimPlayer();

	// Open ISLE.SI and read only the offset table (cheap)
	bool OpenSI();

	// Read a specific object's MxSt and extract its data
	bool ReadObject(uint32_t p_objectId);

	// Animation data extraction and caching
	NpcAnimData* EnsureCached(uint32_t p_objectId);

	// Playback control. p_vehicleROI is the existing ride vehicle ROI
	// (if the player is on a small vehicle), or nullptr.
	void Play(const NpcAnimEntry& p_entry, LegoROI* p_executingROI, LegoROI* p_vehicleROI = nullptr);
	void Tick(float p_deltaTime);
	void Stop();

	bool IsPlaying() const { return m_playing; }

private:
	// SI file state
	si::File* m_siFile;
	bool m_siReady;
	uint32_t m_bufferSize; // SI interleave buffer size from MxHd

	// Offset table: objectId (slot index) -> file offset of MxSt
	std::vector<uint32_t> m_offsetTable;

	// Per-object parsed trees (only for objects we've actually read)
	struct ObjectTree {
		si::Object* root; // Composite object with children
		ObjectTree() : root(nullptr) {}
		~ObjectTree();
		ObjectTree(const ObjectTree&) = delete;
		ObjectTree& operator=(const ObjectTree&) = delete;
		ObjectTree(ObjectTree&& p_other) noexcept;
		ObjectTree& operator=(ObjectTree&& p_other) noexcept;
	};
	std::map<uint32_t, ObjectTree> m_objectTrees;

	// Cached animation data keyed by objectId
	std::map<uint32_t, NpcAnimData> m_cache;

	// Playback state
	bool m_playing;
	bool m_rebaseComputed;
	uint64_t m_startTime; // SDL_GetTicks() at first Tick() — wall-clock to match miniaudio
	NpcAnimData* m_currentData;
	LegoROI* m_executingROI;
	MxMatrix m_savedTransform;
	MxMatrix m_animPose0;    // Player character's animation transform at time 0
	MxMatrix m_rebaseMatrix; // Transform from animation world-space to player's local frame

	// Borrowed vehicle ROI (renamed during playback, restored on Stop)
	LegoROI* m_vehicleROI;
	std::string m_savedVehicleName;

	// ROI map for skeletal animation
	LegoROI** m_roiMap;
	MxU32 m_roiMapSize;

	// Extra ROIs created for the animation (characters + props).
	// Characters (CharacterCloner::Clone) are released with ReleaseActor.
	// Props (CreateAutoROI) are released with ReleaseAutoROI.
	// Distinguished at cleanup via CharacterManager()->Exists().
	LegoROI** m_propROIs;
	uint8_t m_propCount;

	// Audio playback
	struct ActiveSound {
		LegoCacheSound* sound;
		uint32_t timeOffset;
		bool started;
	};
	std::vector<ActiveSound> m_activeSounds;

	// Phoneme playback
	std::vector<PhonemeState> m_phonemeStates;

	// SI parsing helpers
	bool ReadSIHeader();
	bool ReadMxSt(uint32_t p_offset, si::Object* p_root);
	si::Object* ReadMxOb(si::File* p_file);
	void ReadDataChunks(si::File* p_file, uint32_t p_stEnd, si::Object* p_root);

	// Animation parsing helpers
	bool ParseAnimationChild(si::Object* p_child, NpcAnimData& p_data);
	bool ParseSoundChild(si::Object* p_child, NpcAnimData& p_data);
	bool ParsePhonemeChild(si::Object* p_child, NpcAnimData& p_data);

	void InitPhonemes(NpcAnimData& p_data);
	void TickPhonemes(float p_elapsedMs);
	void CleanupPhonemes();
	void CleanupProps();
	void CleanupSounds();
};

} // namespace Common
} // namespace Extensions
