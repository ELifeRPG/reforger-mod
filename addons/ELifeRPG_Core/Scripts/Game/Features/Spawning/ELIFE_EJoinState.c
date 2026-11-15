//------------------------------------------------------------------------------------------------
//! Server-side join state of one player (spec 5.2).
enum ELIFE_EJoinState
{
	NONE,
	SESSION_PENDING,
	REJECTED,
	CREATING,
	SELECTING,
	LOADING_CHARACTER,
	SPAWNING,
	IN_GAME,
	ENDED,
	//! Player is not linked to an account yet; waits on the link screen until a recheck (spec 5.2).
	LINKING,
	//! Character confirmed; the player picks a spawn point on the spawn selection screen (mode SELECT_PER_RESTART).
	SELECTING_SPAWN
}
