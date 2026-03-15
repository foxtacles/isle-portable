#pragma once

#include "mxwavepresenter.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct FLIC_HEADER;
class LegoAnim;

namespace si
{
class File;
class Interleaf;
class Object;
} // namespace si

namespace Multiplayer::Animation
{

// Extracted animation data from ISLE.SI
struct SceneAnimData {
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

	SceneAnimData();
	~SceneAnimData();

	SceneAnimData(const SceneAnimData&) = delete;
	SceneAnimData& operator=(const SceneAnimData&) = delete;
	SceneAnimData(SceneAnimData&& p_other) noexcept;
	SceneAnimData& operator=(SceneAnimData&& p_other) noexcept;

private:
	void ReleaseTracks();
};

// Owns the SI file handle, reads objects on demand, parses animation/sound/phoneme
// data, and caches results. Bypasses the streaming pipeline and its singleplayer
// side effects.
//
// SI reading strategy: reads only the RIFF header + MxOf offset table
// (contiguous at file start, one fetch). Then for each requested object,
// seeks to its MxSt offset and reads just that one object tree + data
// (one more fetch per object). This avoids the ObjectsOnly scan which
// would touch hundreds of offsets across the file.
class Loader {
public:
	Loader();
	~Loader();

	// Open ISLE.SI and read only the offset table (cheap)
	bool OpenSI();

	// Animation data extraction and caching
	SceneAnimData* EnsureCached(uint32_t p_objectId);

private:
	bool ReadObject(uint32_t p_objectId);
	bool ParseAnimationChild(si::Object* p_child, SceneAnimData& p_data);
	bool ParseSoundChild(si::Object* p_child, SceneAnimData& p_data);
	bool ParsePhonemeChild(si::Object* p_child, SceneAnimData& p_data);

	si::File* m_siFile;
	si::Interleaf* m_interleaf;
	bool m_siReady;
	std::map<uint32_t, SceneAnimData> m_cache;
};

} // namespace Multiplayer::Animation
