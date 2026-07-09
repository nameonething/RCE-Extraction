class CfgPatches
{
	class ReconquestEvac_Scripts
	{
		units[] = {};
		weapons[] = { "ReconquestEvacSignalPistol" };
		requiredVersion = 0.1;
		// Everything this mod actually stands on (addon names verified from the installed PBOs):
		//  - DZ_Data: vanilla base (Flaregun / flare ammo classes we inherit from)
		//  - DayZExpansion_Core_Scripts + DayZExpansion_Vehicles_Scripts: the ExpansionHelicopterScript
		//    script class this mod extends
		//  - DayZExpansion_Vehicles_Air_Uh1h (in @DayZ-Expansion-Licensed): the ExpansionUh1h airframe the
		//    manager spawns. Declaring it makes a missing Expansion install a hard server-start error
		//    instead of a silent "FAILED to spawn" at the first extraction.
		requiredAddons[] =
		{
			"DZ_Data",
			"DayZExpansion_Core_Scripts",
			"DayZExpansion_Vehicles_Scripts",
			"DayZExpansion_Vehicles_Air_Uh1h"
		};
	};
};

class CfgMods
{
	class ReconquestEvac
	{
		dir = "ReconquestEvac";
		picture = "";
		action = "";
		hideName = 0;
		hidePicture = 0;
		name = "RCE EXTRACTION";
		credits = "RCE";
		author = "RCE";
		authorID = "";
		version = "1.0";
		extra = 0;
		// NOTE: "mod" (not "servermod") so clients load it too -- the flare is a
		// physical item players hold and render.
		type = "mod";
		dependencies[] = { "Game", "World", "Mission" };

		class defs
		{
			class gameScriptModule
			{
				value = "";
				files[] = { "ReconquestEvac/scripts/3_Game" };
			};
			class worldScriptModule
			{
				value = "";
				files[] = { "ReconquestEvac/scripts/4_World" };
			};
			class missionScriptModule
			{
				value = "";
				files[] = { "ReconquestEvac/scripts/5_Mission" };
			};
		};
	};
};

// GetCartridgeAtIndex / "cannot eject chambered cartridge" in the chamber FSM) traced to cartridges whose
// type was a modded, config-only CfgAmmo class: the engine's native cartridge storage/lookup does not
// handle it reliably. The signal round therefore stores a BONE-STOCK vanilla projectile
// (Bullet_FlareGreen -- a green flare, visually distinct from ordinary red flares). Exclusivity lives
// entirely on the PILE class below: loading is gated per-magazine-class, so vanilla flare guns can't
// chamber our round and our pistol can't chamber vanilla flares.

class CfgMagazines
{
	class Ammo_FlareGreen;	// forward-declare the vanilla green-flare cartridge pile

	// The ONLY round the signal pistol accepts -- a REAL item players obtain and load themselves (trader /
	// loot / alongside the pistol). Vanilla flare guns cannot chamber it (their chamberableFrom doesn't
	// list it) and the signal pistol cannot chamber vanilla flares. Inherits the green pile's model +
	// vanilla Bullet_FlareGreen cartridge; fired, it burns GREEN.
	class ReconquestEvac_Ammo_Flare: Ammo_FlareGreen
	{
		scope = 2;
		displayName = "Extraction Signal Round";
		descriptionShort = "A dedicated signal cartridge for the Extraction Signal Pistol. Fires a green flare. Ordinary flare guns cannot chamber it.";
		count = 1;		// a pile holds a single cartridge
	};
};

class CfgWeapons
{
	class Flaregun;		// forward-declare the vanilla flare gun we inherit from

	// Single-use extraction signal pistol. Spawns EMPTY like a vanilla flare gun; the player loads a
	// dedicated Extraction Signal Round and fires to call the helicopter -- a successful call RUINS the
	// pistol (single use), a rejected call refunds the round as an item (ReconquestEvacSignalPistol.c).
	// chamberableFrom REPLACES the vanilla flare list, so ordinary flare rounds (Ammo_Flare*) do NOT fit;
	// the engine also uses this list to replicate the chambered round to clients.
	class ReconquestEvacSignalPistol: Flaregun
	{
		scope = 2;		// 2 = spawnable (console / central economy / trader)
		displayName = "Extraction Signal Pistol";
		descriptionShort = "Load an Extraction Signal Round and fire to call a helicopter extraction. Single use -- ruins when the call succeeds.";
		chamberableFrom[] = { "ReconquestEvac_Ammo_Flare" };
	};
};
