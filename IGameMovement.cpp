#include "precompiled.h"
#include "IGameMovement.h"
#include "C_CSGameRules.h"
#include "Gametypes.h"
#include <math.h>
#include "ClassIDS.h"
#include "UsedConvars.h"
#include "vphysics_interface.h"
#include "Includes.h"
#include "LocalPlayer.h"

static CCSGameMovement g_GameMovement;
IGameMovement* g_pGameMovement = (IGameMovement*)&g_GameMovement;
extern bool g_bIsExiting;

inline const Vector& CMoveData::GetAbsOrigin() const
{
	return _m_vecAbsOrigin;
}

// This is implemented with a more exhaustive test in gamemovement.cpp.  We check if the origin being requested is
//  inside solid, which it never should be
inline void CMoveData::SetAbsOrigin(const Vector& vec)
{
	_m_vecAbsOrigin = vec;
}

//-----------------------------------------------------------------------------
// Traces the player's collision bounds in quadrants, looking for a plane that
// can be stood upon (normal's z >= 0.7f).  Regardless of success or failure,
// replace the fraction and endpos with the original ones, so we don't try to
// move the player down to the new floor and get stuck on a leaning wall that
// the original trace hit first.
//-----------------------------------------------------------------------------
void TracePlayerBBoxForGround(const Vector& start, const Vector& end, const Vector& minsSrc, const Vector& maxsSrc, IHandleEntity* player, unsigned int fMask, int collisionGroup, trace_t& pm)
{
	Ray_t ray;
	Vector mins, maxs;

	float fraction = pm.fraction;
	Vector endpos = pm.endpos;

	// Check the -x, -y quadrant
	mins = minsSrc;
	maxs.Init(min(0, maxsSrc.x), min(0, maxsSrc.y), maxsSrc.z);
	ray.Init(start, end, mins, maxs);
	UTIL_TraceRay(ray, fMask, player, collisionGroup, &pm);
	if (pm.m_pEnt && pm.plane.normal[2] >= 0.7)
	{
		pm.fraction = fraction;
		pm.endpos = endpos;
		return;
	}

	// Check the +x, +y quadrant
	mins.Init(max(0, minsSrc.x), max(0, minsSrc.y), minsSrc.z);
	maxs = maxsSrc;
	ray.Init(start, end, mins, maxs);
	UTIL_TraceRay(ray, fMask, player, collisionGroup, &pm);
	if (pm.m_pEnt && pm.plane.normal[2] >= 0.7)
	{
		pm.fraction = fraction;
		pm.endpos = endpos;
		return;
	}

	// Check the -x, +y quadrant
	mins.Init(minsSrc.x, max(0, minsSrc.y), minsSrc.z);
	maxs.Init(min(0, maxsSrc.x), maxsSrc.y, maxsSrc.z);
	ray.Init(start, end, mins, maxs);
	UTIL_TraceRay(ray, fMask, player, collisionGroup, &pm);
	if (pm.m_pEnt && pm.plane.normal[2] >= 0.7)
	{
		pm.fraction = fraction;
		pm.endpos = endpos;
		return;
	}

	// Check the +x, -y quadrant
	mins.Init(max(0, minsSrc.x), minsSrc.y, minsSrc.z);
	maxs.Init(maxsSrc.x, min(0, maxsSrc.y), maxsSrc.z);
	ray.Init(start, end, mins, maxs);
	UTIL_TraceRay(ray, fMask, player, collisionGroup, &pm);
	if (pm.m_pEnt && pm.plane.normal[2] >= 0.7)
	{
		pm.fraction = fraction;
		pm.endpos = endpos;
		return;
	}

	pm.fraction = fraction;
	pm.endpos = endpos;
}

//-----------------------------------------------------------------------------
// Purpose: Constructs GameMovement interface
//-----------------------------------------------------------------------------
CGameMovement::CGameMovement(void)
{
	m_nOldWaterLevel = WL_NotInWater;
	m_flWaterEntryTime = 0;
	m_nOnLadder = 0;
	m_bProcessingMovement = false;

	mv = NULL;

	memset(m_flStuckCheckTime, 0, sizeof(m_flStuckCheckTime));
	m_pTraceListData = NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
CGameMovement::~CGameMovement(void)
{
	if (!g_bIsExiting && Interfaces::EngineTrace)
	{
		Interfaces::EngineTrace->FreeTraceListData(m_pTraceListData);
	}
}

CCSGameMovement::CCSGameMovement()
{
}

void CGameMovement::nullsub3()
{
}

void CCSGameMovement::nullsub3()
{
}

// -----------------------------------
// Implementation
// -----------------------------------

void CGameMovement::ProcessMovement(CBasePlayer* basePlayer, CMoveData* moveData)
{
	float storedFrametime = Interfaces::Globals->frametime;

	m_nTraceCount = 0;

	Interfaces::Globals->frametime *= basePlayer->GetLaggedMovement();

	ResetGetWaterContentsForPointCache();

	mv = moveData;
	m_bSpeedCropped = false;
	player = basePlayer;

	// Calls CBasePlayer's 269th index
	//(*(void (__thiscall **)(DWORD))(*(_DWORD *)basePlayer + 0x434))(basePlayer);
	mv->_m_flMaxSpeed = basePlayer->GetPlayerMaxSpeed();

	m_bProcessingMovement = true;

	//DiffPrint("start %f %f %f", mv->GetAbsOrigin().x, mv->GetAbsOrigin().y, mv->GetAbsOrigin().z);

	//(*(void (__thiscall **)(DWORD))(*(_DWORD *)gameMovement_0 + 0x44))(gameMovement_0);
	PlayerMove();

	// FinishMove(); inlined
	mv->m_nOldButtons = mv->m_nButtons;

	//DiffPrint("end %f %f %f", mv->GetAbsOrigin().x, mv->GetAbsOrigin().y, mv->GetAbsOrigin().z);

	Interfaces::Globals->frametime = storedFrametime;

	m_bProcessingMovement = false;
}

void CCSGameMovement::ProcessMovement(CBasePlayer* basePlayer, CMoveData* moveData)
{
	m_pCSPlayer = basePlayer;

	if (!m_pCSPlayer->IsBot())
		CGameMovement::ProcessMovement(basePlayer, moveData);
}

void CGameMovement::DiffPrint(char const* fmt, ...)
{
}

void CCSGameMovement::DiffPrint(char const* fmt, ...)
{
}

void CGameMovement::Reset()
{
	player = nullptr;
}

void CGameMovement::StartTrackPredictionErrors(CBasePlayer* basePlayer)
{
	if (basePlayer->IsBot())
		return;

	player = basePlayer;
}

void CGameMovement::FinishTrackPredictionErrors(CBasePlayer* basePlayer)
{
	if (basePlayer->IsBot())
		return;
}

const Vector& CGameMovement::GetPlayerMins(bool ducked) const
{
	if (ducked)
		// (*(*g_pGameRules + 120))() + 0x14;
		return (*g_pGameRules)->GetViewVectors()->m_vDuckHullMin;

	// (*(*g_pGameRules + 120))() + 0xC;
	return (*g_pGameRules)->GetViewVectors()->m_vHullMin;
}

const Vector& CGameMovement::GetPlayerMaxs(bool ducked) const
{
	if (ducked)
		// (*(*g_pGameRules + 120))() + 0x30;
		return (*g_pGameRules)->GetViewVectors()->m_vDuckHullMax;

	// (*(*g_pGameRules + 120))() + 0x18;
	return (*g_pGameRules)->GetViewVectors()->m_vHullMax;
}

const Vector& CGameMovement::GetPlayerViewOffset(bool ducked) const
{
	if (ducked)
		// (*(*g_pGameRules + 120))() + 0x3C;
		return (*g_pGameRules)->GetViewVectors()->m_vDuckView;

	// (*(*g_pGameRules + 120))() + 0;
	return (*g_pGameRules)->GetViewVectors()->m_vView;
}

bool CGameMovement::IsMovingPlayerStuck() const
{
	if (m_bProcessingMovement)
	{
		if (!m_bInStuckTest)
		{
			CBasePlayer* basePlayer = player;
			if (basePlayer && basePlayer->GetStuckLast() > 0)
				return true;
		}
	}

	return false;
}

CBasePlayer* CGameMovement::GetMovingPlayer() const
{
	if (m_bProcessingMovement)
		return player;

	return nullptr;
}

void CGameMovement::UnblockPusher(CBasePlayer* pPlayer, CBaseEntity* pPusher)
{
	// TODO
}

void CCSGameMovement::UnblockPusher(CBasePlayer* pPlayer, CBaseEntity* pPusher)
{
	// TODO
}

void CGameMovement::SetupMovementBounds(CMoveData* moveData)
{
	if (m_pTraceListData)
		m_pTraceListData->Reset();
	else
		m_pTraceListData = Interfaces::EngineTrace->AllocTraceListData();

	if (moveData->m_nPlayerHandle == -1)
		return;

	CBasePlayer* basePlayer = (CBaseEntity*)moveData->m_nPlayerHandle.Get();

	// ClearBounds inlined and optimized (doesn't use 99999 and -99999, but rather LOVAL/HIVAL constants)
	Vector moveMins = { FLT_MIN, FLT_MIN, FLT_MIN };//{ (float)0xFF7FFFFF, (float)0xFF7FFFFF, (float)0xFF7FFFFF };
	Vector moveMaxs = { FLT_MAX, FLT_MAX, FLT_MAX };//{ (float)0x7F7FFFFF, (float)0x7F7FFFFF, (float)0x7F7FFFFF };

	Vector start = moveData->_m_vecAbsOrigin;
	float radius = ((moveData->m_vecVelocity_.Length() + moveData->_m_flMaxSpeed) * Interfaces::Globals->frametime) + 1.0f;

	Vector playerMins = GetPlayerMins(false);
	Vector playerMaxs = GetPlayerMaxs(false);

	Vector bloat = { radius, radius, radius + basePlayer->GetStepSize() };

	AddPointToBounds(start + playerMaxs + bloat, moveMins, moveMaxs);
	AddPointToBounds(start + playerMaxs - bloat, moveMins, moveMaxs);

	// (*(*enginetrace + 28))(&moveMins, &moveMaxs, *(gameMovement + 0xE4C));
	Interfaces::EngineTrace->SetupLeafAndEntityListBox(moveMins, moveMaxs, m_pTraceListData);
}

Vector CGameMovement::GetPlayerMins()
{
	// (*(**(this + 4) + 0x474))()
	if (player->GetObserverMode())
		// (*(*g_pGameRules + 120))() + 72;
		return (*g_pGameRules)->GetViewVectors()->m_vObsHullMin;

	// ( *(*(v1 + 4) + 0x3034) )
	if (player->GetDucked())
		// (*(v3 + 120))(g_pGameRules) + 36
		return (*g_pGameRules)->GetViewVectors()->m_vDuckHullMin;

	// (*(v3 + 120))(g_pGameRules) + 12
	return (*g_pGameRules)->GetViewVectors()->m_vHullMin;
}

Vector CGameMovement::GetPlayerMaxs()
{
	// (*(**(this + 4) + 0x474))()
	if (player->GetObserverMode())
		// (*(*g_pGameRules + 120))() + 84;
		return (*g_pGameRules)->GetViewVectors()->m_vObsHullMax;

	// ( *(*(v1 + 4) + 0x3034) )
	if (player->GetDucked())
		// (*(v3 + 120))(g_pGameRules) + 48
		return (*g_pGameRules)->GetViewVectors()->m_vDuckHullMax;

	// (*(v3 + 120))(g_pGameRules) + 24
	return (*g_pGameRules)->GetViewVectors()->m_vHullMax;
}

void CGameMovement::TracePlayerBBox(const Vector& rayStart, const Vector& rayEnd, int fMask, int collisionGroup, trace_t& tracePtr)
{
	Ray_t ray;
	m_nTraceCount++;

	Vector playerMaxs = GetPlayerMaxs();
	Vector playerMins = GetPlayerMins();

	ray.Init(rayStart, rayEnd, playerMins, playerMaxs);

	ITraceFilter* filter = LockTraceFilter(collisionGroup);

	if (m_pTraceListData == nullptr || !m_pTraceListData->CanTraceRay(ray))
		Interfaces::EngineTrace->TraceRay(ray, fMask, filter, &tracePtr);
	else
		Interfaces::EngineTrace->TraceRayAgainstLeafAndEntityList(ray, m_pTraceListData, fMask, filter, &tracePtr);

	UnlockTraceFilter(filter);
}

unsigned int CGameMovement::PlayerSolidMask(bool brushOnly, CBasePlayer* testPlayer /*= nullptr;*/)
{
	if (brushOnly)
		return 0x1400B;

	return 0x201400B;
}

//dangerzone correct
unsigned int CCSGameMovement::PlayerSolidMask(bool brushOnly, CBasePlayer* testPlayer)
{
	bool isBot = true;

	if (player != nullptr)
	{
		if (!player->IsBot())
			isBot = false;
	}

	unsigned int v9 = 0x1400B;

	if (!brushOnly)
		// v9 = (*(int (**)(void))(**(_DWORD **)(v2 + 4) + 592))();
		// Calls CBasePlayer's 148th function
		v9 = player->PhysicsSolidMaskForEntity();

	unsigned int result = v9 | 0x20000;

	if (!isBot)
		result = v9;

	return result;
}

void CGameMovement::FullNoClipMove(float factor, float maxaccel)
{
	const float maxspeed = sv_maxspeed.GetVar()->GetFloat() * factor;

	Vector forward, right, up;
	AngleVectors(mv->m_vecViewAngles, &forward, &right, &up);

	if (mv->m_nButtons & IN_SPEED)
		factor *= 0.5f;

	float fmove = mv->m_flForwardMove * factor;
	float sidemove = mv->m_flSideMove * factor;

	VectorNormalizeFast(forward);
	VectorNormalizeFast(right);

	Vector wishVel;
	for (int i = 0; i < 3; ++i)
		wishVel[i] = forward[i] * fmove + right[i] * sidemove;

	wishVel.z += mv->m_flUpMove * factor;

	Vector wishdir = wishVel;

	float wishspeed = VectorNormalize(wishdir);

	if (wishspeed > maxspeed)
	{
		VectorScale(wishVel, maxspeed / wishspeed, wishVel);
		wishspeed = maxspeed;
	}

	if (maxaccel <= 0.f)
	{
		mv->m_vecVelocity_ = wishVel;
	}
	else
	{
		Accelerate(wishdir, wishspeed, maxaccel);

		float spd = mv->m_vecVelocity_.Length();

		if (spd < 1.f)
		{
			mv->m_vecVelocity_.x = 0.f;
			mv->m_vecVelocity_.y = 0.f;
			mv->m_vecVelocity_.z = 0.f;
			return;
		}

		float control = (spd < maxspeed * 0.25f) ? (maxspeed * 0.25f) : spd;

		float friction = sv_friction.GetVar()->GetFloat() * player->GetSurfaceFriction();

		float drop = control * friction * Interfaces::Globals->frametime;

		float newspeed = fmaxf(spd - drop, 0.f);

		newspeed /= spd;
		VectorScale(mv->m_vecVelocity_, newspeed, mv->m_vecVelocity_);
	}

	Vector temp;
	VectorMA(mv->_m_vecAbsOrigin, Interfaces::Globals->frametime, mv->m_vecVelocity_, temp);
	mv->_m_vecAbsOrigin = temp;

	if (maxaccel < 0.f)
	{
		mv->m_vecVelocity_.x = 0.f;
		mv->m_vecVelocity_.y = 0.f;
		mv->m_vecVelocity_.z = 0.f;
	}
}

void CGameMovement::FullObserverMove()
{
	// (*(*ecx->player + 0x474))();
	int observerMode = player->GetObserverMode();

	if (observerMode == OBS_MODE_IN_EYE || observerMode == OBS_MODE_CHASE)
	{
		// (*(*ecx->player + 0x478))();
		CBasePlayer* observerTarget = (CBasePlayer*)player->GetObserverTarget();

		if (observerTarget != nullptr)
		{
			mv->_m_vecAbsOrigin = *observerTarget->GetAbsOrigin();
			mv->m_vecViewAngles = observerTarget->GetAbsAngles();
			mv->m_vecVelocity_ = *observerTarget->GetAbsVelocity();
		}
	}
	else if (observerMode == OBS_MODE_ROAMING)
	{
		if (sv_specnoclip.GetVar()->GetBool())
		{
			FullNoClipMove(sv_specaccelerate.GetVar()->GetFloat(), sv_specspeed.GetVar()->GetFloat());
		}
		else
		{
			Vector forward, right, up;
			AngleVectors(mv->m_vecViewAngles, &forward, &right, &up);

			float factor = sv_specspeed.GetVar()->GetFloat();

			if (mv->m_nButtons & IN_SPEED)
				factor *= 0.5f;

			float fmove = mv->m_flForwardMove * factor;
			float sidemove = mv->m_flSideMove * factor;

			VectorNormalizeFast(forward);
			VectorNormalizeFast(right);

			Vector wishVel;
			for (int i = 0; i < 3; ++i)
				wishVel[i] = forward[i] * fmove + right[i] * sidemove;

			wishVel.z += mv->m_flUpMove;

			Vector wishdir = wishVel;

			float wishspeed = VectorNormalize(wishdir);

			float maxspeed = sv_maxvelocity.GetVar()->GetFloat();

			wishspeed = fminf(wishspeed, maxspeed);
			VectorScale(wishVel, mv->_m_flMaxSpeed / wishspeed, wishVel);

			Accelerate(wishdir, wishspeed, sv_specaccelerate.GetVar()->GetFloat());

			float spd = mv->m_vecVelocity_.Length();

			if (spd < 1.f)
			{
				mv->m_vecVelocity_.x = 0.f;
				mv->m_vecVelocity_.y = 0.f;
				mv->m_vecVelocity_.z = 0.f;
				return;
			}

			float friction = sv_friction.GetVar()->GetFloat() * player->GetSurfaceFriction();

			float drop = spd * friction * Interfaces::Globals->frametime;

			float newspeed = fmaxf(spd - drop, 0.f);

			newspeed /= spd;
			VectorScale(mv->m_vecVelocity_, newspeed, mv->m_vecVelocity_);

			CGameMovement::CheckVelocity();

			CGameMovement::TryPlayerMove();
		}
	}
}

void CGameMovement::PlayerMove()
{
	CheckParameters();

	mv->m_outWishVel = { 0, 0, 0 };
	mv->m_outJumpVel = { 0, 0, 0 };

	(*Interfaces::MoveHelperClient)->ResetTouchList();

	ReduceTimers();

	AngleVectors(mv->m_vecViewAngles, &m_vecForward, &m_vecRight, &m_vecUp);

	auto moveType = player->GetMoveType();

	if (moveType == MOVETYPE_NOCLIP || moveType == MOVETYPE_ISOMETRIC || moveType == MOVETYPE_OBSERVER || player->GetDeadFlag() || !CheckInterval(STUCK) || !CheckStuck())
	{
		if (moveType != MOVETYPE_WALK || mv->m_bGameCodeMovedPlayer || !sv_optimizedmovement.GetVar()->GetBool())
			CategorizePosition();
		else if (mv->m_vecVelocity_.z > 250.0f)
		{
			SetGroundEntity(nullptr);
		}

		m_nOldWaterLevel = player->GetWaterLevel();

		if (player->GetGroundEntity() == nullptr)
		{
			// *(*(cgamemovement + 8) + 0x48) ^ 80000000800000008000000080000000h same as negating floats
			player->SetFallVelocity(-mv->m_vecVelocity_.z);
		}

		m_nOnLadder = 0;

		//  (*(*basePlayer_1 + 0x544))(*(basePlayer_1 + 0x35A0), &moveData_1->m_vecAbsOrigin, &moveData_1->m_vecVelocity);
		// Calls CBasePlayer's 337th index with surfacedata (player + 35A0)
		player->UpdateStepSound(player->GetSurfaceData(), mv->_m_vecAbsOrigin, mv->m_vecVelocity_);

		UpdateDuckJumpEyeOffset();
		Duck();

		if (!player->GetDeadFlag() && !(player->GetFlags() & FL_ONTRAIN) && !LadderMove())
		{
			if (moveType == MOVETYPE_LADDER)
			{
				//*(basePlayer_3 + 0x258) = 2;
				// *(*(cgamemovement + 4) + 0x259) = 0;
				player->SetMoveType(MOVETYPE_WALK);
				player->SetMoveCollide(MOVECOLLIDE_DEFAULT);
			}
		}

		switch (player->GetMoveType())
		{
		case MOVETYPE_NONE:
			return;

		case MOVETYPE_ISOMETRIC:
		case MOVETYPE_WALK:
		{
			FullWalkMove();
			break;
		}

		case MOVETYPE_FLY:
		case MOVETYPE_FLYGRAVITY:
		{
			FullTossMove();
			break;
		}
		case MOVETYPE_NOCLIP:
		{
			FullNoClipMove(sv_noclipspeed.GetVar()->GetFloat(), sv_noclipaccelerate.GetVar()->GetFloat());
			break;
		}
		case MOVETYPE_LADDER:
		{
			FullLadderMove();
			break;
		}

		case MOVETYPE_OBSERVER:
		{
			FullObserverMove();
			break;
		}
		default:
			//DevMsg(1, "Bogus pmove player movetype %i on (%i) 0=cl 1=sv\n", player->IsServer(), 0);
			break;
		}
	}
}

void CCSGameMovement::AutoMountMove()
{
	auto player = m_pCSPlayer;
	float dt = player->GetAutomoveTargetTime() - player->GetAutomoveStartTime();
	float newtime = (Interfaces::Globals->curtime - (player->GetAutomoveTargetTime() - dt)) / dt;
	newtime = clamp(newtime, 0.0f, 1.0f);
	float fuck = powf(newtime, 0.6214906f);
	Vector origin = (player->GetAutomoveTargetEnd() - player->GetAutoMoveOrigin()) * fuck + player->GetAutoMoveOrigin();
	mv->m_vecVelocity_ = vecZero;
	mv->_m_vecAbsOrigin = origin;
	if ((origin - player->GetAutomoveTargetEnd()).Length() >= 1.5f)
	{
		mv->m_flForwardMove = 0.0f;
		mv->m_flSideMove = 0.0f;
		mv->m_flUpMove = 0.0f;
		mv->m_nButtons &= 0xFFFFF9E5;
	}
	else
	{
		if (player->IsAutoMounting())
			player->SetIsAutoMounting(0);

		Vector vForward, vRight, vUp;
		AngleVectors(mv->m_vecViewAngles, &vForward, &vRight, &vUp);

		VectorNormalizeFast(vForward);
		VectorNormalizeFast(vRight);
		VectorNormalizeFast(vUp);

		Vector2D dest;
		Vector delta = player->GetAutomoveTargetEnd() - player->GetAutoMoveOrigin();

		if (delta.Length2D() == 0.0f)
		{
			dest = { 0.0f, 0.0f };
		}
		else
		{
			dest.x = delta.x * (1.0f / vUp.z);
			dest.y = delta.y * (1.0f / vUp.z);
		}

		float newflt = ((((dest.y * vRight.x) + (dest.x * vRight.y) + (0.0f * 0.0f)) + 1.0f) * 0.5f) * fmaxf(player->GetAutomoveTargetEnd().y, 80.0f);

		mv->m_vecVelocity_ = { dest.x * newflt, dest.y * newflt, 0.0f };

		OnLand(50.0f);
		player->GetPunchAdr()->x = 0.0f;
	}
}

//dangerzone correct
void CCSGameMovement::PlayerMove()
{
	if (!CanMove(m_pCSPlayer))
	{
		mv->m_flForwardMove = 0.0f;
		mv->m_flSideMove = 0.0f;
		mv->m_flUpMove = 0.0f;
		mv->m_nButtons &= ~(IN_JUMP | IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
	}

	if (m_pCSPlayer->IsAutoMounting() <= 0)
	{
		CGameMovement::PlayerMove(); // BaseClass::PlayerMove();

		if (m_pCSPlayer->GetFlags() & FL_ONGROUND)
		{
			float velocitymodifier = m_pCSPlayer->GetVelocityModifier();
			if (velocitymodifier < 1.0f)
			{
				float newvelocitymodifier = velocitymodifier + (Interfaces::Globals->frametime * 0.4f);
				if (newvelocitymodifier >= 0.0f)
					newvelocitymodifier = fminf(newvelocitymodifier, 1.0f);

				if (velocitymodifier != newvelocitymodifier)
					m_pCSPlayer->SetVelocityModifier(velocitymodifier);
			}
		}

		m_pCSPlayer->SetDesiredCollisionGroup(m_pCSPlayer->GetCollisionGroup());
	}
	else
	{
		AutoMountMove();
	}
}

//-----------------------------------------------------------------------------
// Performs the collision resolution for fliers.
//-----------------------------------------------------------------------------
void CGameMovement::PerformFlyCollisionResolution(trace_t& pm, Vector& move)
{
	Vector base;
	float vel;
	float backoff;

	switch (player->GetMoveCollide())
	{
	case MOVECOLLIDE_FLY_CUSTOM:
		// Do nothing; the velocity should have been modified by touch
		// FIXME: It seems wrong for touch to modify velocity
		// given that it can be called in a number of places
		// where collision resolution do *not* in fact occur

		// Should this ever occur for players!?
		//Assert(0);
		break;

	case MOVECOLLIDE_FLY_BOUNCE:
	case MOVECOLLIDE_DEFAULT:
	{
		if (player->GetMoveCollide() == MOVECOLLIDE_FLY_BOUNCE)
			backoff = 2.0 - player->GetSurfaceFriction();
		else
			backoff = 1;

		ClipVelocity(mv->m_vecVelocity_, pm.plane.normal, mv->m_vecVelocity_, backoff);
	}
	break;

	default:
		// Invalid collide type!
		//Assert(0);
		break;
	}

	// stop if on ground
	if (pm.plane.normal[2] > 0.7)
	{
		base.Init();
		if (mv->m_vecVelocity_[2] < sv_gravity.GetVar()->GetFloat() * Interfaces::Globals->frametime)
		{
			// we're rolling on the ground, add static friction.
			SetGroundEntity(&pm);
			mv->m_vecVelocity_[2] = 0;
		}

		vel = DotProduct(mv->m_vecVelocity_, mv->m_vecVelocity_);

		// Con_DPrintf("%f %f: %.0f %.0f %.0f\n", vel, trace.fraction, ent->velocity[0], ent->velocity[1], ent->velocity[2] );

		if (vel < (30 * 30) || (player->GetMoveCollide() != MOVECOLLIDE_FLY_BOUNCE))
		{
			SetGroundEntity(&pm);
			mv->m_vecVelocity_.Init();
		}
		else
		{
			VectorScale(mv->m_vecVelocity_, (1.0 - pm.fraction) * Interfaces::Globals->frametime * 0.9, move);
			PushEntity(move, &pm);
		}
		VectorSubtract(mv->m_vecVelocity_, base, mv->m_vecVelocity_);
	}
}

float CGameMovement::CalcRoll(const QAngle& angles, const Vector& velocity, float rollangle, float rollspeed)
{
	Vector forward, right, up;
	AngleVectors((QAngle)angles, &forward, &right, &up);

	float side = DotProduct(velocity, right);
	float sign = (side < 0.f) ? -1.f : 1.f;
	side = fabs(side);

	float temp = rollangle;
	if (side < rollspeed)
		side = (side * temp) / rollspeed;
	else
		side = temp;

	return side * sign;
}

void CGameMovement::DecayViewPunchAngle()
{
	// dont know if its a float or int
	auto viewPunchDecay = view_punch_decay.GetVar()->GetFloat();

	float timemultiplier = Interfaces::Globals->interval_per_tick * viewPunchDecay;
	timemultiplier = expf(-timemultiplier);
	QAngle viewpunch = player->GetViewPunch();
	QAngle newpunch = viewpunch * timemultiplier;

	if (sqrtf(pow(newpunch.x, 2) + pow(newpunch.y, 2) + pow(newpunch.z, 2)) <= 0.0f)
		newpunch = angZero;

	if (newpunch != viewpunch)
	{
		player->GetLocalData()->NetworkStateChanged((void*)player->GetViewPunchAdr());
		player->SetViewPunch(newpunch);
	}
}

//dangerzone correct
void CGameMovement::CheckWaterJump()
{
	if (player->GetWaterJumpTime() == 0.0f)
	{
		if (mv->m_vecVelocity_.z >= -180.f)
		{
			Vector flatVelocity = { mv->m_vecVelocity_.x, mv->m_vecVelocity_.y, 0.0f };
			float curspeed = VectorNormalize(flatVelocity);

			Vector flatForward = { m_vecForward.x, m_vecForward.y, 0.0f };
			VectorNormalizeFast(flatForward);

			if (curspeed != 0.0f && DotProduct(flatVelocity, flatForward) >= 0.0f)
			{
				Vector start, end;
				start = mv->_m_vecAbsOrigin + GetPlayerMins() + GetPlayerMaxs() * 0.5f;

				VectorMA(start, 24.0f, flatForward, end);

				auto mask = PlayerSolidMask();

				trace_t tr;
				TracePlayerBBox(start, end, mask, COLLISION_GROUP_PLAYER_MOVEMENT, tr);

				if (tr.fraction < 1.f)
				{
					// physicsObj = *(v34 + 0x29C);
					IPhysicsObject* physObj = tr.m_pEnt->VPhysicsGetObject();

					if (physObj)
					{
						if ((physObj->GetGameFlags() & FVPHYSICS_PLAYER_HELD))
							return;
					}

					// !((*(*physicsObj + 76))() & 4) )
					if (!tr.m_pEnt || tr.m_pEnt->CanWaterJump())
					{
						start.z = mv->_m_vecAbsOrigin.z + player->GetViewOffset().z + 8.f;

						VectorMA(start, 24.0f, flatForward, end);
						// vecWaterJumpVel = *(DWORD*)(player + 0x33E8);
						Vector waterjumpvel;
						VectorMA(Vector(0, 0, 0), -50.0f, tr.plane.normal, waterjumpvel);
						player->SetWaterJumpVel(waterjumpvel);

						TracePlayerBBox(start, end, mask, COLLISION_GROUP_PLAYER_MOVEMENT, tr);

						if (tr.fraction == 1.f)
						{
							start = end;
							end.z -= 1024.0f;

							TracePlayerBBox(start, end, mask, COLLISION_GROUP_PLAYER_MOVEMENT, tr);

							if (tr.fraction < 1.f && tr.plane.normal.z >= 0.7)
							{
								mv->m_vecVelocity_.z = 256.f;
								mv->m_nOldButtons |= IN_JUMP;
								player->AddFlag(FL_WATERJUMP);
								player->SetWaterJumpTime(2000.f);
							}
						}
					}
				}
			}
		}
	}
}

void CGameMovement::WaterMove()
{
	Vector forward, right, up;
	AngleVectors(mv->m_vecViewAngles, &forward, &right, &up);

	Vector wishvel;

	wishvel.x = (forward.x * mv->m_flForwardMove) + (right.x * mv->m_flSideMove);
	wishvel.y = (forward.y * mv->m_flForwardMove) + (right.y * mv->m_flSideMove);
	wishvel.z = (forward.z * mv->m_flForwardMove) + (right.z * mv->m_flSideMove);

	if (sv_water_swim_mode.GetVar()->GetInt() == 1)
	{
		float headz = mv->_m_vecAbsOrigin.z + player->GetViewOffset().z;
		//prevent going under water
		float waterz = UTIL_FindWaterSurface(mv->_m_vecAbsOrigin, headz, mv->_m_vecAbsOrigin.z);
		float newflt = (headz - 12.0f) - waterz;

		if (newflt >= 0.0f)
		{
			if (newflt <= 0.0f)
			{
				wishvel.z = 0.0f;
			}
			else
			{
				wishvel.z = newflt * -5.0f;
			}
		}
		else
		{
			SetGroundEntity(nullptr);
			wishvel.z = newflt * -15.0f;
		}
	}
	else
	{
		if (mv->m_nButtons & IN_JUMP)
		{
			wishvel.z = mv->m_flClientMaxSpeed + wishvel.z;
		}
		else if (mv->m_flForwardMove == 0.f && mv->m_flSideMove == 0.f && mv->m_flUpMove == 0.f)
		{
			float v18 = 0.0f;
			float v19 = (mv->m_flForwardMove * forward.z) + (mv->m_flForwardMove * forward.z);
			if (v19 >= 0.0f)
				v18 = fminf(mv->m_flClientMaxSpeed, v19);
			wishvel.z += (mv->m_flUpMove + v18);
		}
		else
		{
			wishvel.z -= 60.0f;
		}
	}

	Vector wishdir = wishvel;
	float wishspeed = VectorNormalize(wishdir);

	if (wishspeed > mv->_m_flMaxSpeed)
	{
		VectorScale(wishvel, mv->_m_flMaxSpeed / wishspeed, wishvel);
		wishspeed = mv->_m_flMaxSpeed;
	}

	wishspeed *= sv_water_movespeed_multiplier.GetVar()->GetFloat();

	Vector temp = mv->m_vecVelocity_;
	float speed = VectorNormalize(temp);

	float newspeed = 0.f;

	if (speed != 0.f)
	{
		// m_surfaceFriction = 0x3210;
		newspeed = speed - Interfaces::Globals->curtime * speed * sv_friction.GetVar()->GetFloat() * player->GetSurfaceFriction();

		if (newspeed < 0.1f)
			newspeed = 0.0f;

		VectorScale(mv->m_vecVelocity_, newspeed / speed, mv->m_vecVelocity_);
	}
	else
	{
		newspeed = 0.f;
	}

	if (wishspeed >= 0.1f)
	{
		float addspeed = wishspeed - newspeed;

		if (addspeed > 0.f)
		{
			VectorNormalizeFast(wishvel);
			float accelspeed = sv_accelerate.GetVar()->GetFloat() * wishspeed * Interfaces::Globals->curtime * player->GetSurfaceFriction();

			if (accelspeed > addspeed)
				accelspeed = addspeed;

			mv->m_vecVelocity_.x += (accelspeed * wishvel.x);
			mv->m_vecVelocity_.y += (accelspeed * wishvel.y);
			mv->m_vecVelocity_.z += (accelspeed * wishvel.z);

			mv->m_outWishVel.x += (accelspeed * wishvel.x);
			mv->m_outWishVel.y += (accelspeed * wishvel.x);
			mv->m_outWishVel.z += (accelspeed * wishvel.x);
		}
	}

	// *(ent + 0x11C) base velocity
	mv->m_vecVelocity_ += player->GetBaseVelocity();

	Vector dest;
	VectorMA(mv->_m_vecAbsOrigin, Interfaces::Globals->curtime, mv->m_vecVelocity_, dest);

	trace_t tr;
	TracePlayerBBox(mv->_m_vecAbsOrigin, dest, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

	if (tr.fraction != 1.f)
	{
		if (player->GetGroundEntity() != nullptr)
		{
			StepMove(dest, tr);
			goto jmp;
		}

		TryPlayerMove();
		goto jmp;
	}
	else
	{
		Vector start = dest;

		if (player->GetAllowAutoMovement())
			start.z += player->GetStepSize() + 1.f;

		TracePlayerBBox(start, dest, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

		if (tr.startsolid && !tr.allsolid)
		{
			float stepDist = tr.endpos.z - mv->_m_vecAbsOrigin.z;
			mv->m_outStepHeight += stepDist;

			mv->_m_vecAbsOrigin = tr.endpos;

			goto jmp;
		}

		TryPlayerMove();
	}

jmp:
	mv->m_vecVelocity_ -= player->GetBaseVelocity();
}

void CGameMovement::SlimeMove()
{
	Vector forward, right, up;
	AngleVectors(mv->m_vecViewAngles, &forward, &right, &up);

	Vector wishvel;

	wishvel.x = (forward.x * mv->m_flForwardMove) + (right.x * mv->m_flSideMove);
	wishvel.y = forward.y * (mv->m_flForwardMove) + (right.y * mv->m_flSideMove);
	wishvel.z = forward.z * (mv->m_flForwardMove) + (right.z * mv->m_flSideMove) - 5.0f;

	if (mv->m_nButtons & IN_JUMP)
		wishvel.z += mv->m_flClientMaxSpeed;

	float wishspeed = wishvel.Length();

	if (wishspeed > mv->_m_flMaxSpeed)
	{
		wishspeed = mv->_m_flMaxSpeed;
		VectorScale(wishvel, mv->_m_flMaxSpeed / wishspeed, wishvel);
	}

	float speed = mv->m_vecVelocity_.Length();

	float newspeed = 0.f;

	if (speed != 0.f)
	{
		// m_surfaceFriction = 0x3210;
		newspeed = speed - Interfaces::Globals->curtime * speed * sv_friction.GetVar()->GetFloat() * player->GetSurfaceFriction();

		if (newspeed < 0.1f)
			newspeed = 0.0f;

		VectorScale(mv->m_vecVelocity_, newspeed / speed, mv->m_vecVelocity_);
	}
	else
	{
		newspeed = 0.f;
	}

	if (wishspeed >= 0.1f)
	{
		float addspeed = wishspeed - newspeed;

		if (addspeed > 0.f)
		{
			VectorNormalizeFast(wishvel);
			float accelspeed = sv_accelerate.GetVar()->GetFloat() * wishspeed * Interfaces::Globals->curtime * player->GetSurfaceFriction();

			if (accelspeed > addspeed)
				accelspeed = addspeed;

			mv->m_vecVelocity_.x += (accelspeed * wishvel.x);
			mv->m_vecVelocity_.y += (accelspeed * wishvel.y);
			mv->m_vecVelocity_.z += (accelspeed * wishvel.z);

			mv->m_outWishVel.x += (accelspeed * wishvel.x);
			mv->m_outWishVel.y += (accelspeed * wishvel.x);
			mv->m_outWishVel.z += (accelspeed * wishvel.x);
		}
	}

	// *(ent + 0x11C) base velocity
	mv->m_vecVelocity_ += player->GetBaseVelocity();

	float maxvelspeed, minvelspeed;

	if (player->GetUnknownEntity(0) == 0)
	{
		if (player->GetWaterLevel() < WL_Waist)
		{
			maxvelspeed = 120.0f;
			minvelspeed = -50.0f;
		}
		else
		{
			maxvelspeed = 60.0f;
			minvelspeed = -25.0f;
		}
		if (minvelspeed <= mv->m_vecVelocity_.z)
			minvelspeed = fminf(mv->m_vecVelocity_.z, 5.0f);
	}
	else
	{
		if (player->GetWaterLevel() < WL_Waist)
		{
			minvelspeed = -100.0f;
			maxvelspeed = 80.0f;
			if (mv->m_vecVelocity_.z >= 100.0f)
				minvelspeed = fminf(mv->m_vecVelocity_.z, 0.0f);
		}
		else
		{
			minvelspeed = -50.0f;
			maxvelspeed = 30.0f;
			if (mv->m_vecVelocity_.z >= -50.0f)
				minvelspeed = fminf(mv->m_vecVelocity_.z, 0.0f);
		}
	}
	mv->m_vecVelocity_.z = minvelspeed;

	if (mv->m_vecVelocity_.Length() > maxvelspeed)
	{
		VectorNormalize(mv->m_vecVelocity_);
		mv->m_vecVelocity_ *= maxvelspeed;
	}

	Vector dest;
	VectorMA(mv->_m_vecAbsOrigin, Interfaces::Globals->curtime, mv->m_vecVelocity_, dest);

	trace_t tr;
	TracePlayerBBox(mv->_m_vecAbsOrigin, dest, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

	if (tr.fraction != 1.f)
	{
		if (player->GetGroundEntity() != nullptr)
		{
			StepMove(dest, tr);
			mv->m_vecVelocity_ -= player->GetBaseVelocity();
			return;
		}

		TryPlayerMove();

		mv->m_vecVelocity_ -= player->GetBaseVelocity();
		return;
	}
	else
	{
		Vector start = dest;

		if (player->GetAllowAutoMovement())
			start.z += player->GetStepSize() + 1.f;

		TracePlayerBBox(start, dest, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

		if (tr.startsolid && !tr.allsolid)
		{
			float stepDist = tr.endpos.z - mv->_m_vecAbsOrigin.z;
			mv->m_outStepHeight += stepDist;

			mv->_m_vecAbsOrigin = tr.endpos;

			mv->m_vecVelocity_ -= player->GetBaseVelocity();
			return;
		}

		TryPlayerMove();
	}

	mv->m_vecVelocity_ -= player->GetBaseVelocity();
}

void CGameMovement::WaterJump()
{
	if (player->GetWaterJumpTime() > 10000.f)
		player->SetWaterJumpTime(10000.f);

	if (player->GetWaterJumpTime() != 0.f)
	{
		player->SetWaterJumpTime(player->GetWaterJumpTime() - Interfaces::Globals->frametime * 10000.f);

		if (player->GetWaterJumpTime() <= 0.f || player->GetWaterLevel() == 0)
		{
			player->SetWaterJumpTime(0.f);
			player->RemoveFlag(FL_WATERJUMP);
		}

		mv->m_vecVelocity_.x = player->GetWaterJumpVel().x;
		mv->m_vecVelocity_.y = player->GetWaterJumpVel().y;
	}
}

void CGameMovement::Friction()
{
	if (player->GetWaterJumpTime() == 0.f)
	{
		float speed = mv->m_vecVelocity_.Length();

		if (speed >= 0.1f)
		{
			float drop = 0.f;

			if (player->GetGroundEntity() != nullptr)
			{
				float friction = player->GetSurfaceFriction() * sv_friction.GetVar()->GetFloat();
				float stopspeed = sv_stopspeed.GetVar()->GetFloat();
				float control = speed < stopspeed ? stopspeed : speed;

				drop = (control * friction) * Interfaces::Globals->frametime;
			}

			float newspeed = fmaxf(speed - drop, 0.f);

			if (newspeed != speed)
			{
				newspeed /= speed;
				mv->m_vecVelocity_ *= newspeed;
			}

			mv->m_outWishVel -= (mv->m_vecVelocity_ * (1.f - newspeed));
		}
	}
}

void CGameMovement::AirAccelerate(Vector& wishdir, float wishspeed, float accel)
{
	if (!player->GetDeadFlag() && player->GetWaterJumpTime() == 0.f)
	{
		float v3 = fminf(wishspeed, 30.f) - mv->m_vecVelocity_.Dot(wishdir);

		if (v3 > 0.f)
		{
			float v7 = fminf(wishspeed * accel * Interfaces::Globals->frametime * player->GetSurfaceFriction(), v3);

			mv->m_vecVelocity_ += wishdir * v7;
			mv->m_outWishVel += wishdir * v7;
		}
	}
}


//This function is not fully rebuilt because Valve changed this function many times shortly after dangerzone came out and I didn't feel like finishing it
void CCSGameMovement::AirMove()
{
	CGameMovement::AirMove();

	auto cplayer = m_pCSPlayer;
	if (cplayer->IsAutoMounting())
		cplayer->SetIsAutoMounting(0);

	int ledgehelper = sv_ledge_mantle_helper.GetVar()->GetInt();
	if (ledgehelper <= 0)
		return;

	bool debug = sv_ledge_mantle_helper_debug.GetVar()->GetBool();

	Vector vForward;
	cplayer->EyeVectors(&vForward);

	float v107;
	float v108;

	float len = vForward.Length2D();
	if (len == 0.0f)
	{
		v108 = 0.0f;
		v107 = 0.0f;
	}
	else
	{
		v107 = (1.0f / len) * vForward.x;
		v108 = (1.0f / len) * vForward.y;
	}
	Vector boxmin = { -12.0f, -12.0f, 0.0f };
	Vector boxmax = { 12.0f, 12.0f, 3.0f };
	Vector boxmin2 = { -10.0f, -10.0f, 0.0f };
	Vector boxmax2 = { 10.0f, 10.0f, 24.0f };

	if (mv->m_flForwardMove <= 0.0f || !(mv->m_nButtons & IN_DUCK))
		return;

	Vector vel = mv->m_vecVelocity_;

	float speed = vel.Length();

	float speedclamp = fmaxf(speed * 0.12f, 12.0f);

	Vector *origin = cplayer->GetAbsOrigin();

	trace_t tr;
	UTIL_TraceLine(*origin, *origin + Vector(0.0f, 0.0f, -14.0f), 0x2000B, cplayer, COLLISION_GROUP_PLAYER_MOVEMENT, &tr);


	Vector end;
	end.x = (v107 * speedclamp) + origin->x;
	end.y = (v108 * speedclamp) + origin->y;
	end.z = (speedclamp * 0.0f) + origin->z;

	trace_t tr2;
	UTIL_TraceHull(*origin, end, boxmin2, boxmax2, PlayerSolidMask(), cplayer, COLLISION_GROUP_PLAYER_MOVEMENT, tr2);

	if (debug)
	{
		if (tr2.fraction == 1.0f && Interfaces::DebugOverlay)
			Interfaces::DebugOverlay->AddBoxOverlay(tr2.endpos, boxmin2, boxmax2, angZero, 255, 0, 80, 255, 6.0f);

		if (tr.fraction != 1.0f)
		{
			Vector endpos = *cplayer->GetAbsOrigin();
			endpos.z -= 14.0f;
			if (Interfaces::ClientEntList->GetBaseEntity(Interfaces::EngineClient->GetLocalPlayer()) && Interfaces::DebugOverlay)
				Interfaces::DebugOverlay->AddLineOverlay(*cplayer->GetAbsOrigin(), endpos, 255, 0, 80, 1, 6.0f);
		}
	}
	if (tr.fraction != 1.0f)
		return;
	if (tr2.fraction >= 1.0f)
		return;

	Vector pos1 = *cplayer->GetAbsOrigin();
	pos1.z += 2.0f;

	Vector pos2 = pos2;
	float v110 = v107 + 20.0f;
	pos2.x += (v107 * (v107 + 20.0f));
	pos2.y += (v108 * (v107 + 20.0f));
	pos2.z += ((v107 + 20.0f) * 0.0f);

	ledgehelper = sv_ledge_mantle_helper.GetVar()->GetFloat();

	if (ledgehelper == 3)
	{
		float zclamp = fminf(vForward.z, 0.44f);
		Vector eyepos;
		cplayer->EyePosition(eyepos);

		pos1.x = eyepos.x;
		pos1.y = eyepos.y;

		eyepos.z -= 4.0f;
		pos2.x = (v110 * speedclamp) + eyepos.x;
		pos2.y = (v110 * vForward.y) + eyepos.y;
		pos2.z = eyepos.z + (zclamp * v110);

		pos1.z = eyepos.z;
	}

	CTraceFilterForPlayerHeadCollision filter;
	filter.m_pPassEnt1 = (IHandleEntity*)cplayer;
	filter.m_pSkip = filter.m_pPassEnt1;
	filter.m_iCollisionGroup = COLLISION_GROUP_PLAYER_MOVEMENT;
	filter.m_Unknown = 0;

	trace_t tr3;
	UTIL_TraceHull(pos1, pos2, boxmin, boxmax, PlayerSolidMask(), (ITraceFilter*)&filter, tr3);

	if (debug)
	{
		if (tr3.fraction != 1.0f)
		{
			if (Interfaces::DebugOverlay)
				Interfaces::DebugOverlay->AddBoxOverlay(tr3.endpos, boxmin, boxmax, angZero, 255, 0, 0, 128, 6.0f);
			return;
		}
		if (Interfaces::DebugOverlay)
			Interfaces::DebugOverlay->AddBoxOverlay(tr3.endpos, boxmin, boxmax, angZero, 0, 255, 0, 255, 6.0f);
	}
	else if (tr3.fraction != 1.0f)
	{
		return;
	}

	float v47 = (tr2.fraction * v107) + 1.5f;
	trace_t tr4;

	Vector startnew;
	startnew.z = pos1.z;
	startnew.x = tr2.endpos.x + (v107 * v47);
	startnew.y = tr2.endpos.y + (v108 * v47);
	Vector endnew;
	endnew.x = startnew.x;
	endnew.y = startnew.y;
	endnew.z = pos1.z - 64.0f;

	UTIL_TraceHull(startnew, endnew, boxmin, boxmax, PlayerSolidMask(), (ITraceFilter*)&filter, tr4);
	if (tr4.DidHit())
	{
		Vector newend2;
		newend2 = tr4.endpos;
		newend2.z += 3.0f;

		CViewVectors* vectors = GetGamerules()->GetViewVectors();
		trace_t tr5;
		UTIL_TraceHull(tr4.endpos, newend2, vectors->m_vHullMin, vectors->m_vHullMax, PlayerSolidMask(), (ITraceFilter*)&filter, tr5);

		bool v100;

		if (tr5.startsolid || tr5.allsolid || (v100 = true, tr5.fraction != 1.0f))
			v100 = false;

		Vector newend3 = tr4.endpos;
		newend3.z += 3.0f;
		trace_t tr6;

		UTIL_TraceHull(tr4.endpos, newend3, vectors->m_vDuckHullMin, vectors->m_vDuckHullMax, PlayerSolidMask(), (ITraceFilter*)&filter, tr6);

		if (tr6.startsolid || tr6.allsolid || tr6.fraction != 1.0f)
		{
			Vector end4;
			//end4.x = tr4.endpos.x + (tr.endpos.x)

			//FIXME: FINISH ME
		}
	}
}

void CGameMovement::AirMove()
{
	Vector forward, right, up;

	AngleVectors(mv->m_vecViewAngles, &forward, &right, &up);

	float sidemove = mv->m_flSideMove;
	float forwardmove = mv->m_flForwardMove;

	forward.z = 0.f;
	right.z = 0.f;

	VectorNormalizeFast(forward);
	VectorNormalizeFast(right);

	Vector wishVel = {
		forward.x * forwardmove + right.x * sidemove,
		forward.y * forwardmove + right.y * sidemove,
		0.f
	};

	Vector wishdir = wishVel;
	float wishspeed = VectorNormalize(wishdir);

	if (wishspeed != 0.f && wishspeed > mv->_m_flMaxSpeed)
	{
		wishspeed = mv->_m_flMaxSpeed;
	}

	float airaccelerate;
	float airaccelerate_parachute;

	CBaseEntity *movingplayer = GetMovingPlayer();
	if (!movingplayer || !movingplayer->IsPlayer() || movingplayer->GetMaxFallVelocity() == 0.0f)
	{
		airaccelerate = sv_airaccelerate.GetVar()->GetFloat();
	}
	else
	{
		if (!movingplayer->IsSpawnRappelling())
		{
			if (sv_air_pushaway_dist.GetVar()->GetFloat())
			{
				movingplayer = GetMovingPlayer();
				if (movingplayer->GetAliveVMT())
				{
					Vector someVector = vecZero;
					Vector destVector = vecZero;
					for (int i = 1; i <= MAX_PLAYERS; i++) //VALVE BUG: this is set to 0 and < MAX_PLAYERS which is wrong!!
					{
						CBaseEntity* playerindex = Interfaces::ClientEntList->GetBaseEntity(i); //UTIL_PlayerFromIndex is the original function
						if (playerindex && playerindex->IsPlayer() && movingplayer != playerindex && playerindex->GetAliveVMT() && playerindex->GetMaxFallVelocity() != 0.0f)
						{
							Vector movingplayerorigin = *playerindex->GetAbsOrigin();
							Vector vecDelta = movingplayerorigin - *movingplayer->GetAbsOrigin();
							float dist = vecDelta.Length();
							float pushawaydist = sv_air_pushaway_dist.GetVar()->GetFloat();
							if (pushawaydist > dist)
							{
								float maxspd = mv->_m_flMaxSpeed;
								if (pushawaydist == 32.0f)
								{
									if (dist - pushawaydist >= 0.0f)
										maxspd = 0.0f;
								}
								else
								{
									float v41 = (dist - 32.0f) / (pushawaydist - 32.0f);
									v41 = clamp(v41, 0.0f, 1.0f);
									maxspd *= (1.0f - v41);
								}

								Vector normalized = movingplayerorigin;
								VectorNormalizeFast(normalized);
								destVector = someVector;
								Vector newpos = normalized * maxspd;

								destVector = someVector + newpos;
								someVector += newpos;

								continue;
							}
						}
						destVector = someVector;
					}

					if (destVector.LengthSqr() != 0.0f)
					{
						Vector destcpy = destVector;
						VectorNormalizeFast(destcpy);
						wishspeed = destcpy.Length();
					}
				}
			}
			airaccelerate = sv_airaccelerate_parachute.GetVar()->GetFloat();
			//if couldn't find sv_airaccelerate_parachute, use sv_airaccelerate
		}
		else
		{
			airaccelerate = sv_airaccelerate_rappel.GetVar()->GetFloat();
		}
	}

	AirAccelerate(wishdir, wishspeed, airaccelerate);

	mv->m_vecVelocity_ += player->GetBaseVelocity();

	TryPlayerMove();

	mv->m_vecVelocity_ -= player->GetBaseVelocity();
}

bool CGameMovement::CanAccelerate()
{
	return (!player->GetDeadFlag() && player->GetWaterJumpTime() == 0.f);
}

//dangerzone correct
bool CCSGameMovement::CanAccelerate()
{
	if (m_pCSPlayer->GetPlayerState())
		return player->IsObserver();
	else
		return (player->GetWaterJumpTime() == 0.f);
}

void CGameMovement::Accelerate(Vector& wishdir, float wishspeed, float accel)
{
	if (CanAccelerate())
	{
		float v6 = wishspeed - mv->m_vecVelocity_.Dot(wishdir);

		if (v6 > 0.f)
		{
			float v7 = fminf(Interfaces::Globals->frametime * accel * fmaxf(wishspeed, 250.f) * player->GetSurfaceFriction(), v6);

			mv->m_vecVelocity_ += wishdir * v7;
		}
	}
}

//dangerzone correct, NOT CORRECT AFTER APRIL 30, 2019 UPDATE
void CCSGameMovement::Accelerate(Vector& wishdir, float wishspeed, float accel)
{
	if (!CanAccelerate())
		return;

	float v7 = mv->m_vecVelocity_.Dot(wishdir);
	float v60 = wishspeed - v7;

	if (v60 <= 0.f)
		return;

	float v57 = fmaxf(v7, 0.f);
	bool bDucking = true;

	if (!(mv->m_nButtons & IN_DUCK))
	{
		if (!player->GetDucking() && !(player->GetFlags() & FL_DUCKING))
			bDucking = false;
	}

	bool bUnknown2 = true;

	if (!(mv->m_nButtons & IN_SPEED) || bDucking)
		bUnknown2 = false;

	float backupfinalwishspeed;
	float finalwishspeed = fmaxf(wishspeed, 250.f);
	float backupfinalwishspeed_new = finalwishspeed;
	float finalwishspeed2;

	CBaseEntity* move_player = player;
	if (!player || !player->IsPlayer())
		move_player = nullptr;

	CBaseCombatWeapon* weapon = player->GetWeapon();

	bool bShouldSlowSpeedDown = false;
	bool bUnknown = false;
	float abs_final_wish_speed;

	if (sv_accelerate_use_weapon_speed.GetVar()->GetBool() && weapon != nullptr)
	{
		float flWpnMoveModifier = weapon->GetMaxSpeed();
		float flClampedWpnMoveModifier;
		float flBackupWpnMoveModifier;

		if (weapon->GetZoomLevelVMT() <= 0 || weapon->GetNumZoomLevels() <= 1)
			bShouldSlowSpeedDown = false;
		else
		{
			flBackupWpnMoveModifier = weapon->GetMaxSpeed() * _CS_PLAYER_SPEED_WALK_MODIFIER;
			if (110.0f > flBackupWpnMoveModifier)
			{
				bShouldSlowSpeedDown = true;
			}
			else
			{
				bShouldSlowSpeedDown = false;
			}
		}

		flWpnMoveModifier = weapon->GetMaxSpeed() * _CS_PLAYER_MAXSPEED_MODIFIER;

		flClampedWpnMoveModifier = fminf(flWpnMoveModifier, 1.0f);

		if (!bDucking && !bUnknown2 || bShouldSlowSpeedDown)
			finalwishspeed *= flClampedWpnMoveModifier;

		//decrypts(0)
		static ConVar* sv_weapon_encumbrance_scale = Interfaces::Cvar->FindVar(XorStr("sv_weapon_encumbrance_scale"));
		//encrypts(0)
		if (sv_weapon_encumbrance_scale->GetFloat() != 0.0f && move_player)
		{
			float enc = move_player->GetEncumberance();
			flBackupWpnMoveModifier = enc;
			if (flClampedWpnMoveModifier > enc)
			{
				bUnknown = true;
				//decrypts(0)
				static ConVar* sv_weapon_encumbrance_scale = Interfaces::Cvar->FindVar(XorStr("sv_weapon_encumbrance_scale"));
				//encrypts(0)
				flClampedWpnMoveModifier += ((flBackupWpnMoveModifier - flClampedWpnMoveModifier) * sv_weapon_encumbrance_scale->GetFloat());
			}
			abs_final_wish_speed = fmaxf(flClampedWpnMoveModifier, _CS_PLAYER_SPEED_DUCK_MODIFIER) * backupfinalwishspeed_new;
		}
		else
		{
			abs_final_wish_speed = flClampedWpnMoveModifier * backupfinalwishspeed_new;
		}
	}
	else
	{
		abs_final_wish_speed = finalwishspeed;
	}

	if (bDucking)
	{
		if (!bShouldSlowSpeedDown)
			finalwishspeed *= _CS_PLAYER_SPEED_DUCK_MODIFIER;
		abs_final_wish_speed *= _CS_PLAYER_SPEED_DUCK_MODIFIER;
	}

	if (bUnknown2)
	{
		if (!m_pCSPlayer->HasHeavyArmor() && !m_pCSPlayer->IsCarryingHostage() && !bShouldSlowSpeedDown)
			finalwishspeed *= _CS_PLAYER_SPEED_WALK_MODIFIER;

		abs_final_wish_speed *= _CS_PLAYER_SPEED_WALK_MODIFIER;
	}

	float surfacefriction = player->GetSurfaceFriction();
	float v26 = ((Interfaces::Globals->frametime * accel) * finalwishspeed) * surfacefriction;

	if (bUnknown && v57 > (abs_final_wish_speed - v26))
	{
		float v27 = 1.0f
			- (fmaxf(v57 - (abs_final_wish_speed - v26), 0.0f)
				/ fmaxf(abs_final_wish_speed - (abs_final_wish_speed - v26), 0.0f));

		if (v27 >= 0.0f)
			accel = fminf(v27, 1.0f) * accel;
		else
			accel *= 0.0f;
	}

	if (bUnknown2 && v57 > (abs_final_wish_speed - 5.0f))
	{
		float v28 = fmaxf(v57 - (abs_final_wish_speed - 5.0f), 0.0f)
			/ fmaxf(abs_final_wish_speed - (float)(abs_final_wish_speed - 5.0f), 0.0f);
		if ((1.0f - v28) >= 0.0f)
			accel = fminf(1.0f - v28, 1.0f) * accel;
		else
			accel = 0.0f * accel;
	}

	float v30 = fminf(((Interfaces::Globals->frametime * accel) * finalwishspeed) * surfacefriction, v60);

	if (move_player)
	{
		float boostdelta = move_player->GetHealthShotBoostExpirationTime() - Interfaces::Globals->curtime;
		if (boostdelta >= 0.0f && fminf(boostdelta, 1.0f) > 0.0f)
		{
			//decrypts(0)
			static ConVar* healthshot_healthboost_speed_multiplier = Interfaces::Cvar->FindVar(XorStr("healthshot_healthboost_speed_multiplier"));
			//encrypts(0)
			if (boostdelta >= 0.0f)
				boostdelta = fminf(boostdelta, 1.0f);
			v30 = v30 * (((healthshot_healthboost_speed_multiplier->GetFloat() - 1.0f) * boostdelta) + 1.0f);
		}
	}

	//float v24 = fminf(Interfaces::Globals->frametime * accel * backupfinalwishspeed * player->GetSurfaceFriction(), v46);

	mv->m_vecVelocity_ += wishdir * v30;

	// movss   xmm0, dword ptr [ecx+0A2C0h]
	if (m_pCSPlayer->GetGroundAccelLinearFracLastTime() != Interfaces::Globals->curtime)
	{
		m_pCSPlayer->SetGroundAccelLinearFracLastTime(Interfaces::Globals->curtime);
	}

	if (mv->m_vecOldVelocity.x > -0.0099999998f && mv->m_vecOldVelocity.x < 0.0099999998f)
	{
		if (mv->m_vecOldVelocity.y > -0.0099999998f && mv->m_vecOldVelocity.y < 0.0099999998f)
		{
			if (mv->m_vecOldVelocity.z > -0.0099999998f && mv->m_vecOldVelocity.z < 0.0099999998f)
			{
				mv->m_vecOldVelocity = mv->m_vecVelocity_;
				mv->somefloat = Interfaces::Globals->curtime;
				return;
			}
		}
	}

	// subss   xmm0, dword ptr [edx+58h]
	float v37 = Interfaces::Globals->curtime - mv->somefloat;

	if (v37 > 0.35f)
	{
		mv->m_vecOldVelocity = mv->m_vecVelocity_;
		mv->somefloat = Interfaces::Globals->curtime;
		return;
	}

	Vector2D& velocity = mv->m_vecVelocity_.AsVector2D();
	Vector2D oldvelocity = mv->m_vecOldVelocity.AsVector2D();
	velocity.NormalizeInPlace();
	oldvelocity.NormalizeInPlace();

	float velocitylengths = oldvelocity.y * velocity.y + oldvelocity.x * velocity.x;

	if (velocitylengths > 0.8f)
	{
		if (mv->m_vecVelocity_.Length2DSqr() <= mv->m_vecOldVelocity.Length2DSqr())
			return;

		mv->m_vecOldVelocity = mv->m_vecVelocity_;
		mv->somefloat = Interfaces::Globals->curtime;
		return;
	}

	if (velocitylengths < -0.8f && mv->m_vecOldVelocity.Length2D() < 225.f && mv->m_vecOldVelocity.Length2D() > 115.f && mv->m_vecVelocity_.Length2D() > 115.f)
	{
		player->EyeVectors(&oldvelocity.AsVector(), 0, 0); //Basically just does AngleVectors unless in vehicle
		float velocitylenghts2 = (oldvelocity.y * velocity.y) + (oldvelocity.x * velocity.x);

		if (velocitylenghts2 > -0.3f && velocitylenghts2 < 0.3f)
		{
			if (m_pCSPlayer->GetWeapon())
			{
				mv->m_vecOldVelocity = mv->m_vecVelocity_;
				mv->somefloat = Interfaces::Globals->curtime;
			}
		}
	}
}

//dangerzone correct
void CCSGameMovement::WalkMove()
{
	if (m_pCSPlayer->IsAutoMounting())
		m_pCSPlayer->SetIsAutoMounting(0);
	CGameMovement::WalkMove();
}

//dangerzone correct
void CGameMovement::WalkMove()
{
	Vector forward, right, up;
	AngleVectors(mv->m_vecViewAngles, &forward, &right, &up);

	auto ground = player->GetGroundEntity();

	if (forward[2] != 0)
	{
		forward[2] = 0;
		VectorNormalizeFast(forward);
	}

	if (right[2] != 0)
	{
		right[2] = 0;
		VectorNormalizeFast(right);
	}

	float forwardmove = mv->m_flForwardMove;
	float sidemove = mv->m_flSideMove;

	Vector wishVel = {
		forward.x * forwardmove + right.x * sidemove,
		forward.y * forwardmove + right.y * sidemove,
		0.f
	};

	Vector wishdir = wishVel;
	float wishspeed = VectorNormalize(wishdir);

	if (wishspeed != 0.f && wishspeed > mv->_m_flMaxSpeed)
	{
		VectorScale(wishVel, mv->_m_flMaxSpeed / wishspeed, wishVel);
		wishspeed = mv->_m_flMaxSpeed;
	}

	mv->m_vecVelocity_.z = 0.f;

	Accelerate(wishdir, wishspeed, sv_accelerate.GetVar()->GetFloat());

	mv->m_vecVelocity_.z = 0.f;

	float speed = mv->_m_flMaxSpeed;

	if (mv->m_vecVelocity_.LengthSqr() > speed * speed)
	{
		float v24 = speed / mv->m_vecVelocity_.Length();

		mv->m_vecVelocity_ *= v24;
	}

	mv->m_vecVelocity_ += player->GetBaseVelocity();

	if (mv->m_vecVelocity_.Length() < 1.0f)
	{
		mv->m_vecVelocity_.x = 0.0f;
		mv->m_vecVelocity_.y = 0.0f;
		mv->m_vecVelocity_.z = 0.0f;

		mv->m_vecVelocity_ -= player->GetBaseVelocity();
		return;
	}

	Vector temp = {
		mv->m_vecVelocity_.x * Interfaces::Globals->frametime + mv->_m_vecAbsOrigin.x,
		mv->m_vecVelocity_.y * Interfaces::Globals->frametime + mv->_m_vecAbsOrigin.y,
		mv->_m_vecAbsOrigin.z
	};

	trace_t tr;
	TracePlayerBBox(mv->_m_vecAbsOrigin, temp, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

	mv->m_outWishVel += wishdir * wishspeed;

	if (tr.fraction == 1.f)
	{
		mv->_m_vecAbsOrigin = tr.endpos;
	}
	else
	{
		// if ( (ground == -1 || (v38 = &dword_14A8286C[4 * ground], v38[1] != ground >> 16) || !*v38)
		// && (v39 = *(gamemovement_1 + 4), !*(v39 + 0x25A))
		// || (v39 = *(gamemovement_1 + 4), *(v39 + 0x31FC) != 0.0) )
		if ((ground == nullptr && player->GetWaterLevel() == 0) || player->GetWaterJumpTime() != 0.f)
		{
			mv->m_vecVelocity_ -= player->GetBaseVelocity();
			return;
		}

		StepMove(temp, tr);
	}

	mv->m_vecVelocity_ -= player->GetBaseVelocity();

	StayOnGround();
}

void CGameMovement::StayOnGround()
{
	Vector start = {
		mv->_m_vecAbsOrigin.x,
		mv->_m_vecAbsOrigin.y,
		mv->_m_vecAbsOrigin.z + 2.f
	};

	Vector end = {
		mv->_m_vecAbsOrigin.x,
		mv->_m_vecAbsOrigin.y,
		mv->_m_vecAbsOrigin.z - player->GetStepSize()
	};

	trace_t tr;
	TracePlayerBBox(mv->_m_vecAbsOrigin, start, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

	Vector temp = tr.endpos;

	TracePlayerBBox(temp, end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

	if (tr.fraction > 0.f && tr.fraction < 1.f && !tr.startsolid && tr.plane.normal.z >= 0.7f)
	{
		float unk = fabs(mv->_m_vecAbsOrigin.z - tr.endpos.z);

		if (unk > 0.015625f)
			mv->_m_vecAbsOrigin = tr.endpos;
	}
}

//dangerzone correct
void CGameMovement::FullWalkMove()
{
	if (!CheckWater())
		StartGravity();

	if (player->GetWaterJumpTime() != 0.f)
	{
		WaterJump();
		TryPlayerMove();
		CheckWater();
		return;
	}

	if (player->GetWaterLevel() >= WL_Feet)
	{
		auto type = player->GetWaterType();
		if (type & CONTENTS_SLIME) //what
		{
			SlimeMove();
			CategorizePosition();
			if (player->GetGroundEntity() != nullptr)
				mv->m_vecVelocity_.z = 0.f;
			goto FinishedSlime;
		}
	}

	if (player->GetWaterLevel() < WL_Waist)
	{
		if (mv->m_nButtons & IN_JUMP)
			CheckJumpButton();
		else
			mv->m_nOldButtons &= ~IN_JUMP;

		if (player->GetGroundEntity() != nullptr)
		{
			mv->m_vecVelocity_.z = 0.f;
			player->SetFallVelocity(0.f);
			Friction();
		}

		CheckVelocity();

		if (player->GetGroundEntity() != nullptr)
		{
			WalkMove();
			player->SetHasWalkMovedSinceLastJump(true);
			//*(bool*)(player + 0x3208) = 1; // TODO: Some sort of boolean ?
		}
		else
			AirMove();

		CategorizePosition();
		CheckVelocity();

		if (!CheckWater())
			FinishGravity();

		if (player->GetGroundEntity() != nullptr)
			mv->m_vecVelocity_.z = 0.f;

		CheckFalling();
	}
	else
	{
		if (player->GetWaterLevel() == 2)
			CheckWaterJump();

		if (mv->m_vecVelocity_.z < 0.f)
		{
			if (player->GetWaterJumpTime() != 0.f)
				player->SetWaterJumpTime(0.f);
		}

		if (mv->m_nButtons & IN_JUMP)
			CheckJumpButton();
		else
			mv->m_nOldButtons &= ~IN_JUMP;

		WaterMove();

		CategorizePosition();

		if (player->GetGroundEntity() != nullptr)
			mv->m_vecVelocity_.z = 0.f;
	}
FinishedSlime:
	if (m_nOldWaterLevel != 0.f && player->GetWaterLevel() != 0)
		return;
	else if (player->GetWaterLevel() == 0)
		return;

	if (player->GetAbsVelocity()->Length() > 135.f)
	{
		//decrypts(0)
		(*Interfaces::MoveHelperClient)->StartSound(mv->_m_vecAbsOrigin, XorStr("Player.Swim"));
		//encrypts(0)
	}
}

void CGameMovement::OnJump(float stamina)
{
}

//dangerzone correct, NOT CORRECT AFTER 4/30/19
void CCSGameMovement::OnJump(float stamina)
{
	float newStamina = 0.f;

	float v6 = (sv_staminajumpcost.GetVar()->GetFloat() * stamina) + m_pCSPlayer->GetStamina();

	if (v6 >= 0.f)
		newStamina = fminf(sv_staminamax.GetVar()->GetFloat(), v6);

	if (m_pCSPlayer->GetStamina() != newStamina)
		m_pCSPlayer->SetStamina(newStamina);

	m_pCSPlayer->OnJump(stamina);
}

void CGameMovement::OnLand(float flFallVelocity)
{
}

//dangerzone correct
void CCSGameMovement::OnLand(float stamina)
{
	float newStamina = 0.f;

	float v6 = (sv_staminalandcost.GetVar()->GetFloat() * stamina) + m_pCSPlayer->GetStamina();

	if (v6 >= 0.f)
		newStamina = fminf(sv_staminamax.GetVar()->GetFloat(), v6);

	if (m_pCSPlayer->GetStamina() != newStamina)
		m_pCSPlayer->SetStamina(newStamina);

	// C_CSPlayer::OnLand
	m_pCSPlayer->OnLand(stamina);
	//sub_103A9AC0(m_pCSPlayer, stamina);
}

bool CGameMovement::CheckInterval(IntervalType_t type)
{
	int tickInterval = GetCheckInterval(type);

	return (player->CurrentCommandNumber() + player->entindex()) % tickInterval == 0;
}

int CGameMovement::GetCheckInterval(IntervalType_t type)
{
	int tickInterval = 1;

	switch (type)
	{
	case IntervalType_t::GROUND:
	{
		tickInterval = static_cast<int>(0.3f / Interfaces::Globals->interval_per_tick);
		break;
	}

	case IntervalType_t::STUCK:
	{
		if (player->GetStuckLast() != 0)
			tickInterval = 1;
		else if (Interfaces::Globals->maxClients == 1)
			tickInterval = static_cast<int>(0.2f / Interfaces::Globals->interval_per_tick);
		else
			tickInterval = static_cast<int>(1.f / Interfaces::Globals->interval_per_tick);
		break;
	}

	case IntervalType_t::LADDER:
	{
		tickInterval = 2;
		break;
	}

	case IntervalType_t::LADDER_WEDGE:
	{
		tickInterval = static_cast<int>(0.5f / Interfaces::Globals->interval_per_tick);
		break;
	}

	default:
		break;
	}

	return tickInterval;
}

//dangerzone correct
void CGameMovement::StartGravity()
{
	// (float)*(DWORD*)(player + 0xDC);
	float grav = player->GetGravity();

	if (grav == 0.0f)
		grav = 1.0f;

	Vector basevel = player->GetBaseVelocity();

	mv->m_vecVelocity_.z -= ((sv_gravity.GetVar()->GetFloat() * grav) * 0.5f) * Interfaces::Globals->frametime;
	mv->m_vecVelocity_.z += basevel.z * Interfaces::Globals->frametime;

	basevel.y = basevel.x;
	player->SetBaseVelocity(basevel);

	CheckVelocity();
}

void CGameMovement::FinishGravity()
{
	if (player->GetWaterJumpTime() == 0.f)
	{
		float grav = player->GetGravity();
		if (grav == 0.0f)
			grav = 1.0f;

		mv->m_vecVelocity_.z -= sv_gravity.GetVar()->GetFloat() * grav * Interfaces::Globals->frametime * 0.5f;
		CheckVelocity();
	}
}

void CGameMovement::AddGravity()
{
	if (player->GetWaterJumpTime() == 0.f)
	{
		float grav = player->GetGravity();

		if (grav == 0.f)
			grav = 1.f;

		Vector basevel = player->GetBaseVelocity();

		mv->m_vecVelocity_.z -= sv_gravity.GetVar()->GetFloat() * grav * Interfaces::Globals->frametime;
		mv->m_vecVelocity_.z += basevel.z * Interfaces::Globals->frametime;

		basevel.z = 0.f;
		player->SetBaseVelocity(basevel);

		CheckVelocity();
	}
}

bool CGameMovement::CheckJumpButton()
{
	if (player->GetDeadFlag())
	{
		mv->m_nOldButtons |= IN_JUMP;
		return false;
	}

	if (player->GetWaterJumpTime() != 0.0f)
	{
		player->SetWaterJumpTime(player->GetWaterJumpTime() - Interfaces::Globals->frametime);

		if (player->GetWaterJumpTime() < 0.0f)
			player->SetWaterJumpTime(0.0f);

		return false;
	}

	if (player->GetWaterLevel() >= 2)
	{
		SetGroundEntity(nullptr);

#if 1
		if (player->GetWaterType() == CONTENTS_WATER)
			mv->m_vecVelocity_.z = 100.0f;
		else if (player->GetWaterType() == CONTENTS_SLIME)
			mv->m_vecVelocity_.z = 80.0f;
#else
		// v7 = 32 * (*(basePlayer_1 + 0xE8) & 1) | 0x10;
		auto v7 = 32 * (player->m_nWaterType & 1) | 0x10;

		if (player->m_nWaterType & 2)
			v7 = 32 * (player->m_nWaterType & 1);

		if (v7 == 32)
			mv->m_vecVelocity_.z = 100.0f;
		else
		{
			auto v8 = 32 * (player->m_nWaterType & 1) | 0x10;

			if (player->m_nWaterType & 2)
				v8 = 32 * (player->m_nWaterType & 1);

			if (v8 == 16)
				mv->m_vecVelocity_.z = 80.f;
		}
#endif

		// if ( *(v9 + 0x3200) <= 0.0 )
		if (player->GetSwimSoundTime() <= 0.0f)
		{
			player->SetSwimSoundTime(1000.f);
			//decrypts(0)
			(*Interfaces::MoveHelperClient)->StartSound(mv->_m_vecAbsOrigin, XorStr("Player.Swim"));
			//encrypts(0)
			return false;
		}

		return false;
	}

	// unknown player + 0x3208
	player->SetHasWalkMovedSinceLastJump(false);

	if (player->GetGroundEntity() == nullptr)
	{
		mv->m_nOldButtons |= IN_JUMP;
		return false;
	}

	// unknown player + 0x31B4
	if (player->GetSlowMovement() || mv->m_nOldButtons & IN_JUMP || (player->GetDucking() && player->GetFlags() & FL_DUCKING) || player->GetDuckJumpTimeMsecs() > 0)
	{
		return false;
	}

	SetGroundEntity(nullptr);

	// (*(**(gameMovement + 4) + 0x548))(*(gameMovement + 8) + 0xAC, *(*(gameMovement + 4) + 0x35A0), 0x3F800000, 1);
	player->PlayStepSound(mv->_m_vecAbsOrigin, player->GetSurfaceData(), 1.f, true, false);

	// (*(*g_pMoveHelper + 40))(2)
	(*Interfaces::MoveHelperClient)->PlayerSetAnimation(PLAYER_JUMP);

	float groundFactor;
	if (player->GetSurfaceData())
		// v16 = *(v15 + 0x54);
		groundFactor = player->GetSurfaceData()->game.jumpFactor;
	else
		groundFactor = 1.f;

	float v18;
	if (player->GetDucking() || player->GetFlags() & FL_DUCKING)
		v18 = groundFactor * 268.32816f;
	else
		v18 = mv->m_vecVelocity_.z + (groundFactor * 268.32816f);

	mv->m_vecVelocity_.z = v18;

	FinishGravity();

	mv->m_outJumpVel.z += mv->m_vecVelocity_.z - v18;
	mv->m_outStepHeight += 0.15f;

	// this is a nullsub(mv->m_outJumpVel) /shrug
	// (*(*gameMovement + 0x7C))(gameMovement, *(*(gameMovement + 8) + 0x8C));
	OnJump(mv->m_outJumpVel.z);

	if (Interfaces::Globals->maxClients == 1)
	{
		// player + 0x3000
		player->SetJumpTime(510.f);
		player->SetInDuckJump(true);
	}

	mv->m_nOldButtons |= IN_JUMP;
	return true;
}

//dangerzone correct
bool CCSGameMovement::CheckJumpButton()
{
	if (m_pCSPlayer->GetDeadFlag() && !m_pCSPlayer->IsPlayerGhost())
	{
		mv->m_nOldButtons |= IN_JUMP;
		return false;
	}

	if (m_pCSPlayer->GetWaterJumpTime() == 0.f)
	{
		// if ( (*(*m_pCSPlayer + 0x5CC))() && (*(*m_pCSPlayer + 0x5D0))() )
		//if (m_pCSPlayer->IsTaunting() && m_pCSPlayer->IsInThirdPersonTaunt())  //they removed this in the operation update on 11/18/2019
		//	return false;

		if (m_pCSPlayer->GetWaterLevel() < 2)
		{
			bool is_standing_on_player = false;
			bool ground_ent_is_in_air = false;

			auto groundEnt = m_pCSPlayer->GetGroundEntity();

			// if ( groundEnt && (*(*(groundEnt + 8) + 0x28))(groundEnt + 8) && (*(*groundEntCopy + 0x260))(groundEntCopy) )
			if (groundEnt != nullptr && groundEnt->entindex() != 0 && groundEnt->IsPlayer())
			{
				is_standing_on_player = true;
				auto groundGroundEnt = groundEnt->GetGroundEntity();
				ground_ent_is_in_air = !groundGroundEnt;
			}

			//0x3208
			player->SetHasWalkMovedSinceLastJump(false);

			if (m_pCSPlayer->GetGroundEntity() == nullptr)
			{
				mv->m_nOldButtons |= IN_JUMP;
				return false;
			}

			if (mv->m_nOldButtons & IN_JUMP && !sv_autobunnyhopping.GetVar()->GetBool())
				return false;

			if (!sv_enablebunnyhopping.GetVar()->GetBool() && !m_pCSPlayer->IsPlayerGhost())
				PreventBunnyJumping();

			SetGroundEntity(nullptr);

			float flJumpFactor = 1.f;

			if (sqrtf(((mv->m_vecVelocity_.x * mv->m_vecVelocity_.x) + (mv->m_vecVelocity_.y * mv->m_vecVelocity_.y)) + (mv->m_vecVelocity_.z * mv->m_vecVelocity_.z)) > 126.0 || m_pCSPlayer->HasHeavyArmor() && !IsPlayingGuardian())
			{
				// (*(*m_pCSPlayer + 0x548))(&mv->_m_vecAbsOrigin, player->m_surfacedata, 0x3F800000, 1);
				m_pCSPlayer->PlayStepSound(mv->_m_vecAbsOrigin, player->GetSurfaceData(), 1.f, true, false);
			}

			if (!m_pCSPlayer->IsPlayerGhost())
			{
				m_pCSPlayer->PlayClientUnknownSound(mv->_m_vecAbsOrigin, player->GetSurfaceData());

				// (*(*m_pCSPlayer + 0x5C8))();
				m_pCSPlayer->PlayClientJumpSound();
			}

			//if(!*(bool*)(m_pCSPlayer + 0x3951))
			if (!m_pCSPlayer->UsesServerSideJumpAnimation())
			{
				// (*(**(v21 + 0x3870) + 24))(8, 0);
				m_pCSPlayer->DoAnimationEvent((PlayerAnimEvent_t)8, 0);
			}

			if (player->GetSurfaceData() != nullptr)
				// movss   xmm0, dword ptr [eax+54h] // game.jumpFactor + 4
				flJumpFactor = player->GetSurfaceData()->game.jumpFactor;

			if (player == nullptr || player->IsBot() && !(mv->m_nButtons & IN_JUMP))
			{
				// mov     byte ptr [eax+0A300h], 1
				//m_pCSPlayer->m_duckUntilOnGround = true;
				m_pCSPlayer->SetDuckUntilOnGround(true);
				CCSGameMovement::FinishDuck();
			}

			float startz = mv->m_vecVelocity_.z;

			if (ground_ent_is_in_air)
				mv->m_vecVelocity_.z = 0.f;
			else
			{
				if (m_pCSPlayer->GetDuckUntilOnGround() || m_pCSPlayer->GetDucking() || m_pCSPlayer->GetFlags() & FL_DUCKING || is_standing_on_player)
				{
					mv->m_vecVelocity_.z = sv_jump_impulse.GetVar()->GetFloat() * flJumpFactor;
				}
				else
				{
					mv->m_vecVelocity_.z += sv_jump_impulse.GetVar()->GetFloat() * flJumpFactor;
				}
			}

			float v26 = 0.0f;

			if (m_pCSPlayer->GetStamina() > 0.0f)
			{
				float v28 = 1.0f - m_pCSPlayer->GetStamina() * 0.01f;
				if (v28 >= 0.0f)
					v26 = fmin(v28, 1.0f);

				mv->m_vecVelocity_.z *= v26;
			}

			FinishGravity();

			mv->m_outWishVel.z += (mv->m_vecVelocity_.z - startz);
			mv->m_outStepHeight += 0.1f;
			OnJump(mv->m_outWishVel.z);
			mv->m_nOldButtons |= IN_JUMP;
			return true;
		}
		else
		{
			SetGroundEntity(nullptr);

#if 1
			if (player->GetWaterType() == CONTENTS_WATER) // We move up a certain amount
				mv->m_vecVelocity_[2] = 100;
			else if (player->GetWaterType() == CONTENTS_SLIME)
				mv->m_vecVelocity_[2] = 80;
#else
			int v11 = 32 * (m_pCSPlayer->m_nWaterType & 1) | 0x10;

			if (!(m_pCSPlayer->m_nWaterType & 2))
				v11 = 32 * (m_pCSPlayer->m_nWaterType & 1);

			if (v11 == 32)
				mv->m_vecVelocity_.z = 100.f;
			else
			{
				int v12 = 32 * (m_pCSPlayer->m_nWaterType & 1) | 0x10;

				if (!(m_pCSPlayer->m_nWaterType & 2))
					v12 = 32 * (m_pCSPlayer->m_nWaterType & 1);

				if (v12 == 16)
					mv->m_vecVelocity_.z = 80.f;
			}
#endif

			if (m_pCSPlayer->GetSwimSoundTime() > 0.f)
				return false;

			m_pCSPlayer->SetSwimSoundTime(1000.f);
			//decrypts(0)
			(*Interfaces::MoveHelperClient)->StartSound(mv->_m_vecAbsOrigin, XorStr("Player.Swim"));
			//encrypts(0)
			return false;
		}
	}
	else
	{
		m_pCSPlayer->SetWaterJumpTime(m_pCSPlayer->GetWaterJumpTime() - Interfaces::Globals->frametime);

		if (m_pCSPlayer->GetWaterJumpTime() < 0.f)
			m_pCSPlayer->SetWaterJumpTime(0.f);

		return false;
	}

	return false;
}

void CGameMovement::FullTossMove()
{
	CheckWater();

	if (mv->m_flForwardMove != 0.0f || mv->m_flSideMove != 0.0f || mv->m_flUpMove != 0.0f)
	{
		Vector forward, right, up;
		AngleVectors(mv->m_vecViewAngles, &forward, &right, &up);

		float forwardmove = mv->m_flForwardMove;
		float sidemove = mv->m_flSideMove;

		VectorNormalizeFast(forward);
		VectorNormalizeFast(right);

		Vector wishVel = {
			forward.x * forwardmove + right.x * sidemove,
			forward.y * forwardmove + right.y * sidemove,
			forward.z * forwardmove + right.z * sidemove
		};

		wishVel.z += mv->m_flUpMove;

		Vector wishdir = wishVel;
		float wishspeed = VectorNormalize(wishdir);

		if (wishspeed > mv->_m_flMaxSpeed)
		{
			VectorScale(wishVel, mv->_m_flMaxSpeed / wishspeed, wishVel);
			wishspeed = mv->_m_flMaxSpeed;
		}

		Accelerate(wishdir, wishspeed, sv_accelerate.GetVar()->GetFloat());
	}

	if (mv->m_vecVelocity_.z > 0.f)
		SetGroundEntity(nullptr);

	if (player->GetGroundEntity() != nullptr || player->GetBaseVelocity() != Vector(0, 0, 0) || mv->m_vecVelocity_ != Vector(0, 0, 0))
	{
		CheckVelocity();

		if (player->GetMoveType() == MOVETYPE_FLYGRAVITY)
			AddGravity();

		mv->m_vecVelocity_ += player->GetBaseVelocity();

		CheckVelocity();

		Vector temp;
		VectorScale(mv->m_vecVelocity_, Interfaces::Globals->frametime, temp);

		mv->m_vecVelocity_ -= player->GetBaseVelocity();

		Vector end;
		trace_t tr;
		TracePlayerBBox(mv->_m_vecAbsOrigin, end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

		if (tr.fraction < 1.f && !tr.allsolid)
			(*Interfaces::MoveHelperClient)->AddToTouched(tr, mv->m_vecVelocity_);

		if (tr.allsolid)
		{
			SetGroundEntity(&tr);

			mv->m_vecVelocity_.x = 0.f;
			mv->m_vecVelocity_.y = 0.f;
			mv->m_vecVelocity_.z = 0.f;
		}
		else
		{
			if (tr.fraction != 1.f)
			{
				PerformFlyCollisionResolution(tr, temp);
				CheckWater();
			}
		}
	}
}

void CGameMovement::FullLadderMove()
{
	CheckWater();

	if (mv->m_nButtons & IN_JUMP)
		CheckJumpButton();
	else
		mv->m_nOldButtons &= ~IN_JUMP;

	mv->m_vecVelocity_ += player->GetBaseVelocity();

	TryPlayerMove();

	mv->m_vecVelocity_ -= player->GetBaseVelocity();
}

int CGameMovement::TryPlayerMove(Vector* firstDest /*= nullptr*/, trace_t* firstTrace /*= nullptr */)
{
	Vector originalVelocity = mv->m_vecVelocity_;
	Vector primalVelocity = mv->m_vecVelocity_;
	Vector unknownVelocity = mv->m_vecVelocity_;
	trace_t pm;

	float allFraction = 0.0f;
	float fVol = 1.0f;

	int bumpcount = 0;
	int blocked = 0;
	int numplanes = 0;
	int oldnumplanes = 0;
	int numbumps = 4;

	Vector planes[5];

	float frametime = Interfaces::Globals->frametime;

	while (1)
	{
		if (mv->m_vecVelocity_.Length() == 0.0f)
			break;

		// Assume we can move all the way from the current origin to the
		//  end point
		Vector end = mv->_m_vecAbsOrigin + (mv->m_vecVelocity_ * frametime);

		// See if we can make it from origin to end point.

		// If their velocity Z is 0, then we can avoid an extra trace here during WalkMove.
		if (firstDest && (end == *firstDest))
		{
			// looks recursive, we call this with the firstTrace tho
			//sub_1017C3E0(firstTrace);
			pm = *firstTrace;
		}
		else
		{
			TracePlayerBBox(mv->_m_vecAbsOrigin, end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm);
			fVol = 1.f;
			numplanes = oldnumplanes;
		}

		if (pm.fraction > 0.0f && pm.fraction < 0.0001f) //0.000099999997f
		{
			pm.fraction = 0.0f;
		}

		allFraction += pm.fraction;

		// If we started in a solid object, or we were in solid space
		//  the whole way, zero out our velocity and return that we
		//  are blocked by floor and wall.
		if (pm.allsolid)
		{
			mv->m_vecVelocity_.Zero();
			return 4;
		}

		// If we moved some portion of the total distance, then
		//  copy the end position into the pmove.origin and
		//  zero the plane counter.
		if (pm.fraction > 0.0f)
		{
			if (pm.fraction == 1.0f)
			{
				// There's a precision issue with terrain tracing that can cause a swept box to successfully trace
				// when the end position is stuck in the triangle.  Re-run the test with an uswept box to catch that
				// case until the bug is fixed.
				// If we detect getting stuck, don't allow the movement
				trace_t stuck;
				TracePlayerBBox(pm.endpos, pm.endpos, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, stuck);
				fVol = 1.f;

				if (stuck.allsolid || stuck.fraction != 1.f)
				{
					mv->m_vecVelocity_.Zero();
					primalVelocity.x = unknownVelocity.x;
					primalVelocity.y = unknownVelocity.y;
					break;
				}
			}

			// actually covered some distance
			numplanes = 0;
			mv->_m_vecAbsOrigin = pm.endpos;
			originalVelocity = mv->m_vecVelocity_;
		}

		// If we covered the entire distance, we are done
		//  and can return.
		if (pm.fraction == 1.f)
		{
			primalVelocity.x = unknownVelocity.x;
			primalVelocity.y = unknownVelocity.y;
			break;
		}

		// Save entity that blocked us (since fraction was < 1.0)
		//  for contact
		// Add it if it's not already in the list!!!
		(*Interfaces::MoveHelperClient)->AddToTouched(pm, mv->m_vecVelocity_);

		// If the plane we hit has a high z component in the normal, then
		//  it's probably a floor
		if (pm.plane.normal.z > 0.7f)
		{
			blocked |= 1; // floor
		}

		// If the plane has a zero z component in the normal, then it's a
		//  step or wall
		if (fabsf(pm.plane.normal.z) < 0.0001f) //0.000099999997f
		{
			pm.plane.normal.z = 0.0f;
			blocked |= 2; // step / wall
		}

		// Reduce amount of m_flFrameTime left by total time left * fraction
		//  that we covered.
		frametime -= (pm.fraction * frametime);

		// Did we run out of planes to clip against?
		if (numplanes >= 5)
		{
			// this shouldn't really happen
			//  Stop our movement if so.
			fVol = 1.f;

			mv->m_vecVelocity_.Zero();
			primalVelocity.x = unknownVelocity.x;
			primalVelocity.y = unknownVelocity.y;

			break;
		}

		// Set up next clipping plane
		planes[numplanes++] = pm.plane.normal;
		oldnumplanes = numplanes;

		Vector newVelocity;
		// modify original_velocity so it parallels all of the clip planes
		//

		// reflect player velocity
		// Only give this a try for first impact plane because you can get yourself stuck in an acute corner by jumping in place
		//  and pressing forward and nobody was really using this bounce/reflection feature anyway...

		if (numplanes == 1)
		{
			if (player->GetMoveType() == MOVETYPE_WALK && player->GetGroundEntity() == nullptr)
			{
				if (planes[0].z <= 0.7f)
				{
					ClipVelocity(originalVelocity, planes[0], newVelocity, (1.f - player->GetSurfaceFriction() * sv_bounce.GetVar()->GetFloat()) + 1.f);
				}
				else
				{
					// floor or slope
					ClipVelocity(originalVelocity, planes[0], newVelocity, 1.f);
				}

				primalVelocity.x = unknownVelocity.x;
				primalVelocity.y = unknownVelocity.y;
				mv->m_vecVelocity_ = newVelocity;
				originalVelocity = newVelocity;

				fVol = 1.0f;

				if (++bumpcount >= numbumps)
					break;

				continue;
			}
		}

		int i, j;
		for (i = 0; i < numplanes; i++)
		{
			ClipVelocity(originalVelocity, planes[i], mv->m_vecVelocity_, 1);

			for (j = 0; j < numplanes; j++)
			{
				if (j != i)
				{
					// Are we now moving against this plane?
					if (mv->m_vecVelocity_.Dot(planes[j]) < 0)
						break; // not ok
				}
			}
			if (j == numplanes) // Didn't have to clip, so we're ok
				break;
		}

		// Did we go all the way through plane set
		if (i == numplanes)
		{
			// go along the crease
			if (numplanes != 2)
			{
				fVol = 1.f;

				mv->m_vecVelocity_.x = 0.f;
				mv->m_vecVelocity_.y = 0.f;
				mv->m_vecVelocity_.z = 0.f;

				primalVelocity.x = unknownVelocity.x;
				primalVelocity.y = unknownVelocity.y;

				break;
			}

			Vector dir;
			CrossProduct(planes[0], planes[1], dir);
			dir.NormalizeInPlace();

			float d = dir.Dot(mv->m_vecVelocity_);
			VectorScale(dir, d, mv->m_vecVelocity_);
		}
		else
		{
			// go along this plane
			// pmove.velocity is set in clipping call, no need to set again.
		}

		primalVelocity.x = unknownVelocity.x;
		primalVelocity.y = unknownVelocity.y;

		//
		// if original velocity is against the original velocity, stop dead
		// to avoid tiny occilations in sloping corners
		//
		float d = mv->m_vecVelocity_.Dot(unknownVelocity);
		if (d <= 0.0f)
		{
			fVol = 1.0f;
			mv->m_vecVelocity_.Zero();
			break;
		}

		fVol = 1.0f;
		if (++bumpcount >= numbumps)
			break;
	}

	if (allFraction == 0.f)
	{
		mv->m_vecVelocity_.Zero();
	}

	auto fLateralStoppingAmount = primalVelocity.Length2D() - mv->m_vecVelocity_.Length2D();

	if (fLateralStoppingAmount <= 1160.f)
	{
		if (fLateralStoppingAmount <= 580.f)
			return blocked;

		fVol = 0.85f;
	}

	PlayerRoughLandingEffects(fVol);
	return blocked;
}

bool CGameMovement::LadderMove()
{
	if (player->GetMoveType() == MOVETYPE_NOCLIP || !GameHasLadders())
		return false;

	Vector wishdir;

	if (player->GetMoveType() == MOVETYPE_LADDER)
	{
		// movss   xmm2, dword ptr [eax+3214h];
		wishdir = -player->GetVecLadderNormal();
	}
	else
	{
		float forwardmove = mv->m_flForwardMove;
		float sidemove = mv->m_flSideMove;

		if (forwardmove == 0.f && sidemove == 0.0f)
			return false;

		wishdir = (m_vecRight * sidemove) + (m_vecForward * forwardmove);
		VectorNormalizeFast(wishdir);
	}

	Vector end;
	trace_t tr;

	VectorMA(mv->_m_vecAbsOrigin, LadderDistance(), wishdir, end);
	TracePlayerBBox(mv->_m_vecAbsOrigin, end, LadderMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

	if (tr.fraction == 1.f || !OnLadder(tr))
	{
		//0x3208
		if (player->HasWalkMovedSinceLastJump())
		{
			if (player->GetMoveType() != MOVETYPE_LADDER)
			{
				if (player->GetGroundEntity() == nullptr)
				{
					if (mv->m_vecVelocity_.z <= 0.f && mv->m_vecVelocity_.z > -50.f
						&& fabs(mv->m_vecVelocity_.x) > 0.f
						&& fabs(mv->m_vecVelocity_.y) > 0.f)
					{
						Vector start = mv->_m_vecAbsOrigin;
						start.z -= 6.0f;

						Vector normalized = mv->m_vecVelocity_;
						VectorNormalizeFast(normalized);
						normalized *= 24.f;

						Vector end = mv->_m_vecAbsOrigin - normalized;
						TracePlayerBBox(start, end, LadderMask() & 0xFFFEFFFF, COLLISION_GROUP_PLAYER_MOVEMENT, tr);

						if (tr.fraction == 1.f && OnLadder(tr) && tr.plane.normal.z != 1.f)
						{
							player->SetMoveType(MOVETYPE_LADDER);
							player->SetMoveCollide(MOVECOLLIDE_DEFAULT);

							Vector ladderNormal = player->GetVecLadderNormal();

							if (tr.plane.normal != ladderNormal)
							{
								player->SetVecLadderNormal(tr.plane.normal);
							}

							mv->m_vecVelocity_.x = 0.f;
							mv->m_vecVelocity_.y = 0.f;
							mv->m_vecVelocity_.z = 0.f;

							TracePlayerBBox(start, end, LadderMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

							mv->_m_vecAbsOrigin = tr.endpos;

							if (player->GetMoveType() != MOVETYPE_LADDER)
								// *(float*)(player + 0x3204)
								player->SetTimeNotOnLadder(Interfaces::Globals->curtime + 0.2f);

							player->SetMoveType(MOVETYPE_LADDER);
							player->SetMoveCollide(MOVECOLLIDE_DEFAULT);

							if (tr.plane.normal != player->GetVecLadderNormal())
							{
								player->SetVecLadderNormal(tr.plane.normal);
							}

							Vector floor = mv->_m_vecAbsOrigin;
							floor.z += (GetPlayerMins().z - 1.f);

							bool onFloor = Interfaces::EngineTrace->GetPointContents(floor) == CONTENTS_SOLID || player->GetGroundEntity() != nullptr;

							player->SetGravity(0.f);

							float climbSpeed = ClimbSpeed();

							float forwardspeed = 0.f;
							float rightspeed = 0.f;

							if (mv->m_nButtons & IN_BACK)
								forwardspeed += -climbSpeed;
							if (mv->m_nButtons & IN_FORWARD)
								forwardspeed += climbSpeed;
							if (mv->m_nButtons & IN_MOVELEFT)
								rightspeed += -climbSpeed;
							if (mv->m_nButtons & IN_MOVERIGHT)
								rightspeed += climbSpeed;

							if (mv->m_nButtons & IN_JUMP)
							{
								if (Interfaces::Globals->curtime >= player->GetTimeNotOnLadder())
								{
									player->SetMoveType(MOVETYPE_WALK);
									player->SetMoveCollide(MOVECOLLIDE_DEFAULT);

									mv->m_vecVelocity_ = tr.plane.normal * 270.f;
									return true;
								}
							}
							else
							{
								if (forwardspeed == 0.f && rightspeed == 0.f)
								{
									mv->m_vecVelocity_.x = 0.f;
									mv->m_vecVelocity_.y = 0.f;
									mv->m_vecVelocity_.z = 0.f;
									return true;
								}

								// Fuck trying to do actual math functions, it's psuedocode with comments if you're curious
								float v56 = tr.plane.normal.x;
								float v57 = m_vecForward.z * forwardspeed;
								float v58 = (rightspeed * m_vecRight.x) + (m_vecForward.x * forwardspeed);
								float v59 = m_vecForward.y * forwardspeed;
								float v60 = tr.plane.normal.z;
								float v61 = v58;
								float v62 = (m_vecRight.y * rightspeed) + v59;
								float v63 = (m_vecRight.z * rightspeed) + v57;
								float v64 = (tr.plane.normal.z * 0.f) - tr.plane.normal.y;
								float v65 = tr.plane.normal.y;
								float v66 = tr.plane.normal.x - (tr.plane.normal.z * 0.f);
								float v67 = (tr.plane.normal.y * 0.f) - (tr.plane.normal.x * 0.f);
								float v68 = 1.f / sqrtf((v66 * v66) + (v64 * v64) + (v67 * v67)) + 0.00000011920929f;
								float v93 = v66 * v68; // normalized something
								float v100 = v67 * v68; // normalized something
								float v106 = v64 * v68; // normalized something
								float normal = ((v65 * v62) + (v56 * v61) + (v60 * v63)); // dot product
								float v102 = v63 - (v60 * normal);
								float v104 = v61 - (v56 * normal);
								float v97 = v62 - (v65 * normal);
								float v111 = (v65 * (v67 * v68)) - (v60 * (v66 * v68));
								float v112 = (v60 * (v64 * v68)) - (v56 * (v67 * v68));
								float v89 = (v56 * (v66 * v68)) - (v65 * (v64 * v68));
								float v103 = ((v97 * v112) + (v104 * v111)) + (v102 * v89);
								float v110 = ((v97 * (v66 * v68)) + (v104 * (v64 * v68))) + (v102 * (v67 * v68));
								float v69 = ((v97 * (v66 * v68)) + (v104 * (v64 * v68))) + (v102 * (v67 * v68));
								float v70 = ((v64 * v68) * v69) + (v56 * normal);
								float v71 = ((v66 * v68) * v69) + (v65 * normal);
								float v72 = (v100 * v69) + (v60 * normal);
								float v73 = 1.f / (sqrtf(((v71 * v71) + (v70 * v70)) + (v72 * v72)) + 0.00000011920929f);
								float v74 = v71 * v73; // normalized something
								float v75 = v72 * v73; // normalized something
								float v76 = v56 * (v70 * v73); // normalized something
								float v77 = ((v65 * v74) + v76) + (tr.plane.normal.z * v75);
								float v98 = ((v65 * v74) + v76) + (tr.plane.normal.z * v75);

								float v80, v78, v79, v81, v82;
								if (sv_ladder_angle.GetVar()->GetFloat() <= v77)
								{
									v80 = v111;
									v78 = v112;
									v79 = v89;
									v81 = v97;
									v82 = v102;
								}
								else
								{
									v78 = v112;
									v79 = v89;
									v80 = v111;
									v81 = (v112 * v103) + ((v93 * sv_ladder_dampen.GetVar()->GetFloat()) * v110);
									v82 = (v89 * v103) + ((v100 * sv_ladder_dampen.GetVar()->GetFloat()) * v110);
									v104 = (v111 * v103) + ((v106 * sv_ladder_dampen.GetVar()->GetFloat()) * v110);
								}

								mv->m_vecVelocity_.x = (v80 * -normal) + v104;
								mv->m_vecVelocity_.x = (v78 * -normal) + v81;
								mv->m_vecVelocity_.z = (v79 * -normal) + v82;

								if (sv_ladder_scale_speed.GetVar()->GetFloat() > 0.f)
								{
									mv->m_vecVelocity_ *= sv_ladder_scale_speed.GetVar()->GetFloat();
								}

								if (onFloor && normal > 0.f)
								{
									mv->m_vecVelocity_ += (tr.plane.normal * 200.f);
								}
							}
						}
					}
				}
			}
		}
	}

	return true;
}

//dangerzone correct
bool CCSGameMovement::LadderMove()
{
	bool onLadder = CGameMovement::LadderMove();

	if (onLadder)
	{
		if (m_pCSPlayer != nullptr)
			// sub_103A9C60(v3, &v1->mv->_m_vecAbsOrigin.x, &v3->m_vecLadderNormal.x);
			m_pCSPlayer->SurpressLadderChecks(&mv->_m_vecAbsOrigin, &m_pCSPlayer->GetVecLadderNormal());
	}

	return onLadder;
}

bool CGameMovement::OnLadder(trace_t& pm)
{
	if (!(pm.contents & CONTENTS_LADDER))
	{
		IPhysicsSurfaceProps* props = (*Interfaces::MoveHelperClient)->GetSurfaceProps();

		if (props == nullptr)
			return false;

		surfacedata_t* surfaceData = props->GetSurfaceData(pm.surface.surfaceProps);
		if (!surfaceData || !surfaceData->game.climbable)
			return false;
	}

	return true;
}

//dangerzone correct
bool CCSGameMovement::OnLadder(trace_t& trace)
{
	if (trace.plane.normal.z != 1.f)
	{
		if (trace.contents & CONTENTS_LADDER)
		{
			auto physProps = (*Interfaces::MoveHelperClient)->GetSurfaceProps();

			if (physProps != nullptr)
			{
				auto surfaceData = physProps->GetSurfaceData(trace.surface.surfaceProps);

				if (surfaceData != nullptr)
				{
					if (surfaceData->game.climbable)
						return true;
				}
			}
		}
	}

	return false;
}

float CGameMovement::LadderDistance()
{
	return 2.f;
}

float CCSGameMovement::LadderDistance()
{
	if (player->GetMoveType() == MOVETYPE_LADDER)
		return 10.f;

	return 2.f;
}

unsigned int CGameMovement::LadderMask()
{
	return 0x201400B;
}

float CGameMovement::ClimbSpeed()
{
	return 200.f;
}

float CCSGameMovement::ClimbSpeed() const
{
	if (mv->m_nButtons & (IN_SPEED | IN_DUCK))
		return 68.f;

	return 200.f;
}

float CGameMovement::LadderLateralMultiplier()
{
	return 1.f;
}

const int nanmask = 255 << 23;
#define IS_NAN(x) (((*(int*)&x) & nanmask) == nanmask)

void CGameMovement::CheckVelocity(void)
{
	int i;

	//
	// bound velocity
	//

	Vector org = mv->GetAbsOrigin();

	for (i = 0; i < 3; i++)
	{
		// See if it's bogus.
		if (IS_NAN(mv->m_vecVelocity_[i]))
		{
			//DevMsg(1, "PM  Got a NaN velocity %s\n", DescribeAxis(i));
			mv->m_vecVelocity_[i] = 0;
		}

		if (IS_NAN(org[i]))
		{
			//printf(1, "PM  Got a NaN origin on %s\n", DescribeAxis(i));
			org[i] = 0;
			mv->SetAbsOrigin(org);
		}

		// Bound it.
		if (mv->m_vecVelocity_[i] > sv_maxvelocity.GetVar()->GetFloat())
		{
			//DevMsg(1, "PM  Got a velocity too high on %s\n", DescribeAxis(i));
			mv->m_vecVelocity_[i] = sv_maxvelocity.GetVar()->GetFloat();
		}
		else if (mv->m_vecVelocity_[i] < -sv_maxvelocity.GetVar()->GetFloat())
		{
			//DevMsg(1, "PM  Got a velocity too low on %s\n", DescribeAxis(i));
			mv->m_vecVelocity_[i] = -sv_maxvelocity.GetVar()->GetFloat();
		}
	}
}

float CCSGameMovement::LadderLateralMultiplier() const
{
	if (mv->m_nButtons & IN_DUCK)
		return 1.f;

	return 0.5f;
}

void CGameMovement::ResetGetWaterContentsForPointCache()
{
	for (int slot = 0; slot < MAX_PC_CACHE_SLOTS; ++slot)
	{
		for (int i = 0; i < MAX_PLAYERS; ++i)
		{
			m_CachedGetPointContents[i][slot] = -9999;
		}
	}
}

int CGameMovement::GetWaterContentsForPointCached(const Vector& point, int slot)
{
	int idx = player->entindex() - 1;

	if (m_CachedGetPointContents[idx][slot] == -9999 || point.DistToSqr(m_CachedGetPointContentsPoint[idx][slot]) > 1)
	{
		m_CachedGetPointContents[idx][slot] = Interfaces::EngineTrace->GetPointContents(point, MASK_WATER);
		m_CachedGetPointContentsPoint[idx][slot] = point;
	}

	return m_CachedGetPointContents[idx][slot];
}

void CGameMovement::PushEntity(Vector& push, trace_t* pTrace)
{
	Vector end;

	VectorAdd(mv->GetAbsOrigin(), push, end);
	TracePlayerBBox(mv->GetAbsOrigin(), end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, *pTrace);
	mv->SetAbsOrigin(pTrace->endpos);

	// So we can run impact function afterwards.
	// If
	if (pTrace->fraction < 1.0 && !pTrace->allsolid)
	{
		(*Interfaces::MoveHelperClient)->AddToTouched(*pTrace, mv->m_vecVelocity_);
	}
}

int CGameMovement::ClipVelocity(Vector& in, Vector& normal, Vector& out, float overbounce)
{
	int blocked = 0;
	float angle = normal.z;

	if (angle > 0.f)
		blocked |= 1;
	if (angle == 0.f)
		blocked |= 2;

	float backoff = DotProduct(in, normal) * overbounce;

	for (int i = 0; i < 3; ++i)
	{
		float change = normal[i] * backoff;
		out[i] = in[i] - change;
	}

	float adjust = DotProduct(out, normal);

	if (adjust < 0.f)
	{
		if (adjust >= -0.03125f)
			adjust = -0.03125f;

		out -= normal * adjust;
	}

	return blocked;
}

//dangerzone correct
bool CGameMovement::CheckWater()
{
	Vector point;
	GetWaterCheckPosition(1, &point);

	player->SetWaterLevel(0);
	player->SetWaterTypeDirect(0);

	int cont = GetWaterContentsForPointCached(point, 0);

	if (cont & MASK_WATER)
	{
		player->SetWaterType(cont);

		GetWaterCheckPosition(2, &point);
		cont = GetWaterContentsForPointCached(point, 1);
		if (cont & MASK_WATER)
		{
			player->SetWaterLevel(2);

			GetWaterCheckPosition(3, &point);
			cont = GetWaterContentsForPointCached(point, 2);

			if (cont & MASK_WATER)
				player->SetWaterLevel(3);
		}
	}

	if (m_nOldWaterLevel == 0.f && player->GetWaterLevel() != 0)
		// *(float*)(gamemovement + 0x10)
		m_flWaterEntryTime = Interfaces::Globals->curtime;

	return player->GetWaterLevel() > 1;
}

void CGameMovement::GetWaterCheckPosition(int waterLevel, Vector* pos)
{
	pos->x = mv->_m_vecAbsOrigin.x + (GetPlayerMins().x + GetPlayerMaxs().x) * 0.5f;
	pos->y = mv->_m_vecAbsOrigin.y + (GetPlayerMins().y + GetPlayerMaxs().y) * 0.5f;

	if (waterLevel == WL_Waist)
	{
		pos->z = mv->_m_vecAbsOrigin.z + (GetPlayerMins().z + GetPlayerMaxs().z) * 0.5f;
	}
	else
	{
		if (waterLevel == WL_Eyes)
			pos->z = mv->_m_vecAbsOrigin.z + player->GetViewOffset().z;
		else
			pos->z = mv->_m_vecAbsOrigin.z + GetPlayerMins().z + 1.f;
	}
}

void CGameMovement::CategorizePosition()
{
	player->SetSurfaceFriction(1.f);

	CheckWater();

	if (player->IsObserver())
		return;

	Vector point = mv->_m_vecAbsOrigin;
	point.z -= 2.0f;

	bool bMovingUp = (mv->m_vecVelocity_.z > 0.f);
	bool bMovingUpRapidly = (mv->m_vecVelocity_.z > 140.f);

	if (bMovingUpRapidly)
	{
		if (player->GetGroundEntity() != nullptr)
		{
			bMovingUpRapidly = (player->GetGroundEntity()->GetAbsVelocity()->z > 140.f);
		}
	}

	bool bMoveToEndPos = false;

	if (player->GetMoveType() == MOVETYPE_WALK)
	{
		if (player->GetGroundEntity() != nullptr)
		{
			bMoveToEndPos = true;
			point.z -= player->GetStepSize();
		}
	}

	if (bMovingUpRapidly || (bMovingUp && player->GetMoveType() == MOVETYPE_LADDER))
	{
		SetGroundEntity(nullptr);
	}
	else
	{
		trace_t tr;
		TracePlayerBBox(mv->_m_vecAbsOrigin, point, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

		bool v10 = (TraceIsOnGroundOrPlayer(&tr) == 0);

		if (!v10)
			goto jmp;

		ITraceFilter* filter = LockTraceFilter(COLLISION_GROUP_PLAYER_MOVEMENT);
		TracePlayerBBoxForGround(mv->_m_vecAbsOrigin, point, GetPlayerMins(), GetPlayerMaxs(), mv->m_nPlayerHandle.Get(), PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);
		UnlockTraceFilter(filter);

		v10 = (TraceIsOnGroundOrPlayer(&tr) == 0);

		if (v10)
		{
			SetGroundEntity(nullptr);
			if (mv->m_vecVelocity_.z > 0.f)
			{
				if (player->GetMoveType() != MOVETYPE_NOCLIP)
					player->SetSurfaceFriction(0.25f);
			}
		}
		else
		{
		jmp:
			SetGroundEntity(&tr);
			if (bMoveToEndPos && !tr.startsolid && tr.fraction > 0.f && tr.fraction < 1.f)
				mv->_m_vecAbsOrigin = tr.endpos;
		}
	}
}

//-----------------------------------------------------------------------------
// Figures out how the constraint should slow us down
//-----------------------------------------------------------------------------
float CGameMovement::ComputeConstraintSpeedFactor(void)
{
	// If we have a constraint, slow down because of that too.
	if (!mv || mv->m_flConstraintRadius == 0.0f)
		return 1.0f;

	float flDistSq = mv->GetAbsOrigin().DistToSqr(mv->m_vecConstraintCenter);

	float flOuterRadiusSq = mv->m_flConstraintRadius * mv->m_flConstraintRadius;
	float flInnerRadiusSq = mv->m_flConstraintRadius - mv->m_flConstraintWidth;
	flInnerRadiusSq *= flInnerRadiusSq;

	// Only slow us down if we're inside the constraint ring
	if ((flDistSq <= flInnerRadiusSq) || (flDistSq >= flOuterRadiusSq))
		return 1.0f;

	// Only slow us down if we're running away from the center
	Vector vecDesired;
	VectorMultiply(m_vecForward, mv->m_flForwardMove, vecDesired);
	VectorMA(vecDesired, mv->m_flSideMove, m_vecRight, vecDesired);
	VectorMA(vecDesired, mv->m_flUpMove, m_vecUp, vecDesired);

	Vector vecDelta;
	VectorSubtract(mv->GetAbsOrigin(), mv->m_vecConstraintCenter, vecDelta);
	VectorNormalizeFast(vecDelta);
	VectorNormalizeFast(vecDesired);
	if (DotProduct(vecDelta, vecDesired) < 0.0f)
		return 1.0f;

	float flFrac = (sqrt(flDistSq) - (mv->m_flConstraintRadius - mv->m_flConstraintWidth)) / mv->m_flConstraintWidth;

	float flSpeedFactor = Lerp(flFrac, 1.0f, mv->m_flConstraintSpeedFactor);
	return flSpeedFactor;
}

void CGameMovement::CheckParameters()
{
	if (player->GetMoveType() != MOVETYPE_ISOMETRIC && player->GetMoveType() != MOVETYPE_NOCLIP && player->GetMoveType() != MOVETYPE_OBSERVER)
	{
		float speed = (mv->m_flForwardMove * mv->m_flForwardMove) + (mv->m_flSideMove * mv->m_flSideMove) + (mv->m_flUpMove * mv->m_flUpMove);

		if (mv->m_flClientMaxSpeed != 0.f)
			mv->_m_flMaxSpeed = fminf(mv->_m_flMaxSpeed, mv->m_flClientMaxSpeed);

		float flSpeedFactor = 1.f;

		if (player->GetSurfaceData() != nullptr)
		{
			flSpeedFactor = player->GetSurfaceData()->game.maxSpeedFactor;
		}

		float flConstraintSpeedFactor = ComputeConstraintSpeedFactor();

		mv->_m_flMaxSpeed *= flSpeedFactor;

		if (speed != 0.f)
		{
			if (speed > (mv->_m_flMaxSpeed * mv->_m_flMaxSpeed))
			{
				float ratio = mv->_m_flMaxSpeed / sqrt(speed);

				mv->m_flForwardMove *= ratio;
				mv->m_flSideMove *= ratio;
				mv->m_flUpMove *= ratio;
			}
		}
	}

	if (player->GetFlags() & 0x50 || player->GetHealth() <= 0)
	{
		mv->m_flForwardMove = 0.f;
		mv->m_flSideMove = 0.f;
		mv->m_flUpMove = 0.f;
	}

	DecayViewPunchAngle();

	if (player->GetHealth() <= 0)
	{
		mv->m_vecAngles = mv->m_vecOldAngles;
	}
	else
	{
		QAngle vangle = mv->m_vecAngles;
		vangle += player->GetPunch(); //player->m_vecPunchAngle;

		if (player->GetMoveType() == MOVETYPE_ISOMETRIC || player->GetMoveType() == MOVETYPE_NOCLIP)
		{
			mv->m_vecAngles.z = 0.f;
		}
		else
		{
			mv->m_vecAngles.z = CalcRoll(vangle, mv->m_vecVelocity_, sv_rollangle.GetVar()->GetFloat(), sv_rollspeed.GetVar()->GetFloat());
		}

		mv->m_vecAngles.x = vangle.x;
		mv->m_vecAngles.y = vangle.y;
	}

	if (player->GetHealth() <= 0)
	{
		//  v24 = (*(*g_pGameRules + 120))();
		// (*(v23 + 680))(v2->player, v24 + 96);
		player->SetViewOffset((*g_pGameRules)->GetViewVectors()->m_vDeadViewHeight);
	}

	mv->m_vecAngles.y = AngleNormalize(mv->m_vecAngles.y);
}

//dangerzone correct
void CCSGameMovement::CheckParameters()
{
	auto buttons = mv->m_nButtons;

	if (mv->m_nButtons & IN_DUCK)
		mv->m_nButtons |= IN_BULLRUSH; // 0x400000;

	if ((mv->m_nButtons ^ mv->m_nOldButtons) & IN_BULLRUSH) // 0x400000
	{
		float newduckspeed = fmaxf(player->GetDuckSpeed() - 2.f, 0.f);

		if (player->GetDuckSpeed() != newduckspeed)
			player->SetDuckSpeed(newduckspeed);
	}

	if (m_pCSPlayer->GetDuckUntilOnGround() && mv->m_nButtons & IN_DUCK)
		m_pCSPlayer->SetDuckUntilOnGround(false);

	// !sub_10390E60(this)()
	// if(m_flDuckSpeed < 1.5) || !(m_iFlags & FL_DUCKING) && curtime < (m_flDuckTime + sv_timebetweenducks))
	if (!CCSGameMovement::IsPlayerDucking())
		mv->m_nButtons &= ~IN_DUCK;

	if (m_pCSPlayer->GetDuckUntilOnGround())
	{
		if (player->GetGroundEntity() == nullptr)
		{
			if (player->GetMoveType() != MOVETYPE_LADDER)
				mv->m_nButtons |= IN_DUCK;
		}
	}

	if (m_pCSPlayer->GetDuckOverride())
		mv->m_nButtons |= IN_DUCK;

	auto movingForward = mv->m_nButtons & IN_FORWARD;
	auto movingBackward = mv->m_nButtons & IN_BACK;
	auto movingRight = mv->m_nButtons & IN_MOVERIGHT;
	auto movingLeft = mv->m_nButtons & IN_MOVELEFT;
	auto walking = mv->m_nButtons & IN_SPEED;
	auto pressinganymovebuttons = mv->m_nButtons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_RUN); //0x1618;

	auto walkstate = walking;

	bool pressingbothforwardandback;
	bool pressingbothsidebuttons;

	if (!movingForward)
		pressingbothforwardandback = false;
	else
		pressingbothforwardandback = movingBackward;

	// same as above
	//if (!movingForward || (pressingbothforwardandback = true, !movingBackward))
	//	pressingbothforwardandback = false;

	if (!movingRight)
		pressingbothsidebuttons = false;
	else
		pressingbothsidebuttons = movingLeft;

	// same as above
	//if (!movingRight || (pressingbothsidebuttons = true, !movingLeft))
	//	pressingbothsidebuttons = false;

	if (mv->m_nButtons & IN_DUCK || player->GetDucking() || player->GetFlags() & FL_DUCKING)
	{
		walkstate = false;
	}
	else if (walking)
	{
		float adjustedmaxspeed = mv->_m_flMaxSpeed * _CS_PLAYER_SPEED_WALK_MODIFIER;
		Vector velocity = m_pCSPlayer->GetVelocity();

		if ((adjustedmaxspeed + 25.f) > velocity.Length())
		{
			mv->_m_flMaxSpeed = adjustedmaxspeed;

			if (!m_pCSPlayer->GetIsWalking())
				m_pCSPlayer->SetIsWalking(true);
		}

		goto jmp_35;
	}

	if (m_pCSPlayer->GetIsWalking())
		m_pCSPlayer->SetIsWalking(false);

jmp_35:

	if (player->GetMoveType() != MOVETYPE_ISOMETRIC && player->GetMoveType() != MOVETYPE_NOCLIP && player->GetMoveType() != MOVETYPE_OBSERVER)
	{
		float sidemove = mv->m_flSideMove;
		float forwardmove = mv->m_flForwardMove;
		float upmove = mv->m_flUpMove;

		float v27 = (sidemove * sidemove) + (forwardmove * forwardmove) + (upmove * upmove);
		float speedFactor = 1.f;

		if (player->GetSurfaceData() != nullptr)
		{
			speedFactor = player->GetSurfaceData()->game.maxSpeedFactor;
		}

		float flConstraintSpeedFactor = ComputeConstraintSpeedFactor();
		if (flConstraintSpeedFactor < speedFactor)
			speedFactor = flConstraintSpeedFactor;

		if (m_pCSPlayer->GetFlags() & FL_ONGROUND)
		{
			speedFactor *= m_pCSPlayer->GetVelocityModifier();
		}

		CBaseCombatWeapon* activeWeap = m_pCSPlayer->GetWeapon(); //m_pCSPlayer->GetActiveWeapon();
		//FIXME: dynamic_cast here from C_BaseCombatWeapon to C_WeaponCSBase

		if (activeWeap != nullptr && mv->m_nButtons & IN_ATTACK && !activeWeap->IsReloading() && activeWeap->GetClipOne() > 0)
		{
			//(*(*combatweapon + 0x6E0))(combatweapon); // Calls C_BaseCombatWeapon's 440th index
			speedFactor *= activeWeap->GetMaxSpeed2();
		}

		mv->_m_flMaxSpeed *= speedFactor;

		if (m_pCSPlayer->GetStamina() > 0.f)
		{
			float v35 = 1.f - m_pCSPlayer->GetStamina() * 0.01f;
			float v36 = 0.f;

			if (v35 >= 0.f)
				v36 = fmin(v35, 1.f);

			float v37 = v36;
			mv->_m_flMaxSpeed *= (v37 * v37);
		}

		if (v27 != 0.f && v27 > (mv->_m_flMaxSpeed * mv->_m_flMaxSpeed))
		{
			float ratio = mv->_m_flMaxSpeed / sqrt(v27);

			mv->m_flForwardMove *= ratio;
			mv->m_flSideMove *= ratio;
			mv->m_flUpMove *= ratio;
		}
	}

	// (*(*player + 0x474))() != 6 )
	if ((player->GetFlags() & 0x50 || player->GetHealth() <= 0) && player->GetObserverMode() != OBS_MODE_ROAMING)
	{
		mv->m_flForwardMove = 0.f;
		mv->m_flSideMove = 0.f;
		mv->m_flUpMove = 0.f;
	}

	DecayViewPunchAngle();
	DecayAimPunchAngle();

	if (player->GetHealth() <= 0)
	{
		mv->m_vecAngles = mv->m_vecOldAngles;
	}
	else
	{
		QAngle angles = mv->m_vecAngles + player->GetPunch();

		if (player->GetMoveType() == MOVETYPE_ISOMETRIC || player->GetMoveType() == MOVETYPE_NOCLIP)
		{
			mv->m_vecAngles.z = 0.f;
		}
		else
		{
			mv->m_vecAngles.z = CalcRoll(angles, mv->m_vecVelocity_, sv_rollangle.GetVar()->GetFloat(), sv_rollspeed.GetVar()->GetFloat());
		}

		mv->m_vecAngles = angles;
	}

	if (player->GetHealth() <= 0 && player->GetObserverMode() == OBS_MODE_DEATHCAM)
	{
		player->SetViewOffset((*g_pGameRules)->GetViewVectors()->m_vDeadViewHeight);
	}

	mv->m_vecAngles.y = AngleNormalize(mv->m_vecAngles.y);

	if (player->GetObserverMode() == OBS_MODE_NONE)
	{
		if (player->GetMoveType() != MOVETYPE_LADDER)
		{
			int nLevels = 0;
			CBaseEntity* pCurGround = player->GetGroundEntity();
			while (pCurGround && pCurGround->IsPlayer() && nLevels < 1000)
			{
				pCurGround = pCurGround->GetGroundEntity();
				++nLevels;

#ifdef _DEBUG
				if (nLevels == 1000)
					printf("BUG: CCSGameMovement::CheckParameters - too many stacking levels.\n");
#endif

				if (nLevels > 1)
				{
					mv->m_flForwardMove = mv->_m_flMaxSpeed * 3.f;
					mv->m_flSideMove = 0.f;
					mv->m_nButtons = 0;
					mv->m_nImpulseCommand = 0;
				}
			}
		}
	}

	if (m_pCSPlayer->GetMoveState() > 0)
		m_pCSPlayer->SetMoveState(0);

#if 1
	if (pressinganymovebuttons)
	{
		if (pressingbothforwardandback)
		{
			if (pressingbothsidebuttons)
			{
				//can't move if pressing all the move keys
				if (m_pCSPlayer->GetMoveState() > 0)
					m_pCSPlayer->SetMoveState(0);
			}
			else if (movingRight || movingLeft)
			{
				if (m_pCSPlayer->GetMoveState() != 2)
					m_pCSPlayer->SetMoveState(2);
			}
			else
			{
				if (m_pCSPlayer->GetMoveState() > 0)
					m_pCSPlayer->SetMoveState(0);
			}
		}
		else
		{
			if (!pressingbothsidebuttons || movingForward)
			{
				if (m_pCSPlayer->GetMoveState() != 2)
					m_pCSPlayer->SetMoveState(2);
			}
			else if (!movingBackward)
			{
				if (m_pCSPlayer->GetMoveState() > 0)
					m_pCSPlayer->SetMoveState(0);
			}
			else
			{
				if (m_pCSPlayer->GetMoveState() != 2)
					m_pCSPlayer->SetMoveState(2);
			}
		}
	}
#else
	bool v64 = false;
	if (pressinganymovebuttons)
	{
		if (pressingbothforwardandback)
		{
			if (pressingbothsidebuttons)
				goto jmp_103;
			if (movingRight)
				goto jmp_111;

			v64 = (movingLeft == 0);
		}
		else
		{
			if (!pressingbothsidebuttons || movingForward)
			{
			jmp_111:
				if (m_pCSPlayer->GetMoveState() != 2)
					m_pCSPlayer->SetMoveState(2);
				goto jmp_113;
			}

			v64 = (movingBackward == 0);
		}

		if (v64)
		{
		jmp_103:
			if (m_pCSPlayer->GetMoveState() > 0)
				m_pCSPlayer->SetMoveState(0);

			goto jmp_113;
		}

		goto jmp_111;
	}
#endif

jmp_113:

	if (m_pCSPlayer->GetMoveState() == 2 && walkstate && m_pCSPlayer->GetMoveState() != 1)
		m_pCSPlayer->SetMoveState(1);
}

void CGameMovement::ReduceTimers()
{
	if (player->GetDuckTimeMsecs() > 0)
	{
		player->SetDuckTimeMsecs((float)player->GetDuckTimeMsecs() - (Interfaces::Globals->frametime * 1000.f));

		if (player->GetDuckTimeMsecs() < 0)
			player->SetDuckTimeMsecs(0);
	}

	if (player->GetDuckJumpTimeMsecs() > 0)
	{
		player->SetDuckJumpTimeMsecs((float)player->GetDuckJumpTimeMsecs() - (Interfaces::Globals->frametime * 1000.f));

		if (player->GetDuckJumpTimeMsecs() < 0)
			player->SetDuckJumpTimeMsecs(0);
	}

	if (player->GetJumpTimeMsecs() > 0)
	{
		player->SetJumpTimeMsecs((float)player->GetJumpTimeMsecs() - (Interfaces::Globals->frametime * 1000.f));

		if (player->GetJumpTimeMsecs() < 0)
			player->SetJumpTimeMsecs(0);
	}

	if (player->GetSwimSoundTime() > 0.f)
	{
		player->SetSwimSoundTime(player->GetSwimSoundTime() - (Interfaces::Globals->frametime * 1000.f));

		if (player->GetSwimSoundTime() < 0.f)
			player->SetSwimSoundTime(0.f);
	}
}

void CCSGameMovement::ReduceTimers()
{
	if (m_pCSPlayer->GetStamina() > 0.f)
	{
		float v4 = Interfaces::Globals->frametime * sv_staminarecoveryrate.GetVar()->GetFloat();

		if (m_pCSPlayer->GetStamina() != (m_pCSPlayer->GetStamina() - v4))
			m_pCSPlayer->SetStamina(m_pCSPlayer->GetStamina() - v4);

		if (m_pCSPlayer->GetStamina() < 0.f)
			m_pCSPlayer->SetStamina(0.f);
	}

	CGameMovement::ReduceTimers();
}

void CGameMovement::CheckFalling()
{
	if (player->GetGroundEntity() != nullptr)
	{
		float fallvel = player->GetFallVelocity();
		if (fallvel > 0.f)
		{
			if (player->GetHealth() > 0 && fallvel >= 350.f)
			{
				bool bAlive = true;
				float fVol = 0.5f;

				if (player->GetWaterLevel() == 0)
				{
					auto groundEnt = player->GetGroundEntity();

					if (groundEnt->GetAbsVelocity()->z < 0.f)
					{
						auto playerGroundEnt = player->GetGroundEntity();
						player->SetFallVelocity(player->GetFallVelocity() + playerGroundEnt->GetAbsVelocity()->z);
						player->SetFallVelocity(fmaxf(player->GetFallVelocity(), 0.1f));
					}

					fallvel = player->GetFallVelocity();

					if (fallvel <= 580.f)
					{
						if (fallvel <= 290.f)
						{
							if (fallvel >= 200.f)
								fVol = 0.5f;
							else
								fVol = 0.f;
						}
						else
						{
							fVol = 0.85000002f;
						}
					}
					else
					{
						fVol = 1.0f;
						LocalPlayer.CalledPlayerHurt = true;
#ifdef _DEBUG
						printf("HURT ME\n");
#endif
						bAlive = (*Interfaces::MoveHelperClient)->PlayerFallingDamage();
					}
				}

				CGameMovement::PlayerRoughLandingEffects(fVol);

				if (bAlive)
					(*Interfaces::MoveHelperClient)->PlayerSetAnimation(PLAYER_WALK);
			}

			if (player->GetFallVelocity() > 16.f && player->GetFallVelocity() <= 1024.f)
			{
				QAngle punchangle;
				player->GetPunchVMT(punchangle);

				punchangle.x = player->GetFallVelocity() * 0.001f;
				if (punchangle.x < 0.75f)
					punchangle.x = 0.75f;

				player->SetPunchVMT(punchangle);
			}
			OnLand(player->GetFallVelocity());
			player->SetFallVelocity(0.f);
		}
	}
}

void CGameMovement::PlayerRoughLandingEffects(float fVol)
{
	if (fVol > 0.f)
	{
		// mov     dword ptr [eax+320Ch], 400.0
		player->SetStepSoundTime(400.0f);

		player->PlayStepSound(mv->_m_vecAbsOrigin, player->GetSurfaceData(), fmaxf(fVol, 0.1f), false, false);

		float TargetZPunch = (player->GetFallVelocity() - 580.f) * 0.013f;

		QAngle punch = player->GetPunch();

		if (punch.z != TargetZPunch)
		{
			player->GetLocalData()->NetworkStateChanged(player->GetPunchAdr());
			punch.z = TargetZPunch;
			player->SetPunch(punch);
		}

		punch = player->GetPunch();

		if (punch.x > 8.f && punch.x != 8.f)
		{
			player->GetLocalData()->NetworkStateChanged(player->GetPunchAdr());
			punch.x = 8.0f;
			player->SetPunch(punch);
		}
	}
}

//dangerzone correct
void CGameMovement::Duck()
{
	int buttonsChanged = (mv->m_nOldButtons ^ mv->m_nButtons);
	int buttonsPressed = (buttonsChanged & mv->m_nButtons);
	int buttonsReleased = (buttonsChanged & mv->m_nOldButtons);

	CBaseEntity* groundEntityPtr = player->GetGroundEntity();

	int v10 = (player->GetFlags() >> 1) & 1;
	int v39 = player->GetDuckJumpTimeMsecs();

	if (mv->m_nButtons & IN_DUCK)
		mv->m_nOldButtons |= IN_DUCK;
	else
		mv->m_nOldButtons &= ~IN_DUCK;

	if (player->GetHealth() <= 0)
		return;

	HandleDuckingSpeedCrop();

	if (!(mv->m_nButtons & IN_DUCK))
	{
		if (!player->GetDucking() && !v10 && player->GetJumpTimeMsecs() <= 0)
		{
			if (player->GetHealth() > 0 && !player->IsObserver() && !player->IsInAVehicle() && player->GetDuckJumpTimeMsecs() == 0)
			{
				if (fabs(player->GetViewOffset().z - GetPlayerViewOffset(false).z) > 0.1f)
					SetDuckedEyeOffset(0.f);
			}
		}

		return;
	}

	if (mv->m_nButtons & IN_DUCK || player->GetJumpTimeMsecs() > 0)
	{
		if (buttonsPressed & IN_DUCK && !v10 && player->GetJumpTimeMsecs() <= 0 && player->GetDuckJumpTimeMsecs() <= 0)
		{
			player->SetDuckTimeMsecs(1000);
			player->SetDucking(TRUE);
		}

		if (!player->GetDucking())
			goto jmp_70;

		if (player->GetJumpTimeMsecs() > 0)
		{
		jmp_71:
			if (v10)
			{
				if (!(mv->m_nButtons & IN_DUCK))
				{
					trace_t tr;
					if (CanUnduckJump(tr))
					{
						FinishUnduckJump(tr);
						player->SetDuckJumpTimeMsecs(((((1.f - tr.fraction) * tr.fraction) * 200.f) + 800.f));
					}
				}
			}
			else
			{
				StartUnduckJump();
			}

			return;
		}

		if (player->GetDuckJumpTimeMsecs() > 0)
		{
		jmp_70:
			if (player->GetJumpTimeMsecs() <= 0)
			{
				return;
			}

			goto jmp_71;
		}

		auto temp = 1000 - player->GetDuckTimeMsecs();
		if (temp >= 0 && temp > 200)
		{
			goto jmp_69;
		}
		else
		{
			temp = 0;
		}

		if (!v10 && groundEntityPtr != nullptr)
		{
			float v29 = 0.f;
			float v30 = (float)temp * 0.005f;

			if (v30 >= 0.f)
				v29 = fminf(v30, 1.f);

			SetDuckedEyeOffset(((v29 * v29) * 3.f) - (((v29 * v29) * 2.f) * v29));
			goto jmp_70;
		}

	jmp_69:
		FinishDuck();
		goto jmp_70;
	}

	if (player->IsInDuckJump())
	{
		trace_t tr;

		if (CanUnduckJump(tr))
		{
			FinishUnduckJump(tr);

			if (tr.fraction < 1.f)
				player->SetDuckJumpTimeMsecs((((1.f - tr.fraction) * 200.f) + 800.f));
		}
	}

	if (player->GetDuckJumpTimeMsecs() <= 0)
	{
		if (player->GetAllowAutoMovement() || player->GetDucking() || groundEntityPtr == nullptr)
		{
			if (buttonsReleased & IN_DUCK)
			{
				if (v10 && player->GetJumpTimeMsecs() <= 0)
				{
					player->SetDuckTimeMsecs(1000);
				}
				else if (player->GetDucking() && !player->GetDucked())
				{
					float temp = (float)(1000 - player->GetDuckTimeMsecs()) * 0.005f;
					float newtemp = 0.f;
					if (temp >= 0.f)
						newtemp = fminf(temp, 1.f);
					else
						newtemp = 0.f;

					player->SetDuckTimeMsecs(800 - (int)(newtemp * -200.f));
				}
			}

			if (CanUnduck())
			{
				if (player->GetDucking() && !player->GetDucked())
					return;

				int temp = 1000 - player->GetDuckTimeMsecs();

				if (temp >= 0)
				{
					if (temp > 200)
						goto jmp_50;
				}
				else
				{
					temp = 0;
				}

				if (groundEntityPtr != nullptr || player->GetJumpTimeMsecs() > 0)
				{
					float v24 = 0.0f;
					float v25 = (float)temp * 0.005f;

					if (v25 >= 0.f)
						v24 = fminf(v25, 1.f);

					SetDuckedEyeOffset((((1.f - v24) * (1.f - v24)) * 3.f) - ((((1.f - v24) * (1.f - v24)) * 2.f) * (1.f - v24)));

					player->SetDucking(TRUE);
					return;
				}

			jmp_50:
				FinishUnduck();
				return;
			}

			if (player->GetDuckTimeMsecs() != 1000)
			{
				SetDuckedEyeOffset(1.f);
				player->SetDuckTimeMsecs(1000);
				player->SetDucked(TRUE);
				player->SetDucking(FALSE);
				player->AddFlag(FL_DUCKING);
			}
		}
	}
}

bool CCSGameMovement::IsPlayerDucking()
{
	if (m_pCSPlayer->GetDuckSpeed() <= 1.5f || !(m_pCSPlayer->GetFlags() & FL_DUCKING) || (m_pCSPlayer->GetLastDuckTime() + sv_timebetweenducks.GetVar()->GetFloat()) > Interfaces::Globals->curtime)
		return false;
	return true;
}

//dangerzone correct
bool CCSGameMovement::CanMove(CBasePlayer* ent)
{
	if (ent->GetMoveType() != MOVETYPE_NONE)
	{
		if (ent->GetObserverMode())
			return true;

		auto state = ent->GetPlayerState();
		CBaseCombatWeapon* weap;

		if ((state == STATE_ACTIVE || state == STATE_OBSERVER_MODE)
			&& !ent->IsGrabbingHostage()
			&& !ent->IsDefusing()
			&& !ent->BlockingUseActionInProgress()
			&& (!(*g_pGameRules)->IsFreezePeriod() || ent->GetCanMoveDuringFreezePeriod())
			&& (weap = ent->GetCSWeapon(ClassID::_CC4), (!weap || !weap->StartedArming())))
		{
			return true;
		}
	}

	return false;
}

//dangerzone correct
void CCSGameMovement::ApplyDuckRatio(float flDuckAmount)
{
	if (player->GetObserverMode() != 6 && !m_bSpeedCropped)
	{
		if (mv->m_nButtons & IN_DUCK || player->GetDucking() || player->GetFlags() & FL_DUCKING)
		{
			float duckratio = ((flDuckAmount * 0.34) + 1.0f) - flDuckAmount;
			mv->m_flForwardMove *= duckratio;
			mv->m_flSideMove *= duckratio;
			mv->m_flUpMove *= duckratio;
			mv->_m_flMaxSpeed *= duckratio;
			m_bSpeedCropped = true;
		}
	}
}

//dangerzone correct
void CCSGameMovement::Duck()
{
	bool onladder = false;
	CBaseEntity* groundEntity = player->GetGroundEntity();

	if (!groundEntity)
	{
		if (player->GetMoveType() == MOVETYPE_LADDER)
			onladder = true;
	}

	if (mv->m_nButtons & IN_DUCK)
		mv->m_nOldButtons |= IN_DUCK;
	else
		mv->m_nOldButtons &= ~IN_DUCK;

	if (player->GetHealth() <= 0 && !(player->GetObserverMode()))
	{
		if (player->GetDuckOverride())
			player->SetDuckOverride(false);

		if (!(player->GetFlags() & FL_DUCKING))
		{
			FinishDuck();
			return;
		}
	}

	float newduckspeed = 0.f;

	if (!m_pCSPlayer->GetDuckUntilOnGround())
	{
		float duckspeed = player->GetDuckSpeed();
		float v32 = Interfaces::Globals->frametime * 3.f;

		if ((8.f - duckspeed) <= v32)
		{
			if (-v32 <= (8.f - duckspeed))
				newduckspeed = 8.f;
			else
				newduckspeed = duckspeed - v32;
		}
		else
		{
			newduckspeed = v32 + duckspeed;
		}

		if (player->GetDuckSpeed() != newduckspeed)
			player->SetDuckSpeed(newduckspeed);

		if (player->GetDuckSpeed() < 8.f)
		{
			float duckamount = player->GetDuckAmount();

			if (duckamount > 0.f && duckamount < 1.f)
				goto jmp_37;

			// Calls C_BasePlayer's 10th func
			Vector* absorigin = player->GetAbsOrigin();

			// probably some sort of velocity lol
			Vector2D duckingorigin = player->GetDuckingOrigin();
			Vector duckingorigin2 = { duckingorigin.x, duckingorigin.y, 0 };

			if ((duckingorigin2 - *absorigin).Length2DSqr() > 4096.f)
			{
				float v51;
				float v50 = Interfaces::Globals->frametime * 6.0f;

				if ((8.0f - player->GetDuckSpeed()) <= v50)
				{
					if (-v50 <= (8.0f - player->GetDuckSpeed()))
						v51 = 8.0f;
					else
						v51 = player->GetDuckSpeed() - v50;
				}
				else
				{
					v51 = player->GetDuckSpeed() + v50;
				}

				if (player->GetDuckSpeed() != v51)
					player->SetDuckSpeed(v51);

				goto jmp_37;
			}
		}
		else
		{
			// Calls C_BasePlayer's 10th func
			Vector* absorigin = player->GetAbsOrigin();

			player->SetDuckingOrigin(*absorigin);
		}

	jmp_37:
		float v60;
		if (mv->m_nButtons & IN_DUCK)
		{
			if (player->GetDuckAmount() < 1.f)
				player->SetDucking(TRUE);

			if (player->GetDucking())
			{
				float v57 = player->GetDuckSpeed() * 0.8f;

				if (player->IsDefusing())
					v57 *= 0.4f;

				float v59 = Interfaces::Globals->frametime * v57;

				if ((1.0f - player->GetDuckAmount()) <= v59)
				{
					if (-v59 <= (1.0f - player->GetDuckAmount()))
						v60 = 1.0f;
					else
						v60 = player->GetDuckAmount() - v59;
				}
				else
				{
					v60 = player->GetDuckAmount() + v59;
				}

				if (player->GetDuckAmount() != v60)
					player->SetDuckAmount(v60);

				if (player->GetDuckAmount() < 1.0f && groundEntity != nullptr)
					SetDuckedEyeOffset(player->GetDuckAmount());
				else
					FinishDuck();

				if (player->GetDuckAmount() >= 0.1f && !(player->GetFlags() & FL_ANIMDUCKING))
					player->AddFlag(FL_ANIMDUCKING);
			}
		}
		else
		{
			if (player->GetDuckAmount() > 0.0f)
				player->SetDucking(TRUE);

			if (player->GetDucking() && player->GetAllowAutoMovement() || groundEntity == nullptr)
			{
				bool v39 = CanUnduck();

				if (v39)
				{
					float adjust = fmaxf(player->GetDuckSpeed(), 1.5f);

					if (player->IsDefusing())
						adjust *= 0.4f;

					float v44 = Interfaces::Globals->frametime * adjust;
					float v45;

					if (-player->GetDuckAmount() <= v44)
					{
						if (-v44 <= -player->GetDuckAmount())
							v45 = 0.0f;
						else
							v45 = player->GetDuckAmount() - v44;
					}
					else
					{
						v45 = player->GetDuckAmount() + v44;
					}

					if (player->GetDuckAmount() != v45)
						player->SetDuckAmount(v45);

					player->SetDucked(FALSE);

					if (player->GetDuckAmount() > 0.0f && groundEntity != nullptr)
						SetDuckedEyeOffset(player->GetDuckAmount());
					else
						FinishUnduck();

					if (player->GetDuckAmount() <= 0.75f && (player->GetFlags() & (FL_ANIMDUCKING | FL_DUCKING)))
						player->RemoveFlag(FL_ANIMDUCKING | FL_DUCKING);
				}
				else
				{
					float v84 = 1.0f;

					if (player->GetDuckAmount() != 1.0f)
						player->SetDuckAmount(1.0f);

					player->SetDucked(TRUE);
					player->SetDucking(FALSE);
					player->AddFlag(FL_ANIMDUCKING | FL_DUCKING); // |= 6
					SetDuckedEyeOffset(player->GetDuckAmount());
				}
			}
		}

		if (player->GetDuckAmount() <= 0.0f && player->GetFlags() & FL_ANIMDUCKING)
			player->RemoveFlag(FL_ANIMDUCKING);

		// bool : return (Interfaces::EngineClient->IsHLTV()  || Interfaces::EngineClient->IsPlayingDemo()) && (*(*engineclient + 904))() <= 13546;
		if ((Interfaces::EngineClient->IsHLTV() || Interfaces::EngineClient->IsPlayingDemo()) && Interfaces::EngineClient->GetEngineBuildNumber() <= 13546)
		{
			if (player->GetDuckTimeMsecs() >= 0)
				player->AddFlag(FL_DUCKING | FL_ANIMDUCKING);
			else
				player->RemoveFlag(FL_DUCKING | FL_ANIMDUCKING);

			float v66 = 150.f - (float)player->GetDuckTimeMsecs();
			float v67 = fmaxf(v66, 0.f) * 0.0066666668f;

			if (player->GetDuckAmount() != v67)
				player->SetDuckAmount(v67);

			SetDuckedEyeOffset(player->GetDuckAmount());
		}

		ApplyDuckRatio(m_pCSPlayer->GetDuckAmount());
		return;
	}

	if (!(player->GetFlags() & FL_DUCKING))
	{
		m_pCSPlayer->SetDuckUntilOnGround(false);
		return;
	}

	if (!onladder)
	{
		m_pCSPlayer->SetDuckUntilOnGround(false);

		if (CanUnduck())
		{
			FinishDuck();
			return;
		}
	}

	if (mv->m_vecVelocity_.z <= 0.f)
	{
		trace_t tr;

		Vector newOrigin = mv->_m_vecAbsOrigin;

		Vector hullSizeNormal = (*g_pGameRules)->GetViewVectors()->m_vHullMax - (*g_pGameRules)->GetViewVectors()->m_vHullMin;
		Vector hullSizeDuck = (*g_pGameRules)->GetViewVectors()->m_vDuckHullMax - (*g_pGameRules)->GetViewVectors()->m_vDuckHullMin;
		Vector delta = hullSizeNormal - hullSizeDuck;
		newOrigin -= delta;

		Vector groundCheck = newOrigin;
		groundCheck.z -= player->GetStepSize();

		UTIL_TraceHull(newOrigin, groundCheck, (*g_pGameRules)->GetViewVectors()->m_vHullMin, (*g_pGameRules)->GetViewVectors()->m_vHullMax, /*CGameMovement::*/ PlayerSolidMask(), player, COLLISION_GROUP_PLAYER_MOVEMENT, tr);

		if (!tr.startsolid && tr.fraction != 1.f)
		{
			m_pCSPlayer->SetDuckUntilOnGround(false);

			if (CanUnduck())
			{
				FinishUnduck();
				return;
			}
		}
	}
}

void CGameMovement::HandleDuckingSpeedCrop()
{
	// test  byte ptr [ecx+0C3Ch], 1
	if (m_bSpeedCropped & 1)
	{
		if (player->GetFlags() & FL_DUCKING)
		{
			if (player->GetGroundEntity() != nullptr)
			{
				mv->m_flForwardMove *= 0.33333334f;
				mv->m_flSideMove *= 0.33333334f;
				mv->m_flUpMove *= 0.33333334f;
				m_bSpeedCropped = true;
				//m_bSpeedCropped |= 1;
			}
		}
	}
}

void CGameMovement::FinishUnduck()
{
	Vector newOrigin = mv->_m_vecAbsOrigin;

	if (player->GetGroundEntity() != nullptr)
	{
		for (int i = 0; i < 3; ++i)
			newOrigin[i] += (*g_pGameRules)->GetViewVectors()->m_vDuckHullMin[i] - (*g_pGameRules)->GetViewVectors()->m_vHullMin[i];
	}
	else
	{
		Vector hullSizeNormal = (*g_pGameRules)->GetViewVectors()->m_vHullMax - (*g_pGameRules)->GetViewVectors()->m_vHullMin;
		Vector hullSizeDuck = (*g_pGameRules)->GetViewVectors()->m_vDuckHullMax - (*g_pGameRules)->GetViewVectors()->m_vDuckHullMin;
		Vector delta = hullSizeNormal - hullSizeDuck;

		newOrigin -= delta; //+= -delta
	}

	player->SetDucked(FALSE);
	player->RemoveFlag(FL_DUCKING | FL_ANIMDUCKING); // ~6
	player->SetDucking(FALSE);
	player->SetInDuckJump(false);

	Vector viewOffset = GetPlayerViewOffset(false);

	player->SetViewOffset(viewOffset);

	player->SetDuckTimeMsecs(0);

	mv->_m_vecAbsOrigin = newOrigin;

	player->ResetLatched();

	CategorizePosition();
}

//dangerzone correct
void CCSGameMovement::FinishUnduck()
{
	Vector newOrigin = mv->_m_vecAbsOrigin;

	if (player->GetGroundEntity() != nullptr || player->GetMoveType() == MOVETYPE_LADDER)
	{
		newOrigin += (*g_pGameRules)->GetViewVectors()->m_vDuckHullMin - (*g_pGameRules)->GetViewVectors()->m_vHullMin;
	}
	else
	{
		Vector hullSizeNormal = (*g_pGameRules)->GetViewVectors()->m_vHullMax - (*g_pGameRules)->GetViewVectors()->m_vHullMin;
		Vector hullSizeDuck = (*g_pGameRules)->GetViewVectors()->m_vDuckHullMax - (*g_pGameRules)->GetViewVectors()->m_vDuckHullMin;
		Vector delta = (hullSizeNormal - hullSizeDuck) * -0.5f;

		newOrigin += delta;
	}

	player->RemoveFlag(FL_DUCKING | FL_ANIMDUCKING);
	player->SetDucked(FALSE);
	player->SetDucking(FALSE);
	player->SetDuckTimeMsecs(0);

	auto offset = GetPlayerViewOffset(false);
	player->SetViewOffset(offset);

	CategorizePosition();

	if (player->GetDuckAmount() > 0.f)
		player->SetDuckAmount(0.f);
}

void CGameMovement::FinishDuck()
{
	if (!(player->GetFlags() & FL_DUCKING))
	{
		player->AddFlag(FL_DUCKING);
		player->SetDucking(FALSE);
		player->SetDucked(TRUE);

		Vector viewOffset = GetPlayerViewOffset(true);
		player->SetViewOffset(viewOffset);

		if (player->GetGroundEntity() != nullptr)
		{
			Vector origin = mv->_m_vecAbsOrigin;

			for (int i = 0; i < 3; ++i)
				origin[i] -= ((*g_pGameRules)->GetViewVectors()->m_vDuckHullMin[i] - (*g_pGameRules)->GetViewVectors()->m_vHullMin[i]);

			mv->_m_vecAbsOrigin = origin;
		}
		else
		{
			Vector hullSizeNormal = (*g_pGameRules)->GetViewVectors()->m_vHullMax - (*g_pGameRules)->GetViewVectors()->m_vHullMin;
			Vector hullSizeDuck = (*g_pGameRules)->GetViewVectors()->m_vDuckHullMax - (*g_pGameRules)->GetViewVectors()->m_vDuckHullMin;
			Vector delta = hullSizeNormal - hullSizeDuck;

			mv->_m_vecAbsOrigin += delta;

			player->ResetLatched();
		}

		FixPlayerCrouchStuck(true);
		CategorizePosition();
	}
}

//dangerzone correct
void CCSGameMovement::FinishDuck()
{
	Vector newOrigin = mv->_m_vecAbsOrigin;

	if (player->GetGroundEntity() != nullptr || player->GetMoveType() == MOVETYPE_LADDER || player->GetWaterLevel() >= WL_Waist)
	{
		newOrigin -= (*g_pGameRules)->GetViewVectors()->m_vDuckHullMin - (*g_pGameRules)->GetViewVectors()->m_vHullMin;
	}
	else
	{
		Vector hullSizeNormal = (*g_pGameRules)->GetViewVectors()->m_vHullMax - (*g_pGameRules)->GetViewVectors()->m_vHullMin;
		Vector hullSizeDuck = (*g_pGameRules)->GetViewVectors()->m_vDuckHullMax - (*g_pGameRules)->GetViewVectors()->m_vDuckHullMin;
		Vector delta = (hullSizeNormal - hullSizeDuck) * -0.5f;

		newOrigin += delta;
	}

	mv->_m_vecAbsOrigin = newOrigin;

	auto offset = GetPlayerViewOffset(true);

	player->SetViewOffset(offset);

	player->SetDucking(FALSE);
	player->SetDucked(TRUE);
	player->SetLastDuckTime(Interfaces::Globals->curtime);
	player->AddFlag(FL_DUCKING | FL_ANIMDUCKING);

	FixPlayerCrouchStuck(true);
	CategorizePosition();

	if (player->GetDuckAmount() != 1.0f)
		player->SetDuckAmount(1.0f);
}

bool CGameMovement::CanUnduck()
{
	trace_t tr;
	Vector newOrigin = mv->_m_vecAbsOrigin;

	if (player->GetGroundEntity() != nullptr)
	{
		for (int i = 0; i < 3; ++i)
			newOrigin[i] -= ((*g_pGameRules)->GetViewVectors()->m_vDuckHullMin[i] - (*g_pGameRules)->GetViewVectors()->m_vHullMin[i]);
	}
	else
	{
		Vector hullSizeNormal = (*g_pGameRules)->GetViewVectors()->m_vHullMax - (*g_pGameRules)->GetViewVectors()->m_vHullMin;
		Vector hullSizeDuck = (*g_pGameRules)->GetViewVectors()->m_vDuckHullMax - (*g_pGameRules)->GetViewVectors()->m_vDuckHullMin;
		Vector delta = hullSizeNormal - hullSizeDuck;

		newOrigin -= delta; //+= -delta;
	}

	bool duckedBackup = player->GetDucked();
	player->SetDucked(FALSE);
	TracePlayerBBox(mv->_m_vecAbsOrigin, newOrigin, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);
	player->SetDucked(duckedBackup);

	return (!tr.startsolid && tr.fraction == 1.f);
}

//dangerzone correct
bool CCSGameMovement::CanUnduck()
{
	if (m_pCSPlayer->GetDuckOverride())
		return false;

	if (player->GetMoveType() == MOVETYPE_NOCLIP)
		return true;

	trace_t trace;

	Vector newOrigin = mv->_m_vecAbsOrigin;

	if (player->GetGroundEntity() != nullptr)
	{
		newOrigin += (*g_pGameRules)->GetViewVectors()->m_vDuckHullMin - (*g_pGameRules)->GetViewVectors()->m_vHullMin;
	}
	else
	{
		Vector hullSizeNormal = (*g_pGameRules)->GetViewVectors()->m_vHullMax - (*g_pGameRules)->GetViewVectors()->m_vHullMin;
		Vector hullSizeDuck = (*g_pGameRules)->GetViewVectors()->m_vDuckHullMax - (*g_pGameRules)->GetViewVectors()->m_vDuckHullMin;
		Vector delta = (hullSizeNormal - hullSizeDuck) * -0.5f;

		newOrigin += delta;
	}

	//dangerzone update:
	CTraceFilterForPlayerHeadCollision filter;
	filter.m_pPassEnt1 = (IHandleEntity*)player;
	filter.m_pSkip = (IHandleEntity*)player;
	filter.m_Unknown = 0;
	filter.m_iCollisionGroup = COLLISION_GROUP_PLAYER_MOVEMENT;

	UTIL_TraceHull(mv->_m_vecAbsOrigin, newOrigin, (*g_pGameRules)->GetViewVectors()->m_vHullMin, (*g_pGameRules)->GetViewVectors()->m_vHullMax, PlayerSolidMask(), (ITraceFilter*)&filter, trace);

	if (trace.startsolid || trace.fraction != 1.f)
		return false;

	return true;
}

//dangerzone correct
void CGameMovement::UpdateDuckJumpEyeOffset()
{
	auto duckjumptimesecs = player->GetDuckJumpTimeMsecs();

	if (duckjumptimesecs != 0)
	{
		auto v3 = 1000 - duckjumptimesecs;

		if (v3 >= 0)
		{
			if (v3 > 200)
			{
				player->SetDuckJumpTimeMsecs(0);
				SetDuckedEyeOffset(0.f);
				return;
			}
		}
		else
		{
			v3 = 0;
		}

		float v4 = 0.f;
		float v5 = (float)v3 * 0.0049999999f;

		if (v5 >= 0.f)
			v4 = fminf(v5, 1.f);

		SetDuckedEyeOffset((((1.f - v4) * (1.f - v4)) * 3.f) - ((((1.f - v4) * (1.f - v4)) * 2.f) * (1.f - v4)));
	}
}

bool CGameMovement::CanUnduckJump(trace_t& tr)
{
	Vector end = mv->_m_vecAbsOrigin;
	end.z -= 36.f;

	TracePlayerBBox(mv->_m_vecAbsOrigin, end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, tr);

	if (tr.fraction >= 1.f)
		return false;

	end.z = mv->_m_vecAbsOrigin.z - (tr.fraction * 36.f);

	bool saveducked = player->GetDucked();

	player->SetDucked(FALSE);

	trace_t trace;
	TracePlayerBBox(mv->_m_vecAbsOrigin, end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, trace);

	player->SetDucked(saveducked);

	if (trace.startsolid)
		return false;

	return true;
}

void CGameMovement::StartUnduckJump()
{
	player->AddFlag(FL_DUCKING);
	player->SetDucked(TRUE);
	player->SetDucking(FALSE);

	Vector viewOffset = GetPlayerViewOffset(true);
	player->SetViewOffset(viewOffset);

	Vector hullSizeNormal = (*g_pGameRules)->GetViewVectors()->m_vHullMax - (*g_pGameRules)->GetViewVectors()->m_vHullMin;
	Vector hullSizeDuck = (*g_pGameRules)->GetViewVectors()->m_vDuckHullMax - (*g_pGameRules)->GetViewVectors()->m_vDuckHullMin;
	Vector delta = hullSizeNormal - hullSizeDuck;

	mv->_m_vecAbsOrigin = mv->_m_vecAbsOrigin + delta;

	FixPlayerCrouchStuck(true);
	CategorizePosition();
}

void CGameMovement::FinishUnduckJump(trace_t& tr)
{
	Vector newOrigin = mv->_m_vecAbsOrigin;

	Vector hullSizeNormal = (*g_pGameRules)->GetViewVectors()->m_vHullMax - (*g_pGameRules)->GetViewVectors()->m_vHullMin;
	Vector hullSizeDuck = (*g_pGameRules)->GetViewVectors()->m_vDuckHullMax - (*g_pGameRules)->GetViewVectors()->m_vDuckHullMin;
	Vector delta = hullSizeNormal - hullSizeDuck;

	float storedDeltaZ = delta.z;
	delta.z *= tr.fraction;
	storedDeltaZ -= delta.z;

	player->RemoveFlag(FL_DUCKING | FL_ANIMDUCKING); // ~6
	player->SetDucked(FALSE);
	player->SetDucking(FALSE);
	player->SetInDuckJump(false);
	player->SetDuckTimeMsecs(0);
	player->SetDuckJumpTimeMsecs(0);
	player->SetJumpTimeMsecs(0);

	Vector viewOffset = GetPlayerViewOffset(false);
	viewOffset.z -= storedDeltaZ;

	player->SetViewOffset(viewOffset);

	newOrigin -= delta;
	mv->_m_vecAbsOrigin = newOrigin;

	CategorizePosition();
}

void CGameMovement::SetDuckedEyeOffset(float duckFraction)
{
	float newDuckFraction = ((duckFraction * duckFraction) * 3.f) - (((duckFraction * duckFraction) * 2.f) * duckFraction);

	Vector mins_duck = GetPlayerMins(true);
	Vector mins_stand = GetPlayerMins(false);

	float fMore = (mins_duck.z - mins_stand.z);

	Vector view_duck = GetPlayerViewOffset(true);
	Vector view_stand = GetPlayerViewOffset(false);

	Vector temp = player->GetViewOffset();
	temp.z = ((view_duck.z - fMore) * newDuckFraction) + ((1.f - newDuckFraction) * view_stand.z);

	player->SetViewOffset(temp);
}

void CGameMovement::FixPlayerCrouchStuck(bool upward)
{
	bool direction = (upward != 0);

	trace_t trace;
	if (TestPlayerPosition(mv->_m_vecAbsOrigin, COLLISION_GROUP_PLAYER_MOVEMENT, trace) != -1)
	{
		Vector temp = mv->_m_vecAbsOrigin;

		for (int i = 0; i < 36; ++i)
		{
			Vector orig = mv->_m_vecAbsOrigin;
			orig.z += direction;
			mv->_m_vecAbsOrigin = orig;

			if (TestPlayerPosition(mv->_m_vecAbsOrigin, COLLISION_GROUP_PLAYER_MOVEMENT, trace) == -1)
				return;
		}

		mv->_m_vecAbsOrigin = temp;
	}
}

CreateStuckTableFn oCreateStuckTable;
Vector** rgv3tStuckTable; //[54];

int GetRandomStuckOffsets(CBasePlayer* pPlayer, Vector& offset)
{
	// Last time we did a full
	int idx;
	idx = pPlayer->GetStuckLast();
	pPlayer->SetStuckLast(pPlayer->GetStuckLast() + 1);

	VectorCopy(*rgv3tStuckTable[idx % 54], offset);

	return (idx % 54);
}

//-----------------------------------------------------------------------------
// Purpose:
// Input  : nIndex -
//			server -
//-----------------------------------------------------------------------------
void ResetStuckOffsets(CBasePlayer* pPlayer)
{
	pPlayer->SetStuckLast(0);
}

#define CHECKSTUCK_MINTIME 0.05f
//-----------------------------------------------------------------------------
// Purpose:
// Input  : &input -
// Output : int
//-----------------------------------------------------------------------------
int CGameMovement::CheckStuck(void)
{
	//#if defined( CLIENT_DLL )
	// Don't bother trying to jitter the player on the client, the server position will fix any true "stuck" issues and
	//  we send player origins with full precision anyway so getting stuck due to epsilon issues shouldn't occur...
	// The potential bad effect would be interacting with NPCs with a lot of lag might feel more warpy w/o the unstuck code running
	if (!cl_pred_checkstuck.GetVar()->GetBool())
		return 0;
	//#endif

	Vector base;
	Vector offset;
	Vector test;
	EntityHandle_t hitent;
	int idx;
	float fTime;
	trace_t traceresult;

	oCreateStuckTable();

	// player->SetStuckCharacter( NULL );

	m_bInStuckTest = true;
	hitent = TestPlayerPosition(mv->GetAbsOrigin(), COLLISION_GROUP_PLAYER_MOVEMENT, traceresult);
	m_bInStuckTest = false;
	if (hitent == INVALID_ENTITY_HANDLE)
	{
		ResetStuckOffsets(player);
		return 0;
	}

	// Deal with stuckness...
#if 0
#ifndef DEDICATED
	if (developer.GetVar()->GetBool())
	{
		bool isServer = player->IsServer();
		engine->Con_NPrintf(isServer, "%s stuck on object %i/%s",
			isServer ? "server" : "client",
			hitent.GetEntryIndex(), MoveHelper()->GetName(hitent));
	}
#endif
#endif

	VectorCopy(mv->GetAbsOrigin(), base);

	//
	// Deal with precision error in network.
	//
	// World or BSP model
	if (!player->IsServer())
	{
		if ((*Interfaces::MoveHelperClient)->IsWorldEntity(hitent))
		{
			int nReps = 0;
			ResetStuckOffsets(player);
			do
			{
				GetRandomStuckOffsets(player, offset);
				VectorAdd(base, offset, test);

				if (TestPlayerPosition(test, COLLISION_GROUP_PLAYER_MOVEMENT, traceresult) == INVALID_ENTITY_HANDLE)
				{
					ResetStuckOffsets(player);
					mv->SetAbsOrigin(test);
					return 0;
				}
				nReps++;
			} while (nReps < 54);
		}
	}

	// Only an issue on the client.
	idx = player->IsServer() ? 0 : 1;

	static DWORD Plat_FloatTime_Adr = 0;
	if (Plat_FloatTime_Adr == 0)
	{
		//decrypts(0)
		Plat_FloatTime_Adr = (DWORD)GetProcAddress((HMODULE)Tier0Handle, XorStr("Plat_FloatTime"));
		//encrypts(0)
	}

	fTime = ((double(*)())Plat_FloatTime_Adr)();

	// Too soon?
	if (m_flStuckCheckTime[player->entindex()][idx] >= fTime - CHECKSTUCK_MINTIME)
	{
		return 1;
	}
	m_flStuckCheckTime[player->entindex()][idx] = fTime;

	(*Interfaces::MoveHelperClient)->AddToTouched(traceresult, mv->m_vecVelocity_);
	m_bInStuckTest = true;
	GetRandomStuckOffsets(player, offset);
	VectorAdd(base, offset, test);

	if (TestPlayerPosition(test, COLLISION_GROUP_PLAYER_MOVEMENT, traceresult) == INVALID_ENTITY_HANDLE)
	{
		ResetStuckOffsets(player);
		mv->SetAbsOrigin(test);
		return 0;
	}
	m_bInStuckTest = false;

	return 1;
}

void CGameMovement::CategorizeGroundSurface(trace_t& tr)
{
	IPhysicsSurfaceProps* physProps = (*Interfaces::MoveHelperClient)->GetSurfaceProps();

	// mov     [ecx+359Ch], edx
	player->SetSurfaceProps(tr.surface.surfaceProps);

	player->SetSurfaceData(physProps->GetSurfaceData(player->GetSurfaceProps()));

	physProps->GetPhysicsProperties(player->GetSurfaceProps(), 0, 0, player->GetSurfaceFrictionAdr(), 0);

	player->SetSurfaceFriction(player->GetSurfaceFriction() * 1.25f);

	if (player->GetSurfaceFriction() > 1.f)
		player->SetSurfaceFriction(1.f);

	// mov     [ecx+35A4h], al
	player->SetTextureType(player->GetSurfaceData()->game.gamematerial);
}

bool CGameMovement::InWater()
{
	return (player->GetWaterLevel() > 1);
}

CBaseHandle CGameMovement::TestPlayerPosition(const Vector& pos, int collisionGroup, trace_t& pm)
{
	++m_nTraceCount;

	Vector maxs = GetPlayerMaxs();
	Vector mins = GetPlayerMins();

	Ray_t ray;
	ray.Init(pos, pos, mins, maxs);

	ITraceFilter* filter = LockTraceFilter(collisionGroup);
	UTIL_TraceRay(ray, PlayerSolidMask(), filter, &pm);
	UnlockTraceFilter(filter);

	if ((pm.contents & PlayerSolidMask()) && pm.m_pEnt != nullptr)
	{
		// v11 = *(*(*v10 + 8))();
		return pm.m_pEnt->GetRefEHandle();
	}

	return (CBaseHandle)-1;
}

void CGameMovement::SetGroundEntity(trace_t* pm)
{
	CBaseEntity *newGround = nullptr, *oldGround = nullptr;

	if (pm != nullptr)
		newGround = pm->m_pEnt;
	else
		newGround = nullptr;

	if (player->GetGroundEntity() == nullptr)
		oldGround = nullptr;
	else
		oldGround = player->GetGroundEntity();

	Vector baseVelocity = player->GetBaseVelocity();

	if (oldGround)
	{
		if (!newGround)
		{
			Vector* vel = oldGround->GetAbsVelocity();
			// v13 = *(v17 + 0x94) + *&v18;
			baseVelocity += *vel;
			baseVelocity.z = vel->z;
		}
	}
	else if (newGround)
	{
		Vector* vel = newGround->GetAbsVelocity();
		baseVelocity -= *vel;
		baseVelocity.z = vel->z;
	}

	player->SetBaseVelocity(baseVelocity);
	player->SetGroundEntity(newGround);

	if (newGround != nullptr)
	{
		CategorizeGroundSurface(*pm);
		player->SetWaterJumpTime(0.f);

		if (pm->DidHitWorld())
			(*Interfaces::MoveHelperClient)->AddToTouched(*pm, mv->m_vecVelocity_);

		if (player->GetMoveType() != MOVETYPE_NOCLIP)
			mv->m_vecVelocity_.z = 0.f;
	}
}

void CGameMovement::StepMove(Vector& vecDestination, trace_t& trace)
{
	//
	// Save the move position and velocity in case we need to put it back later.
	//
	Vector vecPos, vecVel;
	VectorCopy(mv->GetAbsOrigin(), vecPos);
	VectorCopy(mv->m_vecVelocity_, vecVel);

	//
	// First try walking straight to where they want to go.
	//
	Vector vecEndPos;
	VectorCopy(vecDestination, vecEndPos);
	TryPlayerMove(&vecEndPos, &trace);

	//
	// mv now contains where they ended up if they tried to walk straight there.
	// Save those results for use later.
	//
	Vector vecDownPos, vecDownVel;
	VectorCopy(mv->GetAbsOrigin(), vecDownPos);
	VectorCopy(mv->m_vecVelocity_, vecDownVel);

	//
	// Reset original values to try some other things.
	//
	mv->_m_vecAbsOrigin = vecPos;
	VectorCopy(vecVel, mv->m_vecVelocity_);

	//
	// Move up a stair height.
	// Slide forward at the same velocity but from the higher position.
	//
	VectorCopy(mv->GetAbsOrigin(), vecEndPos);
	if (player->GetAllowAutoMovement())
		vecEndPos.z += player->GetStepSize() + 0.03125f;

	// Only step up as high as we have headroom to do so.
	TracePlayerBBox(mv->_m_vecAbsOrigin, vecEndPos, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, trace);

	if (!trace.startsolid && !trace.allsolid)
		mv->_m_vecAbsOrigin = trace.endpos;

	TryPlayerMove();

	//
	// Move down a stair (attempt to).
	// Slide forward at the same velocity from the lower position.
	//
	vecEndPos = mv->_m_vecAbsOrigin;

	if (player->GetAllowAutoMovement())
		vecEndPos.z -= player->GetStepSize() + 0.03125f;

	TracePlayerBBox(mv->_m_vecAbsOrigin, vecEndPos, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, trace);

	// If we are not on the ground any more then use the original movement attempt.
	if (trace.plane.normal.z < 0.7f)
	{
		mv->_m_vecAbsOrigin = vecDownPos;
		mv->m_vecVelocity_ = vecDownVel;

		float dist = mv->_m_vecAbsOrigin.z - vecPos.z;

		if (dist > 0.f)
			mv->m_outStepHeight += dist;

		return;
	}

	// If the trace ended up in empty space, copy the end over to the origin.
	if (!trace.startsolid && !trace.allsolid)
		mv->_m_vecAbsOrigin = trace.endpos;

	// Copy this origin to up.
	Vector vecUpPos = mv->_m_vecAbsOrigin;

	// decide which one went farther
	float flDownDist = (vecDownPos.x - vecPos.x) * (vecDownPos.x - vecPos.x) + (vecDownPos.y - vecPos.y) * (vecDownPos.y - vecPos.y);
	float flUpDist = (vecUpPos.x - vecPos.x) * (vecUpPos.x - vecPos.x) + (vecUpPos.y - vecPos.y) * (vecUpPos.y - vecPos.y);

	if (flDownDist > flUpDist)
	{
		mv->_m_vecAbsOrigin = vecDownPos;
		mv->m_vecVelocity_ = vecDownVel;
	}
	else
	{
		// copy z value from slide move
		mv->m_vecVelocity_.z = vecDownVel.z;
	}

	float dist = mv->_m_vecAbsOrigin.z - vecPos.z;

	if (dist > 0.f)
		mv->m_outStepHeight += dist;
}

ITraceFilter* CGameMovement::LockTraceFilter(int collisionGroup)
{
	// mov     edx, s_nTraceFilterCount // s_nTraceFilterCount = 14F83F9C
	// cmp     edx, 8
	if (*s_nTraceFilterCount >= 8)
		return nullptr;

	// lea     esi, s_TraceFilter[eax*4] ; // s_TraceFilter = 10AA5610

	CTraceFilterSkipTwoEntities_CSGO* filter = (CTraceFilterSkipTwoEntities_CSGO*)((DWORD)s_TraceFilter + sizeof(CTraceFilterSkipTwoEntities_CSGO) * *s_nTraceFilterCount);
	*s_nTraceFilterCount = *s_nTraceFilterCount + 1;

	filter->SetPassEntity(mv->m_nPlayerHandle.Get());
	filter->SetCollisionGroup(collisionGroup);

	return (ITraceFilter*)filter;
}

void CGameMovement::UnlockTraceFilter(ITraceFilter*& filter)
{
	*s_nTraceFilterCount = *s_nTraceFilterCount - 1;
	filter = NULL;
}

bool CGameMovement::GameHasLadders()
{
	return true;
}

void CCSGameMovement::PreventBunnyJumping()
{
	// eax+322Ch
	float adjustedmaxspeed = player->GetMaxSpeed() * 1.1f;

	if (adjustedmaxspeed > 0.f)
	{
		float len = sqrtf(((mv->m_vecVelocity_.x * mv->m_vecVelocity_.x) + (mv->m_vecVelocity_.y * mv->m_vecVelocity_.y)) + (mv->m_vecVelocity_.z * mv->m_vecVelocity_.z));

		if (adjustedmaxspeed < len)
		{
			float ratio = adjustedmaxspeed / len;

			mv->m_vecVelocity_ *= ratio;
		}
	}
}

void CCSGameMovement::DecayAimPunchAngle()
{
	// mov 		xmm0, qword ptr [edx+301Ch]
	QAngle aimPunchAng = m_pCSPlayer->GetPunch();
	// mov 		xmm0, qword ptr [edx+3028h]
	Vector aimPunchAngVel = m_pCSPlayer->GetPunchVel();

	float v26 = Interfaces::Globals->interval_per_tick * weapon_recoil_decay2_lin.GetVar()->GetFloat();

	float v5 = Interfaces::Globals->interval_per_tick;
	v5 *= weapon_recoil_decay2_exp.GetVar()->GetFloat();

	float v6 = expf(-v5);

	QAngle tmp = aimPunchAng * v6;

	float v9 = sqrtf((tmp.x * tmp.x) + (tmp.y * tmp.y) + (tmp.z * tmp.z));

	Vector v10;
	if (v9 <= v26)
	{
		v10.x = 0.f;
		v10.y = 0.f;
		v10.z = 0.f;
	}
	else
	{
		v10.x = tmp.x * (1.f - (v26 / v9));
		v10.y = tmp.y * (1.f - (v26 / v9));
		v10.z = tmp.z * (1.f - (v26 / v9));
	}

	Vector v11 = v10 + ((aimPunchAngVel * Interfaces::Globals->interval_per_tick) * 0.5f);

	float decayamount = Interfaces::Globals->interval_per_tick * weapon_recoil_vel_decay.GetVar()->GetFloat();
	decayamount = expf(-decayamount);

	Vector newpunchvel = aimPunchAngVel * decayamount;

	Vector newaimpunch = ((newpunchvel * Interfaces::Globals->interval_per_tick) * 0.5f) + v11;

	if (newaimpunch.x != aimPunchAng.x || newaimpunch.y != aimPunchAng.y || newaimpunch.z != aimPunchAng.z)
	{
		player->SetPunch(QAngle(newaimpunch.x, newaimpunch.y, newaimpunch.z));
	}

	if (newpunchvel.x != aimPunchAngVel.x || newpunchvel.y != aimPunchAngVel.y || newpunchvel.z != aimPunchAngVel.z)
	{
		player->SetPunchVel(newpunchvel);
	}
}