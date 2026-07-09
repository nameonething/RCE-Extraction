// Modded helicopter base for evac helis.
//
// The Expansion UH1H ("ExpansionUh1h") is ExpansionHelicopterScript : CarScript.
// A driverless heli auto-stops its engine, so we drive the (net-synced) rotor speed directly.
modded class ExpansionHelicopterScript
{
	// Drive the (net-synced) rotor speed directly. This MUST re-assert EVERY flight tick: the heli is
	// driverless, and with SetRequiredSimulation(true) the Expansion sim stays active and spins the rotor DOWN
	// (engine auto-stop) every physics step -- so anything that skips the per-tick set (e.g. a "skip if
	// unchanged" guard) lets the sim win and the rotor decays to a stop mid-flight. Re-assert unconditionally;
	// the engine throttles the actual sync send rate, so SetSynchDirty() per tick is cheap.
	void Reconquest_SetRotors(float speed)
	{
		if ( m_Simulation )
		{
			m_Simulation.m_RotorSpeed = speed;
			SetSynchDirty();
		}
	}

	// Extraction state for the proximity HUD (net-synced server->client): phase 0=none,1=inbound,2=board,
	// 3=departing,4=enroute,5=arrived; countdown = seconds left for the current phase (0 if none).
	protected int m_RceGuiPhase, m_RceGuiCountdown;
	// S2 AUTOPILOT flag (net-synced). Raised on every evac heli the manager flies. The client smoother finds the
	// heli via THIS flag, and the EOnSimulate override keeps the heli in the driverless/autopilot path if a test
	// ever puts a cosmetic body in seat 0 again. Default false -> a single never-dirtied bool in production.
	protected bool m_RceAutopilot;

	void ExpansionHelicopterScript()
	{
		RegisterNetSyncVariableInt( "m_RceGuiPhase", 0, 10 );
		RegisterNetSyncVariableInt( "m_RceGuiCountdown", 0, 32000 );  // seconds (board/depart) OR metres-to-safe-zone (in flight; cross-map legs exceed 10km, so the range must cover the full map diagonal)
		RegisterNetSyncVariableBool( "m_RceAutopilot" );
	}

	void Reconquest_SetAutopilot(bool on)   // server-side: mark this heli as autopilot-flown
	{
		m_RceAutopilot = on;
		SetSynchDirty();
	}

	bool Reconquest_IsAutopilot()           // client+server: is this an autopilot-flown evac heli?
	{
		return m_RceAutopilot;
	}

	// ===================== SHOOTABLE HULL (evac helis only) =====================
	// Vehicle hulls transfer next to no projectile damage into global Health natively -- players emptying
	// mags into the airframe saw "no damage" and destruction was practically unreachable. Evac helis get an
	// explicit hull pool instead: every firearm/explosion hit chips it (armor-absorbed hits still count at
	// least RCE_HULL_MIN_HIT), and when the pool is spent the heli is destroyed outright -> Expansion blows
	// it up (EnableHelicopterExplosions) and the manager's shot-down handler announces it server-wide.
	// Player-flown Expansion helis are untouched (gated on the autopilot flag). Server-side only.
	// Pool size comes from $profile:rce_evac_settings.txt (HullHP, default 35000 -- deliberately TOUGH:
	// shooting one down is a coordinated effort, not a lone rifleman's whim). Admin-tunable, no rebuild.
	static const float RCE_HULL_MIN_HIT = 40.0;    // floor per projectile hit, so armor can't nullify fire
	protected float m_RceHullDamage;               // accumulated; fresh airframe = fresh pool

	override void EEHitBy(TotalDamageResult damageResult, int damageType, EntityAI source, int component, string dmgZone, string ammo, vector modelPos, float speedCoef)
	{
		super.EEHitBy( damageResult, damageType, source, component, dmgZone, ammo, modelPos, speedCoef );

		if ( !GetGame() || !GetGame().IsServer() )
			return;
		if ( !m_RceAutopilot || IsDamageDestroyed() )
			return;
		if ( damageType != DamageType.FIRE_ARM && damageType != DamageType.EXPLOSION )
			return;

		float hullHP = ReconquestEvacManager.GetInstance().Reconquest_GetHullHP();
		float dmg = damageResult.GetHighestDamage( "Health" );
		if ( damageType == DamageType.FIRE_ARM && dmg < RCE_HULL_MIN_HIT )
			dmg = RCE_HULL_MIN_HIT;
		m_RceHullDamage = m_RceHullDamage + dmg;
		Print( "[ReconquestEvac] evac heli hit (" + ammo + " zone=" + dmgZone + " dmg=" + dmg.ToString() + ") hull " + m_RceHullDamage.ToString() + "/" + hullHP.ToString() );

		if ( m_RceHullDamage >= hullHP )
		{
			Print( "[ReconquestEvac] evac heli hull spent - destroying airframe" );
			SetHealth( "", "", 0.0 );   // Expansion explodes it; the manager's FlightTick announces the kill
		}
	}

	// Expansion's pilotless auto-hover logic treats a driverless heli as abandoned: after
	// (PilotlessAutoHoverEngineStopDelay - 10) seconds it plays Expansion_Mh6_Warning_SoundSet and
	// eventually stops the engine. Evac helis are ALWAYS driverless (script-flown), so that alarm
	// fires mid-flight. Suppress it for autopilot helis only -- player-flown Expansion helis unchanged.
	override void OnPostSimulation(float pDt)
	{
		if ( m_RceAutopilot )
			m_Expansion_PilotlessTime = 0;

		super.OnPostSimulation( pDt );

		if ( !m_RceAutopilot )
			return;

		m_Expansion_PilotlessTime = 0;

		if ( m_Expansion_HeliWarningSound )
		{
			m_Expansion_HeliWarningSound.Stop();
			m_Expansion_HeliWarningSound = null;
		}
	}

	// Expansion's EOnSimulate (carscript.c:2397) sets m_State.m_HasDriver from the seat-0 occupant and then runs
	// the driven force model. We fly the heli ourselves, so when the autopilot flag is set we re-clear m_HasDriver
	// AFTER super as a backstop. On clients (not the physics host) m_HasDriver is already false -> no-op.
	override void EOnSimulate(IEntity other, float dt)
	{
		super.EOnSimulate( other, dt );
		if ( m_RceAutopilot && m_State )
			m_State.m_HasDriver = false;
	}

	void Reconquest_SetGui(int phase, int countdown)   // server-side
	{
		m_RceGuiPhase = phase;
		m_RceGuiCountdown = countdown;
		SetSynchDirty();
	}

	int Reconquest_GetGuiPhase()     { return m_RceGuiPhase; }       // client-side
	int Reconquest_GetGuiCountdown() { return m_RceGuiCountdown; }


	// Cosmetic pilot bodies can block door/get-in on the CLIENT: Transport::IsIgnoredObject only
	// treats a seated body as ignorable when GetCommand_Vehicle()->GetTransport()==this, which is NOT
	// reliably true for a server-scripted survivor on remote clients. NOCOLLISION (server-only) also
	// misses the client's physics layer. Treat the autopilot heli's cosmetic pilot seat occupant as
	// ignored here so IsAreaAtDoorFree passes on every machine.
	override bool IsIgnoredObject( Object o )
	{
		if ( m_RceAutopilot && o )
		{
			Human cosmetic = CrewMember( ReconquestEvacManager.PILOT_SEAT_INDEX );
			if ( cosmetic && cosmetic == o )
				return true;
			PlayerBase pb = PlayerBase.Cast( o );
			if ( pb && pb.Reconquest_IsCosmeticPilot() )
				return true;
		}
		return super.IsIgnoredObject( o );
	}
}
