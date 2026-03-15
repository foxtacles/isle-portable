#include "extensions/common/npcanimplayer.h"

#include "3dmanager/lego3dmanager.h"
#include "anim/legoanim.h"
#include "extensions/common/animutils.h"
#include "extensions/common/charactercloner.h"
#include "extensions/common/pathutils.h"
#include "flic.h"
#include "legocachsound.h"
#include "legocharactermanager.h"
#include "legomain.h"
#include "legovideomanager.h"
#include "misc.h"
#include "misc/legocontainer.h"
#include "misc/legostorage.h"
#include "misc/legotree.h"
#include "mxbitmap.h"
#include "mxgeometry/mxgeometry3d.h"
#include "roi/legoroi.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <file.h>
#include <interleaf.h>

using namespace Extensions::Common;

// --- NpcAnimData ---

NpcAnimData::NpcAnimData() : anim(nullptr), duration(0.0f), boundingRadius(0.0f)
{
	centerPoint[0] = centerPoint[1] = centerPoint[2] = 0.0f;
}

NpcAnimData::~NpcAnimData()
{
	delete anim;

	for (auto& track : audioTracks) {
		delete[] track.pcmData;
	}

	for (auto& track : phonemeTracks) {
		delete[] reinterpret_cast<MxU8*>(track.flcHeader);
	}
}

NpcAnimData::NpcAnimData(NpcAnimData&& p_other) noexcept
	: anim(p_other.anim), duration(p_other.duration), boundingRadius(p_other.boundingRadius),
	  audioTracks(std::move(p_other.audioTracks)), phonemeTracks(std::move(p_other.phonemeTracks))
{
	centerPoint[0] = p_other.centerPoint[0];
	centerPoint[1] = p_other.centerPoint[1];
	centerPoint[2] = p_other.centerPoint[2];
	p_other.anim = nullptr;
}

NpcAnimData& NpcAnimData::operator=(NpcAnimData&& p_other) noexcept
{
	if (this != &p_other) {
		delete anim;
		for (auto& track : audioTracks) {
			delete[] track.pcmData;
		}
		for (auto& track : phonemeTracks) {
			delete[] reinterpret_cast<MxU8*>(track.flcHeader);
		}

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

// --- NpcAnimPlayer ---

NpcAnimPlayer::NpcAnimPlayer()
	: m_siFile(nullptr), m_interleaf(nullptr), m_siReady(false), m_playing(false),
	  m_rebaseComputed(false), m_startTime(0), m_currentData(nullptr), m_executingROI(nullptr),
	  m_vehicleROI(nullptr), m_roiMap(nullptr), m_roiMapSize(0), m_propROIs(nullptr), m_propCount(0)
{
}

NpcAnimPlayer::~NpcAnimPlayer()
{
	if (m_playing) {
		Stop();
	}
	delete m_interleaf;
	delete m_siFile;
}

bool NpcAnimPlayer::OpenSI()
{
	if (m_siReady) {
		return true;
	}

	// Path matches islefiles.cpp entry: /LEGO/Scripts/Isle/ISLE.SI
	m_siFile = new si::File();

	MxString path;
	if (!ResolveGamePath("\\lego\\scripts\\isle\\isle.si", path) ||
		!m_siFile->Open(path.GetData(), si::File::Read)) {
		SDL_Log("NpcAnimPlayer: Could not open ISLE.SI (tried HD and CD paths)");
		delete m_siFile;
		m_siFile = nullptr;
		return false;
	}

	m_interleaf = new si::Interleaf();
	if (m_interleaf->Read(m_siFile, si::Interleaf::HeaderOnly) != si::Interleaf::ERROR_SUCCESS) {
		SDL_Log("NpcAnimPlayer: Could not read ISLE.SI header");
		delete m_interleaf;
		m_interleaf = nullptr;
		m_siFile->Close();
		delete m_siFile;
		m_siFile = nullptr;
		return false;
	}

	m_siReady = true;
	SDL_Log("NpcAnimPlayer: ISLE.SI opened, %zu offset table entries", m_interleaf->GetChildCount());
	return true;
}

bool NpcAnimPlayer::ReadObject(uint32_t p_objectId)
{
	if (!m_siReady) {
		return false;
	}

	// Check if already read (type != Null means populated)
	size_t childCount = m_interleaf->GetChildCount();
	if (p_objectId >= childCount) {
		SDL_Log("NpcAnimPlayer: Object %u out of range (%zu slots)", p_objectId, childCount);
		return false;
	}

	si::Object* obj = static_cast<si::Object*>(m_interleaf->GetChildAt(p_objectId));
	if (obj->type() != si::MxOb::Null) {
		return true;
	}

	SDL_Log("NpcAnimPlayer: Reading object %u", p_objectId);
	return m_interleaf->ReadObject(m_siFile, p_objectId) == si::Interleaf::ERROR_SUCCESS;
}

bool NpcAnimPlayer::ParseAnimationChild(si::Object* p_child, NpcAnimData& p_data)
{
	auto& chunks = p_child->data_;
	if (chunks.empty()) {
		SDL_Log("NpcAnimPlayer: Animation child id=%u has no data chunks", p_child->id());
		return false;
	}

	auto& firstChunk = chunks[0];
	if (firstChunk.size() < 7 * sizeof(MxS32)) {
		SDL_Log("NpcAnimPlayer: Animation data too small (%zu bytes)", firstChunk.size());
		return false;
	}

	// Parse per LegoAnimPresenter::CreateAnim (legoanimpresenter.cpp:145-193)
	LegoMemory storage(firstChunk.data(), (LegoU32) firstChunk.size());

	MxS32 magicSig;
	if (storage.Read(&magicSig, sizeof(MxS32)) != SUCCESS || magicSig != 0x11) {
		SDL_Log("NpcAnimPlayer: Bad magic signature 0x%x (expected 0x11)", magicSig);
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
		SDL_Log("NpcAnimPlayer: LegoAnim::Read failed");
		delete p_data.anim;
		p_data.anim = nullptr;
		return false;
	}

	p_data.duration = (float) p_data.anim->GetDuration();
	SDL_Log(
		"NpcAnimPlayer: Parsed animation, duration=%.1fms, radius=%.2f, data chunks=%zu",
		p_data.duration,
		p_data.boundingRadius,
		p_child->data_.size()
	);
	return true;
}

bool NpcAnimPlayer::ParseSoundChild(si::Object* p_child, NpcAnimData& p_data)
{
	auto& chunks = p_child->data_;
	if (chunks.size() < 2) {
		SDL_Log("NpcAnimPlayer: Sound child id=%u has fewer than 2 data chunks", p_child->id());
		return false;
	}

	// In the SI streaming format:
	// - data_[0] is the raw MxWavePresenter::WaveFormat struct (24 bytes)
	//   (see MxWavePresenter::ReadyTickle which memcpy's the first chunk directly)
	// - data_[1..N] are raw PCM audio data blocks

	const auto& header = chunks[0];
	if (header.size() < sizeof(MxWavePresenter::WaveFormat)) {
		SDL_Log("NpcAnimPlayer: WAV format chunk too small (%zu bytes)", header.size());
		return false;
	}

	NpcAnimData::AudioTrack track;
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
		SDL_Log("NpcAnimPlayer: WAV has no PCM data chunks");
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

	SDL_Log(
		"NpcAnimPlayer: Parsed audio: %uHz %uch %ubit %u bytes PCM, timeOffset=%u, volume=%d",
		track.format.m_samplesPerSec,
		track.format.m_channels,
		track.format.m_bitsPerSample,
		track.pcmDataSize,
		track.timeOffset,
		track.volume
	);
	p_data.audioTracks.push_back(std::move(track));
	return true;
}

bool NpcAnimPlayer::ParsePhonemeChild(si::Object* p_child, NpcAnimData& p_data)
{
	auto& chunks = p_child->data_;
	if (chunks.size() < 2) {
		SDL_Log("NpcAnimPlayer: Phoneme child has fewer than 2 data chunks");
		return false;
	}

	NpcAnimData::PhonemeTrack track;

	// data_[0] = FLIC_HEADER
	const auto& headerChunk = chunks[0];
	if (headerChunk.size() < sizeof(FLIC_HEADER)) {
		SDL_Log("NpcAnimPlayer: Phoneme header chunk too small");
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

	SDL_Log(
		"NpcAnimPlayer: Parsed phoneme track: %ux%u, %zu frames, roi='%s', offset=%u",
		track.width,
		track.height,
		track.frameData.size(),
		track.roiName.c_str(),
		track.timeOffset
	);
	p_data.phonemeTracks.push_back(std::move(track));
	return true;
}

NpcAnimData* NpcAnimPlayer::EnsureCached(uint32_t p_objectId)
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

	SDL_Log("NpcAnimPlayer: Processing composite id=%u, %zu children", composite->id(), composite->GetChildCount());

	NpcAnimData data;
	bool hasAnim = false;

	for (size_t i = 0; i < composite->GetChildCount(); i++) {
		si::Object* child = static_cast<si::Object*>(composite->GetChildAt(i));

		SDL_Log(
			"NpcAnimPlayer:   Checking child %zu: id=%u type=%d presenter='%s' data_chunks=%zu",
			i,
			child->id(),
			(int) child->type(),
			child->presenter_.c_str(),
			child->data_.size()
		);

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
		SDL_Log("NpcAnimPlayer: No animation found in object %u", p_objectId);
		return nullptr;
	}

	auto result = m_cache.emplace(p_objectId, std::move(data));
	return &result.first->second;
}

void NpcAnimPlayer::Play(const NpcAnimEntry& p_entry, LegoROI* p_executingROI, LegoROI* p_vehicleROI)
{
	if (m_playing) {
		Stop();
	}

	if (!p_executingROI) {
		return;
	}

	NpcAnimData* data = EnsureCached(p_entry.objectId);
	if (!data || !data->anim) {
		return;
	}

	m_currentData = data;
	m_executingROI = p_executingROI;

	// Dump animation tree structure
	{
		std::function<void(LegoTreeNode*, int)> dumpNode = [&](LegoTreeNode* node, int depth) {
			LegoAnimNodeData* nd = (LegoAnimNodeData*) node->GetData();
			const char* name = nd ? nd->GetName() : "(null)";
			char indent[64] = {};
			for (int d = 0; d < depth && d < 30; d++) {
				indent[d * 2] = ' ';
				indent[d * 2 + 1] = ' ';
			}
			SDL_Log("NpcAnimPlayer: %s node '%s' children=%u", indent, name, node->GetNumChildren());
			for (LegoU32 i = 0; i < node->GetNumChildren(); i++) {
				dumpNode(node->GetChild(i), depth + 1);
			}
		};
		LegoTreeNode* root = data->anim->GetRoot();
		SDL_Log("NpcAnimPlayer: Animation tree dump:");
		dumpNode(root, 0);
	}

	// Build ROI map -- only the executing player's ROI.
	// Target character's *-prefixed nodes get index 0 (NULL) -> transforms skipped.
	AnimUtils::BuildROIMap(data->anim, p_executingROI, nullptr, 0, m_roiMap, m_roiMapSize);

	if (!m_roiMap) {
		SDL_Log("NpcAnimPlayer: Failed to build ROI map");
		m_currentData = nullptr;
		m_executingROI = nullptr;
		return;
	}

	// Create ROIs for extra actors and props using the animation's own
	// actor metadata (LegoAnimActorEntry), matching the original game's
	// CreateManagedActors/CreateSceneROIs approach. Each entry has a type
	// that tells us exactly how to create the ROI.
	std::vector<LegoROI*> createdROIs;
	{
		// Determine the player character's 2-letter prefix for matching.
		// The last 2 chars of the animation name encode the character
		// (same convention used by GetCharacterIndex).
		char playerPrefix[3] = {0};
		size_t nameLen = SDL_strlen(p_entry.name);
		if (nameLen >= 2) {
			playerPrefix[0] = p_entry.name[nameLen - 2];
			playerPrefix[1] = p_entry.name[nameLen - 1];
		}

		LegoU32 numActors = data->anim->GetNumActors();
		SDL_Log("NpcAnimPlayer: Animation has %u actors", numActors);

		for (LegoU32 i = 0; i < numActors; i++) {
			const char* actorName = data->anim->GetActorName(i);
			LegoU32 actorType = data->anim->GetActorType(i);

			if (!actorName || *actorName == '\0') {
				continue;
			}

			// Strip '*' prefix for lookup
			const char* lookupName = (*actorName == '*') ? actorName + 1 : actorName;
			std::string lowered(lookupName);
			std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::tolower);

			// Skip the player character — already have p_executingROI
			if (playerPrefix[0] && !SDL_strncasecmp(lookupName, playerPrefix, 2)) {
				SDL_Log("NpcAnimPlayer:   Actor %u: '%s' type=%u -> PLAYER (skip)", i, actorName, actorType);
				continue;
			}

			LegoROI* roi = nullptr;

			if (actorType == LegoAnimActorEntry::e_managedLegoActor) {
				// Character — use CharacterCloner for full compound ROI
				char uniqueName[64];
				SDL_snprintf(uniqueName, sizeof(uniqueName), "npc_char_%s", lowered.c_str());

				roi = CharacterCloner::Clone(CharacterManager(), uniqueName, lowered.c_str());
				if (roi) {
					roi->SetName(lowered.c_str());
					VideoManager()->Get3DManager()->Add(*roi);
					SDL_Log(
						"NpcAnimPlayer:   Actor %u: '%s' type=%u -> CHARACTER '%s'",
						i,
						actorName,
						actorType,
						lowered.c_str()
					);
				}
			}
			else if (actorType == LegoAnimActorEntry::e_managedInvisibleRoiTrimmed || actorType == LegoAnimActorEntry::e_sceneRoi1 || actorType == LegoAnimActorEntry::e_sceneRoi2) {
				// Prop with digit-trimmed LOD name
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
					SDL_Log(
						"NpcAnimPlayer:   Actor %u: '%s' type=%u -> PROP '%s' (lod='%s')",
						i,
						actorName,
						actorType,
						lowered.c_str(),
						lodName.c_str()
					);
				}
			}
			else if (actorType == LegoAnimActorEntry::e_managedInvisibleRoi) {
				// Prop with exact name
				char uniqueName[64];
				SDL_snprintf(uniqueName, sizeof(uniqueName), "npc_prop_%s", lowered.c_str());
				roi = CharacterManager()->CreateAutoROI(uniqueName, lowered.c_str(), FALSE);
				if (roi) {
					roi->SetName(lowered.c_str());
					SDL_Log(
						"NpcAnimPlayer:   Actor %u: '%s' type=%u -> PROP '%s'",
						i,
						actorName,
						actorType,
						lowered.c_str()
					);
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
					SDL_Log(
						"NpcAnimPlayer:   Actor %u: '%s' type=%u -> SCENE PROP '%s' (lod='%s')",
						i,
						actorName,
						actorType,
						lowered.c_str(),
						lodName.c_str()
					);
				}
				else if (p_vehicleROI && !m_vehicleROI) {
					// Prop creation failed — reuse the existing ride vehicle ROI
					m_vehicleROI = p_vehicleROI;
					m_savedVehicleName = p_vehicleROI->GetName();
					p_vehicleROI->SetName(lowered.c_str());
					roi = p_vehicleROI;
					SDL_Log(
						"NpcAnimPlayer:   Actor %u: '%s' type=%u -> REUSE VEHICLE (was '%s')",
						i,
						actorName,
						actorType,
						m_savedVehicleName.c_str()
					);
				}
			}

			if (roi) {
				createdROIs.push_back(roi);
			}
			else {
				SDL_Log("NpcAnimPlayer:   Actor %u: '%s' type=%u -> FAILED to create", i, actorName, actorType);
			}
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
		AnimUtils::BuildROIMap(data->anim, p_executingROI, m_propROIs, m_propCount, m_roiMap, m_roiMapSize);
	}

	// Dump final ROI map
	if (m_roiMap) {
		SDL_Log("NpcAnimPlayer: Final ROI map (%u entries):", m_roiMapSize);
		for (MxU32 ri = 0; ri < m_roiMapSize; ri++) {
			if (m_roiMap[ri]) {
				SDL_Log("NpcAnimPlayer:   roiMap[%u] = '%s'", ri, m_roiMap[ri]->GetName());
			}
			else {
				SDL_Log("NpcAnimPlayer:   roiMap[%u] = NULL", ri);
			}
		}
	}

	// Dump animation tree node ROI indices after BuildROIMap
	{
		std::function<void(LegoTreeNode*, int)> dumpIndices = [&](LegoTreeNode* node, int depth) {
			LegoAnimNodeData* nd = (LegoAnimNodeData*) node->GetData();
			const char* name = nd ? nd->GetName() : "(null)";
			MxU32 idx = nd ? nd->GetROIIndex() : 0;
			char indent[64] = {};
			for (int d = 0; d < depth && d < 30; d++) {
				indent[d * 2] = ' ';
				indent[d * 2 + 1] = ' ';
			}
			const char* roiName =
				(idx > 0 && idx < m_roiMapSize && m_roiMap[idx]) ? m_roiMap[idx]->GetName() : "(unmapped)";
			SDL_Log("NpcAnimPlayer: %s '%s' roiIdx=%u -> '%s'", indent, name, idx, roiName);
			for (LegoU32 i = 0; i < node->GetNumChildren(); i++) {
				dumpIndices(node->GetChild(i), depth + 1);
			}
		};
		SDL_Log("NpcAnimPlayer: Animation tree with ROI assignments:");
		dumpIndices(data->anim->GetRoot(), 0);
	}

	// Save initial transform
	m_savedTransform = p_executingROI->GetLocal2World();

	// Init phoneme state
	InitPhonemes(*data);

	// Create sounds upfront (during the same freeze as EnsureCached).
	for (auto& audioTrack : data->audioTracks) {
		LegoCacheSound* sound = new LegoCacheSound();
		MxString mediaSrcPath(audioTrack.mediaSrcPath.c_str());
		if (sound->Create(
				audioTrack.format,
				mediaSrcPath,
				audioTrack.volume,
				audioTrack.pcmData,
				audioTrack.pcmDataSize
			) == SUCCESS) {
			ActiveSound active;
			active.sound = sound;
			active.timeOffset = audioTrack.timeOffset;
			active.started = false;
			m_activeSounds.push_back(active);
		}
		else {
			delete sound;
		}
	}

	// Clock starts on first Tick(), not here. All setup (file I/O,
	// parsing, sound buffer creation) is complete by the time Play()
	// returns, so the first Tick() starts from a clean state.
	m_startTime = 0;
	m_playing = true;

	SDL_Log(
		"NpcAnimPlayer: Playing animation '%s' (objectId=%u, duration=%.1fms) on ROI '%s'",
		p_entry.name,
		p_entry.objectId,
		data->duration,
		p_executingROI->GetName()
	);
}

void NpcAnimPlayer::Tick(float p_deltaTime)
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
	// Apply transforms to the entire tree from root, matching the original
	// game's approach (LegoAnimPresenter::ApplyTransformWithVisibilityAndCam).
	// The original passes m_transform as the initial matrix; we compute a
	// rebase matrix that maps the animation's absolute world-space origin
	// to the player's current position.
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

		// Apply the entire animation tree with the rebase matrix.
		// This correctly transforms all characters and props from animation
		// world-space to the player's local frame.
		LegoTreeNode* root = m_currentData->anim->GetRoot();
		for (LegoU32 i = 0; i < root->GetNumChildren(); i++) {
			LegoROI::ApplyAnimationTransformation(root->GetChild(i), m_rebaseMatrix, (LegoTime) elapsed, m_roiMap);
		}
	}

	// 2. Audio -- start sounds when their time offset is reached
	for (auto& active : m_activeSounds) {
		if (!active.started && elapsed >= (float) active.timeOffset) {
			active.sound->Play(m_executingROI->GetName(), FALSE);
			active.started = true;
		}
		if (active.started) {
			active.sound->FUN_10006be0();
		}
	}

	// 3. Phoneme frames
	TickPhonemes(elapsed);
}

void NpcAnimPlayer::Stop()
{
	if (!m_playing) {
		return;
	}

	SDL_Log("NpcAnimPlayer: Stopping animation");

	CleanupSounds();
	CleanupPhonemes();
	CleanupProps();

	// Restore borrowed vehicle ROI name after cleanup (CleanupProps skips it)
	if (m_vehicleROI && !m_savedVehicleName.empty()) {
		m_vehicleROI->SetName(m_savedVehicleName.c_str());
		m_vehicleROI = nullptr;
		m_savedVehicleName.clear();
	}

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

void NpcAnimPlayer::InitPhonemes(NpcAnimData& p_data)
{
	for (auto& track : p_data.phonemeTracks) {
		PhonemeState state;
		state.originalTexture = nullptr;
		state.cachedTexture = nullptr;
		state.bitmap = nullptr;
		state.currentFrame = -1;

		if (!m_executingROI) {
			m_phonemeStates.push_back(state);
			continue;
		}

		LegoROI* head = m_executingROI->FindChildROI("head", m_executingROI);
		if (!head) {
			SDL_Log("NpcAnimPlayer: Could not find 'head' child ROI for phoneme");
			m_phonemeStates.push_back(state);
			continue;
		}

		LegoTextureInfo* originalInfo = nullptr;
		head->GetTextureInfo(originalInfo);
		if (!originalInfo) {
			SDL_Log("NpcAnimPlayer: Could not get head texture info");
			m_phonemeStates.push_back(state);
			continue;
		}
		state.originalTexture = originalInfo;

		LegoTextureInfo* cached = TextureContainer()->GetCached(originalInfo);
		if (!cached) {
			SDL_Log("NpcAnimPlayer: Could not create cached texture");
			m_phonemeStates.push_back(state);
			continue;
		}
		state.cachedTexture = cached;

		CharacterManager()->SetHeadTexture(m_executingROI, cached);

		state.bitmap = new MxBitmap();
		state.bitmap->SetSize(track.width, track.height, NULL, FALSE);

		m_phonemeStates.push_back(state);
	}
}

void NpcAnimPlayer::TickPhonemes(float p_elapsedMs)
{
	if (!m_currentData) {
		return;
	}

	for (size_t i = 0; i < m_currentData->phonemeTracks.size() && i < m_phonemeStates.size(); i++) {
		auto& track = m_currentData->phonemeTracks[i];
		auto& state = m_phonemeStates[i];

		if (!state.bitmap || !state.cachedTexture) {
			continue;
		}

		float trackElapsed = p_elapsedMs - (float) track.timeOffset;
		if (trackElapsed < 0.0f) {
			continue;
		}

		if (track.flcHeader->speed == 0) {
			continue;
		}

		int targetFrame = (int) (trackElapsed / (float) track.flcHeader->speed);
		if (targetFrame == state.currentFrame) {
			continue;
		}
		if (targetFrame >= (int) track.frameData.size()) {
			continue;
		}

		int startFrame = state.currentFrame + 1;
		if (startFrame < 0) {
			startFrame = 0;
		}

		for (int f = startFrame; f <= targetFrame; f++) {
			const auto& data = track.frameData[f];
			if (data.size() < sizeof(MxS32)) {
				continue;
			}

			MxS32 rectCount;
			SDL_memcpy(&rectCount, data.data(), sizeof(MxS32));
			size_t headerSize = sizeof(MxS32) + rectCount * sizeof(MxRect32);
			if (data.size() <= headerSize) {
				continue;
			}

			FLIC_FRAME* flcFrame = (FLIC_FRAME*) (data.data() + headerSize);

			if (f == 0) {
				SDL_Log(
					"NpcAnimPlayer: Phoneme frame 0: dataSize=%zu rectCount=%d headerSize=%zu "
					"flcFrame->size=%u flcFrame->type=0x%x flcFrame->chunks=%u",
					data.size(),
					rectCount,
					headerSize,
					flcFrame->size,
					flcFrame->type,
					flcFrame->chunks
				);
			}

			BYTE decodedColorMap;
			DecodeFLCFrame(
				&state.bitmap->GetBitmapInfo()->m_bmiHeader,
				state.bitmap->GetImage(),
				track.flcHeader,
				flcFrame,
				&decodedColorMap
			);

			// When the FLC frame updates the palette, apply it to the texture surface
			if (decodedColorMap && state.cachedTexture->m_palette) {
				PALETTEENTRY entries[256];
				RGBQUAD* colors = state.bitmap->GetBitmapInfo()->m_bmiColors;
				for (int c = 0; c < 256; c++) {
					entries[c].peRed = colors[c].rgbRed;
					entries[c].peGreen = colors[c].rgbGreen;
					entries[c].peBlue = colors[c].rgbBlue;
					entries[c].peFlags = PC_NONE;
				}
				state.cachedTexture->m_palette->SetEntries(0, 0, 256, entries);
			}
		}

		state.cachedTexture->LoadBits(state.bitmap->GetImage());
		state.currentFrame = targetFrame;
	}
}

void NpcAnimPlayer::CleanupPhonemes()
{
	for (size_t i = 0; i < m_phonemeStates.size(); i++) {
		auto& state = m_phonemeStates[i];

		if (m_executingROI && state.originalTexture) {
			// Restore original head texture by passing the saved original.
			// We can't pass NULL because SetHeadTexture(NULL) uses
			// GetActorInfo(roi->GetName()) which won't find our display ROI.
			CharacterManager()->SetHeadTexture(m_executingROI, state.originalTexture);
		}

		// Only erase the cached copy, NOT the original texture.
		if (state.cachedTexture) {
			TextureContainer()->EraseCached(state.cachedTexture);
		}

		delete state.bitmap;
	}
	m_phonemeStates.clear();
}

void NpcAnimPlayer::CleanupProps()
{
	for (uint8_t i = 0; i < m_propCount; i++) {
		if (m_propROIs[i]) {
			// Skip borrowed vehicle ROI — it belongs to the ride animation system
			if (m_propROIs[i] == m_vehicleROI) {
				SDL_Log("NpcAnimPlayer: CleanupProps: SKIPPING borrowed vehicle ROI '%s'", m_propROIs[i]->GetName());
				continue;
			}

			SDL_Log(
				"NpcAnimPlayer: CleanupProps: releasing ROI '%s' (refCount=%u)",
				m_propROIs[i]->GetName(),
				CharacterManager()->GetRefCount(m_propROIs[i])
			);
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

void NpcAnimPlayer::CleanupSounds()
{
	for (auto& active : m_activeSounds) {
		if (active.started) {
			active.sound->Stop();
		}
		delete active.sound;
	}
	m_activeSounds.clear();
}
