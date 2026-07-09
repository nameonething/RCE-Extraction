// NOTE: the ReconquestEvacRPC id class lives in ReconquestEvacManager.c (4_World) so that modded
// PlayerBase (4_World) can reference the ids too -- 4_World cannot see 5_Mission classes.

// Client side of ReconquestEvac: the driverless-heli render SMOOTHER (production), the proximity
// extraction HUD, and the admin F7 landing-path / hull-HP editor.
modded class MissionGameplay
{
	protected ExpansionHelicopterScript m_RceHeli;     // cached evac heli the smoother tracks
	protected int    m_RceSearchThrottle;              // frames until the next (re)acquire scan
	// CLIENT-SIDE SMOOTHING EXPERIMENT (Approach 1): low-pass the heli's stepping net-synced position so the
	// RENDERED motion is continuous between ~15-20Hz snapshots. Toggle to A/B against the unsmoothed baseline.
	static const bool  RCE_SMOOTH = true;
	static const float RCE_SMOOTH_RATE = 10.0;         // orientation low-pass rate (1/s)
	// Smoothing is only safe for an observer CLOSE enough to (a) perceive jitter and (b) receive frequent
	// net-sync snapshots. A distant observer gets sparse updates; dead-reckoning stale velocity there flies
	// the RENDERED airframe off to a wrong spot -> the heli looks "stuck in the air" or is nowhere the
	// viewer looks (invisible). Beyond this range we leave the engine's own net-synced position alone.
	static const float RCE_SMOOTH_RANGE  = 250.0;      // metres: only smooth within this of the local player
	static const int   RCE_STALE_MS      = 400;        // no fresh snapshot for this long -> stop extrapolating
	static const float RCE_MAX_DIVERGE   = 25.0;       // render may never sit further than this from the truth
	protected bool   m_RceSmoothInit;
	protected vector m_RceRenderPos, m_RceLastSet, m_RceSnapPos, m_RceEstVel;
	protected vector m_RceRenderOri, m_RceTargetOri;
	protected int    m_RceSnapTime;
	// proximity extraction HUD
	protected Widget     m_RceHud;
	protected TextWidget m_RceHudTitle, m_RceHudStatus;
	protected bool       m_RceHudCreated;
	protected ExpansionHelicopterScript m_RceHudHeli;
	protected int        m_RceHudThrottle;
	// Admin safe-zone path editor. Server authorization is enforced when saving; the client overlay is only a
	// capture surface for admins standing in-world at the desired approach/touchdown points.
	protected Widget     m_RceAdminRoot;
	protected TextWidget m_RceAdminTitle, m_RceAdminStatus;
	protected bool       m_RceAdminCreated;
	protected bool       m_RceAdminOpen;
	protected ref array<vector> m_RceAdminPath;
	protected string     m_RceAdminMessage;

	override void OnInit()
	{
		super.OnInit();
		DayZGame dz = DayZGame.Cast( GetGame() );
		if ( dz )
			dz.Event_OnRPC.Insert( Reconquest_OnAdminPathAck );
	}

	override void OnUpdate(float timeslice)
	{
		super.OnUpdate( timeslice );

		if ( !GetGame() || GetGame().IsServer() )
			return;

		PlayerBase p = PlayerBase.Cast( GetGame().GetPlayer() );
		if ( !p )
			return;

		Reconquest_UpdateHud( p );   // proximity extraction countdown (independent of the smoothing below)
		Reconquest_UpdateAdminEditor( p );

		ExpansionHelicopterScript heli = Reconquest_FindEvacHeli( p );
		if ( !heli )
		{
			// No valid evac heli: the SMOOTHER must re-seed on the next heli it acquires -- carrying stale
			// m_RceLastSet/m_RceEstVel across helis would render the NEXT extraction's heli at the PREVIOUS
			// one's last smoothed position for a frame. (Previously this reset only ran for rig-flagged
			// rides, so production clients kept stale state between extractions.)
			m_RceSmoothInit = false;
			return;
		}

		// --- client-side smoothing: replace the heli's stepping render position with a low-passed one. We detect a
		//     fresh net-sync snapshot as GetPosition jumping away from the value WE last wrote; between snapshots the
		//     entity holds, so we ramp toward the latest target. The probe below then measures this rendered result
		//     (= exactly what the player sees). If SetPosition is reverted by the engine, posMean stays ~baseline.
		if ( RCE_SMOOTH )
		{
			vector cur    = heli.GetPosition();
			vector curOri = heli.GetOrientation();
			int    now    = GetGame().GetTime();   // client game time, ms

			// PROXIMITY GATE: only rewrite the heli's render transform when it is close to the LOCAL player.
			// Far observers get sparse updates -> the dead-reckoner below would diverge from the truth and
			// render a "stuck"/invisible airframe. Out of range, do nothing: the engine's net-synced position
			// stands (steppy but truthful, and imperceptible at this distance).
			if ( vector.Distance( cur, p.GetPosition() ) > RCE_SMOOTH_RANGE )
			{
				m_RceSmoothInit = false;   // re-seed cleanly when it comes back into range
				return;
			}

			if ( !m_RceSmoothInit )
			{
				m_RceRenderPos = cur; m_RceLastSet = cur; m_RceSnapPos = cur; m_RceEstVel = "0 0 0";
				m_RceRenderOri = curOri; m_RceTargetOri = curOri;
				m_RceSnapTime = now;
				m_RceSmoothInit = true;
			}
			bool snap = ( vector.Distance( cur, m_RceLastSet ) > 0.02 );

			// While the heli is parked for boarding / pre-depart countdown / disembark, pin the client render
			// pose to the net-synced truth. Dead-reckoning inbound cruise velocity through the door window
			// shifts door-reach volumes on the client and breaks close/get-in actions.
			int guiPhase = heli.Reconquest_GetGuiPhase();
			if ( guiPhase == 2 || guiPhase == 3 || guiPhase == 5 )
			{
				m_RceEstVel = "0 0 0";
				m_RceRenderPos = cur;
				m_RceLastSet = cur;
				m_RceTargetOri = curOri;
				m_RceRenderOri = curOri;
				heli.SetPosition( m_RceRenderPos );
				heli.SetOrientation( m_RceRenderOri );
			}
			else
			{
				// Velocity-extrapolation dead-reckoner: render the heli forward along its estimated velocity
				// between the ~15-20Hz net-sync snapshots, so cruise looks continuous instead of stepped.
				// Sustained high-speed cruise is the residual case (a driverless vehicle gets no native client
				// prediction); the gate above keeps this to close, frequently-updated observers where it helps.
				m_RceRenderPos = m_RceLastSet + m_RceEstVel * timeslice;
				if ( snap )
				{
					float dtSnap = ( now - m_RceSnapTime ) / 1000.0;
					if ( dtSnap > 0.005 && dtSnap < 0.6 )
					{
						vector measVel = ( cur - m_RceSnapPos ) * ( 1.0 / dtSnap );
						m_RceEstVel = m_RceEstVel * 0.4 + measVel * 0.6;
					}
					m_RceSnapPos = cur;
					m_RceSnapTime = now;
					m_RceTargetOri = curOri;
					m_RceRenderPos = m_RceRenderPos + ( cur - m_RceRenderPos ) * 0.3;   // gentle pull toward truth
				}
				else if ( now - m_RceSnapTime > RCE_STALE_MS )
				{
					// No fresh snapshot for a while (a lag spike, or priority dropped): keep extrapolating a
					// stale velocity and the render flies away from the real heli. Stop -> hold at the last
					// net-synced truth until updates resume.
					m_RceEstVel = "0 0 0";
					m_RceRenderPos = cur;
				}

				// HARD SAFETY: the rendered airframe may NEVER sit further than RCE_MAX_DIVERGE from the true
				// net-synced position. Normal in-range smoothing stays a few metres off truth, so this only
				// fires on a genuine desync -> the heli can't appear "stuck" far from where it actually is.
				if ( vector.Distance( m_RceRenderPos, cur ) > RCE_MAX_DIVERGE )
				{
					m_RceRenderPos = cur;
					m_RceEstVel = "0 0 0";
				}

				// orientation low-pass, wrap-aware on yaw/roll (pitch is bounded)
				float ao = Math.Clamp( timeslice * RCE_SMOOTH_RATE, 0.0, 1.0 );
				m_RceRenderOri[0] = m_RceRenderOri[0] + Reconquest_WrapDeg( m_RceTargetOri[0] - m_RceRenderOri[0] ) * ao;
				m_RceRenderOri[1] = m_RceRenderOri[1] + ( m_RceTargetOri[1] - m_RceRenderOri[1] ) * ao;
				m_RceRenderOri[2] = m_RceRenderOri[2] + Reconquest_WrapDeg( m_RceTargetOri[2] - m_RceRenderOri[2] ) * ao;

				heli.SetPosition( m_RceRenderPos );
				heli.SetOrientation( m_RceRenderOri );
				m_RceLastSet = m_RceRenderPos;
			}
		}

	}

	override void OnKeyPress(int key)
	{
		super.OnKeyPress( key );

		PlayerBase p = PlayerBase.Cast( GetGame().GetPlayer() );
		if ( !p )
			return;

		if ( key == KeyCode.KC_F7 )
		{
			// Non-admins get NOTHING -- no overlay, no denial message. The server sent this flag at connect
			// from the allowlist; the save RPC is still independently authorized server-side. The one
			// exception: if the editor is ALREADY open (flag flipped false mid-session, e.g. de-listed then
			// respawned), F7 must still be able to CLOSE it -- otherwise the overlay is stuck for the session.
			if ( !p.Reconquest_IsPathAdmin() && !m_RceAdminOpen )
				return;
			if ( !m_RceAdminPath )
				m_RceAdminPath = new array<vector>;
			m_RceAdminOpen = !m_RceAdminOpen;
			if ( m_RceAdminOpen )
				m_RceAdminMessage = "Stand on each approach point and press F8. Add touchdown last.";
			return;
		}

		if ( !m_RceAdminOpen )
			return;

		if ( key == KeyCode.KC_F8 )
		{
			if ( !m_RceAdminPath )
				m_RceAdminPath = new array<vector>;
			vector pos = p.GetPosition();
			pos[1] = GetGame().SurfaceY( pos[0], pos[2] );
			m_RceAdminPath.Insert( pos );
			m_RceAdminMessage = "Added point " + m_RceAdminPath.Count().ToString() + ": " + pos.ToString();
		}
		else if ( key == KeyCode.KC_F9 )
		{
			Reconquest_SendAdminPath();
		}
		else if ( key == KeyCode.KC_F10 )
		{
			if ( m_RceAdminPath )
				m_RceAdminPath.Clear();
			m_RceAdminMessage = "Draft path cleared.";
		}
		else if ( key == KeyCode.KC_F11 )
		{
			Reconquest_CycleHullPreset( p );
		}
	}

	// Heli hull HP presets the F7 editor cycles through with F11. Each press applies immediately
	// (server-validated against the admin allowlist) and persists to $profile:rce_evac_settings.txt.
	protected static ref array<float> RCE_HULL_PRESETS = {5000.0, 10000.0, 15000.0, 20000.0, 25000.0, 35000.0, 50000.0, 75000.0, 100000.0};
	protected int   m_RceHullPresetIdx = -1;
	protected float m_RceLocalHullHP;   // last value we requested/displayed (0 = use the server-sent value)

	protected void Reconquest_CycleHullPreset( PlayerBase p )
	{
		if ( m_RceHullPresetIdx < 0 )
		{
			// first press: start from the preset matching the server's current value (if it is one)
			m_RceHullPresetIdx = 0;
			float cur = p.Reconquest_GetAdminHullHP();
			for ( int i = 0; i < RCE_HULL_PRESETS.Count(); i++ )
			{
				if ( Math.AbsFloat( RCE_HULL_PRESETS.Get( i ) - cur ) < 1.0 )
				{
					m_RceHullPresetIdx = i;
					break;
				}
			}
		}
		m_RceHullPresetIdx = ( m_RceHullPresetIdx + 1 ) % RCE_HULL_PRESETS.Count();
		float hp = RCE_HULL_PRESETS.Get( m_RceHullPresetIdx );
		m_RceLocalHullHP = hp;

		ScriptRPC rpc = new ScriptRPC();
		rpc.Write( hp );
		rpc.Send( null, ReconquestEvacRPC.ADMIN_SET_HULL, true );
		m_RceAdminMessage = "Setting heli hull HP to " + Math.Round( hp ).ToString() + "...";
	}

	void Reconquest_OnAdminPathAck(PlayerIdentity sender, Object target, int rpc_type, ParamsReadContext ctx)
	{
		if ( rpc_type != ReconquestEvacRPC.ADMIN_PATH_ACK )
			return;
		if ( !GetGame() || GetGame().IsServer() )
			return;

		bool ok;
		string msg;
		if ( !ctx.Read( ok ) || !ctx.Read( msg ) )
			return;
		if ( ok )
			m_RceAdminMessage = "Saved: " + msg;
		else
			m_RceAdminMessage = "Denied: " + msg;
		m_RceAdminOpen = true;
	}

	// Nearest DRIVERLESS Expansion heli within range (re-scan ~0.5s, cache otherwise). Driverless = no owning
	// physics-host client = the heli that gets no engine interpolation, i.e. exactly the scripted/evac heli we
	// must smooth. A player-driven heli (driver in seat 0) is excluded -- the engine already interpolates it.
	protected ExpansionHelicopterScript Reconquest_FindEvacHeli(PlayerBase p)
	{
		if ( m_RceHeli && Reconquest_HeliValid( m_RceHeli, p ) )
			return m_RceHeli;

		m_RceSearchThrottle--;
		if ( m_RceSearchThrottle > 0 )
			return null;
		m_RceSearchThrottle = 30;   // ~0.5s between scans

		array<Object> objs = new array<Object>;
		array<CargoBase> cargos = new array<CargoBase>;
		GetGame().GetObjectsAtPosition3D( p.GetPosition(), 500.0, objs, cargos );
		foreach ( Object o : objs )
		{
			ExpansionHelicopterScript h = ExpansionHelicopterScript.Cast( o );
			if ( h && h.Reconquest_IsAutopilot() )   // autopilot-flown evac heli
			{
				m_RceHeli = h;
				return h;
			}
		}
		m_RceHeli = null;
		return null;
	}

	// ===================== PROXIMITY EXTRACTION HUD =====================
	// A small countdown panel at TOP-CENTER (~1 inch from the top), shown ONLY while the local player is near an
	// active extraction heli (proximity-based, not server-wide). Reads the heli's net-synced phase + countdown/distance.
	protected void Reconquest_UpdateHud(PlayerBase p)
	{
		if ( !m_RceHudCreated )
		{
			m_RceHudCreated = true;
			m_RceHud = GetGame().GetWorkspace().CreateWidgets( "ReconquestEvac/gui/reconquest_evac_hud.layout" );
			if ( m_RceHud )
			{
				m_RceHudTitle  = TextWidget.Cast( m_RceHud.FindAnyWidget( "rce_hud_title" ) );
				m_RceHudStatus = TextWidget.Cast( m_RceHud.FindAnyWidget( "rce_hud_status" ) );
				if ( m_RceHudTitle )
					m_RceHudTitle.SetText( "EXTRACTION" );
				m_RceHud.Show( false );
			}
		}
		if ( !m_RceHud )
			return;

		// Which heli's state to show: if the LOCAL player is seated in an evac heli with an active
		// extraction, that heli wins unconditionally -- every passenger (signaller or friend) gets the
		// en-route readout straight from their own transport, no spatial query involved. Otherwise fall
		// back to the nearest active heli (inbound/board/departing info for players on the ground).
		ExpansionHelicopterScript seatedHeli = null;
		HumanCommandVehicle hcv = p.GetCommand_Vehicle();
		if ( hcv )
			seatedHeli = ExpansionHelicopterScript.Cast( hcv.GetTransport() );

		ExpansionHelicopterScript h;
		if ( seatedHeli && seatedHeli.Reconquest_GetGuiPhase() > 0 )
			h = seatedHeli;
		else
			h = Reconquest_FindHudHeli( p, p.Reconquest_IsSignaller() );

		if ( h )
		{
			// En-route / arrived info (phase 4+) is for PASSENGERS of this heli only: a player who missed
			// the boarding window must not keep a distance-to-safezone readout while the heli flies away,
			// and bystanders at the safe zone don't need the "disembark" prompt.
			if ( h.Reconquest_GetGuiPhase() >= 4 && h != seatedHeli )
				h = null;
		}

		if ( h )
		{
			// while NOT aboard, also show how far the heli is (inbound approach / landed boarding run)
			int distToHeli = -1;
			if ( h != seatedHeli )
				distToHeli = (int) vector.Distance( h.GetPosition(), p.GetPosition() );
			if ( m_RceHudStatus )
				m_RceHudStatus.SetText( Reconquest_HudText( h.Reconquest_GetGuiPhase(), h.Reconquest_GetGuiCountdown(), distToHeli ) );
			m_RceHud.Show( true );
		}
		else
			m_RceHud.Show( false );
	}

	// Nearest evac heli with an ACTIVE extraction (guiPhase>0); cached, re-scanned ~0.5s. For the CALLER
	// (isSignaller, targeted RPC flag) there is NO proximity cull and the scan reaches the whole network
	// bubble -- the person who fired the signal keeps the readout no matter where they stand; everyone else
	// gets the normal 350m proximity behavior.
	protected ExpansionHelicopterScript Reconquest_FindHudHeli(PlayerBase p, bool isSignaller)
	{
		if ( m_RceHudHeli && m_RceHudHeli.Reconquest_GetGuiPhase() > 0 )
		{
			if ( isSignaller || vector.Distance( m_RceHudHeli.GetPosition(), p.GetPosition() ) < 400.0 )
				return m_RceHudHeli;
		}

		m_RceHudThrottle--;
		if ( m_RceHudThrottle > 0 )
			return null;
		m_RceHudThrottle = 30;

		float scanRadius = 350.0;
		if ( isSignaller )
			scanRadius = 1600.0;   // network-bubble reach; beyond it the client has no entity to read anyway

		array<Object> objs = new array<Object>;
		array<CargoBase> cargos = new array<CargoBase>;
		GetGame().GetObjectsAtPosition3D( p.GetPosition(), scanRadius, objs, cargos );
		foreach ( Object o : objs )
		{
			ExpansionHelicopterScript h = ExpansionHelicopterScript.Cast( o );
			if ( h && h.Reconquest_GetGuiPhase() > 0 )
			{
				m_RceHudHeli = h;
				return h;
			}
		}
		m_RceHudHeli = null;
		return null;
	}

	// distToHeli: metres from the local player to the heli, -1 to omit (player is seated in it)
	protected string Reconquest_HudText(int phase, int cd, int distToHeli)
	{
		string dist = "";
		if ( distToHeli >= 0 )
			dist = " -- " + Reconquest_FmtDist( distToHeli );
		if ( phase == 1 ) return "Helicopter inbound" + dist;
		if ( phase == 2 ) return "Board helicopter (" + cd.ToString() + "s)" + dist;
		if ( phase == 3 ) return "Departing in " + cd.ToString() + "s" + dist;
		if ( phase == 4 ) return "Safe zone: " + Reconquest_FmtDist( cd );
		if ( phase == 5 ) return "Arrived: disembark";
		return "";
	}

	protected void Reconquest_UpdateAdminEditor(PlayerBase p)
	{
		if ( !m_RceAdminCreated )
		{
			m_RceAdminCreated = true;
			m_RceAdminRoot = GetGame().GetWorkspace().CreateWidgets( "ReconquestEvac/gui/reconquest_evac_admin.layout" );
			if ( m_RceAdminRoot )
			{
				m_RceAdminTitle  = TextWidget.Cast( m_RceAdminRoot.FindAnyWidget( "rce_admin_title" ) );
				m_RceAdminStatus = TextWidget.Cast( m_RceAdminRoot.FindAnyWidget( "rce_admin_status" ) );
				if ( m_RceAdminTitle )
					m_RceAdminTitle.SetText( "EVAC LANDING PATH" );
				m_RceAdminRoot.Show( false );
			}
		}
		if ( !m_RceAdminRoot )
			return;

		if ( !m_RceAdminOpen )
		{
			m_RceAdminRoot.Show( false );
			return;
		}

		if ( !m_RceAdminPath )
			m_RceAdminPath = new array<vector>;
		vector pos = p.GetPosition();
		pos[1] = GetGame().SurfaceY( pos[0], pos[2] );
		float hullShown = m_RceLocalHullHP;
		if ( hullShown <= 0 )
			hullShown = p.Reconquest_GetAdminHullHP();
		string text = "F7 close | F8 add current | F9 save | F10 clear | F11 heli HP";
		text = text + "\nHeli hull HP: " + Math.Round( hullShown ).ToString();
		text = text + "\nCurrent: " + pos.ToString();
		text = text + "\nPoints: " + m_RceAdminPath.Count().ToString();
		if ( m_RceAdminPath.Count() > 0 )
			text = text + "\nLast: " + m_RceAdminPath.Get( m_RceAdminPath.Count() - 1 ).ToString();
		if ( m_RceAdminMessage != "" )
			text = text + "\n" + m_RceAdminMessage;
		if ( m_RceAdminStatus )
			m_RceAdminStatus.SetText( text );
		m_RceAdminRoot.Show( true );
	}

	protected void Reconquest_SendAdminPath()
	{
		if ( !m_RceAdminPath || m_RceAdminPath.Count() == 0 )
		{
			m_RceAdminMessage = "Add at least one path point first.";
			return;
		}

		ScriptRPC rpc = new ScriptRPC();
		rpc.Write( m_RceAdminPath.Count() );
		foreach ( vector p : m_RceAdminPath )
			rpc.Write( p );
		rpc.Send( null, ReconquestEvacRPC.ADMIN_SAVE_PATH, true );
		m_RceAdminMessage = "Saving path to server...";
	}

	// cd carries the metres-to-safe-zone during flight (phase 4). Show km with one decimal past 1000 m.
	protected string Reconquest_FmtDist(int metres)
	{
		if ( metres >= 1000 )
		{
			int km     = metres / 1000;
			int tenths = ( metres % 1000 ) / 100;
			return km.ToString() + "." + tenths.ToString() + " km";
		}
		return metres.ToString() + " m";
	}

	protected bool Reconquest_HeliValid(ExpansionHelicopterScript h, PlayerBase p)
	{
		if ( !h || !h.Reconquest_IsAutopilot() )   // not (or no longer) an autopilot evac heli -> stop smoothing
			return false;
		vector d = h.GetPosition() - p.GetPosition();
		return d.Length() < 600.0;
	}

	protected float Reconquest_WrapDeg(float a)
	{
		while ( a > 180.0 ) a -= 360.0;
		while ( a < -180.0 ) a += 360.0;
		return a;
	}

}

modded class MissionServer
{
	override void OnInit()
	{
		super.OnInit();
		DayZGame dz = DayZGame.Cast( GetGame() );
		if ( dz )
		{
			dz.Event_OnRPC.Insert( Reconquest_OnAdminSavePath );
			dz.Event_OnRPC.Insert( Reconquest_OnAdminSetHull );
		}
		// first-boot convenience: create all profile config files with documented templates
		ReconquestEvacManager.GetInstance().Reconquest_EnsureProfileFiles();
	}

	// Admin (F11 in the F7 editor) heli hull HP change: allowlist-authorized server-side, applied live,
	// persisted to the settings file. Ack reuses the editor's existing Saved/Denied message channel.
	void Reconquest_OnAdminSetHull(PlayerIdentity sender, Object target, int rpc_type, ParamsReadContext ctx)
	{
		if ( rpc_type != ReconquestEvacRPC.ADMIN_SET_HULL )
			return;
		if ( !GetGame() || !GetGame().IsServer() )
			return;

		float hp;
		string result;
		bool ok = false;
		if ( ctx.Read( hp ) )
			ok = ReconquestEvacManager.GetInstance().AdminSetHullHP( sender, hp, result );
		else
			result = "Malformed hull HP request.";

		ScriptRPC ack = new ScriptRPC();
		ack.Write( ok );
		ack.Write( result );
		ack.Send( null, ReconquestEvacRPC.ADMIN_PATH_ACK, true, sender );
	}

	// Fires on EVERY character hand-over to a client (new character AND client-ready), so the admin flag
	// reaches each new PlayerBase entity. Sent as a TARGETED RPC to the owning client only -- a net-synced
	// bool would broadcast every player's admin status to all clients in range (ESP-readable admin leak).
	// Vanilla invokes this on both the new-character and ready paths, so the second send covers any race
	// on the first. The flag only gates the client-side F7 editor UI -- saving the path is still authorized
	// server-side against the allowlist on every RPC.
	override void InvokeOnConnect(PlayerBase player, PlayerIdentity identity)
	{
		super.InvokeOnConnect( player, identity );
		if ( player && identity )
		{
			ScriptRPC rpc = new ScriptRPC();
			rpc.Write( ReconquestEvacManager.GetInstance().IsAdminIdentity( identity ) );
			rpc.Write( ReconquestEvacManager.GetInstance().Reconquest_GetHullHP() );   // shown in the F7 editor
			rpc.Send( player, ReconquestEvacRPC.ADMIN_FLAG, true, identity );
		}
	}

	void Reconquest_OnAdminSavePath(PlayerIdentity sender, Object target, int rpc_type, ParamsReadContext ctx)
	{
		if ( rpc_type != ReconquestEvacRPC.ADMIN_SAVE_PATH )
			return;
		if ( !GetGame() || !GetGame().IsServer() )
			return;

		int count;
		array<vector> points = new array<vector>;
		string result;
		bool ok = false;

		if ( ctx.Read( count ) && count > 0 && count <= 32 )
		{
			for ( int i = 0; i < count; i++ )
			{
				vector p;
				if ( ctx.Read( p ) )
					points.Insert( p );
			}

			if ( points.Count() == count )
				ok = ReconquestEvacManager.GetInstance().AdminSaveSafeZonePath( sender, points, result );
			else
				result = "Path RPC was incomplete.";
		}
		else
			result = "Invalid path point count.";

		ScriptRPC ack = new ScriptRPC();
		ack.Write( ok );
		ack.Write( result );
		ack.Send( null, ReconquestEvacRPC.ADMIN_PATH_ACK, true, sender );
	}
}
