#include "extensions/multiplayer.h"

#include "extensions/extensions.h"
#include "extensions/multiplayer/charactercustomizer.h"
#include "extensions/multiplayer/customizestate.h"
#include "extensions/multiplayer/networkmanager.h"
#include "extensions/multiplayer/networktransport.h"
#include "extensions/multiplayer/protocol.h"
#include "islepathactor.h"
#include "legoactor.h"
#include "legoactors.h"
#include "legoentity.h"
#include "legoeventnotificationparam.h"
#include "legogamestate.h"
#include "legopathactor.h"
#include "misc.h"
#include "roi/legoroi.h"

#include <SDL3/SDL_stdinc.h>

#ifdef __EMSCRIPTEN__
#include "extensions/multiplayer/platforms/emscripten/callbacks.h"
#include "extensions/multiplayer/platforms/emscripten/websockettransport.h"

#include <emscripten.h>
#endif

using namespace Extensions;

static uint8_t ResolveDisplayActorIndex(const char* p_name)
{
	for (int i = 0; i < static_cast<int>(sizeOfArray(g_actorInfoInit)); i++) {
		if (!SDL_strcasecmp(g_actorInfoInit[i].m_name, p_name)) {
			return static_cast<uint8_t>(i);
		}
	}
	return Multiplayer::DISPLAY_ACTOR_NONE;
}

std::map<std::string, std::string> MultiplayerExt::options;
bool MultiplayerExt::enabled = false;
std::string MultiplayerExt::relayUrl;
std::string MultiplayerExt::room;
Multiplayer::NetworkManager* MultiplayerExt::s_networkManager = nullptr;
Multiplayer::NetworkTransport* MultiplayerExt::s_transport = nullptr;
Multiplayer::PlatformCallbacks* MultiplayerExt::s_callbacks = nullptr;

void MultiplayerExt::Initialize()
{
	relayUrl = options["multiplayer:relay url"];
	room = options["multiplayer:room"];

#ifdef __EMSCRIPTEN__
	s_transport = new Multiplayer::WebSocketTransport(relayUrl);
	s_callbacks = new Multiplayer::EmscriptenCallbacks();

	s_networkManager = new Multiplayer::NetworkManager();
	s_networkManager->Initialize(s_transport, s_callbacks);

	// Third-person camera enabled by default, toggled via WASM export
	s_networkManager->GetThirdPersonCamera().Enable();

	std::string actor = options["multiplayer:actor"];
	if (!actor.empty()) {
		uint8_t displayIndex = ResolveDisplayActorIndex(actor.c_str());
		if (displayIndex != Multiplayer::DISPLAY_ACTOR_NONE) {
			s_networkManager->SetDisplayActorIndex(displayIndex);
		}
	}

	if (!relayUrl.empty() && !room.empty()) {
		s_networkManager->Connect(room.c_str());
	}
#endif
}

MxBool MultiplayerExt::HandleWorldEnable(LegoWorld* p_world, MxBool p_enable)
{
	if (!s_networkManager) {
		return FALSE;
	}

	if (p_enable) {
		s_networkManager->OnWorldEnabled(p_world);
	}
	else {
		s_networkManager->OnWorldDisabled(p_world);
	}

	return TRUE;
}

MxBool MultiplayerExt::HandleROIClick(LegoROI* p_rootROI, LegoEventNotificationParam& p_param)
{
	if (!s_networkManager) {
		return FALSE;
	}

	Multiplayer::NetworkManager* mgr = s_networkManager;

	// Check if it's a remote player
	Multiplayer::RemotePlayer* remote = mgr->FindPlayerByROI(p_rootROI);

	// Check if it's our own 3rd-person display actor override
	bool isSelf = (mgr->GetThirdPersonCamera().GetDisplayROI() != nullptr &&
				   mgr->GetThirdPersonCamera().GetDisplayROI() == p_rootROI);

	if (!remote && !isSelf) {
		return FALSE;
	}

	// Remote player permission check
	if (remote && !remote->GetAllowRemoteCustomize()) {
		return TRUE; // Consume click, no effect
	}

	// Determine change type from clicker's actor ID
	uint8_t changeType;
	int partIndex = -1;
	switch (GameState()->GetActorId()) {
	case LegoActor::c_pepper:
		if (GameState()->GetCurrentAct() == LegoGameState::e_act2 ||
			GameState()->GetCurrentAct() == LegoGameState::e_act3) {
			return TRUE;
		}
		changeType = Multiplayer::CHANGE_VARIANT;
		break;
	case LegoActor::c_mama:
		changeType = Multiplayer::CHANGE_SOUND;
		break;
	case LegoActor::c_papa:
		changeType = Multiplayer::CHANGE_MOVE;
		break;
	case LegoActor::c_nick:
		changeType = Multiplayer::CHANGE_COLOR;
		if (p_param.GetROI()) {
			partIndex = Multiplayer::CharacterCustomizer::MapClickedPartIndex(p_param.GetROI()->GetName());
		}
		if (partIndex < 0) {
			return TRUE;
		}
		break;
	case LegoActor::c_laura:
		changeType = Multiplayer::CHANGE_MOOD;
		break;
	case LegoActor::c_brickster:
		return TRUE;
	default:
		return FALSE;
	}

	// Get target info
	uint8_t displayActorIndex;
	uint8_t actorId;
	Multiplayer::CustomizeState* state;

	if (remote) {
		displayActorIndex = remote->GetDisplayActorIndex();
		actorId = remote->GetActorId();
		state = &remote->GetCustomizeStateMut();
	}
	else {
		displayActorIndex = mgr->GetThirdPersonCamera().GetDisplayActorIndex();
		actorId = GameState()->GetActorId();
		state = &mgr->GetThirdPersonCamera().GetCustomizeState();
	}

	uint8_t actorInfoIndex =
		Multiplayer::CharacterCustomizer::ResolveActorInfoIndex(displayActorIndex, actorId);

	// Apply change
	switch (changeType) {
	case Multiplayer::CHANGE_VARIANT:
		Multiplayer::CharacterCustomizer::SwitchVariant(p_rootROI, actorInfoIndex, *state);
		break;
	case Multiplayer::CHANGE_SOUND:
		Multiplayer::CharacterCustomizer::SwitchSound(*state);
		break;
	case Multiplayer::CHANGE_MOVE:
		Multiplayer::CharacterCustomizer::SwitchMove(*state);
		break;
	case Multiplayer::CHANGE_COLOR:
		Multiplayer::CharacterCustomizer::SwitchColor(p_rootROI, actorInfoIndex, *state, partIndex);
		break;
	case Multiplayer::CHANGE_MOOD:
		Multiplayer::CharacterCustomizer::SwitchMood(*state);
		break;
	}

	// Play click effects (skip animation when on a vehicle)
	Multiplayer::CharacterCustomizer::PlayClickSound(
		p_rootROI, *state, changeType == Multiplayer::CHANGE_MOOD
	);

	bool inVehicle = remote ? remote->IsInVehicle() : mgr->GetThirdPersonCamera().IsInVehicle();
	if (!inVehicle) {
		MxU32 clickAnimId = Multiplayer::CharacterCustomizer::PlayClickAnimation(p_rootROI, *state);

		if (remote) {
			remote->SetClickAnimObjectId(clickAnimId);
		}
		else {
			mgr->GetThirdPersonCamera().SetClickAnimObjectId(clickAnimId);
		}
	}

	// Send customize message so remote clients see the click animation
	mgr->SendCustomize(
		remote ? remote->GetPeerId() : mgr->GetLocalPeerId(),
		changeType,
		static_cast<uint8_t>(partIndex >= 0 ? partIndex : 0xFF)
	);

	return TRUE;
}

MxBool MultiplayerExt::HandleEntityNotify(LegoEntity* p_entity)
{
	if (!s_networkManager) {
		return FALSE;
	}

	// Only intercept plants and buildings
	MxU8 type = p_entity->GetType();
	if (type != LegoEntity::e_plant && type != LegoEntity::e_building) {
		return FALSE;
	}

	// Determine the change type based on the active character,
	// mirroring the logic in LegoEntity::Notify().
	MxU8 changeType;
	switch (GameState()->GetActorId()) {
	case LegoActor::c_pepper:
		if (GameState()->GetCurrentAct() == LegoGameState::e_act2 ||
			GameState()->GetCurrentAct() == LegoGameState::e_act3) {
			return FALSE;
		}
		changeType = Multiplayer::CHANGE_VARIANT;
		break;
	case LegoActor::c_mama:
		changeType = Multiplayer::CHANGE_SOUND;
		break;
	case LegoActor::c_papa:
		changeType = Multiplayer::CHANGE_MOVE;
		break;
	case LegoActor::c_nick:
		changeType = Multiplayer::CHANGE_COLOR;
		break;
	case LegoActor::c_laura:
		changeType = Multiplayer::CHANGE_MOOD;
		break;
	case LegoActor::c_brickster:
		changeType = Multiplayer::CHANGE_DECREMENT;
		break;
	default:
		return FALSE;
	}

	return s_networkManager->HandleEntityMutation(p_entity, changeType);
}

void MultiplayerExt::HandleActorEnter(IslePathActor* p_actor)
{
	if (s_networkManager) {
		s_networkManager->GetThirdPersonCamera().OnActorEnter(p_actor);
	}
}

void MultiplayerExt::HandleActorExit(IslePathActor* p_actor)
{
	if (s_networkManager) {
		s_networkManager->GetThirdPersonCamera().OnActorExit(p_actor);
	}
}

void MultiplayerExt::HandleCamAnimEnd(LegoPathActor* p_actor)
{
	if (s_networkManager) {
		s_networkManager->GetThirdPersonCamera().OnCamAnimEnd(p_actor);
	}
}

MxBool MultiplayerExt::ShouldInvertMovement(LegoPathActor* p_actor)
{
	if (s_networkManager && UserActor() == p_actor) {
		return s_networkManager->GetThirdPersonCamera().IsActive();
	}

	return FALSE;
}

MxBool MultiplayerExt::IsClonedCharacter(const char* p_name)
{
	if (!s_networkManager) {
		return FALSE;
	}

	return s_networkManager->IsClonedCharacter(p_name) ? TRUE : FALSE;
}

MxBool MultiplayerExt::CheckRejected()
{
	if (s_networkManager && s_networkManager->WasRejected()) {
		return TRUE;
	}

	return FALSE;
}

void MultiplayerExt::SetNetworkManager(Multiplayer::NetworkManager* p_networkManager)
{
	s_networkManager = p_networkManager;
}

Multiplayer::NetworkManager* MultiplayerExt::GetNetworkManager()
{
	return s_networkManager;
}

bool Extensions::IsMultiplayerRejected()
{
	return Extension<MultiplayerExt>::Call(CheckRejected).value_or(FALSE);
}
