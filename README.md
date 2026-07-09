# RCE Extraction

Server-side helicopter extraction for DayZ. A player fires the **Extraction Signal Pistol**; a script-flown
(Expansion UH-1H) helicopter flies in, lands at the nearest safe clearing, waits for the signaller to board,
then delivers everyone aboard to the safe-zone helipad and departs. The flight is a
server-side velocity autopilot with obstacle avoidance, plus a cosmetic (invulnerable, unlootable) survivor
posed in the cockpit.

Licensed under the MIT License (see LICENSE).

---

## Player-facing behavior

### The trigger: Extraction Signal Pistol (`ReconquestEvacSignalPistol`)

- Based on the vanilla flare gun, but it **only chambers its own round** — the **Extraction Signal Round**
  (`ReconquestEvac_Ammo_Flare`), a separate item that fires a **green** flare. Ordinary flares don't fit
  the pistol, and the signal round doesn't fit ordinary flare guns. (The round deliberately carries the
  vanilla `Bullet_FlareGreen` projectile.)
- **Spawns empty; the player loads it** like any flare gun. Distribute both items via your trader/loot
  economy — classnames `ReconquestEvacSignalPistol` and `ReconquestEvac_Ammo_Flare` (one cartridge per pile).
- **Single use.** When a fired signal successfully starts an extraction, the pistol is ruined.
  If the signal is **rejected** (an extraction is already running, or no heli could be dispatched), the
  round is **refunded as an item** into the shooter's inventory (at their feet if full) — the shot is
  never wasted.
- Admin/debug spawning the pistol (COT) drops one signal round beside it instead of vanilla's three
  useless flare piles.
- Only one extraction runs at a time server-wide. A second signal while one is active gets
  *"An extraction is already in progress"* and does **not** interrupt the active one.

### The flight cycle

1. **Inbound** — heli spawns ~600 m out (toward map center) and flies to the landing zone.
   The LZ is the nearest flat, clear, dry spot within 400 m of the signal (see *Collision avoidance*).
   The heli lands on **true ground only** — rooftops and covered positions never qualify (every candidate
   is verified with a downward geometry ray, since terrain height alone doesn't know buildings exist).
   If no landable ground exists within 400 m (e.g. deep inside a dense city block), the signal is
   **refused** and the round refunded — the shooter is told to move to open ground.
2. **Board** — heli lands, rotors spin down, and it waits up to ~120 s.
   Only the **signaller** boarding starts the departure countdown (~15 s). Friends can board and ride
   along, but a stranger squatting in the heli cannot launch — or steal — someone else's evac.
   If the signaller dies or disconnects while the heli waits, the extraction cancels; anyone else seated
   is set down at the LZ first.
3. **En route** — flies the outbound leg (optionally along an admin-defined approach path) to the safe zone.
4. **Drop-off** — lands on the safe-zone pad, disembarks everyone, and departs. Passengers are guaranteed
   to end up on the ground (despawn-drop backstop) — nobody gets carried away or left floating.

**Shot down:** the airframe is destructible in every phase — an extraction can be contested. Because
vehicle hulls natively absorb small-arms fire almost completely, evac helis carry an explicit hull pool
(**`HullHP` in `$profile:rce_evac_settings.txt`**, default 35,000 — no rebuild needed): every firearm or
explosion hit chips it (at least 40 per bullet even when armor absorbs the hit), and when it's spent the
heli is destroyed and explodes. The pool is deliberately
huge — downing an evac heli is a coordinated effort (hundreds of rounds / many explosives), not something
one rifleman does on a whim. The autopilot then releases
the wreck where it died (whoever is aboard is left to the crash), a server-wide message announces *"An
extraction helicopter has been shot down near \<location\>!"* (nearest named map location), and the
extraction slot frees immediately for the next signal. Only the cosmetic pilot is invulnerable — the
helicopter is not. Every hit is logged to the server RPT (`[ReconquestEvac] evac heli hit ...`).

### Status messages & HUD

- The HUD is the **only** announcer of the normal phase flow (inbound / board / departing / en route /
  arrived) — nothing duplicates it. Exceptional events the HUD can't show — signal rejected (extraction
  already running), dispatch failure, no-clear-LZ recall, nobody boarded, extraction cancelled — appear as
  **targeted Expansion notification popups** for the affected player only. The
  server-wide **shot-down announcement** is an Expansion popup for every player.
- The **caller always sees the HUD**: the server flags the signaller's client at extraction start, so their
  readout tracks the heli at any range (network-bubble limited) instead of the 350 m proximity scan.
  While not aboard, the inbound/board/departing lines also append the **distance to the helicopter**
  (e.g. *Board helicopter (87s) — 220 m*).
- The HUD (top-center) shows the current phase to anyone near the heli:
  - *Helicopter inbound / Board helicopter (Xs) / Departing in Xs* — visible to players near the heli.
  - *Safe zone: X.X km / Arrived: disembark* — visible **only to players seated in that heli**
    (every passenger, not just the signaller). Missing the flight means the readout disappears with it.

### The cosmetic pilot

A vanilla survivor posed in the pilot seat, purely visual — the autopilot flies the heli.

- **God mode** (`SetAllowDamage(false)`): can't be killed or knocked out, so there's never a lootable corpse.
- **Unlootable**: his inventory is hidden from every vicinity/inventory UI via a net-synced flag
  (`PlayerBase.IsInventoryVisible` override), closing mod-tool and edge-case loot paths.
- Never occupies the crew slot (seat 0 stays logically free), so he can't block boarding or door actions.

---

## Collision avoidance

The autopilot continuously scans terrain + objects ahead (within a descent band below ~84 m AGL) and holds
clearance margins (16 m over obstacles, 6 m over bare terrain). Landing-zone selection and the final
touchdown column re-check for hazards and redirect if blocked — **except at the configured safe-zone pad**:
the outbound drop-off deliberately touches down on the exact point (see *Safe zone*) and does **not**
re-check the pad or redirect, so keep that pad clear. If an inbound landing column is blocked and no safe
redirect exists (e.g. the signal was fired inside a walled base), the heli holds for ~20 s, then recalls
itself and tells the signaller to move to open ground — an extraction can never hover-lock the server.

**What counts as a hazard** (`Reconquest_IsLandingHazard`):

| Detection | Covers |
|---|---|
| Engine flags (`IsTree/IsRock/IsBuilding`) | Trees, rocks, all static map buildings |
| **Path sphere-casts** | Geometry the object scans miss — very large buildings (grain elevators, industrials) whose object center sits outside the sample radius. Two 8 m-radius sphere-casts (≈ rotor disc): one level along the heading (walls at altitude) and one along the actual motion vector (roofs below, on the descending glide path). Blocked → probe upward for clear air and climb over. A 20 s no-progress watchdog additionally releases + cancels if the airframe ever wedges anyway. |
| **Touchdown column ray** | A downward ray over every LZ candidate and the final touchdown footprint — rooftops/covered spots are never treated as ground, no matter how large or oddly-centered the building. |
| `Transport` cast | **Parked cars, helicopters, boats** — on the signal LZ or under the approach |
| `BaseBuildingBase` / `TentBase` cast | Vanilla fences & watchtowers, tents, and any mod inheriting them |
| Name-fragment list | Fences/walls/gates/towers/antennas/poles… plus base-building mods that use their own base classes: `bbp` (BaseBuildingPlus — verified, all `BBP_*`), `basebuild`+`compound` (RA Base Building / RearmED — verified, `BaseBuilding_*` and `CompoundWall/Gate`), `floor`, `roof`, `foundation`, `platform`, `ramp`, `stair`, `shelter` |

**Extending without a rebuild:** put one lowercase name fragment per line in
`$profile:rce_evac_hazards.txt` (`#` starts a comment). Any object whose classname or debug name contains a
fragment is treated as a hazard by the LZ search, the touchdown check, and the in-flight canopy scan.
(None of the touchdown checks apply at the configured safe-zone pad — see above.)
Use this to cover additional base-building or furniture mods on your server.

> Note: the fragment match is a substring match — keep fragments specific enough not to catch loot items
> (e.g. prefer a mod's classname prefix like `bbp` over generic words). Fragments under 3 characters are
> ignored, duplicates are skipped, and at most 64 extra fragments load (warnings go to the RPT) — a wrong
> file can't degrade the flight scans for the whole session.

---

## Admin setup

### Allowlist — `$profile:rce_evac_admins.txt`

Auto-created as a commented template on first boot. One **Steam64 ID** per line (`#` starts a comment).
Matching is **exact** — player names are deliberately not accepted (names are client-chosen and spoofable).

```
# Reconquest Evac landing-path editor admins -- ONE Steam64 ID per line (exact match; names are not accepted).
# Example (remove the leading # to activate):
# 76561198000000000
```

Ships with **no active admin** — add your own Steam64 ID on an uncommented line.

The list is read live on every save, and once per connect to flag admin clients for the editor UI.

### Landing-path editor (in game, admins only)

Admins can record the exact approach path + touchdown the outbound leg flies at the safe zone.
Non-admins see nothing — the keybinds are inert for them (the server flags admin clients at connect;
saving is additionally re-authorized server-side on every RPC).

| Key | Action |
|---|---|
| **F7** | Toggle the path editor overlay |
| **F8** | Add your current position as a path point (add the touchdown point **last**) |
| **F9** | Save the path to the server |
| **F10** | Clear the draft |
| **F11** | Cycle heli hull HP (5k → 10k → 15k → 20k → 25k → 35k → 50k → 75k → 100k) — applies live, persists to the settings file |

Saved to `$profile:rce_evac_landing_path.txt` (one `x y z` per line, `#` comments; intermediate lines are
fly-through nodes, the last line is the touchdown). The file can also be edited by hand — it re-loads on
each extraction's departure.

### Other profile files

All config files are **auto-created with documented templates on the first server boot** — just open the
profile folder and edit; no file needs to be hand-made.

| File | Purpose |
|---|---|
| `rce_evac_settings.txt` | Tunables (`HullHP` — damage to shoot the heli down, default 35000). Edit + restart, or change live in game with **F11** in the admin editor. |
| `rce_evac_admins.txt` | Landing-path admin allowlist (Steam64 IDs, one per line) |
| `rce_evac_landing_path.txt` | Safe-zone approach path + touchdown override (created by the F7 editor) |
| `rce_evac_hazards.txt` | Extra landing-hazard name fragments (optional) |

### Safe zone

The default drop-off pad is `ReconquestEvacManager.SAFE_ZONE_POS` (currently
`8229.85 469.345 9028.72`), used only when no path file exists. The last point of
`$profile:rce_evac_landing_path.txt` overrides the touchdown outright, so the pad can be moved anywhere
without a rebuild. Changing the map still means editing constants and rebuilding (the inbound/depart
map-center direction and the default pad are Chernarus constants in the manager).

---

## Dependencies & load order

Declared in `CfgPatches.requiredAddons` (verified against the installed PBOs):

- `DZ_Data` — vanilla base (Flaregun / flare ammo classes)
- `DayZExpansion_Core_Scripts`, `DayZExpansion_Vehicles_Scripts` — the `ExpansionHelicopterScript` class
  this mod extends
- `DayZExpansion_Vehicles_Air_Uh1h` — the `ExpansionUh1h` airframe (ships in **@DayZ-Expansion-Licensed**,
  not @DayZ-Expansion-Vehicles)

Server `-mod` order: CF, DabsFramework, Expansion (Animations, Core, Vehicles, Licensed), then this mod
(`@RCE EXTRACTION` when installed from the workshop). Expansion-AI is **not** required.

## Known limitations

- One extraction at a time, server-wide (by design for now; new signals are rejected, not queued).
- The safe-zone position and inbound map-center direction are Chernarus constants in the manager.
- Client-perceived cruise smoothness sits at the netcode ceiling for a driverless server-flown vehicle.
- Production play writes no log files beyond the normal server RPT.
