// Phase-1 boarding diagnostic + evac-driver-seat guard. The guard always blocks players from taking seat 0 on
// script-flown evac helis; optional debug logging records why each passenger seat does or doesn't let you board.
// Gated by ReconquestEvacManager.s_BoardDebug (set false for production). Throttled to one line per component
// per change so it doesn't spam at 60 Hz while you look at the heli.
//
// Read the result in the SERVER RPT first (C:\DayZLocal\profiles\script*.log), then the client RPT.
//   crew = -1            -> that door/component maps to NO seat (config/model issue)
//   canBoard = 0 (false) -> the get-in is being refused (occupied / out of door-reach tolerance / etc.)
//   occupied = 1         -> a crew member is already in that seat
modded class ActionGetInTransport
{
	static ref map<int, bool> s_RceLastLogged = new map<int, bool>;

	override bool ActionCondition( PlayerBase player, ActionTarget target, ItemBase item )
	{
		bool ok = super.ActionCondition( player, target, item );
		ExpansionHelicopterScript heli = null;
		int comp = -1;
		int crew = -1;

		if ( target )
		{
			heli = ExpansionHelicopterScript.Cast( target.GetObject() );
			if ( heli )
			{
				comp = target.GetComponentIndex();
				crew = heli.CrewPositionIndex( comp );
				if ( ok && heli.Reconquest_IsAutopilot() && crew == 0 )
					ok = false;
			}
		}

		if ( ReconquestEvacManager.s_BoardDebug && heli )
		{
				if ( !s_RceLastLogged.Contains( comp ) || s_RceLastLogged.Get( comp ) != ok )
				{
					s_RceLastLogged.Set( comp, ok );
					bool occ = false;
					if ( crew >= 0 && crew < heli.CrewSize() )
						occ = ( heli.CrewMember( crew ) != null );
					Print( "[RCE][BOARD] comp=" + comp.ToString() + " -> crew=" + crew.ToString() + " occupied=" + occ.ToString() + " canBoard=" + ok.ToString() );
				}
		}

		return ok;
	}
}
