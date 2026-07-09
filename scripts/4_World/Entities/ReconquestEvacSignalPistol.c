// Single-use extraction signal pistol -- the production trigger for helicopter extractions.
//
// Works like a vanilla flare gun: it spawns EMPTY and the player hand-loads the dedicated
// ReconquestEvac_Ammo_Flare round (the only ammo the config accepts -- vanilla flares don't fit, and this
// round doesn't fit vanilla flare guns). The round is a separate item, hand-loaded rather than
// pre-chambered on spawn: the engine does not reliably replicate a script-chambered round to clients
// (the pistol renders empty client-side and dry-fires with no visible flare). Client-initiated hand-loading
// is the path the engine replicates correctly.
//
// Firing:
//   - extraction STARTED  -> the pistol is RUINED (single use; a ruined weapon can't fire or be repaired)
//   - extraction REJECTED (one already in progress / no heli available) -> the spent round is REFUNDED as
//     a fresh ReconquestEvac_Ammo_Flare in the shooter's inventory (at their feet if full), so a blocked
//     signal is never wasted.
class ReconquestEvacSignalPistol : Flaregun
{
	static const string SIGNAL_ROUND = "ReconquestEvac_Ammo_Flare";   // CfgMagazines class of our round

	// Admin/debug spawn (COT's spawner calls OnDebugSpawn): drop ONE signal round beside the pistol --
	// NOT vanilla Flaregun's behavior of a chambered flare + three Ammo_Flare piles this pistol can't use.
	override void OnDebugSpawn()
	{
		SpawnEntityOnGroundPos( SIGNAL_ROUND, GetPosition() );
	}

	override void EEFired(int muzzleType, int mode, string ammoType)
	{
		super.EEFired( muzzleType, mode, ammoType );

		if ( !GetGame() || !GetGame().IsServer() )
			return;

		PlayerBase signaller = PlayerBase.Cast( GetHierarchyRootPlayer() );
		if ( !signaller )
			return;   // only a player-fired signal calls extraction (not e.g. a rigged ground discharge)

		vector signalPos = signaller.GetPosition();
		Print( "[ReconquestEvac] signal pistol fired at " + signalPos.ToString() + " | signaller=" + signaller );
		if ( ReconquestEvacManager.GetInstance().RequestExtraction( signalPos, signaller ) )
		{
			// single use: ruin the pistol so it can never call another extraction
			SetHealth( "", "", 0.0 );
		}
		else
		{
			// rejected: refund the round as a normal item. The manager already told the shooter WHY (popup);
			// here we only make sure the round can never be silently lost:
			//   1. inventory (normal case)
			//   2. inventory FULL -> drop at the shooter's feet AND tell them, so they don't walk away from it
			//   3. even the ground spawn failed (should be impossible) -> log loudly for the admin
			EntityAI refund = signaller.GetInventory().CreateInInventory( SIGNAL_ROUND );
			if ( !refund )
			{
				Object dropped = GetGame().CreateObjectEx( SIGNAL_ROUND, signaller.GetPosition() + "0 0.3 0", ECE_PLACE_ON_SURFACE );
				if ( dropped )
				{
					Reconquest_NotifyShooter( signaller, "Inventory full -- your signal round was dropped at your feet." );
					Print( "[ReconquestEvac] refund: signaller inventory full, round dropped at " + dropped.GetPosition().ToString() );
				}
				else
				{
					Reconquest_NotifyShooter( signaller, "Signal round refund FAILED -- contact an admin." );
					string shooterName = "?";
					if ( signaller.GetIdentity() )
						shooterName = signaller.GetIdentity().GetName();
					Print( "[ReconquestEvac] ERROR: signal round refund failed entirely for '" + shooterName + "' at " + signaller.GetPosition().ToString() );
				}
			}
		}
	}

	// Targeted popup to the shooter (identity-addressed; nobody else sees it).
	protected void Reconquest_NotifyShooter(PlayerBase p, string msg)
	{
		if ( !p || !p.GetIdentity() )
			return;
		ExpansionNotification( "EXTRACTION", msg, "Helicopter", COLOR_EXPANSION_NOTIFICATION_ORANGE, 5 ).Create( p.GetIdentity() );
	}
};
