//------------------------------------------------------------------------------------------------
//! The map opened from the phone (ELIFE_MapMenuUI). M returns to the phone, Esc and the phone key put it away.
class ELIFE_PhoneMapMenuUI : ELIFE_MapMenuUI
{
	//------------------------------------------------------------------------------------------------
	override bool IsFromPhone()
	{
		return true;
	}
}
