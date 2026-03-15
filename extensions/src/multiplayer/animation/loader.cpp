#include "extensions/multiplayer/animation/loader.h"

#include "extensions/common/pathutils.h"
#include "flic.h"
#include "misc/legostorage.h"
#include "mxwavepresenter.h"

#include "anim/legoanim.h"

#include <SDL3/SDL_stdinc.h>
#include <file.h>
#include <interleaf.h>

using namespace Multiplayer::Animation;

// --- AnimData ---

AnimData::AnimData() : anim(nullptr), duration(0.0f), boundingRadius(0.0f)
{
	centerPoint[0] = centerPoint[1] = centerPoint[2] = 0.0f;
}

AnimData::~AnimData()
{
	delete anim;
	ReleaseTracks();
}

void AnimData::ReleaseTracks()
{
	for (auto& track : audioTracks) {
		delete[] track.pcmData;
	}

	for (auto& track : phonemeTracks) {
		delete[] reinterpret_cast<MxU8*>(track.flcHeader);
	}
}

AnimData::AnimData(AnimData&& p_other) noexcept
	: anim(p_other.anim), duration(p_other.duration), boundingRadius(p_other.boundingRadius),
	  audioTracks(std::move(p_other.audioTracks)), phonemeTracks(std::move(p_other.phonemeTracks))
{
	centerPoint[0] = p_other.centerPoint[0];
	centerPoint[1] = p_other.centerPoint[1];
	centerPoint[2] = p_other.centerPoint[2];
	p_other.anim = nullptr;
}

AnimData& AnimData::operator=(AnimData&& p_other) noexcept
{
	if (this != &p_other) {
		delete anim;
		ReleaseTracks();

		anim = p_other.anim;
		duration = p_other.duration;
		boundingRadius = p_other.boundingRadius;
		centerPoint[0] = p_other.centerPoint[0];
		centerPoint[1] = p_other.centerPoint[1];
		centerPoint[2] = p_other.centerPoint[2];
		audioTracks = std::move(p_other.audioTracks);
		phonemeTracks = std::move(p_other.phonemeTracks);
		p_other.anim = nullptr;
	}
	return *this;
}

// --- Loader ---

Loader::Loader() : m_siFile(nullptr), m_interleaf(nullptr), m_siReady(false)
{
}

Loader::~Loader()
{
	delete m_interleaf;
	delete m_siFile;
}

bool Loader::OpenSI()
{
	if (m_siReady) {
		return true;
	}

	// Path matches islefiles.cpp entry: /LEGO/Scripts/Isle/ISLE.SI
	m_siFile = new si::File();

	MxString path;
	if (!Extensions::Common::ResolveGamePath("\\lego\\scripts\\isle\\isle.si", path) ||
		!m_siFile->Open(path.GetData(), si::File::Read)) {
		delete m_siFile;
		m_siFile = nullptr;
		return false;
	}

	m_interleaf = new si::Interleaf();
	if (m_interleaf->Read(m_siFile, si::Interleaf::HeaderOnly) != si::Interleaf::ERROR_SUCCESS) {
		delete m_interleaf;
		m_interleaf = nullptr;
		m_siFile->Close();
		delete m_siFile;
		m_siFile = nullptr;
		return false;
	}

	m_siReady = true;
	return true;
}

bool Loader::ReadObject(uint32_t p_objectId)
{
	if (!m_siReady) {
		return false;
	}

	size_t childCount = m_interleaf->GetChildCount();
	if (p_objectId >= childCount) {
		return false;
	}

	si::Object* obj = static_cast<si::Object*>(m_interleaf->GetChildAt(p_objectId));
	if (obj->type() != si::MxOb::Null) {
		return true;
	}

	return m_interleaf->ReadObject(m_siFile, p_objectId) == si::Interleaf::ERROR_SUCCESS;
}

bool Loader::ParseAnimationChild(si::Object* p_child, AnimData& p_data)
{
	auto& chunks = p_child->data_;
	if (chunks.empty()) {
		return false;
	}

	auto& firstChunk = chunks[0];
	if (firstChunk.size() < 7 * sizeof(MxS32)) {
		return false;
	}

	// Parse per LegoAnimPresenter::CreateAnim (legoanimpresenter.cpp:145-193)
	LegoMemory storage(firstChunk.data(), (LegoU32) firstChunk.size());

	MxS32 magicSig;
	if (storage.Read(&magicSig, sizeof(MxS32)) != SUCCESS || magicSig != 0x11) {
		return false;
	}

	if (storage.Read(&p_data.boundingRadius, sizeof(float)) != SUCCESS) {
		return false;
	}
	if (storage.Read(&p_data.centerPoint[0], sizeof(float)) != SUCCESS) {
		return false;
	}
	if (storage.Read(&p_data.centerPoint[1], sizeof(float)) != SUCCESS) {
		return false;
	}
	if (storage.Read(&p_data.centerPoint[2], sizeof(float)) != SUCCESS) {
		return false;
	}

	LegoS32 parseScene = 0;
	MxS32 val3;
	if (storage.Read(&parseScene, sizeof(LegoS32)) != SUCCESS) {
		return false;
	}
	if (storage.Read(&val3, sizeof(MxS32)) != SUCCESS) {
		return false;
	}

	p_data.anim = new LegoAnim();
	if (p_data.anim->Read(&storage, parseScene) != SUCCESS) {
		delete p_data.anim;
		p_data.anim = nullptr;
		return false;
	}

	p_data.duration = (float) p_data.anim->GetDuration();
	return true;
}

bool Loader::ParseSoundChild(si::Object* p_child, AnimData& p_data)
{
	auto& chunks = p_child->data_;
	if (chunks.size() < 2) {
		return false;
	}

	// In the SI streaming format:
	// - data_[0] is the raw MxWavePresenter::WaveFormat struct (24 bytes)
	//   (see MxWavePresenter::ReadyTickle which memcpy's the first chunk directly)
	// - data_[1..N] are raw PCM audio data blocks

	const auto& header = chunks[0];
	if (header.size() < sizeof(MxWavePresenter::WaveFormat)) {
		return false;
	}

	AnimData::AudioTrack track;
	SDL_memcpy(&track.format, header.data(), sizeof(MxWavePresenter::WaveFormat));
	track.pcmData = nullptr;
	track.pcmDataSize = 0;
	track.volume = (int32_t) p_child->volume_;
	track.timeOffset = p_child->time_offset_;
	track.mediaSrcPath = p_child->filename_;

	// Concatenate data_[1..N] as raw PCM
	MxU32 totalPcm = 0;
	for (size_t i = 1; i < chunks.size(); i++) {
		totalPcm += (MxU32) chunks[i].size();
	}

	if (totalPcm == 0) {
		return false;
	}

	track.pcmData = new MxU8[totalPcm];
	track.pcmDataSize = totalPcm;
	track.format.m_dataSize = totalPcm;
	MxU32 offset = 0;
	for (size_t i = 1; i < chunks.size(); i++) {
		SDL_memcpy(track.pcmData + offset, chunks[i].data(), chunks[i].size());
		offset += (MxU32) chunks[i].size();
	}

	p_data.audioTracks.push_back(std::move(track));
	return true;
}

bool Loader::ParsePhonemeChild(si::Object* p_child, AnimData& p_data)
{
	auto& chunks = p_child->data_;
	if (chunks.size() < 2) {
		return false;
	}

	AnimData::PhonemeTrack track;

	// data_[0] = FLIC_HEADER
	const auto& headerChunk = chunks[0];
	if (headerChunk.size() < sizeof(FLIC_HEADER)) {
		return false;
	}

	MxU8* headerBuf = new MxU8[headerChunk.size()];
	SDL_memcpy(headerBuf, headerChunk.data(), headerChunk.size());
	track.flcHeader = reinterpret_cast<FLIC_HEADER*>(headerBuf);
	track.width = track.flcHeader->width;
	track.height = track.flcHeader->height;

	// data_[1..N] = frame chunks
	for (size_t i = 1; i < chunks.size(); i++) {
		track.frameData.push_back(chunks[i]);
	}

	// extra_ = ROI name
	if (!p_child->extra_.empty()) {
		track.roiName = std::string(p_child->extra_.data(), p_child->extra_.size());
		// Trim null terminators
		while (!track.roiName.empty() && track.roiName.back() == '\0') {
			track.roiName.pop_back();
		}
	}

	track.timeOffset = p_child->time_offset_;

	p_data.phonemeTracks.push_back(std::move(track));
	return true;
}

AnimData* Loader::EnsureCached(uint32_t p_objectId)
{
	auto it = m_cache.find(p_objectId);
	if (it != m_cache.end()) {
		return &it->second;
	}

	if (!OpenSI()) {
		return nullptr;
	}

	if (!ReadObject(p_objectId)) {
		return nullptr;
	}

	si::Object* composite = static_cast<si::Object*>(m_interleaf->GetChildAt(p_objectId));

	AnimData data;
	bool hasAnim = false;

	for (size_t i = 0; i < composite->GetChildCount(); i++) {
		si::Object* child = static_cast<si::Object*>(composite->GetChildAt(i));

		// Match children by presenter name (types vary: Animation=9, Object=11, Video=3, Sound=4)
		if (child->presenter_.find("LegoPhonemePresenter") != std::string::npos) {
			ParsePhonemeChild(child, data);
		}
		else if (child->presenter_.find("LegoAnimPresenter") != std::string::npos || child->presenter_.find("LegoLoopingAnimPresenter") != std::string::npos) {
			if (!hasAnim) {
				if (ParseAnimationChild(child, data)) {
					hasAnim = true;
				}
			}
		}
		else if (child->filetype() == si::MxOb::WAV) {
			ParseSoundChild(child, data);
		}
	}

	if (!hasAnim) {
		return nullptr;
	}

	auto result = m_cache.emplace(p_objectId, std::move(data));
	return &result.first->second;
}
