#include "racecar.h"

#include "extensions/instrumentation/roi_uaf_log.h"
#include "isle.h"
#include "isle_actions.h"
#include "legocontrolmanager.h"
#include "legoutils.h"
#include "legoworld.h"
#include "misc.h"
#include "mxtransitionmanager.h"

#include <cstdio>

DECOMP_SIZE_ASSERT(RaceCar, 0x164)

// FUNCTION: LEGO1 0x10028200
RaceCar::RaceCar()
{
	m_maxLinearVel = 40.0;
}

// FUNCTION: LEGO1 0x10028420
RaceCar::~RaceCar()
{
	// Bug C v3 instrumentation: log BEFORE Exit() to capture pre-exit
	// m_roi state. Exit() may further modify the actor's path-controller
	// state.
	{
		char site[64];
		std::snprintf(
			site,
			sizeof site,
			"~RaceCar m_roi=0x%08x",
			(unsigned) reinterpret_cast<uintptr_t>(m_roi)
		);
		roi_uaf_log_release(this, "RaceCar", site);
	}

	ControlManager()->Unregister(this);
	Exit();
}

// FUNCTION: LEGO1 0x10028490
MxResult RaceCar::Create(MxDSAction& p_dsAction)
{
	MxResult result = IslePathActor::Create(p_dsAction);
	m_world = CurrentWorld();

	if (m_world) {
		m_world->Add(this);
	}

	ControlManager()->Register(this);
	return result;
}

// FUNCTION: LEGO1 0x100284d0
MxLong RaceCar::HandleClick()
{
	if (!CanExit()) {
		return 1;
	}

	Isle* isle = (Isle*) FindWorld(*g_isleScript, IsleScript::c__Isle);
	isle->SetDestLocation(LegoGameState::Area::e_carrace);
	TransitionManager()->StartTransition(MxTransitionManager::e_mosaic, 50, FALSE, FALSE);
	return 1;
}
