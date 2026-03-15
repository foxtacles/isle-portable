#include "extensions/common/npcanimplayer.h"

#include "3dmanager/lego3dmanager.h"
#include "anim/legoanim.h"
#include "extensions/common/animutils.h"
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

#include <SDL3/SDL_timer.h>

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <file.h>
#include <object.h>
#include <sitypes.h>

using namespace Extensions::Common;

// RIFF chunk IDs (matching libweaver sitypes.h)
static const uint32_t RIFF_ID = 0x46464952;
static const uint32_t OMNI_ID = 0x494e4d4f;
static const uint32_t LIST_ID = 0x5453494c;
static const uint32_t MxHd_ID = 0x6448784d;
static const uint32_t MxOf_ID = 0x664f784d;
static const uint32_t MxSt_ID = 0x7453784d;
static const uint32_t MxOb_ID = 0x624f784d;
static const uint32_t MxCh_ID = 0x6843784d;
static const uint32_t MxDa_ID = 0x6144784d;

// MxCh header size (flags + object + time + data_sz = 2+4+4+4 = 14)
static const uint32_t MXCH_HEADER_SIZE = 14;

// MxCh flags
static const uint16_t FLAG_SPLIT = 0x10;
static const uint16_t FLAG_END = 0x02;

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

// --- ObjectTree ---

NpcAnimPlayer::ObjectTree::~ObjectTree()
{
	delete root;
}

NpcAnimPlayer::ObjectTree::ObjectTree(ObjectTree&& p_other) noexcept : root(p_other.root)
{
	p_other.root = nullptr;
}

NpcAnimPlayer::ObjectTree& NpcAnimPlayer::ObjectTree::operator=(ObjectTree&& p_other) noexcept
{
	if (this != &p_other) {
		delete root;
		root = p_other.root;
		p_other.root = nullptr;
	}
	return *this;
}

// --- NpcAnimPlayer ---

NpcAnimPlayer::NpcAnimPlayer()
	: m_siFile(nullptr), m_siReady(false), m_bufferSize(0), m_playing(false), m_startTime(0),
	  m_currentData(nullptr), m_executingROI(nullptr), m_roiMap(nullptr), m_roiMapSize(0), m_propROIs(nullptr),
	  m_propCount(0)
{
}

NpcAnimPlayer::~NpcAnimPlayer()
{
	if (m_playing) {
		Stop();
	}
	delete m_siFile;
}

bool NpcAnimPlayer::OpenSI()
{
	if (m_siReady) {
		return true;
	}

	m_siFile = new si::File();

	// Path matches islefiles.cpp entry: /LEGO/Scripts/Isle/ISLE.SI
	MxString path = MxString(MxOmni::GetHD()) + "\\lego\\scripts\\isle\\isle.si";
	path.MapPathToFilesystem();
	if (!m_siFile->Open(path.GetData(), si::File::Read)) {
		// Try CD path
		path = MxString(MxOmni::GetCD()) + "\\lego\\scripts\\isle\\isle.si";
		path.MapPathToFilesystem();
		if (!m_siFile->Open(path.GetData(), si::File::Read)) {
			SDL_Log("NpcAnimPlayer: Could not open ISLE.SI (tried HD and CD paths)");
			delete m_siFile;
			m_siFile = nullptr;
			return false;
		}
	}

	if (!ReadSIHeader()) {
		SDL_Log("NpcAnimPlayer: Could not read ISLE.SI header");
		m_siFile->Close();
		delete m_siFile;
		m_siFile = nullptr;
		return false;
	}

	m_siReady = true;
	SDL_Log("NpcAnimPlayer: ISLE.SI opened, %zu offset table entries", m_offsetTable.size());
	return true;
}

// Read just the RIFF header, MxHd, and MxOf offset table.
// These are contiguous at the start of the file (one fetch in Emscripten).
bool NpcAnimPlayer::ReadSIHeader()
{
	// RIFF header
	uint32_t riffId = m_siFile->ReadU32();
	m_siFile->ReadU32(); // size
	uint32_t riffType = m_siFile->ReadU32();
	if (riffId != RIFF_ID || riffType != OMNI_ID) {
		SDL_Log("NpcAnimPlayer: Not a valid SI file");
		return false;
	}

	// MxHd
	uint32_t mxhdId = m_siFile->ReadU32();
	uint32_t mxhdSize = m_siFile->ReadU32();
	if (mxhdId != MxHd_ID) {
		SDL_Log("NpcAnimPlayer: Expected MxHd, got 0x%x", mxhdId);
		return false;
	}
	m_siFile->ReadU32(); // version
	m_bufferSize = m_siFile->ReadU32();
	m_siFile->ReadU32(); // buffer count
	// Skip any remaining MxHd data
	if (mxhdSize > 12) {
		m_siFile->seek(mxhdSize - 12, si::File::SeekCurrent);
	}

	// MxOf
	uint32_t mxofId = m_siFile->ReadU32();
	uint32_t mxofSize = m_siFile->ReadU32();
	if (mxofId != MxOf_ID) {
		SDL_Log("NpcAnimPlayer: Expected MxOf, got 0x%x", mxofId);
		return false;
	}
	// The declared count is the number of offset entries.
	// MxDSFile::ReadChunks uses this count directly (not chunk size).
	uint32_t offsetCount = m_siFile->ReadU32();
	m_offsetTable.resize(offsetCount);
	for (uint32_t i = 0; i < offsetCount; i++) {
		m_offsetTable[i] = m_siFile->ReadU32();
	}

	SDL_Log("NpcAnimPlayer: Read offset table: %u entries", offsetCount);
	return true;
}

// Read a single MxOb from the current file position, returning a new Object.
si::Object* NpcAnimPlayer::ReadMxOb(si::File* p_file)
{
	si::Object* obj = new si::Object();

	obj->type_ = static_cast<si::MxOb::Type>(p_file->ReadU16());
	obj->presenter_ = p_file->ReadString();
	obj->unknown1_ = p_file->ReadU32();
	obj->name_ = p_file->ReadString();
	obj->id_ = p_file->ReadU32();
	obj->flags_ = p_file->ReadU32();
	obj->unknown4_ = p_file->ReadU32();
	obj->duration_ = p_file->ReadU32();
	obj->loops_ = p_file->ReadU32();
	obj->location_ = p_file->ReadVector3();
	obj->direction_ = p_file->ReadVector3();
	obj->up_ = p_file->ReadVector3();

	uint16_t extraSz = p_file->ReadU16();
	obj->extra_ = p_file->ReadBytes(extraSz);

	// Types Presenter, World, Animation skip the filename/filetype fields
	if (obj->type_ != si::MxOb::Presenter && obj->type_ != si::MxOb::World &&
		obj->type_ != si::MxOb::Animation) {
		obj->filename_ = p_file->ReadString();
		obj->unknown26_ = p_file->ReadU32();
		obj->unknown27_ = p_file->ReadU32();
		obj->unknown28_ = p_file->ReadU32();
		obj->filetype_ = static_cast<si::MxOb::FileType>(p_file->ReadU32());
		obj->unknown29_ = p_file->ReadU32();
		obj->unknown30_ = p_file->ReadU32();

		if (obj->filetype_ == si::MxOb::WAV) {
			obj->volume_ = p_file->ReadU32();
		}
	}

	obj->time_offset_ = 0;

	return obj;
}

// Read data chunks from the MxSt, populating data_ on child objects.
void NpcAnimPlayer::ReadDataChunks(si::File* p_file, uint32_t p_stEnd, si::Object* p_root)
{
	// Build ID -> Object map for this tree
	std::map<uint32_t, si::Object*> idMap;
	idMap[p_root->id()] = p_root;
	for (si::Core* childCore : p_root->GetChildren()) {
		si::Object* child = dynamic_cast<si::Object*>(childCore);
		if (child) {
			idMap[child->id()] = child;
		}
	}

	// Track split chunk assembly
	uint32_t joiningSize = 0;
	uint32_t joiningProgress = 0;

	uint32_t chunkCount = 0;

	while (!p_file->atEnd() && (p_file->pos() + 8) < p_stEnd) {
		// Buffer alignment (same as interleaf.cpp ReadObjectData)
		if (m_bufferSize > 0) {
			uint32_t oib = p_file->pos() % m_bufferSize;
			if (oib + 8 > m_bufferSize) {
				p_file->seek(m_bufferSize - oib, si::File::SeekCurrent);
			}
		}

		uint32_t pos = p_file->pos();
		uint32_t id = p_file->ReadU32();
		uint32_t size = p_file->ReadU32();

		if (chunkCount < 8 || (id != MxCh_ID && id != LIST_ID)) {
			SDL_Log(
				"NpcAnimPlayer: ReadDataChunks chunk at pos=%u id=0x%x size=%u (stEnd=%u)",
				pos,
				id,
				size,
				p_stEnd
			);
		}

		if (id == MxCh_ID) {
			uint16_t flags = p_file->ReadU16();
			uint32_t objectId = p_file->ReadU32();
			uint32_t time = p_file->ReadU32();
			uint32_t dataSz = p_file->ReadU32();

			si::bytearray data = p_file->ReadBytes(size - MXCH_HEADER_SIZE);

			// RIFF word-alignment: skip padding byte for odd-sized chunks
			if (size % 2 == 1) {
				p_file->seek(1, si::File::SeekCurrent);
			}

			if (flags & FLAG_END) {
				chunkCount++;
				continue;
			}

			auto it = idMap.find(objectId);
			if (it != idMap.end()) {
				si::Object* obj = it->second;

				if ((flags & FLAG_SPLIT) && joiningSize > 0) {
					obj->data_.back().append(data);
					joiningProgress += data.size();
					if (joiningProgress >= joiningSize) {
						joiningProgress = 0;
						joiningSize = 0;
					}
				}
				else {
					obj->data_.push_back(data);
					if (obj->data_.size() == 2) {
						obj->time_offset_ = time;
					}
					if (flags & FLAG_SPLIT) {
						joiningProgress = data.size();
						joiningSize = dataSz;
					}
				}
			}
			chunkCount++;
		}
		else if (id == LIST_ID) {
			p_file->ReadU32(); // list type
		}
		else if (id == MxSt_ID || id == MxDa_ID) {
			// MxSt/MxDa act as container markers with no extra data (same as libweaver)
		}
		else {
			// Skip unknown chunk
			p_file->seek(pos + 8 + size + (size % 2), si::File::SeekStart);
		}
	}

	SDL_Log("NpcAnimPlayer: ReadDataChunks processed %u MxCh chunks", chunkCount);
	for (auto& [objId, obj] : idMap) {
		if (!obj->data_.empty()) {
			SDL_Log("NpcAnimPlayer:   Object %u: %zu data chunks", objId, obj->data_.size());
		}
	}
}

// Read a specific object's MxSt: parse its MxOb tree + read data chunks.
bool NpcAnimPlayer::ReadMxSt(uint32_t p_offset, si::Object* p_root)
{
	m_siFile->seek(p_offset, si::File::SeekStart);

	// MxSt is a raw RIFF chunk (not a LIST subtype)
	uint32_t stId = m_siFile->ReadU32();
	uint32_t stSize = m_siFile->ReadU32();
	uint32_t stEnd = (uint32_t) m_siFile->pos() + stSize;

	if (stId != MxSt_ID) {
		SDL_Log("NpcAnimPlayer: Expected MxSt at offset %u, got 0x%x", p_offset, stId);
		return false;
	}

	// MxOb header (composite object)
	uint32_t mxobId = m_siFile->ReadU32();
	uint32_t mxobSize = m_siFile->ReadU32();
	uint32_t mxobEnd = (uint32_t) m_siFile->pos() + mxobSize;

	if (mxobId != MxOb_ID) {
		SDL_Log("NpcAnimPlayer: Expected MxOb, got 0x%x", mxobId);
		return false;
	}

	// Read the composite object metadata
	si::Object* temp = ReadMxOb(m_siFile);
	p_root->type_ = temp->type_;
	p_root->presenter_ = temp->presenter_;
	p_root->name_ = temp->name_;
	p_root->id_ = temp->id_;
	p_root->flags_ = temp->flags_;
	p_root->duration_ = temp->duration_;
	p_root->filename_ = temp->filename_;
	p_root->filetype_ = temp->filetype_;
	p_root->volume_ = temp->volume_;
	p_root->extra_ = temp->extra_;
	p_root->time_offset_ = 0;
	delete temp;

	SDL_Log(
		"NpcAnimPlayer: Read composite object id=%u type=%d presenter='%s'",
		p_root->id(),
		(int) p_root->type(),
		p_root->presenter_.c_str()
	);

	// Read child MxObs if present (composite objects have a LIST MxCh with children)
	if (m_siFile->pos() + 8 < mxobEnd) {
		uint32_t childListId = m_siFile->ReadU32();
		uint32_t childListSize = m_siFile->ReadU32();
		uint32_t childListEnd = (uint32_t) m_siFile->pos() + childListSize;

		if (childListId == LIST_ID) {
			uint32_t childListType = m_siFile->ReadU32();
			uint32_t childCount = m_siFile->ReadU32();

			SDL_Log("NpcAnimPlayer: Composite has %u children", childCount);

			for (uint32_t i = 0; i < childCount && m_siFile->pos() + 8 < childListEnd; i++) {
				// Record position before reading chunk header
				uint32_t chunkStart = (uint32_t) m_siFile->pos();
				uint32_t childChunkId = m_siFile->ReadU32();
				uint32_t childChunkSize = m_siFile->ReadU32();
				// Chunk data ends at chunkStart + 8 (header) + childChunkSize
				uint32_t childChunkEnd = chunkStart + 8 + childChunkSize;

				if (childChunkId == MxOb_ID) {
					si::Object* child = ReadMxOb(m_siFile);
					p_root->AppendChild(child);
					SDL_Log(
						"NpcAnimPlayer:   Child %u: id=%u type=%d presenter='%s' filetype=0x%x",
						i,
						child->id(),
						(int) child->type(),
						child->presenter_.c_str(),
						(unsigned) child->filetype()
					);
				}

				// Seek to end of this child chunk (skips any remaining data
				// like nested LIST MxCh for sub-composites)
				m_siFile->seek(childChunkEnd + (childChunkSize % 2), si::File::SeekStart);
			}
		}
	}

	// Seek past the MxOb chunk to the data section
	uint32_t dataStart = mxobEnd + (mxobSize % 2);
	m_siFile->seek(dataStart, si::File::SeekStart);

	SDL_Log(
		"NpcAnimPlayer: Data section at %u, MxSt end at %u (%u bytes of data)",
		dataStart,
		stEnd,
		stEnd - dataStart
	);

	// Read data chunks (LIST MxDa with MxCh entries)
	ReadDataChunks(m_siFile, stEnd, p_root);

	return true;
}

bool NpcAnimPlayer::ReadObject(uint32_t p_objectId)
{
	if (!m_siReady || !m_siFile) {
		return false;
	}

	// Already read?
	if (m_objectTrees.find(p_objectId) != m_objectTrees.end()) {
		return true;
	}

	// The objectId IS the index into the offset table.
	// (see MxStreamController::FUN_100c1a00: offset = provider->GetBufferForDWords()[objectId])
	if (p_objectId >= m_offsetTable.size()) {
		SDL_Log("NpcAnimPlayer: Object %u out of offset table range (%zu entries)", p_objectId, m_offsetTable.size());
		return false;
	}

	uint32_t offset = m_offsetTable[p_objectId];
	if (offset == 0) {
		SDL_Log("NpcAnimPlayer: Object %u has no offset (slot is empty)", p_objectId);
		return false;
	}

	SDL_Log("NpcAnimPlayer: Reading object %u from offset %u", p_objectId, offset);

	ObjectTree tree;
	tree.root = new si::Object();
	if (ReadMxSt(offset, tree.root)) {
		m_objectTrees.emplace(p_objectId, std::move(tree));
		return true;
	}
	return false;
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

	auto treeIt = m_objectTrees.find(p_objectId);
	if (treeIt == m_objectTrees.end() || !treeIt->second.root) {
		return nullptr;
	}

	si::Object* composite = treeIt->second.root;

	SDL_Log(
		"NpcAnimPlayer: Processing composite id=%u, %zu children",
		composite->id(),
		composite->GetChildCount()
	);

	NpcAnimData data;
	bool hasAnim = false;

	for (size_t i = 0; i < composite->GetChildCount(); i++) {
		si::Object* child = dynamic_cast<si::Object*>(composite->GetChildAt(i));
		if (!child) {
			continue;
		}

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
		else if (child->presenter_.find("LegoAnimPresenter") != std::string::npos ||
				 child->presenter_.find("LegoLoopingAnimPresenter") != std::string::npos) {
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

void NpcAnimPlayer::Play(const NpcAnimEntry& p_entry, LegoROI* p_executingROI)
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

	// Build ROI map -- only the executing player's ROI.
	// Target character's *-prefixed nodes get index 0 (NULL) -> transforms skipped.
	AnimUtils::BuildROIMap(data->anim, p_executingROI, nullptr, 0, m_roiMap, m_roiMapSize);

	if (!m_roiMap) {
		SDL_Log("NpcAnimPlayer: Failed to build ROI map");
		m_currentData = nullptr;
		m_executingROI = nullptr;
		return;
	}

	// Build props for unmatched animation nodes (filter out *-prefixed actor nodes)
	std::vector<std::string> unmatchedNames;
	AnimUtils::CollectUnmatchedNodes(data->anim, p_executingROI, unmatchedNames);

	std::vector<LegoROI*> createdROIs;
	for (const std::string& name : unmatchedNames) {
		char uniqueName[64];
		SDL_snprintf(uniqueName, sizeof(uniqueName), "npc_prop_%s", name.c_str());

		LegoROI* propROI = CharacterManager()->CreateAutoROI(uniqueName, name.c_str(), FALSE);
		if (propROI) {
			propROI->SetName(name.c_str());
			createdROIs.push_back(propROI);
		}
	}

	if (!createdROIs.empty()) {
		m_propCount = (uint8_t) createdROIs.size();
		m_propROIs = new LegoROI*[m_propCount];
		for (uint8_t i = 0; i < m_propCount; i++) {
			m_propROIs[i] = createdROIs[i];
		}

		// Rebuild ROI map with props
		delete[] m_roiMap;
		m_roiMap = nullptr;
		m_roiMapSize = 0;
		AnimUtils::BuildROIMap(data->anim, p_executingROI, m_propROIs, m_propCount, m_roiMap, m_roiMapSize);
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
	// NPC animations have absolute world-space transforms. The tree is:
	//   root -> -TILT (camera, roiIdx=0) -> character (roiIdx=N) -> body parts
	// We rebase the character node's transform: compute the animation's
	// position at time 0 as the origin, then for each frame apply only
	// the *delta* from that origin, anchored to the player's saved position.
	// This preserves relative movement (walk somewhere and back) while
	// playing at the player's current location.
	if (m_currentData->anim && m_roiMap) {
		std::function<void(LegoTreeNode*, MxMatrix&)> findAndApply = [&](LegoTreeNode* node, MxMatrix& parentMat) {
			LegoAnimNodeData* data = (LegoAnimNodeData*) node->GetData();
			MxU32 roiIdx = data ? data->GetROIIndex() : 0;

			if (roiIdx != 0 && m_roiMap[roiIdx] == m_executingROI) {
				// This is the character node. Rebase its transform.
				MxMatrix animLocalNow;
				LegoROI::CreateLocalTransform(data, (LegoTime) elapsed, animLocalNow);

				// Compute delta translation from animation start (time 0)
				MxMatrix animLocalStart;
				LegoROI::CreateLocalTransform(data, 0, animLocalStart);
				float dx = animLocalNow[3][0] - animLocalStart[3][0];
				float dy = animLocalNow[3][1] - animLocalStart[3][1];
				float dz = animLocalNow[3][2] - animLocalStart[3][2];

				// Build rebased character transform: player position + delta movement
				MxMatrix rebasedTransform(m_savedTransform);
				rebasedTransform[3][0] += dx;
				rebasedTransform[3][1] += dy;
				rebasedTransform[3][2] += dz;

				// Set character ROI to rebased transform
				m_executingROI->WrappedSetLocal2WorldWithWorldDataUpdate(rebasedTransform);

				// Apply body-part children with rebased transform as parent
				for (LegoU32 i = 0; i < node->GetNumChildren(); i++) {
					LegoROI::ApplyAnimationTransformation(
						node->GetChild(i),
						rebasedTransform,
						(LegoTime) elapsed,
						m_roiMap
					);
				}
				return;
			}

			// Not the character node — compute this node's transform and recurse
			MxMatrix localMat;
			LegoROI::CreateLocalTransform(data, (LegoTime) elapsed, localMat);
			MxMatrix worldMat;
			worldMat.Product(localMat, parentMat);

			for (LegoU32 i = 0; i < node->GetNumChildren(); i++) {
				findAndApply(node->GetChild(i), worldMat);
			}
		};

		MxMatrix identity;
		identity.SetIdentity();
		LegoTreeNode* root = m_currentData->anim->GetRoot();
		for (LegoU32 i = 0; i < root->GetNumChildren(); i++) {
			findAndApply(root->GetChild(i), identity);
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

	delete[] m_roiMap;
	m_roiMap = nullptr;
	m_roiMapSize = 0;

	if (m_executingROI) {
		m_executingROI->WrappedSetLocal2WorldWithWorldDataUpdate(m_savedTransform);
	}

	m_playing = false;
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
			VideoManager()->Get3DManager()->Remove(*m_propROIs[i]);
			CharacterManager()->ReleaseAutoROI(m_propROIs[i]);
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
