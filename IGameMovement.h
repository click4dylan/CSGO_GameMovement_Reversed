#pragma once
#include "CreateMove.h"
#include "VPhysics.h"
#include "CBaseHandle.h"
#include "utlvectorsimple.h"
#include "utlvector.h"
#define MAX_CLIMB_SPEED	200

//-----------------------------------------------------------------------------
// An entity identifier that works in both game + client dlls
//-----------------------------------------------------------------------------

typedef CBaseHandle EntityHandle_t;


#define INVALID_ENTITY_HANDLE INVALID_EHANDLE_INDEX
#define PRINTF_FORMAT_STRING _Printf_format_string_
enum PLAYER_ANIM;

#include "soundflags.h"

class CMoveData
{
	//public:
	//    char pad_0x00[183];
public:
	bool            m_bFirstRunOfFunctions : 1;
	bool            m_bGameCodeMovedPlayer : 1;
	bool            m_bNoAirControl : 1;

	EntityHandle_t    m_nPlayerHandle;    // edict index on server, client entity handle on client

	int                m_nImpulseCommand;    // Impulse command issued.
	QAngle            m_vecViewAngles;    // Command view angles (local space)
	QAngle            m_vecAbsViewAngles;    // Command view angles (world space)
	int                m_nButtons;            // Attack buttons.
	int                m_nOldButtons;        // From host_client->oldbuttons;
	float            m_flForwardMove;
	float            m_flSideMove;
	float            m_flUpMove;

	float            _m_flMaxSpeed;
	float            m_flClientMaxSpeed;

	// Variables from the player edict (sv_player) or entvars on the client.
	// These are copied in here before calling and copied out after calling.
	Vector            m_vecVelocity_;        // edict::velocity        // Current movement direction.
	Vector            m_vecOldVelocity;
	float            somefloat;
	QAngle            m_vecAngles;        // edict::angles
	QAngle            m_vecOldAngles;

	// Output only
	float            m_outStepHeight;    // how much you climbed this move
	Vector            m_outWishVel;        // This is where you tried 
	Vector            m_outJumpVel;        // This is your jump velocity

										   // Movement constraints    (radius 0 means no constraint)
	Vector            m_vecConstraintCenter;
	float            m_flConstraintRadius;
	float            m_flConstraintWidth;
	float            m_flConstraintSpeedFactor;
	bool            m_bConstraintPastRadius;        ///< If no, do no constraining past Radius.  If yes, cap them to SpeedFactor past radius

	void            SetAbsOrigin(const Vector &vec);
	const Vector    &GetAbsOrigin() const;

public:
	Vector            _m_vecAbsOrigin;        // edict::origin

};

//-----------------------------------------------------------------------------
// Functions the engine provides to IGameMovement to assist in its movement.
//-----------------------------------------------------------------------------

abstract_class IMoveHelper
{
public:
	// Call this to set the singleton
	static IMoveHelper* GetSingleton() { return sm_pSingleton; }

	// Methods associated with a particular entity
	virtual	char const*		GetName(EntityHandle_t handle) const = 0;

	// sets the entity being moved
	virtual void	SetHost(CBaseEntity *host) = 0;

	// Adds the trace result to touch list, if contact is not already in list.
	virtual void	ResetTouchList(void) = 0;
	virtual bool	AddToTouched(const CGameTrace& tr, const Vector& impactvelocity) = 0;
	virtual void	ProcessImpacts(void) = 0;

	// Numbered line printf
	virtual void	Con_NPrintf(int idx, char const* fmt, ...) = 0;

	// These have separate server vs client impementations
	virtual void	StartSound(const Vector& origin, int channel, char const* sample, float volume, soundlevel_t soundlevel, int fFlags, int pitch) = 0;
	virtual void	StartSound(const Vector& origin, const char *soundname) = 0;
	virtual void	PlaybackEventFull(int flags, int clientindex, unsigned short eventindex, float delay, Vector& origin, Vector& angles, float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2) = 0;

	// Apply falling damage to m_pHostPlayer based on m_pHostPlayer->m_flFallVelocity.
	virtual bool	PlayerFallingDamage(void) = 0;

	// Apply falling damage to m_pHostPlayer based on m_pHostPlayer->m_flFallVelocity.
	virtual void	PlayerSetAnimation(PLAYER_ANIM playerAnim) = 0;

	virtual IPhysicsSurfaceProps *GetSurfaceProps(void) = 0;

	virtual bool IsWorldEntity(const CBaseHandle &handle) = 0;

protected:
	// Inherited classes can call this to set the singleton
	static void SetSingleton(IMoveHelper* pMoveHelper) { sm_pSingleton = pMoveHelper; }

	// Clients shouldn't call delete directly
	virtual			~IMoveHelper() {}

	// The global instance
	static IMoveHelper* sm_pSingleton;
};

class CMoveHelperClient : public IMoveHelper
{
public:
	CMoveHelperClient(void);
	virtual			~CMoveHelperClient(void);

	char const*		GetName(EntityHandle_t handle) const;

	// touch lists
	virtual void	ResetTouchList(void);
	virtual bool	AddToTouched(const trace_t& tr, const Vector& impactvelocity);
	virtual void	ProcessImpacts(void);

	// Numbered line printf
	virtual void	Con_NPrintf(int idx, char const* fmt, ...);

	virtual bool	PlayerFallingDamage(void);
	virtual void	PlayerSetAnimation(PLAYER_ANIM eAnim);

	// These have separate server vs client impementations
	virtual void	StartSound(const Vector& origin, int channel, char const* sample, float volume, soundlevel_t soundlevel, int fFlags, int pitch);
	virtual void	StartSound(const Vector& origin, const char *soundname);
	virtual void	PlaybackEventFull(int flags, int clientindex, unsigned short eventindex, float delay, Vector& origin, Vector& angles, float fparam1, float fparam2, int iparam1, int iparam2, int bparam1, int bparam2);
	virtual IPhysicsSurfaceProps *GetSurfaceProps(void);

	virtual bool IsWorldEntity(const CBaseHandle &handle);

	void			SetHost(CBaseEntity *host);

private:
	// results, tallied on client and server, but only used by server to run SV_Impact.
	// we store off our velocity in the trace_t structure so that we can determine results
	// of shoving boxes etc. around.
	struct touchlist_t
	{
		Vector	deltavelocity;
		trace_t trace;

		touchlist_t() {}

	private:
		touchlist_t(const touchlist_t &src);
	};

	CUtlVector<touchlist_t>			m_TouchList;

	CBaseEntity*	m_pHost;
};

//-----------------------------------------------------------------------------
// Purpose: The basic player movement interface
//-----------------------------------------------------------------------------

class IGameMovement
{
public:
	virtual			~IGameMovement(void) {}

	// Process the current movement command
	virtual void	ProcessMovement(CBasePlayer *pPlayer, CMoveData *pMove) = 0;
	virtual void	Reset(void) = 0;
	virtual void	StartTrackPredictionErrors(CBasePlayer *pPlayer) = 0;
	virtual void	FinishTrackPredictionErrors(CBasePlayer *pPlayer) = 0;
	virtual void	DiffPrint(char const *fmt, ...) = 0;

	// Allows other parts of the engine to find out the normal and ducked player bbox sizes
	virtual Vector const&	GetPlayerMins(bool ducked) const = 0;
	virtual Vector const&	GetPlayerMaxs(bool ducked) const = 0;
	virtual Vector const&   GetPlayerViewOffset(bool ducked) const = 0;

	virtual bool		IsMovingPlayerStuck(void) const = 0;
	virtual CBasePlayer *GetMovingPlayer(void) const = 0;
	virtual void		UnblockPusher(CBasePlayer *pPlayer, CBaseEntity *pPusher) = 0;

	virtual void SetupMovementBounds(CMoveData *pMove) = 0;
};


class CGameMovement : public IGameMovement
{
public:
	//DECLARE_CLASS_NOBASE(CGameMovement);

	//CGameMovement(void);
	//virtual			~CGameMovement(void);

	CGameMovement(void);
	virtual	~CGameMovement(void);
	virtual void ProcessMovement(CBasePlayer *pPlayer, CMoveData *pMove); // 1
	virtual void Reset();// 2
	virtual void StartTrackPredictionErrors(CBasePlayer* pPlayer); // 3
	virtual void FinishTrackPredictionErrors(CBasePlayer* pPlayer); // 4
	virtual void DiffPrint(char const *fmt, ...); // 5
	virtual const Vector& GetPlayerMins(bool ducked) const; // 6
	virtual const Vector& GetPlayerMaxs(bool ducked) const; // 7
	virtual const Vector& GetPlayerViewOffset(bool ducked) const; // 8
	virtual bool IsMovingPlayerStuck() const; // 9
	virtual CBasePlayer* GetMovingPlayer() const; // 10
	virtual void UnblockPusher(CBasePlayer *pPlayer, CBaseEntity *pPusher); // 11
	virtual void SetupMovementBounds(CMoveData* moveData); // 12
	virtual Vector GetPlayerMins(); // 13
	virtual Vector GetPlayerMaxs(); // 14
	virtual void TracePlayerBBox(const Vector& rayStart, const Vector& rayEnd, int fMask, int collisionGroup, trace_t& tracePtr); // 15
	virtual unsigned int PlayerSolidMask(bool brushOnly = false, CBasePlayer* testPlayer = nullptr); // 16
	virtual void PlayerMove(); // 17
	virtual float CalcRoll(const QAngle& angles, const Vector& velocity, float rollangle, float rollspeed); // 18
	virtual void DecayViewPunchAngle(); // 19
	virtual void CheckWaterJump(); // 20
	virtual void WaterMove(); // 21
	virtual void SlimeMove(); //22
	virtual void WaterJump(); // 23
	virtual void Friction(); // 24
	virtual void AirAccelerate(Vector& wishdir, float wishspeed, float accel); // 25
	virtual void AirMove(); // 26
	virtual bool CanAccelerate(); // 27
	virtual void Accelerate(Vector& wishdir, float wishspeed, float accel); // 28
	virtual void WalkMove(); // 29
	virtual void StayOnGround(); // 30
	virtual void FullWalkMove(); // 31
	virtual void OnJump(float stamina); // 32
	virtual void nullsub3(); // 33
	virtual void OnLand(float flFallVelocity); // 34
	bool		CheckInterval(IntervalType_t type);
	virtual int GetCheckInterval(IntervalType_t type); // 35
	virtual void StartGravity(); // 36
	virtual void FinishGravity(); // 37
	virtual void AddGravity(); // 38
	virtual bool CheckJumpButton(); // 39
	virtual void FullTossMove(); // 40
	virtual void FullLadderMove(); // 41
	virtual int TryPlayerMove(Vector *pFirstDest = NULL, trace_t *pFirstTrace = NULL); // 42
	virtual bool LadderMove(); // 43
	virtual bool OnLadder(trace_t& pm); // 44
	virtual float LadderDistance(); // 45
	virtual unsigned int LadderMask(); // 46
	virtual float ClimbSpeed(); // 47
	virtual float LadderLateralMultiplier(); // 48
	void   CheckVelocity(void);
	virtual int ClipVelocity(Vector& in, Vector& normal, Vector& out, float overbounce); // 49
	virtual bool CheckWater(); // 50
	virtual void GetWaterCheckPosition(int waterLevel, Vector* pos); // 51
	virtual void CategorizePosition(); // 52
	virtual void CheckParameters(); // 53
	virtual void ReduceTimers(); // 54
	virtual void CheckFalling(); // 55
	virtual void PlayerRoughLandingEffects(float fVol); // 56
	virtual void Duck();// 57
	virtual void HandleDuckingSpeedCrop(); //58
	virtual void FinishUnduck(); //59
	virtual void FinishDuck(); // 60
	virtual bool CanUnduck(); // 61
	virtual void UpdateDuckJumpEyeOffset(); // 62
	virtual bool CanUnduckJump(trace_t& tr); // 63
	virtual void StartUnduckJump(); // 64
	virtual void FinishUnduckJump(trace_t& tr); // 65
	virtual void SetDuckedEyeOffset(float duckFraction); // 66
	virtual void FixPlayerCrouchStuck(bool upward); // 67
	virtual void CategorizeGroundSurface(trace_t& tr); // 68
	virtual bool InWater(); // 69
	virtual CBaseHandle TestPlayerPosition(const Vector& pos, int collisionGroup, trace_t& pm); // 70
	virtual void SetGroundEntity(trace_t* pm); // 71
	virtual void StepMove(Vector& vecDestination, trace_t &trace); // 72
	virtual ITraceFilter* LockTraceFilter(int collisionGroup); // 73
	virtual void UnlockTraceFilter(ITraceFilter* &filter); // 74
	virtual bool GameHasLadders(); // 75
	void PerformFlyCollisionResolution(trace_t &pm, Vector &move);
	void PushEntity(Vector& push, trace_t *pTrace);
	void ResetGetWaterContentsForPointCache();
	int GetWaterContentsForPointCached(const Vector &point, int slot);
	float ComputeConstraintSpeedFactor(void);
	int CheckStuck(void);
	void FullNoClipMove(float factor, float maxacceleration);
	void FullObserverMove(void);

	enum
	{
		// eyes, waist, feet points (since they are all deterministic
		MAX_PC_CACHE_SLOTS = 3,
	};

	CBasePlayer *player; //4
	CMoveData *mv; //8
	int m_nOldWaterLevel; //12
	float m_flWaterEntryTime; //16
	int m_nOnLadder; //20
	Vector m_vecForward; //24
	Vector m_vecRight; //36
	Vector m_vecUp; //48
	int m_CachedGetPointContents[MAX_PLAYERS][MAX_PC_CACHE_SLOTS];
	Vector m_CachedGetPointContentsPoint[MAX_PLAYERS][MAX_PC_CACHE_SLOTS];
	BOOL m_bSpeedCropped; //3132
	bool m_bProcessingMovement; //3136
	bool m_bInStuckTest; //3137
	float			m_flStuckCheckTime[MAX_PLAYERS + 1][2]; //3138
	ITraceListData *m_pTraceListData; //3660
	int m_nTraceCount; //3664
};

class CCSGameMovement : public CGameMovement
{
public:
	CCSGameMovement();

	virtual void ProcessMovement(CBasePlayer *basePlayer, CMoveData* moveData); // 1
	// CGameMovement::Reset  // 2
	// CGameMovement::StartTrackPredictionErrors(); // 3
	// CGameMovement::FinishTrackPredictionErrors(); // 4
	virtual void DiffPrint(char const *fmt, ...); // 5
	// CGameMovement::GetPlayerMins(bool ducked) // 6
	// CGameMovement::GetPlayerMaxs(bool ducked) // 7
	// CGameMovement::GetPlayerViewOffset // 8
	// CGameMovement::IsMovingPlayerStuck // 9
	// CGameMovement::GetMovingPlayer // 10
	virtual void UnblockPusher(CBasePlayer *pPlayer, CBaseEntity *pPusher); // 11
	// CGameMovement::SetupMovementBounds // 12
	// CGameMovement::GetPlayerMins(); // 13
	// CGameMovement::GetPlayerMaxs(); // 14
	// CGameMovement::TracePlayerBBox(); // 15
	virtual unsigned int PlayerSolidMask(bool brushOnly = false, CBasePlayer* testPlayer = nullptr); // 16
	virtual void PlayerMove(); // 17
	void AutoMountMove();
	// CGameMovement::CalcRoll // 18
	// CGameMovement::DecayViewPunchAngle // 19
	// CGameMovement::CheckWaterJump // 20
	// CGameMovement::WaterMove(); // 21
	// CGameMovement::SlimeMove(); //22
	// CGameMovement::WaterJump // 23
	// CGameMovement::Friction // 24
	// CGameMovement::AirAccelerate // 25
	virtual void AirMove(); // 26 // 26
	virtual bool CanAccelerate(); // 27
	virtual void Accelerate(Vector&, float, float); // 28
	virtual void WalkMove(); // 29
	// CGameMovement::StayOnGround // 30
	// CGameMovement::FullWalkMove // 31
	virtual void OnJump(float stamina); // 32
	virtual void nullsub3(); // 33
	virtual void OnLand(float flFallVelocity);	// 34
	// CGameMovement::GetCheckInterval // 35
	// CGameMovement::StartGravity // 36
	// CGameMovement::FinishGravity // 37
	// CGameMovement::AddGravity //38
	virtual bool CheckJumpButton(); // 39
	// CGameMovement::FullTossMove // 40
	// CGameMovement::FullLadderMove // 41
	// CGameMovement::TryPlayerMove // 42
	virtual bool LadderMove(); // 43
	virtual bool OnLadder(trace_t& trace); // 44
	virtual float LadderDistance(); // 45
	// CGameMovement::LadderMask // 46
	virtual float ClimbSpeed() const; // 47
	virtual float LadderLateralMultiplier() const; // 48
	// CGameMovement::ClipVelocity // 49
	// CGameMovement::CheckWater // 50
	// CGameMovement::GetWaterCheckPosition // 51
	// CGameMovement::CategorizePosition // 52
	virtual void CheckParameters(); // 53
	virtual void ReduceTimers(); // 54
	// CGameMovement::CheckFalling // 55
	// CGameMovement::PlayerRoughLandingEffects // 56
	virtual void Duck(); // 57
	// CGameMovement::HandleDuckingSpeedCrop // 58
	virtual void FinishUnduck(); // 59
	virtual void FinishDuck(); // 60
	virtual bool CanUnduck(); // 61
	// CGameMovement::UpdateDuckJumpEyeOffset // 62
	// CGameMovement::CanUnduckJump // 63
	// CGameMovement::StartUnduckJump // 64
	// CGameMovement::FinishUnduckJump // 65
	// CGameMovement::SetDuckedEyeOffset // 66
	// CGameMovement::FixPlayerCrouchStuck // 67
	// CGameMovement::CategorizeGroundSurface // 68
	// CGameMovement::InWater // 69
	// CGameMovement::TestPlayerPosition // 70
	// CGameMovement::SetGroundEntity // 71
	// CGameMovement::StepMove // 72
	// CGameMovement::LockTraceFilter // 73
	// CGameMovement::UnlockTraceFilter // 74
	// CGameMovement::GameHasLadders // 75
	virtual void PreventBunnyJumping(); // 76
	virtual void DecayAimPunchAngle(); // 77
	void ApplyDuckRatio(float flDuckAmount);
	bool IsPlayerDucking();
	bool CanMove(CBasePlayer* ent);

	CBasePlayer *m_pCSPlayer; //3668
};

extern IGameMovement *g_pGameMovement;