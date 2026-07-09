// ReconquestEvac player hooks: cosmetic-pilot behavior (pose/collision/loot protection) and the two
// targeted client flags (landing-path admin, extraction signaller).
modded class PlayerBase
{
	// Net-synced: this survivor is a scripted cosmetic pilot (not a real player). Used so every machine
	// strips its collision layer and the heli's IsIgnoredObject can recognise it before crew/command sync.
	protected bool m_RceCosmeticPilot;
	// This player is on the landing-path admin allowlist ($profile:rce_evac_admins.txt). Delivered by a
	// TARGETED RPC to the OWNING client only (ReconquestEvacMission.c InvokeOnConnect) -- deliberately NOT
	// a net-synced variable, which would broadcast every player's admin status to all clients in range
	// (an ESP-readable admin-identity leak). Gates the F7 path editor UI only; the SAVE is still
	// authorized server-side against the allowlist on every RPC.
	protected bool m_RceIsPathAdmin;

	void PlayerBase()
	{
		RegisterNetSyncVariableBool( "m_RceCosmeticPilot" );
	}

	// Current server heli hull HP, delivered with the admin flag so the F7 editor can display it.
	protected float m_RceAdminHullHP;

	bool Reconquest_IsPathAdmin()
	{
		return m_RceIsPathAdmin;
	}

	float Reconquest_GetAdminHullHP()
	{
		return m_RceAdminHullHP;
	}

	// This client CALLED the currently active extraction (targeted RPC from the manager at extraction
	// start, cleared at teardown). The HUD uses it to track the extraction at any range -- the caller must
	// never lose the readout just because the heli is beyond the 350m proximity scan.
	protected bool m_RceIsSignaller;

	bool Reconquest_IsSignaller()
	{
		return m_RceIsSignaller;
	}

	// Receives the server's targeted admin-flag RPC (sent only to the owning client at connect/ready).
	override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
	{
		super.OnRPC( sender, rpc_type, ctx );

		if ( rpc_type == ReconquestEvacRPC.ADMIN_FLAG && GetGame().IsClient() )
		{
			bool adminOn;
			if ( ctx.Read( adminOn ) )
				m_RceIsPathAdmin = adminOn;
			float hullHP;
			if ( ctx.Read( hullHP ) )
				m_RceAdminHullHP = hullHP;
		}
		else if ( rpc_type == ReconquestEvacRPC.SIGNALLER_FLAG && GetGame().IsClient() )
		{
			bool signallerOn;
			if ( ctx.Read( signallerOn ) )
				m_RceIsSignaller = signallerOn;
		}
	}

	// The cosmetic pilot must be unlootable. It already can't be killed or knocked out (god mode), so
	// there is never a lootable corpse; hiding the inventory removes it from every machine's vicinity UI
	// (this hook's sole vanilla caller is the client GUI). The CanRelease* overrides below are the actual
	// SERVER-side boundary. Net-synced flag -> works on every machine.
	override bool IsInventoryVisible()
	{
		if ( m_RceCosmeticPilot )
			return false;
		return super.IsInventoryVisible();
	}

	// SERVER-ENFORCED loot block: the engine consults these hooks when validating raw inventory
	// moves / hand events server-side, so even a hacked client that skips the vicinity UI cannot
	// strip the cosmetic pilot's kit.
	override bool CanReleaseAttachment( EntityAI attachment )
	{
		if ( m_RceCosmeticPilot )
			return false;
		return super.CanReleaseAttachment( attachment );
	}

	override bool CanReleaseCargo( EntityAI cargo )
	{
		if ( m_RceCosmeticPilot )
			return false;
		return super.CanReleaseCargo( cargo );
	}

	void Reconquest_SetCosmeticPilot( bool on )
	{
		m_RceCosmeticPilot = on;
		SetSynchDirty();
	}

	bool Reconquest_IsCosmeticPilot()
	{
		return m_RceCosmeticPilot;
	}

	override void OnCommandHandlerTick(float delta_time, int pCurrentCommandID)
	{
		super.OnCommandHandlerTick( delta_time, pCurrentCommandID );

		// Cosmetic pilot: keep NOCOLLISION on SERVER and CLIENT (StartCommand_Vehicle can rewrite it), and keep the
		// body/head in a neutral vehicle pose. A controllerless survivor can keep stale freelook/raise/movement
		// state, which is what makes it stare sideways or hold its hands too high for the helicopter controls.
		// These input-controller overrides are local per-machine (like the collision layer), but the head/aim pose
		// is server-authoritative and net-synced, so applying them here (this runs server-side too) reaches clients.
		if ( m_RceCosmeticPilot )
		{
			dBodySetInteractionLayer( this, PhxInteractionLayers.NOCOLLISION );
			HumanInputController hic = GetInputController();
			if ( hic )
			{
				hic.OverrideMovementSpeed( HumanInputControllerOverrideType.ENABLED, 0.0 );
				hic.OverrideMovementAngle( HumanInputControllerOverrideType.ENABLED, 0.0 );
				hic.OverrideFreeLook( HumanInputControllerOverrideType.ENABLED, false );
				hic.OverrideAimChangeX( HumanInputControllerOverrideType.ENABLED, 0.0 );
				hic.OverrideAimChangeY( HumanInputControllerOverrideType.ENABLED, 0.0 );
				hic.OverrideRaise( HumanInputControllerOverrideType.ENABLED, false );
			}
		}
	}
}
