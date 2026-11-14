//------------------------------------------------------------------------------------------------
//! Spawn point mode (spec 5.x): which spawn selection a point belongs to.
enum ELIFE_ESpawnMode
{
	//! Mode 1 (default): the player is placed at a random point of this mode on the first spawn of a session.
	RANDOM_FIRST_SPAWN,
	//! Mode 2: the player picks a point of this mode from the spawn selection on every restart.
	SELECT_PER_RESTART
}
