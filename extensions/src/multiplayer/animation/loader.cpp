#include "extensions/multiplayer/animation/loader.h"

#include "anim/legoanim.h"
#include "extensions/common/pathutils.h"
#include "flic.h"
#include "misc/legostorage.h"
#include "mxwavepresenter.h"

#include <SDL3/SDL_stdinc.h>
#include <file.h>
#include <interleaf.h>

using namespace Multiplayer::Animation;

static void ParseExtraDirectives(const si::bytearray& p_extra, SceneAnimData& p_data)
{
	if (p_extra.empty()) {
		return;
	}

	std::string extra(p_extra.data(), p_extra.size());
	while (!extra.empty() && extra.back() == '\0') {
		extra.pop_back();
	}

	if (extra.find("HIDE_ON_STOP") != std::string::npos) {
		p_data.hideOnStop = true;
	}

	size_t pos = extra.find("PTATCAM=");
	if (pos != std::string::npos) {
		pos += 8;
		size_t end = extra.find(' ', pos);
		std::string value = (end != std::string::npos) ? extra.substr(pos, end - pos) : extra.substr(pos);

		size_t start = 0;
		while (start < value.size()) {
			size_t delim = value.find_first_of(":;", start);
			std::string token =
				(delim != std::string::npos) ? value.substr(start, delim - start) : value.substr(start);

			if (!token.empty()) {
				p_data.ptAtCamNames.push_back(token);
			}

			start = (delim != std::string::npos) ? delim + 1 : value.size();
		}
	}
}

SceneAnimData::SceneAnimData() : anim(nullptr), duration(0.0f), actionTransform{}, hideOnStop(false)
{
}

SceneAnimData::~SceneAnimData()
{
	delete anim;
	ReleaseTracks();
}

void SceneAnimData::ReleaseTracks()
{
	for (auto& track : audioTracks) {
		delete[] track.pcmData;
	}

	for (auto& track : phonemeTracks) {
		delete[] reinterpret_cast<MxU8*>(track.flcHeader);
	}
}

SceneAnimData::SceneAnimData(SceneAnimData&& p_other) noexcept
	: anim(p_other.anim), duration(p_other.duration), audioTracks(std::move(p_other.audioTracks)),
	  phonemeTracks(std::move(p_other.phonemeTracks)), actionTransform(p_other.actionTransform),
	  ptAtCamNames(std::move(p_other.ptAtCamNames)), hideOnStop(p_other.hideOnStop)
{
	p_other.anim = nullptr;
}

SceneAnimData& SceneAnimData::operator=(SceneAnimData&& p_other) noexcept
{
	if (this != &p_other) {
		delete anim;
		ReleaseTracks();

		anim = p_other.anim;
		duration = p_other.duration;
		audioTracks = std::move(p_other.audioTracks);
		phonemeTracks = std::move(p_other.phonemeTracks);
		actionTransform = p_other.actionTransform;
		ptAtCamNames = std::move(p_other.ptAtCamNames);
		hideOnStop = p_other.hideOnStop;
		p_other.anim = nullptr;
	}
	return *this;
}

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

bool Loader::ParseAnimationChild(si::Object* p_child, SceneAnimData& p_data)
{
	auto& chunks = p_child->data_;
	if (chunks.empty()) {
		return false;
	}

	auto& firstChunk = chunks[0];
	if (firstChunk.size() < 7 * sizeof(MxS32)) {
		return false;
	}

	LegoMemory storage(firstChunk.data(), (LegoU32) firstChunk.size());

	MxS32 magicSig;
	if (storage.Read(&magicSig, sizeof(MxS32)) != SUCCESS || magicSig != 0x11) {
		return false;
	}

	// Skip boundingRadius + centerPoint[3] (unused, but present in the binary format)
	LegoU32 pos;
	storage.GetPosition(pos);
	storage.SetPosition(pos + 4 * sizeof(float));

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

bool Loader::ParseSoundChild(si::Object* p_child, SceneAnimData& p_data)
{
	auto& chunks = p_child->data_;
	if (chunks.size() < 2) {
		return false;
	}

	// data_[0] = WaveFormat header, data_[1..N] = raw PCM blocks
	const auto& header = chunks[0];
	if (header.size() < sizeof(MxWavePresenter::WaveFormat)) {
		return false;
	}

	SceneAnimData::AudioTrack track;
	SDL_memcpy(&track.format, header.data(), sizeof(MxWavePresenter::WaveFormat));
	track.pcmData = nullptr;
	track.pcmDataSize = 0;
	track.volume = (int32_t) p_child->volume_;
	track.timeOffset = p_child->time_offset_;
	track.mediaSrcPath = p_child->filename_;

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

bool Loader::ParsePhonemeChild(si::Object* p_child, SceneAnimData& p_data)
{
	auto& chunks = p_child->data_;
	if (chunks.size() < 2) {
		return false;
	}

	SceneAnimData::PhonemeTrack track;

	const auto& headerChunk = chunks[0];
	if (headerChunk.size() < sizeof(FLIC_HEADER)) {
		return false;
	}

	MxU8* headerBuf = new MxU8[headerChunk.size()];
	SDL_memcpy(headerBuf, headerChunk.data(), headerChunk.size());
	track.flcHeader = reinterpret_cast<FLIC_HEADER*>(headerBuf);
	track.width = track.flcHeader->width;
	track.height = track.flcHeader->height;

	for (size_t i = 1; i < chunks.size(); i++) {
		track.frameData.push_back(chunks[i]);
	}

	if (!p_child->extra_.empty()) {
		track.roiName = std::string(p_child->extra_.data(), p_child->extra_.size());
		while (!track.roiName.empty() && track.roiName.back() == '\0') {
			track.roiName.pop_back();
		}
	}

	track.timeOffset = p_child->time_offset_;

	p_data.phonemeTracks.push_back(std::move(track));
	return true;
}

SceneAnimData* Loader::EnsureCached(uint32_t p_objectId)
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

	SceneAnimData data;
	bool hasAnim = false;

	for (size_t i = 0; i < composite->GetChildCount(); i++) {
		si::Object* child = static_cast<si::Object*>(composite->GetChildAt(i));

		if (child->presenter_.find("LegoPhonemePresenter") != std::string::npos) {
			ParsePhonemeChild(child, data);
		}
		else if (
			child->presenter_.find("LegoAnimPresenter") != std::string::npos ||
			child->presenter_.find("LegoLoopingAnimPresenter") != std::string::npos
		) {
			if (!hasAnim) {
				if (ParseAnimationChild(child, data)) {
					hasAnim = true;
					ParseExtraDirectives(child->extra_, data);

					// Extract action transform. Try child first, fall back to composite if zero.
					si::Object* source = child;
					if (SDL_fabs(child->direction_.x) < 1e-7 && SDL_fabs(child->direction_.y) < 1e-7 &&
						SDL_fabs(child->direction_.z) < 1e-7) {
						source = composite;
					}

					data.actionTransform.location[0] = (float) source->location_.x;
					data.actionTransform.location[1] = (float) source->location_.y;
					data.actionTransform.location[2] = (float) source->location_.z;
					data.actionTransform.direction[0] = (float) source->direction_.x;
					data.actionTransform.direction[1] = (float) source->direction_.y;
					data.actionTransform.direction[2] = (float) source->direction_.z;
					data.actionTransform.up[0] = (float) source->up_.x;
					data.actionTransform.up[1] = (float) source->up_.y;
					data.actionTransform.up[2] = (float) source->up_.z;

					data.actionTransform.valid =
						(SDL_fabsf(data.actionTransform.direction[0]) >= 0.00000047683716f ||
						 SDL_fabsf(data.actionTransform.direction[1]) >= 0.00000047683716f ||
						 SDL_fabsf(data.actionTransform.direction[2]) >= 0.00000047683716f);
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
