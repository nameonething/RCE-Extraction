// RPC ids shared by the admin landing-path editor (client <-> server) and the per-client admin flag.
// Defined here in 4_World (not 5_Mission) so modded PlayerBase can reference them.
class ReconquestEvacRPC
{
	static const int ADMIN_SAVE_PATH = 965091;
	static const int ADMIN_PATH_ACK  = 965092;
	static const int ADMIN_FLAG      = 965093;
	static const int SIGNALLER_FLAG  = 965094;
	static const int ADMIN_SET_HULL  = 965095;
}

// Server-side coordinator for helicopter extractions.
//
// Velocity-driven flight: each tick we set the rigid-body velocity and let physics + Expansion
// net-sync move/interpolate it (smooth, like a real heli). Takeoff lifts before accelerating;
// landing hovers over the pad and descends; while landed velocity is zeroed so it rests.
class ReconquestEvacManager
{
	static const string HELI_CLASSNAME   = "ExpansionUh1h";   // CarScript-based Huey (Expansion)

	// Drop-off destination: the Altar trader helipad. Production lands on this exact point; admins can override
	// the approach path in $profile:rce_evac_landing_path.txt without rebuilding the PBO.
	static const vector SAFE_ZONE_POS          = "8229.85 469.345 9028.72";

	// --- cosmetic PILOT (visual only) --- posed in the real pilot seat animation, but kept out of the
	// actual crew slot. A real seat-0 crew occupant blocks many DayZ/Expansion heli actions, so the survivor
	// runs a vehicle command for the cockpit pose while seat 0 stays logically free.
	static const int PILOT_SEAT_INDEX = 0;
	// UH-1H seat 0 reports VEHICLESEAT_DRIVER, which visually looks like a car steering wheel grip on this
	// cosmetic non-driver body. Anchor the body at seat 0, but use the co-driver upper-body animation so the
	// hands rest lower/less "car-like" near the cyclic/collective area.

	// --- flight tuning ---
	static const float  INBOUND_DISTANCE  = 600.0;
	static const float  CRUISE_ALT        = 140.0;  // metres above ground it cruises at
	static const float  GLIDE_DISTANCE    = 350.0;  // climb-out / descent glide length (descend in pace)
	static const float  OBSTACLE_CLEARANCE = 16.0;  // metres to clear trees/buildings by
	static const float  TERRAIN_CLEARANCE = 6.0;    // metres to clear bare ground by
	static const float  LOOKAHEAD         = 90.0;   // metres to scan ahead along the flight path
	static const float  MAX_SPEED         = 85.0;
	static const float  ACCEL             = 12.0;
	static const float  DECEL             = 9.0;
	static const float  MAX_ACCEL         = 14.0;   // m/s^2 cap on the horizontal velocity-vector slew (smoothness)
	static const float  MAX_ACCEL_UP      = 22.0;   // m/s^2 cap on UPWARD slew -- generous so clearance climbs aren't throttled
	static const float  PITCH_RATE        = 70.0;   // deg/s max pitch slew (rate-limited like yaw -- kills the front-loaded "tick" step)
	static const float  ROLL_RATE         = 90.0;   // deg/s max roll slew
	static const float  VERT_GAIN         = 1.1;    // altitude P-controller strength
	static const float  VERT_UP           = 20.0;   // m/s max climb (brisk, to clear canopy)
	static const float  CLEARANCE_CLIMB   = 34.0;   // m/s max climb while ACTIVELY avoiding a raycast obstacle (tall masts/towers need a fast climb-out)
	static const float  VERT_DOWN         = 14.0;   // m/s max descent (gentle, smooth landing)
	static const float  LIFTOFF_HEIGHT    = 22.0;   // rise this high before building full forward speed
	static const float  TURN_RATE         = 40.0;   // deg/s max yaw change (eased turns, no snap-rotate)
	static const float  YAW_AVEL_SIGN     = 1.0;    // C (turn jitter): sign of the fed yaw angular velocity; flip to -1.0 if turns look wobbly/worse in-game
	static const float  PITCH_AVEL_SIGN   = 1.0;    // 3a: sign of fed PITCH angular velocity; flip to -1.0 if the NOSE tilt wobbles in-game
	static const float  ROLL_AVEL_SIGN    = 1.0;    // 3a: sign of fed ROLL angular velocity; flip to -1.0 if the BANK/roll wobbles in-game
	static const float  BANK_MAX          = 16.0;   // deg roll banked into a turn
	static const float  PITCH_MAX         = 14.0;   // deg nose-down at full forward speed (visual airframe tilt)
	static const float  ROLL_LAT          = 12.0;   // deg bank into lateral (sideways/crab) motion
	static const float  TURN_MIN_SCALE    = 0.08;   // forward speed floor while turning sharply
	static const float  LZ_APPROACH_DIST  = 12.0;   // m horizontal -> level approach: hold alt, crab to overhead
	static const float  LZ_APPROACH_EXIT  = 16.0;   // hysteresis: ENTER final-approach at 12, only LEAVE past 16 -> no mode flap near the boundary
	static const float  HEADING_FREEZE_DIST = 9.0;  // m horizontal -> stop turning (near-zero bearing is noisy)
	static const float  HEADING_UNFREEZE_DIST = 13.0; // hysteresis: FREEZE heading at 9, only RESUME turning past 13 -> no freeze/unfreeze flap
	static const float  PAD_RING_RADIUS   = 22.0;   // m radius to measure the LZ's surrounding tree-ring height
	static const float  ARRIVE_EPSILON    = 5.0;    // m horizontal -> begin vertical touchdown (wider = less overhead hover)
	static const float  DESCENT_TAPER     = 1.8;    // descent speed (m/s) per metre of height -> soft but decisive touchdown
	static const float  DESCENT_MIN       = 3.5;    // m/s minimum descent so it settles promptly (no crawl near the ground)
	static const float  LAND_THRESHOLD    = 1.2;    // m above local ground -> touchdown (controlled descent carries it most of the way, rest just holds)
	static const float  ROTOR_FLY         = 1.0;
	static const float  SKID_REST         = 0.6;    // m above ground the heli origin sits at on its skids
	static const int    TICK_MS           = 20;     // ~50 Hz
	static const int    COUNTDOWN_TICKS   = 750;    // ~15s
	static const int    WAIT_TIMEOUT_TICKS = 6000;  // ~120s
	static const int    BLOCKED_HOLD_MAX_TICKS = 1000; // ~20s of consecutive blocked-column holds -> recall the heli
	static const int    DROPOFF_TICKS     = 400;    // ~8s ceiling: keep retrying the eject; depart ~1s after empty, despawn-drop only if still stuck at the ceiling
	static const int    DROPOFF_BEAT_TICKS = 50;    // ~1s grace at safe zone before guaranteed crew release (was ~4s)
	static const int    DROPOFF_EMPTY_TICKS = 50;   // ~1s stable-empty before flying the heli away
	static const float  DEPART_DISTANCE   = 400.0;  // m from the safe zone before the emptied heli despawns
	static const int    DEPART_TIMEOUT    = 1200;   // ~24s backstop for the fly-away

	// --- landing-zone search ---
	static const float  LZ_SEARCH_MAX     = 400.0;  // search wide so a real 30 m clearing is reliably found
	static const float  LZ_FLAT_TOLERANCE = 3.0;    // max terrain spread across the pad (relatively flat)
	static const float  LZ_MIN_ELEVATION  = 0.5;
	static const float  LZ_CLEAR_RADIUS   = 30.0;  // require a 30 m circle clear of terrain hazards for searched LZs
	static const float  TOUCHDOWN_CLEAR_RADIUS = 10.0; // narrow helipad footprint: reject towers/fences/walls/gates under the heli
	static const float  TOUCHDOWN_SEARCH_MAX   = 10.0; // production fallback must stay within 10m of the configured pad
	static const float  SAFE_ZONE_WAYPOINT_EPSILON = 25.0; // fly-through waypoint radius; not a landing trigger
	static const float  SAFE_ZONE_SNAP_HEIGHT  = 4.0;  // final admin pad snap height once collision-scanned approach is complete
	static const string SAFE_ZONE_PATH_FILE    = "$profile:rce_evac_landing_path.txt";
	static const string ADMIN_ALLOWLIST_FILE   = "$profile:rce_evac_admins.txt";
	static const string SETTINGS_FILE          = "$profile:rce_evac_settings.txt";

	// phases
	static const int PH_INBOUND   = 0;
	static const int PH_WAIT      = 1;
	static const int PH_COUNTDOWN = 2;
	static const int PH_OUTBOUND  = 3;
	static const int PH_DROPOFF   = 4;
	static const int PH_DEPART    = 5;

	private static ref ReconquestEvacManager s_Instance;
	static bool s_BoardDebug = false;  // Phase-1 boarding diagnostic: log per-seat get-in resolution (PRODUCTION: off)

	static ReconquestEvacManager GetInstance()
	{
		if ( !s_Instance )
			s_Instance = new ReconquestEvacManager();
		return s_Instance;
	}

	protected ExpansionHelicopterScript m_Heli;
	protected DayZPlayerImplement m_Pilot;   // cosmetic pilot seated in PILOT_SEAT_INDEX (visual only; autopilot flies)
	protected PlayerBase m_Signaller;        // the flare-igniter -> target for extraction notifications
	protected bool   m_Active;
	protected int    m_Phase;
	protected int    m_Timer;
	protected float  m_CurSpeed;
	protected float  m_CurYaw;       // eased heading (deg) - rotated toward travel dir at TURN_RATE
	protected float  m_CurPitch;     // eased pitch (deg) - nose down into forward motion
	protected float  m_CurRoll;      // eased roll (deg)  - bank into lateral motion + turns
	protected vector m_LegStart;
	protected vector m_LandingPos;
	protected vector m_SafeZone;
	protected vector m_DepartTarget;
	protected ref array<vector> m_SafeZonePath;
	protected int    m_SafeZonePathIndex;
	protected bool   m_ForceSafeZoneTouchdown;
	protected bool   m_PlayerWasAboard;             // did a real player board? -> guarantee despawn-drop, not fly-away
	protected ref array<PlayerBase> m_DropPlayers;  // players to plant on the ground a frame after the heli despawns
	protected ref array<PlayerBase> m_EvacPassengers; // passengers captured at safe-zone arrival; used to verify actual dismount
	protected vector m_DropPos;
	protected int    m_EmptyDropoffTicks;
	protected float  m_PrevY;        // last tick's altitude (touchdown = was descending, now stopped)
	protected float  m_SmoothY;      // low-pass altitude target -> kills per-tick vertical bobbing
	protected bool   m_DescendStarted;
	protected bool   m_Landing;       // latched once over the pad -> commit to descent (kills approach oscillation)
	protected float  m_GroundRotor;   // rotor speed while parked: ramps FLY -> GROUND for a smooth spin-down
	protected vector m_AppliedVel;    // accel-limited applied velocity VECTOR (the one smoother; used by the slew)
	protected bool   m_Frozen;        // ground phase: heli is kinematic (physics off) and held -- don't touch it
	protected vector m_FrozenPos;
	protected int    m_FreezeTicks;
	protected int    m_StallTicks;
	protected int    m_BlockedHoldTicks;   // consecutive ticks spent in the blocked-landing-column hold (recall backstop)
	protected vector m_ProgressPos;        // wedge watchdog: position at the last 1s progress check
	protected int    m_ProgressTick;       // ticks since the last progress check
	protected int    m_NoProgressSecs;     // consecutive seconds of commanded-but-zero movement
	protected ref array<Object>    m_ScanObjs;    // reused scratch for canopy scans -> no per-call array alloc churn
	protected ref array<CargoBase> m_ScanCargos;
	protected float  m_Dt;              // real elapsed time (s) for the current tick (see FlightTick) -> consistent rate-limits under load
	protected float  m_LastTickTime;    // GetTickTime() (s) of the previous tick, for the real-dt delta
	protected bool   m_FinalApproach;   // hysteresis-latched: in level final-approach mode (enter 18m / exit 24m)
	protected bool   m_HeadingFrozen;   // hysteresis-latched: heading turning frozen near the pad (freeze 9m / resume 13m)
	// Backslide-clamp state: the last velocity we commanded and the position the airframe held after our
	// transform writes. The clamp in FlightTick re-pins the heli when physics steps it BACKWARD against the
	// commanded velocity between ticks -- those sub-metre backward steps are what clients render as jitter.
	protected bool   m_HasLastCmdVel;
	protected vector m_LastCmdVel;
	protected bool   m_HasLastWritePos;
	protected vector m_LastWritePos;

	protected vector m_RcePrevSyncPos;      // last-known good heli position (crash-site naming; live pos is 0,0,0 once destroyed)
	protected bool   m_RceHasPrevSyncPos;
	protected vector m_PrevSettleOri;      // last-tick orientation while settling -> detect when the tilt has stopped

	// lazy cache of the map's named locations (CfgWorlds Names), for the shot-down announcement
	protected ref array<ref ExpansionLocation> m_WorldLocations;

	// Real, signal-triggered extraction. Returns true only if a NEW extraction actually started -- the
	// signal pistol uses this to decide whether the (single-use) signal round was consumed.
	bool RequestExtraction(vector signalPos, PlayerBase signaller)
	{
		if ( !GetGame() || !GetGame().IsServer() )
			return false;
		if ( !signaller )
			return false;
		if ( m_Active )
		{
			// NEVER cancel a live extraction for a new signal: CancelCurrent() deletes the heli wherever it
			// is (dropping any passenger out of the sky mid-flight) and would let one signal grief another
			// player's evac. The new signaller just has to wait for the current cycle to finish.
			Notify( signaller, "EXTRACTION", "An extraction is already in progress -- try again later." );
			return false;
		}
		m_Signaller = signaller;
		if ( !StartExtraction( signalPos ) )   // notifies the signaller with the specific reason itself
		{
			m_Signaller = null;
			return false;
		}
		// no notification here: the HUD announces the approaching heli (phase 1). The signaller flag makes
		// the caller's HUD track this extraction at any range instead of the proximity scan.
		SendSignallerFlag( m_Signaller, true );
		return true;
	}

	// Returns false if nothing was started (no landable ground near the signal / heli spawn failed).
	// Failure notifications go to m_Signaller from HERE (null in test modes -> Notify no-ops).
	protected bool StartExtraction(vector signalPos)
	{
		bool lzFound;
		m_LandingPos = FindLandingSpot( signalPos, lzFound );
		if ( !lzFound )
		{
			Notify( m_Signaller, "EXTRACTION", "No clear landing zone near your position -- move to open ground and signal again." );
			return false;
		}

		vector mapCenter = Vector( 7680, 0, 7680 );
		vector inDir = mapCenter - m_LandingPos;
		inDir[1] = 0;
		inDir.Normalize();
		vector startPos = m_LandingPos + inDir * INBOUND_DISTANCE;
		startPos[1] = GetGame().SurfaceY( startPos[0], startPos[2] ) + CRUISE_ALT;

		// Spawn DIRECTLY at the airborne start point 600m out -- never at the LZ. Creating it at the LZ and
		// teleporting it a line later replicated the LZ frame to nearby clients first: the heli visibly
		// flashed on top of the signaller before snapping onto its approach path.
		Object obj = GetGame().CreateObjectEx( HELI_CLASSNAME, startPos, ECE_NONE );
		m_Heli = ExpansionHelicopterScript.Cast( obj );
		if ( !m_Heli )
		{
			Print( "[ReconquestEvac] FAILED to spawn '" + HELI_CLASSNAME + "'" );
			if ( obj )
				GetGame().ObjectDelete( obj );
			Notify( m_Signaller, "EXTRACTION", "Extraction unavailable -- no helicopter could be dispatched." );
			return false;
		}

		// With no player in the driver seat, EOnSimulate/EOnPostSimulate stop firing CLIENT-side, so passengers
		// receive the airframe as discrete net-sync snapshots instead of a smoothly simulated body -> visible
		// in-flight jitter. This heli is always driverless (autopilot flies it server-side), so force it to
		// stay actively simulated and clients keep interpolating it smoothly.
		m_Heli.SetRequiredSimulation( true );

		// Mark this heli autopilot-flown: the client smoother finds it via this flag, and the heli's EOnSimulate
		// override keeps it in the driverless/autopilot path if a future test ever uses seat 0 again.
		m_Heli.Reconquest_SetAutopilot( true );

		EquipHeli( m_Heli );
		m_Heli.Fill( CarFluid.FUEL, m_Heli.GetFluidCapacity( CarFluid.FUEL ) );
		m_Heli.Reconquest_SetRotors( ROTOR_FLY );

		m_CurSpeed = 0;
		m_DescendStarted = false;
		m_Landing = false;
		m_GroundRotor = ROTOR_FLY;
		m_StallTicks = 0;
		m_BlockedHoldTicks = 0;
		m_ProgressPos = startPos;
		m_ProgressTick = 0;
		m_NoProgressSecs = 0;
		m_LastTickTime = GetGame().GetTickTime();   // seed the real-dt clock so the first tick's delta is sane
		m_Dt = TICK_MS / 1000.0;
		m_FinalApproach = false;   // hysteresis state (reset per extraction)
		m_HeadingFrozen = false;
		m_HasLastCmdVel = false;
		m_LastCmdVel = "0 0 0";
		m_HasLastWritePos = false;
		m_LastWritePos = "0 0 0";
		m_RceHasPrevSyncPos = false;   // per-heli: a stale prev-pos would publish one garbage sync velocity
		m_RcePrevSyncPos = "0 0 0";
		m_PlayerWasAboard = false;
		m_EvacPassengers = null;
		m_EmptyDropoffTicks = 0;
		m_Frozen = false;
		m_FreezeTicks = 0;
		m_SafeZonePathIndex = 0;
		m_ForceSafeZoneTouchdown = false;
		m_PrevY    = startPos[1];
		m_SmoothY  = startPos[1];
		m_LegStart = startPos;
		m_CurYaw   = (m_LandingPos - startPos).VectorToAngles()[0];
		m_Heli.SetPosition( startPos );
		m_Heli.SetOrientation( Vector( m_CurYaw, 0, 0 ) );

		// COSMETIC PILOT: vanilla survivor posed in PILOT_SEAT_INDEX via StartCommand_Vehicle only.
		SpawnPilotVanilla( startPos );

		m_PrevSettleOri = "0 0 0";
		m_AppliedVel     = "0 0 0";

		m_Active = true;
		m_Phase  = PH_INBOUND;
		Print( "[ReconquestEvac] inbound from " + startPos.ToString() + " -> LZ " + m_LandingPos.ToString() );

		GetGame().GetCallQueue( CALL_CATEGORY_SYSTEM ).CallLater( FlightTick, TICK_MS, true );
		return true;
	}

	protected void FlightTick()
	{
		if ( !m_Heli )
		{
			Print( "[ReconquestEvac] heli vanished - aborting" );
			CancelCurrent();
			return;
		}

		// SHOT DOWN: players are allowed to destroy the evac heli (any phase -- inbound, parked, in flight,
		// even with passengers aboard). Detect it FIRST, before any velocity/orientation writes.
		if ( m_Heli.IsDamageDestroyed() )
		{
			OnHeliShotDown();
			return;
		}

		// REAL elapsed time since the last tick (seconds). CallQueue timing is NOT exactly 20ms under server
		// load; a FIXED dt makes the yaw/accel/velocity rate-limits inconsistent with the real interval (the
		// heli moves via physics for the REAL elapsed time but the controller assumed 20ms) -> catch-up jumps +
		// jitter, and it also breaks fix C's self-consistency (fed yaw rate = yawStep/dt must use the SAME dt the
		// step was clamped with). Clamp: lower bound guards the first tick / a zero delta; upper bound (50ms)
		// stops a frame hitch from becoming one violent yaw/accel lurch.
		float nowT = GetGame().GetTickTime();
		float rawDt = nowT - m_LastTickTime;
		m_Dt = Math.Clamp( rawDt, 0.001, 0.05 );
		m_LastTickTime = nowT;

		// BACKSLIDE CLAMP (functional anti-jitter fix, kept from the flight-tuning campaign): if physics
		// stepped the airframe BACKWARD against the commanded velocity since our last write (dot < 0, small
		// magnitude, airborne, cruise phases only), re-pin it to the last written position. Those sub-metre
		// backward steps are exactly what clients rendered as jitter.
		vector tickPos = m_Heli.GetPosition();
		vector tickVel = GetVelocity( m_Heli );
		float speedH = Math.Sqrt( tickVel[0] * tickVel[0] + tickVel[2] * tickVel[2] );
		if ( m_HasLastWritePos && m_HasLastCmdVel && speedH > 5.0 && rawDt < 0.08 && ( m_Phase == PH_INBOUND || m_Phase == PH_OUTBOUND || m_Phase == PH_DEPART ) )
		{
			vector clampDelta = tickPos - m_LastWritePos;
			float clampH = Math.Sqrt( clampDelta[0] * clampDelta[0] + clampDelta[2] * clampDelta[2] );
			float clampDot = clampDelta[0] * m_LastCmdVel[0] + clampDelta[2] * m_LastCmdVel[2];
			float clampGround = GetGame().SurfaceY( tickPos[0], tickPos[2] );
			float clampAlt = tickPos[1] - clampGround;
			if ( clampDot < -0.05 && clampH < 1.0 && clampAlt > 10.0 )
			{
				vector clampPos = tickPos;
				clampPos[0] = m_LastWritePos[0];
				clampPos[2] = m_LastWritePos[2];
				m_Heli.SetPosition( clampPos );
				SetVelocity( m_Heli, tickVel );
			}
		}

		// WEDGE WATCHDOG: an airframe stuck against unscanned geometry would otherwise hover forever and
		// hold the server-wide extraction slot. Once per second in the moving phases, compare the actual
		// movement against the commanded velocity; ~20s of commanded-but-zero movement -> release whoever
		// is seated onto the ground below and cancel. (Legit holds -- blocked-column, boarding, countdown --
		// command zero velocity, so they can never trip this.)
		if ( m_Phase == PH_INBOUND || m_Phase == PH_OUTBOUND || m_Phase == PH_DEPART )
		{
			m_ProgressTick++;
			if ( m_ProgressTick >= 50 )
			{
				m_ProgressTick = 0;
				float cmdH = Math.Sqrt( m_AppliedVel[0] * m_AppliedVel[0] + m_AppliedVel[2] * m_AppliedVel[2] );
				vector movedV = tickPos - m_ProgressPos;
				movedV[1] = 0;
				m_ProgressPos = tickPos;
				if ( cmdH > 3.0 && movedV.Length() < 1.0 )
					m_NoProgressSecs++;
				else
					m_NoProgressSecs = 0;
				if ( m_NoProgressSecs >= 20 )
				{
					Print( "[ReconquestEvac] commanded to move but no progress for 20s - airframe wedged, aborting extraction" );
					Notify( m_Signaller, "EXTRACTION", "Helicopter obstructed -- extraction aborted. Signal again from open ground." );
					CancelStuckFlight();
					return;
				}
			}
		}
		else
		{
			m_ProgressTick = 0;
			m_NoProgressSecs = 0;
			m_ProgressPos = tickPos;
		}

		// CRASH GUARD: if the heli has sunk through the terrain (the fall-through that crashed the server),
		// snap it back above the surface and kill its velocity before the physics solver NaNs.
		vector hp = m_Heli.GetPosition();
		float gy = GetGame().SurfaceY( hp[0], hp[2] );
		if ( hp[1] < gy - 0.3 )
		{
			hp[1] = gy + 0.5;
			m_Heli.SetPosition( hp );
			SetVelocity( m_Heli, "0 0 0" );
			dBodySetAngularVelocity( m_Heli, "0 0 0" );
			m_AppliedVel = "0 0 0";   // emergency snap bypasses the slew; reset it so the next tick ramps from rest
		}

		// Re-assert the cosmetic pilot's seated vehicle command + collision layer every tick.
		if ( m_Pilot )
			MaintainCosmeticPilot();

		// track the last-known good position each tick -- OnHeliShotDown names the crash site from it
		// (Expansion teleports a destroyed heli to "0 0 0" before deleting it, so the live position is useless)
		m_RcePrevSyncPos = m_Heli.GetPosition();
		m_RceHasPrevSyncPos = true;

		// publish the extraction phase + countdown for the client proximity HUD
		Reconquest_UpdateGui();

		if ( m_Phase == PH_INBOUND )
		{
			m_Heli.Reconquest_SetRotors( ROTOR_FLY );
			if ( DriveTo( m_LegStart, m_LandingPos ) )
			{
				m_Phase = PH_WAIT;
				m_Timer = 0;
				Print( "[ReconquestEvac] landed - waiting for a passenger" );
				// no chat line: the HUD's "Board helicopter (Xs)" phase covers this
			}
		}
		else if ( m_Phase == PH_WAIT )
		{
			RestOnGround();
			m_Timer++;

			if ( AnyAuthorizedPlayerAboard() )
			{
				m_PlayerWasAboard = true;
				m_Phase = PH_COUNTDOWN;
				m_Timer = COUNTDOWN_TICKS;
				Print( "[ReconquestEvac] signaller aboard - departing" );
				// no chat line: the HUD's "Departing in Xs" phase covers this
			}
			else if ( !m_Signaller || !m_Signaller.IsAlive() )
			{
				// signaller died or disconnected while the heli waited (deleted entity refs auto-null) and
				// nobody authorized is aboard -> no one left to extract, recall the heli
				Print( "[ReconquestEvac] signaller dead or disconnected - cancelling extraction" );
				CancelWaitPhase();
			}
			else if ( m_Timer > WAIT_TIMEOUT_TICKS )
			{
				Print( "[ReconquestEvac] nobody boarded - leaving" );
				Notify( m_Signaller, "EXTRACTION", "Nobody boarded -- helicopter leaving" );
				CancelWaitPhase();
			}
		}
		else if ( m_Phase == PH_COUNTDOWN )
		{
			RestOnGround();
			m_Timer--;
			if ( m_Timer % 50 == 0 )
				Print( "[ReconquestEvac] departing in " + (m_Timer / 50).ToString() + "s" );
			if ( m_Timer <= 0 )
			{
				vector safeTarget = SAFE_ZONE_POS;
				safeTarget[1] = GetGame().SurfaceY( safeTarget[0], safeTarget[2] );
				LoadSafeZonePath( safeTarget );
				m_LegStart = m_Heli.GetPosition();
				m_CurSpeed = 0;
				m_DescendStarted = false;
				m_Landing = false;
				m_GroundRotor = ROTOR_FLY;
				m_StallTicks = 0;
				m_BlockedHoldTicks = 0;
				m_Frozen = false;
				m_FreezeTicks = 0;
				if ( dBodyIsSet( m_Heli ) )
					dBodyDynamic( m_Heli, true );   // physics back ON for the velocity climb-out + cruise
				m_PrevY    = m_Heli.GetPosition()[1];
				m_SmoothY  = m_Heli.GetPosition()[1];
				m_CurYaw   = m_Heli.GetOrientation()[0];   // ease the turn from where it's pointing now
				m_AppliedVel = "0 0 0";
				m_Phase    = PH_OUTBOUND;
				Print( "[ReconquestEvac] lifting off -> safe-zone path " + m_SafeZonePath.Count().ToString() + " node(s), first leg " + GetCurrentSafeZonePathTarget().ToString() );
			}
		}
		else if ( m_Phase == PH_OUTBOUND )
		{
			m_Heli.Reconquest_SetRotors( ROTOR_FLY );
			vector safeLegTarget = GetCurrentSafeZonePathTarget();
			bool landAtSafeLegTarget = IsFinalSafeZonePathTarget();
			if ( DriveToLeg( m_LegStart, safeLegTarget, landAtSafeLegTarget ) )
			{
				if ( AdvanceSafeZonePath() )
					OnArrivedSafeZone();
			}
		}
		else if ( m_Phase == PH_DROPOFF )
		{
			RestOnGround();
			m_Timer--;
			// Only attempt the (unreliable) in-place force-extract when NO real player boarded. For a real
			// player we go straight to the swap (delete old heli + fly a fresh empty one), so firing
			// TriggerPull here would just play repeated pull-out animations for nothing.
			if ( m_Timer % 25 == 0 )
				EjectAll();
			bool aboard = AnyEvacPassengerStillAttached();
			if ( m_Timer % 25 == 0 )
				Print( "[RCE][EJECT] dropoff t=" + m_Timer.ToString() + " attached=" + aboard.ToString() + " stableEmptyTicks=" + m_EmptyDropoffTicks.ToString() + " wasAboard=" + m_PlayerWasAboard.ToString() );
			if ( m_PlayerWasAboard )
			{
				// A real player boarded. Flying away here is what carried them into the sky: the crew/command
				// "still aboard" check reads FALSE while the player is still PHYSICALLY attached, so it cannot
				// be trusted to gate a fly-away. ObjectDelete is the only guaranteed physical release, so once
				// a player has boarded the dismount is ALWAYS despawn-drop -- NEVER StartDepart. We give a short
				// beat first so EjectAll's clean get-out can try and the H-logs capture the attachment signals.
				if ( m_Timer <= DROPOFF_TICKS - DROPOFF_BEAT_TICKS )
				{
					FinishCycle();
					SwapToFreshHeliAndDepart();
				}
			}
			else if ( !aboard )
			{
				// No real player ever boarded -> safe to fly the empty heli away.
				m_EmptyDropoffTicks++;
				if ( m_EmptyDropoffTicks >= DROPOFF_EMPTY_TICKS || m_Timer <= 0 )
					StartDepart();
			}
			else if ( m_Timer <= 0 )
			{
				m_EmptyDropoffTicks = 0;
				FinishCycle();
				ForceDropAndDespawn();
			}
		}
		else if ( m_Phase == PH_DEPART )
		{
			m_Heli.Reconquest_SetRotors( ROTOR_FLY );
			DriveTo( m_LegStart, m_DepartTarget );   // climb out + cruise away (target is far -> never lands)
			vector away = m_Heli.GetPosition() - m_SafeZone;
			away[1] = 0;
			m_Timer--;
			if ( away.Length() > DEPART_DISTANCE || m_Timer <= 0 )
			{
				FinishCycle();
				CancelCurrent();
			}
		}
	}

	protected void ResetSafeZonePath(vector finalTarget, bool forceExactTouchdown)
	{
		if ( !m_SafeZonePath )
			m_SafeZonePath = new array<vector>;
		m_SafeZonePath.Clear();
		finalTarget[1] = GetGame().SurfaceY( finalTarget[0], finalTarget[2] );
		m_SafeZonePath.Insert( finalTarget );
		m_SafeZonePathIndex = 0;
		m_SafeZone = finalTarget;
		m_ForceSafeZoneTouchdown = forceExactTouchdown;
	}

	protected void LoadSafeZonePath(vector defaultTarget)
	{
		defaultTarget[1] = GetGame().SurfaceY( defaultTarget[0], defaultTarget[2] );
		ref array<vector> points = new array<vector>;

		if ( FileExist( SAFE_ZONE_PATH_FILE ) )
		{
			FileHandle fh = OpenFile( SAFE_ZONE_PATH_FILE, FileMode.READ );
			if ( fh != 0 )
			{
				string line;
				while ( FGets( fh, line ) > 0 )
				{
					line = SanitizePathLine( line );
					if ( line == "" || IsPathCommentLine( line ) )
						continue;
					vector p;
					if ( ParsePathVector( line, p ) )
						points.Insert( p );
				}
				CloseFile( fh );
			}
			else
				Print( "[ReconquestEvac] safe-zone path: could not read " + SAFE_ZONE_PATH_FILE );
		}
		else
			Print( "[ReconquestEvac] safe-zone path: no admin path file" );

		SetSafeZonePathFromPoints( points, defaultTarget );
	}

	protected void SetSafeZonePathFromPoints(array<vector> points, vector defaultTarget)
	{
		if ( !m_SafeZonePath )
			m_SafeZonePath = new array<vector>;
		m_SafeZonePath.Clear();

		defaultTarget[1] = GetGame().SurfaceY( defaultTarget[0], defaultTarget[2] );
		if ( points && points.Count() > 0 )
		{
			foreach ( vector raw : points )
			{
				vector p = raw;
				p[1] = GetGame().SurfaceY( p[0], p[2] );
				m_SafeZonePath.Insert( p );
			}
		}
		else
			m_SafeZonePath.Insert( defaultTarget );

		m_SafeZonePathIndex = 0;
		m_SafeZone = m_SafeZonePath.Get( m_SafeZonePath.Count() - 1 );
		m_ForceSafeZoneTouchdown = true;
		Print( "[ReconquestEvac] safe-zone path active: " + m_SafeZonePath.Count().ToString() + " node(s), touchdown " + m_SafeZone.ToString() + " first=" + GetCurrentSafeZonePathTarget().ToString() );
	}

	protected string SanitizePathLine(string line)
	{
		// NOTE: Enforce string.Trim() returns the trimmed COPY (proto string Trim()) -- it does NOT
		// modify in place, so the result must be assigned.
		line = line.Trim();
		while ( line.Length() > 0 )
		{
			string tail = line.Substring( line.Length() - 1, 1 );
			if ( tail == "\r" || tail == "\n" )
				line = line.Substring( 0, line.Length() - 1 );
			else
				break;
			line = line.Trim();
		}
		return line;
	}

	protected bool IsPathCommentLine(string line)
	{
		if ( line == "" )
			return true;
		return line.Substring( 0, 1 ) == "#";
	}

	protected vector GetCurrentSafeZonePathTarget()
	{
		if ( !m_SafeZonePath || m_SafeZonePath.Count() == 0 )
			return m_SafeZone;
		if ( m_SafeZonePathIndex < 0 )
			m_SafeZonePathIndex = 0;
		if ( m_SafeZonePathIndex >= m_SafeZonePath.Count() )
			m_SafeZonePathIndex = m_SafeZonePath.Count() - 1;
		return m_SafeZonePath.Get( m_SafeZonePathIndex );
	}

	protected bool IsFinalSafeZonePathTarget()
	{
		if ( !m_SafeZonePath || m_SafeZonePath.Count() <= 1 )
			return true;
		return m_SafeZonePathIndex >= m_SafeZonePath.Count() - 1;
	}

	protected bool AdvanceSafeZonePath()
	{
		if ( IsFinalSafeZonePathTarget() )
			return true;

		m_SafeZonePathIndex++;
		m_LegStart = m_Heli.GetPosition();
		m_CurSpeed = Math.Min( m_CurSpeed, 35.0 );
		m_Landing = false;
		m_DescendStarted = false;
		m_StallTicks = 0;
		m_FinalApproach = false;
		m_HeadingFrozen = false;
		m_AppliedVel[0] = m_AppliedVel[0] * 0.5;
		m_AppliedVel[2] = m_AppliedVel[2] * 0.5;
		Print( "[ReconquestEvac] safe-zone path node reached; advancing to node " + m_SafeZonePathIndex.ToString() + " / " + ( m_SafeZonePath.Count() - 1 ).ToString() + " -> " + GetCurrentSafeZonePathTarget().ToString() );
		return false;
	}

	protected bool ShouldForceExactSafeZoneTouchdown(vector target)
	{
		if ( !m_ForceSafeZoneTouchdown )
			return false;
		vector d = target - m_SafeZone;
		d[1] = 0;
		return d.Length() < 1.0;
	}

	protected void SnapHeliToTouchdown(vector target)
	{
		if ( !m_Heli )
			return;
		vector snap = target;
		snap[1] = GetGame().SurfaceY( snap[0], snap[2] ) + SKID_REST;
		m_Heli.SetPosition( snap );
		SetVelocity( m_Heli, "0 0 0" );
		dBodySetAngularVelocity( m_Heli, "0 0 0" );
		m_AppliedVel = "0 0 0";
		m_CurPitch = 0;
		m_CurRoll = 0;
		m_Heli.SetOrientation( Vector( m_CurYaw, 0, 0 ) );
		Print( "[ReconquestEvac] snapped heli to configured safe-zone touchdown " + snap.ToString() );
	}

	bool AdminSaveSafeZonePath(PlayerIdentity identity, array<vector> points, out string result)
	{
		result = "";
		if ( !IsLandingPathAdmin( identity, result ) )
			return false;
		if ( !points || points.Count() == 0 )
		{
			result = "No path points captured.";
			return false;
		}

		FileHandle fh = OpenFile( SAFE_ZONE_PATH_FILE, FileMode.WRITE );
		if ( fh == 0 )
		{
			result = "Could not write " + SAFE_ZONE_PATH_FILE;
			return false;
		}

		FPrintln( fh, "# Reconquest evac safe-zone landing path" );
		FPrintln( fh, "# One vector per line as 'x y z'. Intermediate points are fly-through nodes; last point is touchdown." );
		foreach ( vector p : points )
		{
			p[1] = GetGame().SurfaceY( p[0], p[2] );
			FPrintln( fh, FormatPathVector( p ) );
		}
		CloseFile( fh );

		result = "Saved " + points.Count().ToString() + " landing path point(s).";
		Print( "[ReconquestEvac][ADMIN] " + result + " file=" + SAFE_ZONE_PATH_FILE );
		SetSafeZonePathFromPoints( points, SAFE_ZONE_POS );
		return true;
	}

	protected string FormatPathVector(vector p)
	{
		return p[0].ToString() + " " + p[1].ToString() + " " + p[2].ToString();
	}

	protected bool ParsePathVector(string line, out vector p)
	{
		p = "0 0 0";
		if ( line == "" )
			return false;

		// Preferred on-disk format: "x y z".
		p = line.ToVector();
		if ( !( p[0] == 0.0 && p[2] == 0.0 ) )
			return true;

		// Legacy/admin save format from vector.ToString(): "<x, y, z>"
		TStringArray parts = new TStringArray;
		line.Split( ",", parts );
		if ( parts.Count() < 3 )
			return false;

		p[0] = ParsePathTokenFloat( parts.Get( 0 ) );
		p[1] = ParsePathTokenFloat( parts.Get( 1 ) );
		p[2] = ParsePathTokenFloat( parts.Get( 2 ) );
		return !( p[0] == 0.0 && p[2] == 0.0 );
	}

	protected float ParsePathTokenFloat(string token)
	{
		token = token.Trim();
		int len = token.Length();
		if ( len <= 0 )
			return 0;

		int start = 0;
		int end = len;
		if ( token.Substring( 0, 1 ) == "<" )
			start = 1;
		if ( end > start && token.Substring( end - 1, 1 ) == ">" )
			end = end - 1;
		if ( end <= start )
			return 0;

		string num = token.Substring( start, end - start );
		num = num.Trim();
		return num.ToFloat();
	}

	// ===================== SERVER SETTINGS ($profile:rce_evac_settings.txt) =====================
	// Admin-tunable values, no rebuild needed. The file is auto-created with documented defaults on the
	// first server boot (Reconquest_EnsureProfileFiles); loaded once lazily, so edits apply on restart.
	protected static float s_HullHP = 35000.0;
	protected static bool  s_SettingsLoaded;

	// Total damage the evac heli absorbs before being shot down (read per hit by ReconquestEvacHeli.c).
	float Reconquest_GetHullHP()
	{
		EnsureSettings();
		return s_HullHP;
	}

	protected void EnsureSettings()
	{
		if ( s_SettingsLoaded )
			return;
		s_SettingsLoaded = true;

		if ( !FileExist( SETTINGS_FILE ) )
			return;   // auto-created with defaults at server start; defaults already in the statics

		FileHandle fh = OpenFile( SETTINGS_FILE, FileMode.READ );
		if ( fh == 0 )
			return;
		string line;
		while ( FGets( fh, line ) > 0 )
		{
			line = SanitizePathLine( line );
			if ( line == "" || IsPathCommentLine( line ) )
				continue;
			TStringArray kv = new TStringArray;
			line.Split( "=", kv );
			if ( kv.Count() != 2 )
				continue;
			string key = kv.Get( 0 ).Trim();
			string val = kv.Get( 1 ).Trim();
			key.ToLower();
			if ( key == "hullhp" )
			{
				float hp = val.ToFloat();
				if ( hp >= 100.0 )
				{
					s_HullHP = hp;
					Print( "[ReconquestEvac] settings: HullHP = " + s_HullHP.ToString() );
				}
				else
					Print( "[ReconquestEvac] settings: ignoring HullHP '" + val + "' (must be >= 100)" );
			}
		}
		CloseFile( fh );
	}

	// Admin set of the heli hull HP from the in-game F7 editor: validates the allowlist, clamps, applies
	// LIVE (next hit already uses it) and persists to the settings file so it survives restarts.
	bool AdminSetHullHP(PlayerIdentity identity, float hp, out string result)
	{
		result = "";
		if ( !IsLandingPathAdmin( identity, result ) )
			return false;
		if ( hp < 100.0 )
		{
			result = "Hull HP must be at least 100.";
			return false;
		}
		EnsureSettings();
		s_HullHP = hp;
		WriteSettingsFile();
		result = "Heli hull HP set to " + Math.Round( hp ).ToString() + ".";
		Print( "[ReconquestEvac][ADMIN] " + result );
		return true;
	}

	protected void WriteSettingsFile()
	{
		FileHandle fh = OpenFile( SETTINGS_FILE, FileMode.WRITE );
		if ( fh == 0 )
		{
			Print( "[ReconquestEvac] WARNING: could not write " + SETTINGS_FILE );
			return;
		}
		FPrintln( fh, "# Reconquest Evac settings -- edit here (restart to apply) or live via the in-game F7 admin editor (F11)." );
		FPrintln( fh, "# HullHP: total damage the evac helicopter absorbs before it is shot down (minimum 100)." );
		FPrintln( fh, "HullHP = " + Math.Round( s_HullHP ).ToString() );
		CloseFile( fh );
	}

	// First-boot convenience (called from MissionServer.OnInit): create every profile config file with a
	// documented template, so the server owner discovers + edits them in the profile folder instead of
	// reading source. Existing files are never touched.
	void Reconquest_EnsureProfileFiles()
	{
		FileHandle fh;
		if ( !FileExist( SETTINGS_FILE ) )
		{
			EnsureSettings();       // no file -> statics keep their defaults
			WriteSettingsFile();
			Print( "[ReconquestEvac] created default " + SETTINGS_FILE );
		}
		if ( !FileExist( ADMIN_ALLOWLIST_FILE ) )
		{
			fh = OpenFile( ADMIN_ALLOWLIST_FILE, FileMode.WRITE );
			if ( fh != 0 )
			{
				FPrintln( fh, "# Reconquest Evac landing-path editor admins -- ONE Steam64 ID per line (exact match; names are not accepted)." );
				FPrintln( fh, "# Admins get the in-game F7 path editor (F8 add point / F9 save / F10 clear). Applied on the player's next connect." );
				FPrintln( fh, "# Example (remove the leading # to activate):" );
				FPrintln( fh, "# 76561198000000000" );
				CloseFile( fh );
				Print( "[ReconquestEvac] created template " + ADMIN_ALLOWLIST_FILE );
			}
		}
		if ( !FileExist( HAZARD_TOKENS_FILE ) )
		{
			fh = OpenFile( HAZARD_TOKENS_FILE, FileMode.WRITE );
			if ( fh != 0 )
			{
				FPrintln( fh, "# Extra landing-hazard name fragments, one lowercase fragment per line (min 3 chars, max 64 lines)." );
				FPrintln( fh, "# Any object whose classname contains a fragment is avoided by the LZ search, touchdown check and flight scan." );
				FPrintln( fh, "# Vehicles, vanilla base building, tents, BBP and RearmED parts are already covered built-in." );
				FPrintln( fh, "# Example (remove the leading # to activate):" );
				FPrintln( fh, "# mybasemod_" );
				CloseFile( fh );
				Print( "[ReconquestEvac] created template " + HAZARD_TOKENS_FILE );
			}
		}
	}

	// Strict allowlist membership: EXACT match on the Steam64 id (GetPlainId) or the DayZ hashed id
	// (GetId), one per line, '#' starts a comment line. Player NAMES are deliberately NOT matched (names
	// are client-chosen -> trivially spoofable) and substring matching is gone (a short entry could match
	// inside an unrelated id). Public: also used at connect time to set the client's F7-editor UI flag.
	bool IsAdminIdentity(PlayerIdentity identity)
	{
		if ( !identity )
			return false;
		if ( !FileExist( ADMIN_ALLOWLIST_FILE ) )
			return false;

		string plainId  = identity.GetPlainId();
		string hashedId = identity.GetId();
		FileHandle fh = OpenFile( ADMIN_ALLOWLIST_FILE, FileMode.READ );
		if ( fh == 0 )
			return false;

		bool allowed = false;
		string line;
		while ( FGets( fh, line ) > 0 )
		{
			line = SanitizePathLine( line );
			if ( line == "" || IsPathCommentLine( line ) )
				continue;
			if ( ( plainId != "" && line == plainId ) || ( hashedId != "" && line == hashedId ) )
			{
				allowed = true;
				break;
			}
		}
		CloseFile( fh );
		return allowed;
	}

	protected bool IsLandingPathAdmin(PlayerIdentity identity, out string result)
	{
		result = "";
		if ( !identity )
		{
			result = "No player identity on RPC.";
			return false;
		}

		if ( !FileExist( ADMIN_ALLOWLIST_FILE ) )
		{
			result = "Admin allowlist missing: create $profile:rce_evac_admins.txt with one Steam64 ID per line.";
			Print( "[ReconquestEvac][ADMIN] denied landing path save for '" + identity.GetName() + "': " + result );
			return false;
		}

		if ( IsAdminIdentity( identity ) )
			return true;

		result = "You are not in $profile:rce_evac_admins.txt.";
		Print( "[ReconquestEvac][ADMIN] denied landing path save for '" + identity.GetName() + "' id=" + identity.GetPlainId() );
		return false;
	}

	protected void StartDepart()
	{
		vector pos = m_Heli.GetPosition();
		vector mapCenter = Vector( 7680, 0, 7680 );
		vector awayDir = m_SafeZone - mapCenter;   // head back out toward the map edge, away from centre
		awayDir[1] = 0;
		awayDir.Normalize();
		m_DepartTarget = m_SafeZone + awayDir * 600.0;
		m_DepartTarget[1] = GetGame().SurfaceY( m_DepartTarget[0], m_DepartTarget[2] ) + CRUISE_ALT;
		m_LegStart = pos;
		m_CurSpeed = 0;
		m_DescendStarted = false;
		m_Landing = false;
		m_StallTicks = 0;
		m_BlockedHoldTicks = 0;
		m_Frozen = false;
		m_FreezeTicks = 0;
		if ( dBodyIsSet( m_Heli ) )
			dBodyDynamic( m_Heli, true );   // physics back ON for the empty heli to fly away
		m_PrevY    = pos[1];
		m_SmoothY  = pos[1];
		m_CurYaw   = ( m_DepartTarget - pos ).VectorToAngles()[0];
		m_AppliedVel = "0 0 0";
		m_Phase    = PH_DEPART;
		m_Timer    = DEPART_TIMEOUT;
		Print( "[ReconquestEvac] dropped off - heli departing" );
	}

	protected void FinishCycle()
	{
		Print( "[ReconquestEvac] cycle complete" );
	}

	// Map the current flight phase + timer to the net-synced HUD state (phase code + countdown seconds) the
	// client proximity GUI reads. Phase: 1=inbound, 2=board (cd=secs left to board), 3=departing (cd=secs to
	// liftoff), 4=enroute, 5=arrived, 0=none.
	protected void Reconquest_UpdateGui()
	{
		if ( !m_Heli )
			return;
		int phase = 0;
		int cd = 0;
		if ( m_Phase == PH_INBOUND )
			phase = 1;
		else if ( m_Phase == PH_WAIT )
		{
			phase = 2;
			cd = ( WAIT_TIMEOUT_TICKS - m_Timer ) / 50;
		}
		else if ( m_Phase == PH_COUNTDOWN )
		{
			phase = 3;
			cd = m_Timer / 50;
		}
		else if ( m_Phase == PH_OUTBOUND )
		{
			phase = 4;
			cd = (int) vector.Distance( m_Heli.GetPosition(), m_SafeZone );   // metres remaining to the safe zone
		}
		else if ( m_Phase == PH_DROPOFF )
			phase = 5;
		if ( cd < 0 )
			cd = 0;
		m_Heli.Reconquest_SetGui( phase, cd );
	}

	// Velocity-driven flight toward target. Returns true once settled on the ground at target.
	protected bool DriveTo(vector legStart, vector target)
	{
		return DriveToLeg( legStart, target, true );
	}

	// landAtTarget=false makes `target` a fly-through path node: it keeps cruise/descent clearance but never
	// enters vertical touchdown there. The final safe-zone node still uses the normal landing controller.
	protected bool DriveToLeg(vector legStart, vector target, bool landAtTarget)
	{
		float dt = m_Dt;   // real per-tick elapsed (computed in FlightTick), not the assumed TICK_MS

		vector pos = m_Heli.GetPosition();
		float descended = m_PrevY - pos[1];   // >0 if it dropped since last tick (ground-truth for touchdown)
		m_PrevY = pos[1];

		vector toDest = target - pos;
		toDest[1] = 0;
		float distXZ = toDest.Length();

		float groundTgt = GetGame().SurfaceY( target[0], target[2] );

		if ( !landAtTarget && distXZ <= SAFE_ZONE_WAYPOINT_EPSILON )
			return true;

		// --- vertical touchdown: over the cleared column, descend gently, no turning ---
		// Latch m_Landing once we first reach the pad so small horizontal wiggle across ARRIVE_EPSILON
		// can't bounce us back up to the approach hold (that bounce was the destination oscillation).
		if ( landAtTarget && ( m_Landing || distXZ <= ARRIVE_EPSILON ) )
		{
			bool forceExactTouchdown = ShouldForceExactSafeZoneTouchdown( target );
			if ( !forceExactTouchdown && !IsTouchdownClear( target ) )
			{
				vector saferTarget = ResolveExactLandingTarget( target );
				vector targetDelta = saferTarget - target;
				targetDelta[1] = 0;
				if ( targetDelta.Length() > 1.0 )
				{
					if ( vector.Distance( target, m_SafeZone ) < 1.0 )
						m_SafeZone = saferTarget;
					else
						m_LandingPos = saferTarget;
					m_Landing = false;
					m_FinalApproach = false;
					m_HeadingFrozen = false;
					m_AppliedVel = "0 0 0";
					SetVelocity( m_Heli, "0 0 0" );
					dBodySetAngularVelocity( m_Heli, "0 0 0" );
					m_BlockedHoldTicks = 0;   // progress: a new touchdown target was found
					Print( "[ReconquestEvac] landing column blocked during descent - redirecting touchdown to " + saferTarget.ToString() );
					return false;
				}

				// No known safe redirect. Hold position instead of descending into a tower/fence -- but NOT
				// forever: with vehicles + player-built base parts counting as hazards, a fully blocked column
				// is a realistic state (e.g. the signal was fired inside a walled base). An endless hover would
				// consume the signal AND hold m_Active so no future extraction could ever start until a server
				// restart. After ~20s of consecutive blocked holds, recall the heli and tell the signaller.
				m_AppliedVel = "0 0 0";
				SetVelocity( m_Heli, "0 0 0" );
				dBodySetAngularVelocity( m_Heli, "0 0 0" );
				m_BlockedHoldTicks++;
				if ( m_BlockedHoldTicks % 50 == 1 )
					Print( "[ReconquestEvac] landing column blocked and no safe redirect found - holding above pad (" + ( m_BlockedHoldTicks / 50 ).ToString() + "s)" );
				if ( m_BlockedHoldTicks >= BLOCKED_HOLD_MAX_TICKS )
				{
					Print( "[ReconquestEvac] landing column blocked for " + ( BLOCKED_HOLD_MAX_TICKS / 50 ).ToString() + "s - recalling helicopter" );
					Notify( m_Signaller, "EXTRACTION", "No clear landing zone -- helicopter recalled. Move to open ground and signal again." );
					CancelWaitPhase();   // plants anyone already seated at the LZ, deletes the heli, frees m_Active
				}
				return false;
			}

			m_Landing = true;
			m_BlockedHoldTicks = 0;   // progress: the touchdown column is clear
			// rotors SPINNING through the descent (visual): SetVelocity drives the descent at 50Hz and
			// overrides the rotor lift each tick so it still comes down under control, and absolute-height
			// touchdown detection (h <= LAND_THRESHOLD) means any residual lift-slowing can't fake a landing.
			m_Heli.Reconquest_SetRotors( ROTOR_FLY );

			float localGround = GetGame().SurfaceY( pos[0], pos[2] );
			float h = pos[1] - localGround;

			// touchdown: PRIMARY = height above the ground actually beneath us (absolute, so net-sync lag
			// can't fake it). BACKUP = a SUSTAINED descent stall (>=8 ticks) for helis that rest higher;
			// a single stalled tick is just a sync hiccup, not the ground, so we require a run of them.
			if ( descended > 0.015 )
			{
				m_DescendStarted = true;
				m_StallTicks = 0;
			}
			else if ( m_DescendStarted && descended < 0.004 )
				m_StallTicks++;
			else
				m_StallTicks = 0;

			if ( h <= LAND_THRESHOLD || m_StallTicks >= 8 || ( forceExactTouchdown && h <= SAFE_ZONE_SNAP_HEIGHT ) )
			{
				if ( forceExactTouchdown )
					SnapHeliToTouchdown( target );
				dBodySetAngularVelocity( m_Heli, "0 0 0" );
				return true;
			}

			// gentle tapered descent: vertical is DIRECT (landing safety unchanged), but horizontal eases to
			// zero through the same accel cap so touchdown has no velocity cliff (was an instant horizontal->0)
			float descentLimit = Math.Clamp( h * DESCENT_TAPER, DESCENT_MIN, VERT_DOWN );
			vector hCur = m_AppliedVel;
			hCur[1] = 0;
			vector hStep = ClampVecLen( hCur * -1.0, MAX_ACCEL * dt );
			m_AppliedVel[0] = m_AppliedVel[0] + hStep[0];
			m_AppliedVel[2] = m_AppliedVel[2] + hStep[2];
			// ease vy toward the tapered descent through the accel cap (smooths the ENTRY into descent; the
			// descent RATE and touchdown detection are unchanged, so landing stays safe -- no float/no plunge)
			float vyD = -descentLimit - m_AppliedVel[1];
			if ( vyD > MAX_ACCEL_UP * dt )
				vyD = MAX_ACCEL_UP * dt;
			else if ( vyD < -MAX_ACCEL * dt )
				vyD = -MAX_ACCEL * dt;
			m_AppliedVel[1] = m_AppliedVel[1] + vyD;
			SetVelocity( m_Heli, m_AppliedVel );
			dBodySetAngularVelocity( m_Heli, "0 0 0" );
			m_Heli.SetOrientation( Vector( m_CurYaw, 0, 0 ) );
			m_HasLastCmdVel = true;
			m_LastCmdVel = m_AppliedVel;
			m_HasLastWritePos = true;
			m_LastWritePos = m_Heli.GetPosition();
			return false;
		}

		// not over the pad column yet -> disarm the touchdown detector so a level-off during the
		// approach descent can't be mistaken for landing (that cut the rotors mid-air -> free-fall)
		m_DescendStarted = false;

		vector hdir = toDest;
		hdir.Normalize();

		// --- look ahead along the flight path for terrain & objects ---
		float groundHere = GetGame().SurfaceY( pos[0], pos[2] );
		float lookAhead  = Math.Min( LOOKAHEAD, distXZ );
		float objTop = -9999.0;      // ScanCanopy canopy/terrain tops (descent band) -> feeds the binary gate
		float rayClearY = -9999.0;   // raycast obstacle clear altitude -> feeds the climb + smooth speed cap
		float terrAhead;

		// TERRAIN + CANOPY scan (5x object enumeration): descent band only -- it is tree/ground-clearance work
		// for near the LZ and is wasted CPU at cruise. PERF: CRUISE_ALT*0.6 (=84m AGL) is ~5x OBSTACLE_CLEARANCE.
		if ( pos[1] - groundHere < CRUISE_ALT * 0.6 )
			terrAhead = ScanCanopy( pos, hdir, lookAhead, objTop );
		else
			terrAhead = groundHere;

		// TALL-STRUCTURE raycast -- runs at EVERY altitude, cruise included. Radio masts, antennas, cranes and
		// tall industrials reach UP TO or ABOVE the 140m cruise altitude, so the descent-band scan above cannot
		// see them on the approach. A physics ray ignores terrain (only catches objects), so it is safe to run
		// high. Lookahead scales with speed (5x -> ~425m at 85 m/s): a tall mast must be seen many seconds out
		// so the heli can both slow AND climb over it in the distance available. LEVEL ray along the heading +
		// one along the ACTUAL motion vector; when blocked, resolve the obstacle top and feed the climb logic.
		float rayLook = Math.Clamp( m_CurSpeed * 5.0, LOOKAHEAD, 425.0 );
		if ( rayLook > distXZ )
			rayLook = distXZ;
		float obstacleDist = -1.0;   // horizontal distance to the nearest raycast obstacle ahead (-1 = clear)
		float rayObsTop = -9999.0;   // TOP (world Y) of the tallest struck obstacle, straight from its bbox
		float hitD;
		float ot;
		bool pathBlocked = Reconquest_RayBlocked( pos, hdir, rayLook, hitD, ot );
		if ( pathBlocked )
		{
			obstacleDist = hitD;
			if ( ot > rayObsTop ) rayObsTop = ot;
		}
		vector motionDir = m_AppliedVel;
		if ( motionDir.Length() > 3.0 )
		{
			motionDir.Normalize();
			if ( Reconquest_RayBlocked( pos, motionDir, rayLook, hitD, ot ) )
			{
				pathBlocked = true;
				if ( obstacleDist < 0 || hitD < obstacleDist ) obstacleDist = hitD;
				if ( ot > rayObsTop ) rayObsTop = ot;
			}
		}
		// GLIDE ray toward the actual touchdown point (slopes DOWN along the descent path). The level ray
		// above flies clean over any obstacle SHORTER than the 140m cruise altitude -- a typical 40-60m radio
		// mast on the approach is invisible to it until the heli has descended to the mast's height right on
		// top of it. This ray follows the real glide slope so those are seen at full range. It ends AT the
		// ground target, but ground hits are filtered inside Reconquest_RayBlocked, so only a real structure
		// between the heli and the pad registers.
		vector glideDir = target - pos;
		if ( glideDir.Length() > 9.0 )
		{
			glideDir.Normalize();
			if ( Reconquest_RayBlocked( pos, glideDir, rayLook, hitD, ot ) )
			{
				pathBlocked = true;
				if ( obstacleDist < 0 || hitD < obstacleDist ) obstacleDist = hitD;
				if ( ot > rayObsTop ) rayObsTop = ot;
			}
		}
		if ( pathBlocked )
		{
			// The obstacle TOP comes STRAIGHT from the bounding box of the object the ray hit -- robust for
			// towers both TALLER and SHORTER than cruise, with no dependence on the hazard-name classifier.
			// Only if the ray hit unattributed static geometry (no object) do we fall back to an upward probe.
			if ( rayObsTop > -9000.0 )
			{
				rayClearY = rayObsTop;
			}
			else
			{
				float clearY = pos[1];
				for ( int rp = 0; rp < 16; rp++ )   // up to +192m: clears the tallest Chernarus masts
				{
					clearY = clearY + 12.0;
					vector lvl = pos;
					lvl[1] = clearY;
					if ( !Reconquest_RayBlocked( lvl, hdir, rayLook, hitD, ot ) )
						break;
				}
				rayClearY = clearY;
			}
		}

		// --- desired altitude: glide arc, never below bare-terrain clearance ---
		vector fromStart = pos - legStart;
		fromStart[1] = 0;
		float climbF = Math.Clamp( fromStart.Length() / GLIDE_DISTANCE, 0.0, 1.0 );
		float descF  = 1.0;
		if ( landAtTarget )
			descF = Math.Clamp( distXZ / GLIDE_DISTANCE, 0.0, 1.0 );
		float f = Math.Min( climbF, descF );

		float desiredY = groundHere + CRUISE_ALT * f;
		desiredY = Math.Max( desiredY, terrAhead + TERRAIN_CLEARANCE );

		// --- hazard floor + climb-before-advance gate ---
		// Applied ONLY while outside the cleared pad column: once the heli is over its own clearing
		// (distXZ <= LZ_CLEAR_RADIUS) we must let it descend even though tall trees ring the clearing.
		float gate = 1.0;
		if ( distXZ > LZ_CLEAR_RADIUS )
		{
			float hazardTop = terrAhead;
			float margin    = TERRAIN_CLEARANCE;
			if ( objTop > -9000.0 )
			{
				hazardTop = Math.Max( hazardTop, objTop );
				margin    = OBSTACLE_CLEARANCE;
			}
			desiredY = Math.Max( desiredY, hazardTop + margin );
			gate = Math.Clamp( (pos[1] - hazardTop) / margin, 0.0, 1.0 );
		}
		else if ( distXZ > ARRIVE_EPSILON && objTop > -9000.0 )
		{
			// Narrow safe-zone pads may have towers/fences/buildings inside the old "cleared pad" radius.
			// Keep crossing above those hazards; only the final vertical descent is allowed once the footprint
			// itself is clear and within the 10m touchdown constraint.
			desiredY = Math.Max( desiredY, objTop + OBSTACLE_CLEARANCE );
			gate = Math.Clamp( (pos[1] - objTop) / OBSTACLE_CLEARANCE, 0.0, 1.0 );
		}

		// RAYCAST obstacle (tall mast / tower / crane on the path): raise the CLIMB target so the heli climbs
		// over it. It deliberately does NOT touch the binary gate -- the distance-aware climb-slope speed cap
		// (in the horizontal-speed section below) slows the heli smoothly instead of the gate's slam-to-zero,
		// which coasted the heli into the tower.
		if ( rayClearY > -9000.0 )
			desiredY = Math.Max( desiredY, rayClearY + OBSTACLE_CLEARANCE );

		// --- level final approach: hold above the LZ tree-ring and crab to directly overhead, so the
		//     heli stops descending early (no last-second plunge) before the vertical touchdown ---
		// hysteresis: COMMIT to final approach at <=18m, RELEASE only past 24m -> distXZ wiggling around the
		// boundary while crabbing in can't flap the mode (each flap flipped moveDir/desiredY -> micro-corrections).
		if ( landAtTarget )
		{
			if ( distXZ <= LZ_APPROACH_DIST )
				m_FinalApproach = true;
			else if ( distXZ > LZ_APPROACH_EXIT )
				m_FinalApproach = false;
		}
		else
			m_FinalApproach = false;
		bool finalApproach = m_FinalApproach;
		if ( finalApproach )
		{
			float ringTop = CanopyTopAt( target[0], target[2], PAD_RING_RADIUS );
			float holdY = groundTgt + 2.5;
			if ( ringTop > -9000.0 )
				holdY = Math.Max( holdY, ringTop + 2.0 );
			desiredY = holdY;
			gate = 1.0;   // over the clearing: don't throttle the glide-in
		}

		float altAbove   = pos[1] - groundHere;
		float liftFactor = Math.Clamp( altAbove / LIFTOFF_HEIGHT, 0.0, 1.0 );
		float speedMul   = Math.Min( gate, liftFactor );

		// --- eased heading: rotate toward travel dir at TURN_RATE, bank into the turn. Frozen close to
		//     the pad (bearing to a near-zero-distance target is noisy and would spin the heli). ---
		// hysteresis: FREEZE heading below 9m (bearing to a near point is noisy), RESUME turning only past 13m ->
		// no freeze/unfreeze flap (which snapped yaw updates on/off) in the band right around the threshold.
		if ( landAtTarget )
		{
			if ( distXZ < HEADING_FREEZE_DIST )
				m_HeadingFrozen = true;
			else if ( distXZ >= HEADING_UNFREEZE_DIST )
				m_HeadingFrozen = false;
		}
		else
			m_HeadingFrozen = false;

		float yawErr = 0.0;
		float appliedYawStep = 0.0;   // C: the actual per-tick yaw change applied -> fed as angular velocity below
		if ( !m_HeadingFrozen )
		{
			float desiredYaw = hdir.VectorToAngles()[0];
			yawErr = NormalizeYaw( desiredYaw - m_CurYaw );
			float yawStep = Math.Clamp( yawErr, -TURN_RATE * dt, TURN_RATE * dt );
			m_CurYaw = NormalizeYaw( m_CurYaw + yawStep );
			appliedYawStep = yawStep;
		}
		float bank = Math.Clamp( yawErr / 45.0, -1.0, 1.0 ) * BANK_MAX;
		float turnScale = Math.Clamp( Math.Cos( yawErr * Math.DEG2RAD ), TURN_MIN_SCALE, 1.0 );

		// --- horizontal speed: accelerate, ease to a stop near the target ---
		float decelSpeed = Math.Sqrt( 2.0 * DECEL * distXZ );
		float tgtSpeed   = Math.Min( MAX_SPEED, decelSpeed );
		// glide-slope cap: don't outrun the descent. When we're high above the pad, limit forward speed so
		// shedding (pos.y - padAlt) stays within VERT_DOWN over the remaining distance -> a smooth sloped
		// approach INTO the LZ, instead of arriving overhead still high and plunging straight down (that
		// vertical drop happened heading to a low LZ ringed by higher terrain).
		float altToLose = pos[1] - ( groundTgt + 4.0 );
		if ( landAtTarget && altToLose > VERT_DOWN && distXZ > ARRIVE_EPSILON )
		{
			float glideCap = Math.Max( VERT_DOWN * distXZ / altToLose, 20.0 );
			if ( glideCap < tgtSpeed )
				tgtSpeed = glideCap;
		}
		m_CurSpeed = Math.Min( tgtSpeed, m_CurSpeed + ACCEL * dt );

		// fly where the nose points (curved arcs) en route; on final approach crab straight at the pad
		// (homes to dead-centre even with the heading frozen) while staying at the hold altitude
		vector faceDir = Vector( Math.Sin( m_CurYaw * Math.DEG2RAD ), 0, Math.Cos( m_CurYaw * Math.DEG2RAD ) );
		vector moveDir = faceDir;
		if ( finalApproach )
			moveDir = hdir;
		float hSpeed = m_CurSpeed * speedMul * turnScale;
		// CLIMB-SLOPE CAP for a raycast obstacle ahead: limit forward speed so that -- climbing at the
		// clearance rate -- the heli reaches the required altitude BEFORE it reaches the obstacle. As it
		// closes, obstacleDist shrinks and the cap tightens toward a near-hover, GUARANTEEING it clears even
		// a tall mast detected late. The 0.7 factor covers the vertical accel ramp. obstacleBrake then unlocks
		// harder horizontal braking below so the heli can actually shed cruise speed in time.
		bool obstacleBrake = false;
		if ( obstacleDist > 1.0 && rayClearY > -9000.0 )
		{
			float altToGain = rayClearY + OBSTACLE_CLEARANCE - pos[1];
			if ( altToGain > 1.0 )
			{
				float clearSpeed = ( CLEARANCE_CLIMB * 0.7 ) * obstacleDist / altToGain;
				if ( clearSpeed < hSpeed )
				{
					hSpeed = Math.Max( clearSpeed, 3.0 );   // floor: creep forward, never a dead stop
					obstacleBrake = true;
				}
			}
		}
		// altitude TARGET low-pass (shapes the target, NOT the applied velocity): rise quickly to clear
		// hazards, settle downward gradually -> no bob as the look-ahead catches/loses canopy
		if ( desiredY > m_SmoothY )
			m_SmoothY = m_SmoothY + ( desiredY - m_SmoothY ) * 0.30;
		else
			m_SmoothY = m_SmoothY + ( desiredY - m_SmoothY ) * 0.14;

		// --- build the TARGET velocity vector (everything above shaped it: glide, gate, glide-cap, heading) ---
		// While actively clearing a detected obstacle, raise the vertical climb ceiling so the climb-out is
		// fast enough that the heli barely has to slow.
		float vyUpLimit = VERT_UP;
		if ( rayClearY > -9000.0 )
			vyUpLimit = CLEARANCE_CLIMB;
		float vyTarget = Math.Clamp( (m_SmoothY - pos[1]) * VERT_GAIN, -VERT_DOWN, vyUpLimit );
		vector vTarget = moveDir * hSpeed;
		vTarget[1] = vyTarget;

		// --- ONE acceleration-limited slew of the applied velocity VECTOR: bounds per-tick jerk in every axis,
		//     so the moveDir direction snap, the speed step, and the vy snap all smooth out by construction.
		//     Vertical is split out so a CLEARANCE CLIMB is never throttled by the horizontal cap. ---
		vector delta = vTarget - m_AppliedVel;
		float dvUp = delta[1];
		delta[1] = 0;
		// harder horizontal braking while clearing an obstacle so the heli sheds cruise speed in time instead
		// of coasting into the mast (the normal MAX_ACCEL=14 gave a ~250m coast from 85 m/s).
		float hAccel = MAX_ACCEL;
		if ( obstacleBrake )
			hAccel = 30.0;
		vector dHoriz = ClampVecLen( delta, hAccel * dt );
		float vCapUp   = MAX_ACCEL_UP * dt;
		float vCapDown = MAX_ACCEL * dt;
		float dvUpClamped = dvUp;
		if ( dvUp > vCapUp )
			dvUpClamped = vCapUp;
		else if ( dvUp < -vCapDown )
			dvUpClamped = -vCapDown;
		m_AppliedVel[0] = m_AppliedVel[0] + dHoriz[0];
		m_AppliedVel[1] = m_AppliedVel[1] + dvUpClamped;
		m_AppliedVel[2] = m_AppliedVel[2] + dHoriz[2];

		// tilt the airframe into its ACTUAL motion (from the applied velocity): nose DOWN when moving
		// forward, bank into lateral/crab motion + turns, so it never strafes dead-level
		vector rightDir = Vector( faceDir[2], 0, -faceDir[0] );
		float fwdSpd = m_AppliedVel[0] * faceDir[0] + m_AppliedVel[2] * faceDir[2];
		float latSpd = m_AppliedVel[0] * rightDir[0] + m_AppliedVel[2] * rightDir[2];
		float tgtPitch = -Math.Clamp( fwdSpd / MAX_SPEED, -1.0, 1.0 ) * PITCH_MAX;
		float tgtRoll  = bank + Math.Clamp( latSpd / MAX_SPEED, -1.0, 1.0 ) * ROLL_LAT;
		// RATE-limit pitch/roll (deg/s) the same way yaw + velocity are limited, instead of exponential *0.08
		// easing -- the old ease front-loaded a ~1deg step on tick 1 which read as a discrete "tick".
		float pStep = Math.Clamp( tgtPitch - m_CurPitch, -PITCH_RATE * dt, PITCH_RATE * dt );
		m_CurPitch = m_CurPitch + pStep;
		float rStep = Math.Clamp( tgtRoll - m_CurRoll, -ROLL_RATE * dt, ROLL_RATE * dt );
		m_CurRoll  = m_CurRoll + rStep;

		SetVelocity( m_Heli, m_AppliedVel );
		// Full rate-driven attitude: feed a real WORLD-SPACE angular velocity for ALL THREE axes, not just
		// yaw, so clients interpolate THROUGH the yaw turn AND the pitch/roll tilt instead of snapping between
		// orientation snapshots (without a fed angular velocity, the fast roll-in/out on turn entry/exit snaps
		// on the client). Angular velocities ADD as vectors: yaw about
		// world-up, pitch about the body RIGHT axis, roll about the body FORWARD axis (faceDir/rightDir already
		// built from m_CurYaw; the small tilt of those axes themselves is ignored -- fine for <=16 deg). dBody
		// angular velocity is WORLD-space (engine vehicle-camera reads it as "rotDiffWS", and its camera-lag
		// smoothing feeds off it). SetOrientation STILL re-pins the exact attitude each tick: with REAL dt the fed
		// rate matches the per-tick Euler step, so the re-pin is a CONSISTENCY correction, not a fight, and the
		// body can't drift or over-rotate. Per-axis sign consts -- if ONE axis wobbles in-game, flip THAT sign.
		float yawRateRad   = appliedYawStep * Math.DEG2RAD / dt;
		float pitchRateRad = pStep * Math.DEG2RAD / dt;
		float rollRateRad  = rStep * Math.DEG2RAD / dt;
		vector angVel = Vector( 0, YAW_AVEL_SIGN * yawRateRad, 0 );
		angVel = angVel + rightDir * ( PITCH_AVEL_SIGN * pitchRateRad );
		angVel = angVel + faceDir  * ( ROLL_AVEL_SIGN  * rollRateRad );
		dBodySetAngularVelocity( m_Heli, angVel );
		m_Heli.SetOrientation( Vector( m_CurYaw, m_CurPitch, m_CurRoll ) );
		m_HasLastCmdVel = true;
		m_LastCmdVel = m_AppliedVel;
		m_HasLastWritePos = true;
		m_LastWritePos = m_Heli.GetPosition();

		return false;
	}

	// TRUE if world geometry (a roof, bridge, industrial structure...) hangs over the touchdown column at p.
	// SurfaceY is TERRAIN-only -- it does not know buildings exist -- so "lands on true ground" must be
	// verified with a downward ray. The object scans alone miss mega-buildings whose center is far from the
	// sample point (e.g. a grain elevator / large industrial). Players standing on the pad (typically the
	// signaller) are not obstructions.
	protected bool Reconquest_ColumnBlocked(vector p)
	{
		float g = GetGame().SurfaceY( p[0], p[2] );
		vector top = Vector( p[0], g + 60.0, p[2] );
		vector bot = Vector( p[0], g + 0.5, p[2] );
		vector contactPos;
		vector contactDir;
		int contactComponent;
		set<Object> results = new set<Object>;
		if ( !DayZPhysics.RaycastRV( top, bot, contactPos, contactDir, contactComponent, results, null, m_Heli, false, false, ObjIntersectGeom ) )
			return false;
		if ( results.Count() == 0 )
			return true;   // hit unattributed static geometry -> treat as blocked
		for ( int i = 0; i < results.Count(); i++ )
		{
			Object o = results.Get( i );
			if ( !o )
				continue;
			if ( o == m_Heli || o == m_Pilot || PlayerBase.Cast( o ) )
				continue;
			return true;
		}
		return false;
	}

	// SPHERE-cast (8m radius -> a 16m corridor, the UH-1H's full rotor diameter plus margin) from 8m ahead
	// of the airframe along dir for dist metres. The width matters: a thin ray slips past building edges.
	// Filtered so it can only see REAL obstacles:
	//  - hits within 2m of the terrain surface are ignored (the landing ground / a rising slope the terrain
	//    scan already handles / a standing player's legs)
	//  - the heli itself, the cosmetic pilot and any player (passengers sit inside the sphere's start!)
	//    are never obstacles
	// out hitDist = horizontal distance from `from` to the blocking obstacle (defaults to `dist` when clear).
	// out obsTop  = world-Y TOP of the tallest struck object (from its bounding box), so the caller can hold
	//               above / climb over it; -9999 when the ray hit unattributed geometry (top unknown).
	protected bool Reconquest_RayBlocked(vector from, vector dir, float dist, out float hitDist, out float obsTop)
	{
		hitDist = dist;
		obsTop = -9999.0;
		if ( dist <= 9.0 )
			return false;
		vector s = from + dir * 8.0;
		vector e = from + dir * dist;
		vector contactPos;
		vector contactDir;
		int contactComponent;
		set<Object> results = new set<Object>;
		if ( !DayZPhysics.RaycastRV( s, e, contactPos, contactDir, contactComponent, results, null, m_Heli, false, false, ObjIntersectGeom, 8.0 ) )
			return false;
		float hitGround = GetGame().SurfaceY( contactPos[0], contactPos[2] );
		if ( contactPos[1] - hitGround <= 2.0 )
			return false;   // ground / slope / ground-level clutter -- not an aerial obstacle
		bool isObstacle = false;
		for ( int i = 0; i < results.Count(); i++ )
		{
			Object o = results.Get( i );
			if ( !o )
				continue;
			if ( o == m_Heli || o == m_Pilot || PlayerBase.Cast( o ) )
				continue;
			isObstacle = true;
			vector mm[2];
			o.GetCollisionBox( mm );
			float t = o.GetPosition()[1] + mm[1][1];
			if ( t > obsTop )
				obsTop = t;
		}
		if ( results.Count() == 0 )
			isObstacle = true;   // unattributed static geometry -> obstacle, top unknown (caller falls back)
		if ( !isObstacle )
			return false;
		vector hzd = contactPos - from;
		hzd[1] = 0;
		hitDist = hzd.Length();
		return true;
	}

	// Top (world Y) of the tallest tree/rock/building/fence hazard whose footprint is within `radius` of (x,z).
	// Uses object enumeration + bounding boxes, NOT a single ray, so thin trunks can't be missed.
	protected float CanopyTopAt(float x, float z, float radius)
	{
		float top = -9999.0;
		if ( !m_ScanObjs )
			m_ScanObjs = new array<Object>;
		if ( !m_ScanCargos )
			m_ScanCargos = new array<CargoBase>;
		m_ScanObjs.Clear();
		m_ScanCargos.Clear();
		GetGame().GetObjectsAtPosition3D( Vector( x, GetGame().SurfaceY( x, z ), z ), radius, m_ScanObjs, m_ScanCargos );
		foreach ( Object o : m_ScanObjs )
		{
			if ( Reconquest_IsLandingHazard( o ) )
			{
				vector mm[2];
				o.GetCollisionBox( mm );
				float otop = o.GetPosition()[1] + mm[1][1];
				if ( otop > top )
					top = otop;
			}
		}
		return top;
	}

	// Highest terrain top along the forward path; reports the highest *object* top (tree/building/fence)
	// separately in objTop (-9999 if none) so bare terrain near a cleared pad doesn't block descent.
	protected float ScanCanopy(vector pos, vector hdir, float maxLook, out float objTop)
	{
		objTop = -9999.0;
		float terrTop = GetGame().SurfaceY( pos[0], pos[2] );
		int steps = 4;
		for ( int i = 0; i <= steps; i++ )
		{
			float d  = (maxLook / steps) * i;
			float px = pos[0] + hdir[0] * d;
			float pz = pos[2] + hdir[2] * d;
			float g  = GetGame().SurfaceY( px, pz );
			if ( g > terrTop )
				terrTop = g;
			float ct = CanopyTopAt( px, pz, 14.0 );
			if ( ct > objTop )
				objTop = ct;
		}
		return terrTop;
	}

	protected float NormalizeYaw(float a)
	{
		while ( a > 180.0 )
			a -= 360.0;
		while ( a < -180.0 )
			a += 360.0;
		return a;
	}

	// Clamp a vector's magnitude to maxLen (scalar division only -- vector/float is illegal in Enforce).
	protected vector ClampVecLen(vector v, float maxLen)
	{
		float len = v.Length();
		if ( len <= maxLen )
			return v;
		if ( len <= 0.0001 )
			return "0 0 0";
		float s = maxLen / len;
		vector r = v;
		r[0] = r[0] * s;
		r[1] = r[1] * s;
		r[2] = r[2] * s;
		return r;
	}

	// Player-facing notification: a TARGETED Expansion GUI popup for exactly this player -- not a chat-style
	// MessageStatus line (players read those as global chat). Used ONLY for exceptional events the proximity
	// HUD cannot show (rejection, dispatch failure, recall, cancellation); the normal phase flow
	// (inbound/board/departing/enroute/arrived) is the HUD's job alone, otherwise every phase shows twice.
	protected void Notify( PlayerBase p, string title, string msg )
	{
		if ( !p || msg == "" )
			return;
		PlayerIdentity ident = p.GetIdentity();
		if ( !ident )
			return;
		ExpansionNotification( title, msg, "Helicopter", COLOR_EXPANSION_NOTIFICATION_ORANGE, 5 ).Create( ident );
	}

	// Targeted RPC telling a client "you are (no longer) the signaller of the active extraction". The client
	// HUD uses it to track the extraction at ANY range (network-bubble limited) instead of the 350m proximity
	// scan -- the caller must never lose the readout just by standing far from the heli.
	protected void SendSignallerFlag( PlayerBase p, bool on )
	{
		if ( !p || !p.GetIdentity() )
			return;
		ScriptRPC rpc = new ScriptRPC();
		rpc.Write( on );
		rpc.Send( p, ReconquestEvacRPC.SIGNALLER_FLAG, true, p.GetIdentity() );
	}

	// Server-wide announcement as an Expansion notification POPUP (identity NULL = broadcast to every
	// client), not a chat line. "Helicopter" icon + orange styling match Expansion's own heli notifications.
	protected void NotifyAll( string msg )
	{
		ExpansionNotification( "EXTRACTION", msg, "Helicopter", COLOR_EXPANSION_NOTIFICATION_ORANGE, 7 ).Create();
	}

	// Nearest named map location (CfgWorlds Names via Expansion Core's locator) for flavor text like the
	// shot-down announcement. Falls back to rounded map coordinates on worlds with no named locations.
	protected string NearestLocationName( vector pos )
	{
		if ( !m_WorldLocations )
			m_WorldLocations = ExpansionLocation.GetWorldLocations();

		string best = "";
		float bestD = 999999.0;
		vector pp = pos;
		pp[1] = 0;
		if ( m_WorldLocations )
		{
			foreach ( ExpansionLocation loc : m_WorldLocations )
			{
				if ( !loc || loc.Name == "" )
					continue;
				vector lp = loc.Position;
				lp[1] = 0;
				float d = vector.Distance( lp, pp );
				if ( d < bestD )
				{
					bestD = d;
					best = loc.Name;
				}
			}
		}
		if ( best == "" )
			return "grid " + Math.Round( pos[0] ).ToString() + " / " + Math.Round( pos[2] ).ToString();
		return best;
	}

	// The evac heli is destructible by design (counterplay: an extraction can be contested). The moment the
	// airframe dies we must STOP commanding it -- otherwise the autopilot keeps SetVelocity-ing a burning
	// wreck along its route -- release the extraction slot for the next signal, and announce the kill
	// server-wide. The wreck stays in the world; whoever was aboard is left to the engine's own crash
	// handling (a teleport rescue here would make getting shot down consequence-free).
	protected void OnHeliShotDown()
	{
		// Expansion teleports a destroyed heli to "0 0 0" (killfeed grace period) and deletes the entity
		// itself ~5s later, so the CURRENT position is unusable -- use last tick's tracked position.
		vector crashPos = m_RcePrevSyncPos;
		if ( !m_RceHasPrevSyncPos )
			crashPos = m_Heli.GetPosition();
		string locName = NearestLocationName( crashPos );
		Print( "[ReconquestEvac] evac helicopter DESTROYED at " + crashPos.ToString() + " (near " + locName + ")" );
		NotifyAll( "An extraction helicopter has been shot down near " + locName + "!" );

		SendSignallerFlag( m_Signaller, false );
		GetGame().GetCallQueue( CALL_CATEGORY_SYSTEM ).Remove( FlightTick );
		DespawnPilot();
		// clear the flags clients key off: the smoother must NOT keep dead-reckoning a falling wreck, and
		// the HUD must drop the extraction readout for everyone nearby
		m_Heli.Reconquest_SetGui( 0, 0 );
		m_Heli.Reconquest_SetAutopilot( false );
		m_Heli = null;   // Expansion handles the wreck itself (explosion + delayed entity delete)
		m_Active = false;
		m_EvacPassengers = null;
		m_EmptyDropoffTicks = 0;
		m_Signaller = null;
		m_SafeZonePathIndex = 0;
		m_ForceSafeZoneTouchdown = false;
	}

	// Sit on the ground exactly like a freshly spawned heli: rotors OFF so there's NO lift (the old
	// idle-lift-vs-downward-push tug-of-war was the wobble), then let gravity + ground collision hold it
	// at its own natural skid height. We only kill horizontal drift + any upward velocity (no float);
	// the vertical is left to physics so it settles naturally instead of being pinned in the air.
	protected void RestOnGround()
	{
		// GROUND PHASE: the heli MUST stay a DYNAMIC body (kinematic = can't board it + floated at a guessed
		// height). So: settle it onto its skids with gravity (gravity finds the true rest height -> no
		// float), then STOP touching it -- an undisturbed dynamic heli is what the get-in/get-out actions
		// need (per-tick SetVelocity/SetOrientation was likely cancelling the get-in). Idle rotor lift <
		// weight so gravity holds it; the FlightTick crash-guard backstops any sink.
		if ( m_Frozen )
		{
			// TRUE stillness: spin the parked rotor DOWN to zero. m_RotorSpeed is ONE synced var = rotor visual
			// AND lift/torque, so a spinning parked rotor keeps applying yaw torque + a lift imbalance every
			// physics step (AFTER our script velocity-zeroing) -> the heli slowly rotates + creeps (headless
			// telemetry showed ~1.7 deg/s yaw drift + position creep to the 15cm pin at rotor speed 0.5).
			// Killing the rotor force is the only way to be dead-still while staying a DYNAMIC (boardable) body.
			// Velocity-zero + the drift-pin are belt-and-suspenders; none of this moves the door-reach points.
			m_GroundRotor = m_GroundRotor + ( 0.0 - m_GroundRotor ) * 0.08;
			m_Heli.Reconquest_SetRotors( m_GroundRotor );
			SetVelocity( m_Heli, "0 0 0" );
			dBodySetAngularVelocity( m_Heli, "0 0 0" );
			// belt-and-suspenders: if an external push (collision/sync) ever shifts the parked heli, snap it
			// back to the captured rest pose. >15cm ONLY -- velocity-zero already prevents drift so this almost
			// never fires, a boarding nudge can't reach 15cm, and snapping to m_FrozenPos restores the EXACT
			// boarding-clean transform, so it cannot break get-in.
			vector drift = m_Heli.GetPosition() - m_FrozenPos;
			float dlen = drift.Length();
			if ( dlen > 0.15 )
				m_Heli.SetPosition( m_FrozenPos );
			return;
		}

		// SETTLE to the heli's NATURAL rest pose: rotor OFF (no lift to fight gravity), kill HORIZONTAL drift +
		// any upward bounce, but let the VERTICAL drop onto the skids AND the ORIENTATION settle to the ground
		// slope on their own. NO forced-flat and NO per-tick angular-zero here -- those FOUGHT the collision
		// settle and made the heli visibly tilt/slide for ~8s after freezing. Freeze only once it's genuinely at
		// rest, so the captured pose IS the natural pose -> no post-freeze transient, and an undisturbed resting
		// pose (within the ~1m door-reach tolerance even on a slight slope) is what boarding needs.
		m_GroundRotor = m_GroundRotor + ( 0.0 - m_GroundRotor ) * 0.15;
		m_Heli.Reconquest_SetRotors( m_GroundRotor );
		vector pos = m_Heli.GetPosition();
		float h = pos[1] - GetGame().SurfaceY( pos[0], pos[2] );
		vector v = GetVelocity( m_Heli );
		float vDown = v[1];
		v[0] = 0;
		v[2] = 0;
		if ( v[1] > 0 )
			v[1] = 0;            // kill rotor-lift upward bounce
		else if ( v[1] < -3.0 )
			v[1] = -3.0;         // cap descent so it can't slam through (crash-guard backstops too)
		SetVelocity( m_Heli, v );

		// true-rest detection: near the ground, no longer descending, AND the airframe has stopped tilting onto
		// the slope (per-tick orientation change ~0) -> only then freeze, so we capture the FINAL settled pose
		// and there's no post-freeze tilt drift.
		vector curOri = m_Heli.GetOrientation();
		float oriDelta = Math.AbsFloat( curOri[1] - m_PrevSettleOri[1] ) + Math.AbsFloat( curOri[2] - m_PrevSettleOri[2] );
		m_PrevSettleOri = curOri;
		if ( h < 1.5 && Math.AbsFloat( vDown ) < 0.3 && oriDelta < 0.05 )
			m_FreezeTicks++;
		else
			m_FreezeTicks = 0;
		if ( m_FreezeTicks > 20 )
		{
			m_FrozenPos = m_Heli.GetPosition();
			m_Frozen = true;
			Print( "[ReconquestEvac] heli settled at natural rest h=" + h.ToString() + " - clear to board" );
		}
	}

	protected void OnArrivedSafeZone()
	{
		Print( "[ReconquestEvac] arrived at safe zone - dropping off" );
		// no chat line: the HUD's "Arrived: disembark" phase covers this
		m_Heli.Reconquest_SetRotors( 0.0 );
		CollectEvacPassengers();
		EjectAll();
		m_Phase = PH_DROPOFF;
		m_Timer = DROPOFF_TICKS;
		m_EmptyDropoffTicks = 0;
	}

	protected bool AnyPlayerAboard()
	{
		for ( int i = 0; i < m_Heli.CrewSize(); i++ )
		{
			Human h = m_Heli.CrewMember( i );
			if ( h && h.IsAlive() && h != m_Pilot && PlayerBase.Cast( h ) )
				return true;
		}
		return false;
	}

	// Departure authorization: only the SIGNALLER can trigger the countdown -- a random player hopping in
	// can't launch (or steal) someone else's evac. Friends who board alongside still get carried; everyone
	// aboard is delivered at the safe zone. STRICT m_Signaller match: if the signaller ref is gone
	// (disconnect deletes the entity -> ref auto-nulls) this must return FALSE so the wait-phase cancel
	// fires -- a "!m_Signaller -> anyone counts" fallback would hand the evac to whoever is squatting aboard.
	protected bool AnyAuthorizedPlayerAboard()
	{
		for ( int i = 0; i < m_Heli.CrewSize(); i++ )
		{
			Human h = m_Heli.CrewMember( i );
			PlayerBase pb = PlayerBase.Cast( h );
			if ( !pb || !pb.IsAlive() || pb == m_Pilot )
				continue;
			if ( m_Signaller && pb == m_Signaller )
				return true;
		}
		return false;
	}

	protected void EjectAll()
	{
		int ejected = 0;
		for ( int i = 0; i < m_Heli.CrewSize(); i++ )
		{
			Human h = m_Heli.CrewMember( i );
			if ( !h || h == m_Pilot || !PlayerBase.Cast( h ) )
				continue;
			// THE engine's own force-extract: TriggerPullPlayerOutOfVehicle() does CrewGetOut + the actual
			// pull-out command + re-possess, server-side and NON-fatal (it re-enables sim / resets the death
			// cooldown -- the "death" is only the yank animation). CrewGetOut()/GetOutVehicle() alone never
			// pulled the player out, which is why they stayed stuck until despawn.
			DayZPlayerImplement dp = DayZPlayerImplement.Cast( h );
			if ( !dp )
				continue;
			HumanCommandVehicle hcv = dp.GetCommand_Vehicle();
			if ( hcv && ( hcv.IsGettingOut() || hcv.IsGettingIn() ) )
				continue;   // a get-out is already running for this player -- let it finish, don't re-yank it
			if ( hcv )
				hcv.GetOutVehicle();
			dp.TriggerPullPlayerOutOfVehicle();
			ejected++;
			Print( "[RCE][EJECT] TriggerPullPlayerOutOfVehicle on seat " + i.ToString() + " (crewIdx=" + m_Heli.CrewMemberIndex( dp ).ToString() + ")" );
		}
	}

	protected void CollectEvacPassengers()
	{
		m_EvacPassengers = new array<PlayerBase>;
		if ( !m_Heli )
			return;

		for ( int i = 0; i < m_Heli.CrewSize(); i++ )
		{
			Human h = m_Heli.CrewMember( i );
			PlayerBase p = PlayerBase.Cast( h );
			if ( p && p.IsAlive() && p != m_Pilot )
				m_EvacPassengers.Insert( p );
		}

		Print( "[RCE][EJECT] captured " + m_EvacPassengers.Count().ToString() + " evac passenger(s) for dismount verification" );
	}

	protected bool AnyEvacPassengerStillAttached()
	{
		if ( !m_EvacPassengers || m_EvacPassengers.Count() == 0 )
			return AnyPlayerAboard();

		foreach ( PlayerBase p : m_EvacPassengers )
		{
			if ( IsPassengerAttachedToEvac( p ) )
				return true;
		}

		return false;
	}

	protected bool IsPassengerAttachedToEvac(PlayerBase p)
	{
		if ( !p || !m_Heli )
			return false;

		if ( m_Heli.CrewMemberIndex( p ) >= 0 )
			return true;

		HumanCommandVehicle vc = p.GetCommand_Vehicle();
		if ( !vc )
			return false;

		Transport tr = vc.GetTransport();
		return tr && tr == m_Heli;
	}

	// Spawn a PLAIN VANILLA survivor (no eAI brain, no AddChild) and pose it in the pilot seat. It is deliberately
	// NOT registered as vehicle crew: a real seat-0 occupant makes Expansion/DayZ treat the heli as driven and
	// blocks many interactions. StartCommand_Vehicle gives the cockpit pose; the cosmetic flag + IsIgnoredObject
	// keep the body from blocking door volumes.
	protected void SpawnPilotVanilla(vector nearPos)
	{
		if ( !m_Heli )
			return;
		string cls = "SurvivorM_Mirek";   // plain vanilla survivor (NOT an eAI_* class)
		vector groundPos = Vector( nearPos[0], GetGame().SurfaceY( nearPos[0], nearPos[2] ), nearPos[2] );
		Object o = GetGame().CreateObject( cls, groundPos );
		PlayerBase surv = PlayerBase.Cast( o );
		if ( !surv )
		{
			Print( "[ReconquestEvac] cosmetic pilot spawn FAILED for '" + cls + "'" );
			if ( o )
				GetGame().ObjectDelete( o );
			return;
		}
		// Pilot protection stack: god mode (no kill -> no lootable corpse, no knock-out) + the cosmetic
		// flag, which hides his inventory from every vicinity UI (IsInventoryVisible) AND blocks raw
		// server-side item release (CanReleaseAttachment/CanReleaseCargo) -- see ReconquestEvacPlayer.c.
		surv.SetAllowDamage( false );
		surv.Reconquest_SetCosmeticPilot( true );
		EquipPilot( surv );
		m_Pilot = surv;
		MaintainCosmeticPilot();                      // start the seated-pose command immediately
		bool cmdOn = ( surv.GetCommand_Vehicle() != null );
		bool crewOcc = ( m_Heli.CrewMember( PILOT_SEAT_INDEX ) == surv );
		Print( "[ReconquestEvac] cosmetic pilot spawned (" + cls + ") -> visual seat " + PILOT_SEAT_INDEX.ToString() + " crewSlotOccupied=" + crewOcc.ToString() + " cmd=" + cmdOn.ToString() );
	}

	// Ensure the cosmetic pilot is running its seated vehicle command for THIS heli. Called at spawn and re-asserted
	// every flight tick: the seated pose is sustained by the per-tick CommandHandler, so if the command ever drops
	// (or points at a stale transport) we restart it. No-op once it's already running on the right heli.
	protected void MaintainCosmeticPilot()
	{
		if ( !m_Heli || !m_Pilot )
			return;
		int seatAnim = DayZPlayerConstants.VEHICLESEAT_CODRIVER;
		HumanCommandVehicle hcv = m_Pilot.GetCommand_Vehicle();
		if ( !hcv || hcv.GetTransport() != m_Heli )
		{
			vector heliOri = m_Heli.GetOrientation();
			m_Pilot.SetOrientation( Vector( heliOri[0], 0, 0 ) );
			HumanCommandVehicle cmd = m_Pilot.StartCommand_Vehicle( m_Heli, PILOT_SEAT_INDEX, seatAnim, false );
			if ( cmd )
				cmd.SetVehicleType( m_Heli.GetAnimInstance() );
			hcv = m_Pilot.GetCommand_Vehicle();
		}
		if ( hcv )
		{
			hcv.SetVehicleType( m_Heli.GetAnimInstance() );
			if ( hcv.GetVehicleSeat() != seatAnim )
				hcv.SwitchSeat( PILOT_SEAT_INDEX, seatAnim );
		}
		// StartCommand_Vehicle can opportunistically reserve the transport seat. Clear that reservation so seat 0
		// remains logically free for the action system; the command still supplies the visual cockpit pose.
		if ( m_Heli.CrewMember( PILOT_SEAT_INDEX ) == m_Pilot )
			m_Heli.CrewGetOut( PILOT_SEAT_INDEX );

		// NOTE: the collision-layer strip (dBodySetInteractionLayer NOCOLLISION) is deliberately NOT done here.
		// Interaction layers are LOCAL, non-replicated physics state, and the get-in / door-close checks
		// (IsAreaAtDoorFree -> IsIgnoredObject, transport.c:550-677) run on the INTERACTING PLAYER'S CLIENT -- so a
		// server-only set here does nothing client-side (it was a no-op; that's why NOCOLLISION "made no difference"
		// from the manager). It must run on every machine, so it lives in the pilot's own
		// PlayerBase.OnCommandHandlerTick gated by the net-synced m_RceCosmeticPilot flag (ReconquestEvacPlayer.c).
	}

	// Dress the pilot (best-effort: invalid class names just don't spawn, no error). Swap these for exact
	// Expansion pilot-gear class names if you have them; these are confident vanilla items as a baseline.
	protected void EquipPilot(PlayerBase pilot)
	{
		if ( !pilot )
			return;
		array<string> kit = {
			"GorkaEJacket_Summer",
			"GorkaPants_Summer",
			"MilitaryBoots_Black",
			"GorkaHelmet",
			"TacticalGloves_Black"
		};
		foreach ( string item : kit )
			pilot.GetInventory().CreateInInventory( item );
	}

	protected void DespawnPilot()
	{
		if ( m_Pilot )
		{
			PlayerBase pb = PlayerBase.Cast( m_Pilot );
			if ( pb )
				pb.Reconquest_SetCosmeticPilot( false );
			GetGame().ObjectDelete( m_Pilot );
		}
		m_Pilot = null;
	}

	// Guaranteed dismount: whoever is STILL aboard at despawn gets planted on the ground at the safe zone.
	// Deleting the heli releases its crew (server-authoritative -- this is why despawn always "kicked you
	// out"), and SetPosition after that plants them so they never end up stuck/floating where the seat was.
	// The clean GetOutVehicle path handles the normal case; this is the can't-fail backstop.
	protected void ForceDropAndDespawn()
	{
		m_DropPlayers = new array<PlayerBase>;
		int aboard = 0;
		if ( m_EvacPassengers )
		{
			foreach ( PlayerBase p : m_EvacPassengers )
			{
				// Only plant passengers still attached to THIS heli at timeout.
				if ( p && IsPassengerAttachedToEvac( p ) && m_DropPlayers.Find( p ) == -1 )
					m_DropPlayers.Insert( p );
			}
		}
		if ( m_Heli )
		{
			for ( int i = 0; i < m_Heli.CrewSize(); i++ )
			{
				Human h = m_Heli.CrewMember( i );
				if ( !h || h == m_Pilot || !PlayerBase.Cast( h ) )
					continue;
				PlayerBase pCrew = PlayerBase.Cast( h );
				if ( m_DropPlayers.Find( pCrew ) == -1 )
					m_DropPlayers.Insert( pCrew );
				m_Heli.CrewGetOut( i );   // crew-removal attempt
				DayZPlayerImplement dp = DayZPlayerImplement.Cast( h );
				if ( dp && dp.GetCommand_Vehicle() )
					dp.GetCommand_Vehicle().GetOutVehicle();
				aboard++;
			}
		}
		Print( "[ReconquestEvac] force-dismount: " + aboard.ToString() + " passenger(s) aboard -> despawning heli to release them" );

		m_DropPos = m_SafeZone;
		CancelCurrent();   // ObjectDelete the heli -> server-authoritative crew release (always works)

		// plant them on the ground a frame later, once the heli is actually gone (so SetPosition isn't
		// fighting the vehicle parent that ObjectDelete hasn't finished tearing down yet)
		GetGame().GetCallQueue( CALL_CATEGORY_SYSTEM ).CallLater( PlantDroppedPlayers, 300, false );
	}

	// Player-aboard dismount that still gives a FLY-AWAY. We CANNOT fly the occupied heli (no reliable
	// server-side in-place eject for a client player -> it would carry them up = sky-throw). So: capture the
	// passengers, spawn a FRESH EMPTY heli a few metres off the pad, point m_Heli at it, ObjectDelete the OLD
	// heli (the only guaranteed physical crew release), plant the players on the ground, and StartDepart the
	// new empty heli. The heli that ever held a player is always DELETED (never moved); the heli that flies
	// away never had a player -> zero sky-throw by construction. Falls back to ForceDropAndDespawn if spawn fails.
	protected void SwapToFreshHeliAndDepart()
	{
		if ( !m_Heli )
			return;
		m_DropPlayers = new array<PlayerBase>;
		if ( m_EvacPassengers )
		{
			foreach ( PlayerBase pe : m_EvacPassengers )
			{
				if ( pe && m_DropPlayers.Find( pe ) == -1 )
					m_DropPlayers.Insert( pe );
			}
		}
		for ( int i = 0; i < m_Heli.CrewSize(); i++ )
		{
			Human h = m_Heli.CrewMember( i );
			if ( !h || h == m_Pilot || !PlayerBase.Cast( h ) )
				continue;
			PlayerBase pc = PlayerBase.Cast( h );
			if ( m_DropPlayers.Find( pc ) == -1 )
				m_DropPlayers.Insert( pc );
		}
		m_DropPos = m_SafeZone;
		ExpansionHelicopterScript oldHeli = m_Heli;
		vector oldOri = oldHeli.GetOrientation();
		vector spawnPos = m_SafeZone;
		spawnPos[0] = spawnPos[0] - 6.0;
		spawnPos[1] = GetGame().SurfaceY( spawnPos[0], spawnPos[2] );
		Object obj = GetGame().CreateObjectEx( HELI_CLASSNAME, spawnPos, ECE_PLACE_ON_SURFACE );
		ExpansionHelicopterScript fresh = ExpansionHelicopterScript.Cast( obj );
		if ( !fresh )
		{
			Print( "[ReconquestEvac] swap-heli spawn FAILED - falling back to despawn-drop" );
			if ( obj )
				GetGame().ObjectDelete( obj );
			ForceDropAndDespawn();
			return;
		}
		EquipHeli( fresh );
		fresh.Fill( CarFluid.FUEL, fresh.GetFluidCapacity( CarFluid.FUEL ) );
		fresh.Reconquest_SetRotors( ROTOR_FLY );
		fresh.SetOrientation( oldOri );
		// same flags every manager-flown heli gets in StartExtraction: clients must smooth + not alarm on it
		fresh.SetRequiredSimulation( true );
		fresh.Reconquest_SetAutopilot( true );
		DespawnPilot();
		m_Heli = fresh;
		m_RceHasPrevSyncPos = false;   // new airframe: don't derive a sync velocity across the swap
		GetGame().ObjectDelete( oldHeli );
		SpawnPilotVanilla( spawnPos );
		Print( "[ReconquestEvac] swap: old heli deleted (crew released), fresh heli departing with cosmetic pilot rebound" );
		GetGame().GetCallQueue( CALL_CATEGORY_SYSTEM ).CallLater( PlantDroppedPlayers, 300, false );
		StartDepart();
	}

	protected void PlantDroppedPlayers()
	{
		if ( !m_DropPlayers )
			return;
		foreach ( PlayerBase p : m_DropPlayers )
		{
			if ( p )
			{
				vector d = m_DropPos;
				d[0] = d[0] + 3.0;
				d[1] = GetGame().SurfaceY( d[0], d[2] ) + 0.2;
				p.SetPosition( d );
			}
		}
		Print( "[ReconquestEvac] dropped " + m_DropPlayers.Count().ToString() + " player(s) at the safe zone" );
		m_DropPlayers = null;
	}

	// PH_WAIT cancel that can fire with a NON-signaller seated aboard (wait timeout / signaller death):
	// authorization means a friend in a seat no longer advances the phase, so these cancels must not
	// ObjectDelete the airframe around them. Capture + notify whoever is seated, then plant them a beat
	// after the delete -- same guarantee ForceDropAndDespawn gives at the safe zone, but the plant point
	// here is the LZ (m_LandingPos), NOT m_SafeZone, or a never-extracted squatter would get a free ride.
	// CancelCurrent() deliberately leaves m_DropPlayers/m_DropPos alone (ForceDropAndDespawn relies on that).
	protected void CancelWaitPhase()
	{
		if ( m_Heli && AnyPlayerAboard() )
		{
			m_DropPlayers = new array<PlayerBase>;
			for ( int i = 0; i < m_Heli.CrewSize(); i++ )
			{
				Human h = m_Heli.CrewMember( i );
				if ( !h || h == m_Pilot || !PlayerBase.Cast( h ) )
					continue;
				PlayerBase pb = PlayerBase.Cast( h );
				if ( m_DropPlayers.Find( pb ) == -1 )
				{
					m_DropPlayers.Insert( pb );
					Notify( pb, "EXTRACTION", "Helicopter leaving -- extraction cancelled" );
				}
			}
			m_DropPos = m_LandingPos;
			CancelCurrent();
			GetGame().GetCallQueue( CALL_CATEGORY_SYSTEM ).CallLater( PlantDroppedPlayers, 300, false );
			return;
		}
		CancelCurrent();
	}

	// Emergency teardown for a WEDGED airframe: whoever is seated is planted on the ground DIRECTLY BELOW
	// the heli (not the LZ and not the safe zone -- no free ride in either direction), then the heli
	// despawns and the extraction slot frees. CancelCurrent leaves m_DropPlayers/m_DropPos alone.
	protected void CancelStuckFlight()
	{
		if ( m_Heli && AnyPlayerAboard() )
		{
			m_DropPlayers = new array<PlayerBase>;
			for ( int i = 0; i < m_Heli.CrewSize(); i++ )
			{
				Human h = m_Heli.CrewMember( i );
				if ( !h || h == m_Pilot || !PlayerBase.Cast( h ) )
					continue;
				PlayerBase pb = PlayerBase.Cast( h );
				if ( m_DropPlayers.Find( pb ) == -1 )
				{
					m_DropPlayers.Insert( pb );
					Notify( pb, "EXTRACTION", "Helicopter obstructed -- extraction aborted" );
				}
			}
			vector dp = m_Heli.GetPosition();
			dp[1] = GetGame().SurfaceY( dp[0], dp[2] );
			m_DropPos = dp;
			CancelCurrent();
			GetGame().GetCallQueue( CALL_CATEGORY_SYSTEM ).CallLater( PlantDroppedPlayers, 300, false );
			return;
		}
		CancelCurrent();
	}

	protected void CancelCurrent()
	{
		SendSignallerFlag( m_Signaller, false );   // caller's HUD stops tracking the (ended) extraction
		GetGame().GetCallQueue( CALL_CATEGORY_SYSTEM ).Remove( FlightTick );
		DespawnPilot();
		if ( m_Heli )
			GetGame().ObjectDelete( m_Heli );
		m_Heli   = null;
		m_Active = false;
		m_EvacPassengers = null;
		m_EmptyDropoffTicks = 0;
		m_Signaller = null;
		m_SafeZonePathIndex = 0;
		m_ForceSafeZoneTouchdown = false;
	}

	// Search outward for a landing zone. Tier 1: a fully clear 30m circle (IsGoodLandingSpot). Tier 2: the
	// nearest spot whose 10m touchdown footprint is clear TRUE ground (IsTouchdownClear -- includes the
	// downward column ray, so a rooftop can never qualify). If NEITHER exists within LZ_SEARCH_MAX, the
	// extraction is REFUSED (found=false) -- flying a doomed approach into dense-city geometry is how helis
	// ended up crashed on rooftops; the signaller keeps their round and is told to move to open ground.
	protected vector FindLandingSpot(vector desired, out bool found)
	{
		found = true;
		vector bestTd;
		bool haveTd = false;
		for ( float radius = 0; radius <= LZ_SEARCH_MAX; radius += 10.0 )
		{
			int steps = 8;
			for ( int i = 0; i < steps; i++ )
			{
				vector p = desired;
				if ( radius > 0 )
				{
					float ang = (360.0 / steps) * i * Math.DEG2RAD;
					p[0] = desired[0] + Math.Cos( ang ) * radius;
					p[2] = desired[2] + Math.Sin( ang ) * radius;
				}

				if ( IsGoodLandingSpot( p ) )
				{
					p[1] = GetGame().SurfaceY( p[0], p[2] );
					Print( "[ReconquestEvac] LZ found at " + p.ToString() + " (" + radius.ToString() + "m away)" );
					return p;
				}

				// remember the nearest footprint-clear TRUE-ground spot (tight but landable)
				if ( !haveTd && IsTouchdownClear( p ) )
				{
					bestTd = p;
					bestTd[1] = GetGame().SurfaceY( p[0], p[2] );
					haveTd = true;
				}

				if ( radius == 0 )
					break;
			}
		}

		if ( haveTd )
		{
			Print( "[ReconquestEvac] no ideal LZ within " + LZ_SEARCH_MAX.ToString() + "m - using nearest clear touchdown footprint " + bestTd.ToString() );
			return bestTd;
		}

		found = false;
		desired[1] = GetGame().SurfaceY( desired[0], desired[2] );
		Print( "[ReconquestEvac] NO landable ground within " + LZ_SEARCH_MAX.ToString() + "m of " + desired.ToString() + " - refusing extraction" );
		return desired;
	}

	protected vector ResolveExactLandingTarget(vector exact)
	{
		exact[1] = GetGame().SurfaceY( exact[0], exact[2] );
		if ( IsTouchdownClear( exact ) )
			return exact;

		for ( float radius = 5.0; radius <= TOUCHDOWN_SEARCH_MAX; radius += 5.0 )
		{
			int steps = 12;
			for ( int i = 0; i < steps; i++ )
			{
				float ang = ( 360.0 / steps ) * i * Math.DEG2RAD;
				vector p = exact;
				p[0] = exact[0] + Math.Cos( ang ) * radius;
				p[2] = exact[2] + Math.Sin( ang ) * radius;
				p[1] = GetGame().SurfaceY( p[0], p[2] );
				if ( IsTouchdownClear( p ) )
				{
					Print( "[ReconquestEvac] exact safe-zone pad blocked by landing hazard - using nearest clear touchdown " + p.ToString() + " (" + radius.ToString() + "m from configured pad)" );
					return p;
				}
			}
		}

		Print( "[ReconquestEvac] WARNING: exact safe-zone pad is blocked and no safe touchdown was found within 10m" );
		return exact;
	}

	protected bool IsTouchdownClear(vector p)
	{
		if ( !IsDryLand( p ) )
			return false;
		if ( Reconquest_ColumnBlocked( p ) )
			return false;   // a roof/structure hangs over the column -> this is NOT ground

		float cy = GetGame().SurfaceY( p[0], p[2] );
		float maxDiff = 0;
		for ( int ai = 0; ai < 8; ai++ )
		{
			float ang = ( 360.0 / 8 ) * ai * Math.DEG2RAD;
			float sx = p[0] + Math.Cos( ang ) * TOUCHDOWN_CLEAR_RADIUS;
			float sz = p[2] + Math.Sin( ang ) * TOUCHDOWN_CLEAR_RADIUS;
			maxDiff = Math.Max( maxDiff, Math.AbsFloat( GetGame().SurfaceY( sx, sz ) - cy ) );
		}
		if ( maxDiff > LZ_FLAT_TOLERANCE )
			return false;

		array<Object> nearby = new array<Object>;
		array<CargoBase> cargos = new array<CargoBase>;
		GetGame().GetObjectsAtPosition( Vector( p[0], cy, p[2] ), TOUCHDOWN_CLEAR_RADIUS, nearby, cargos );
		foreach ( Object o : nearby )
		{
			if ( Reconquest_IsLandingHazard( o ) )
				return false;
		}
		return true;
	}

	protected bool IsDryLand(vector p)
	{
		if ( GetGame().SurfaceIsSea( p[0], p[2] ) || GetGame().SurfaceIsPond( p[0], p[2] ) )
			return false;
		if ( GetGame().SurfaceY( p[0], p[2] ) < LZ_MIN_ELEVATION )
			return false;
		return true;
	}

	protected bool IsGoodLandingSpot(vector p)
	{
		float cy = GetGame().SurfaceY( p[0], p[2] );

		if ( cy < LZ_MIN_ELEVATION )
			return false;
		if ( GetGame().SurfaceIsSea( p[0], p[2] ) || GetGame().SurfaceIsPond( p[0], p[2] ) )
			return false;
		if ( Reconquest_ColumnBlocked( p ) )
			return false;   // never pick a spot under/on a roof -- the heli lands on TRUE ground only

		// relatively flat across the whole landing circle: sample 8 directions at 12 m and 24 m out
		float maxDiff = 0;
		for ( int ri = 1; ri <= 2; ri++ )
		{
			float r = ri * 12.0;
			for ( int ai = 0; ai < 8; ai++ )
			{
				float ang = (360.0 / 8) * ai * Math.DEG2RAD;
				float sx = p[0] + Math.Cos( ang ) * r;
				float sz = p[2] + Math.Sin( ang ) * r;
				maxDiff = Math.Max( maxDiff, Math.AbsFloat( GetGame().SurfaceY( sx, sz ) - cy ) );
			}
		}
		if ( maxDiff > LZ_FLAT_TOLERANCE )
			return false;

		array<Object> nearby = new array<Object>;
		array<CargoBase> cargos = new array<CargoBase>;
		GetGame().GetObjectsAtPosition( Vector( p[0], cy, p[2] ), LZ_CLEAR_RADIUS, nearby, cargos );
		foreach ( Object o : nearby )
		{
			if ( Reconquest_IsLandingHazard( o ) )
				return false;
		}

		return true;
	}

	// Hazard name fragments (all lowercase, substring-matched against GetType + GetDebugName). Catches what
	// the engine flags (IsTree/IsRock/IsBuilding) and class casts miss: static map furniture AND player-built
	// base parts from mods that do NOT inherit the vanilla BaseBuildingBase (BaseBuildingPlus "bbp_",
	// RearmED base building, etc.). Server admins can EXTEND the list without a rebuild by putting one
	// fragment per line in $profile:rce_evac_hazards.txt ('#' comments allowed).
	protected static ref TStringArray s_HazardTokens;
	static const string HAZARD_TOKENS_FILE = "$profile:rce_evac_hazards.txt";

	protected void EnsureHazardTokens()
	{
		if ( s_HazardTokens )
			return;
		s_HazardTokens = new TStringArray;
		// static map furniture (the original list)
		s_HazardTokens.Insert( "fence" );
		s_HazardTokens.Insert( "wall" );
		s_HazardTokens.Insert( "gate" );
		s_HazardTokens.Insert( "barrier" );
		s_HazardTokens.Insert( "barricade" );
		s_HazardTokens.Insert( "guardrail" );
		s_HazardTokens.Insert( "fnc" );
		s_HazardTokens.Insert( "tower" );
		s_HazardTokens.Insert( "radio" );
		s_HazardTokens.Insert( "antenna" );
		s_HazardTokens.Insert( "transmitter" );
		s_HazardTokens.Insert( "mast" );
		s_HazardTokens.Insert( "pole" );
		// player-built base parts (mod prefixes + generic part names). Verified against the installed mods:
		// BaseBuildingPlus prefixes everything "BBP_"; RA Base Building (RearmED) names its structures
		// "BaseBuilding_*" / "Compound(Wall|Gate)" -> caught by "basebuild" + the wall/gate words below.
		s_HazardTokens.Insert( "bbp" );        // BaseBuildingPlus classnames (BBP_*)
		s_HazardTokens.Insert( "basebuild" );  // "BaseBuilding_*" prefixes (RA Base Building + generic mods)
		s_HazardTokens.Insert( "compound" );   // RA "CompoundWall/CompoundGate" family (belt-and-suspenders)
		s_HazardTokens.Insert( "floor" );
		s_HazardTokens.Insert( "roof" );
		s_HazardTokens.Insert( "foundation" );
		s_HazardTokens.Insert( "platform" );
		s_HazardTokens.Insert( "ramp" );
		s_HazardTokens.Insert( "stair" );
		s_HazardTokens.Insert( "shelter" );

		// per-server extras (no rebuild needed). Hardened: the list is static for the whole session and the
		// token loop runs inside the 50Hz flight scans, so a malformed file (an accidental object dump, a
		// 1-2 char line that matches everything) must not silently poison every hazard scan until restart.
		if ( FileExist( HAZARD_TOKENS_FILE ) )
		{
			FileHandle fh = OpenFile( HAZARD_TOKENS_FILE, FileMode.READ );
			if ( fh != 0 )
			{
				string line;
				int extras = 0;
				while ( FGets( fh, line ) > 0 )
				{
					line = SanitizePathLine( line );
					if ( line == "" || IsPathCommentLine( line ) )
						continue;
					line.ToLower();
					if ( line.Length() < 3 )
					{
						Print( "[ReconquestEvac] WARNING: ignoring hazard token '" + line + "' from " + HAZARD_TOKENS_FILE + " (tokens under 3 chars match nearly everything)" );
						continue;
					}
					if ( s_HazardTokens.Find( line ) != -1 )
						continue;
					if ( extras >= 64 )
					{
						Print( "[ReconquestEvac] WARNING: " + HAZARD_TOKENS_FILE + " exceeds 64 usable tokens - ignoring the rest (wrong file?)" );
						break;
					}
					s_HazardTokens.Insert( line );
					extras++;
				}
				CloseFile( fh );
				Print( "[ReconquestEvac] loaded " + extras.ToString() + " extra hazard token(s) from " + HAZARD_TOKENS_FILE );
			}
		}
	}

	protected bool Reconquest_IsLandingHazard(Object o)
	{
		if ( !o || o == m_Heli || o == m_Pilot )
			return false;
		if ( PlayerBase.Cast( o ) )
			return false;
		if ( o.IsTree() || o.IsRock() || o.IsBuilding() )
			return true;
		// vehicles: a car/heli/boat parked on the pad or under the approach. Our own airframe is excluded
		// above (o == m_Heli), so the evac heli never flags itself while descending.
		if ( Transport.Cast( o ) )
			return true;
		// player-built structures inheriting the vanilla bases (fences, watchtowers, most BB mods) + tents
		if ( BaseBuildingBase.Cast( o ) || TentBase.Cast( o ) )
			return true;

		EnsureHazardTokens();
		string type = o.GetType();
		type.ToLower();
		string dbg = o.GetDebugName();
		dbg.ToLower();
		foreach ( string tok : s_HazardTokens )
		{
			if ( type.Contains( tok ) || dbg.Contains( tok ) )
				return true;
		}
		return false;
	}

	protected void EquipHeli(EntityAI heli)
	{
		if ( !heli )
			return;

		array<string> parts = {
			"ExpansionHelicopterBattery",
			"ExpansionIgniterPlug",
			"ExpansionHydraulicHoses",
			"HeadlightH7",
			"ExpansionUh1hDoor_1_1",
			"ExpansionUh1hDoor_1_2",
			"ExpansionUh1hDoor_2_1",
			"ExpansionUh1hDoor_2_2"
		};

		foreach ( string part : parts )
		{
			if ( !heli.GetInventory().CreateAttachment( part ) )
				Print( "[ReconquestEvac] (note) could not attach '" + part + "'" );
		}
	}
}
