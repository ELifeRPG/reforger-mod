//------------------------------------------------------------------------------------------------
//! Paging shared by the join card screens (spawn points and characters): CARDS_PER_PAGE cards per page.
class ELIFE_JoinPaging
{
	static const int CARDS_PER_PAGE = 6;

	//------------------------------------------------------------------------------------------------
	//! Number of pages for count cards: 0 for none, otherwise ceil(count / perPage). perPage <= 0 uses CARDS_PER_PAGE.
	static int PageCount(int count, int perPage = 6)
	{
		if (perPage <= 0)
			perPage = CARDS_PER_PAGE;

		if (count <= 0)
			return 0;

		return (count + perPage - 1) / perPage;
	}

	//------------------------------------------------------------------------------------------------
	//! Index of the first card on a page (0-based page). perPage <= 0 uses CARDS_PER_PAGE.
	static int PageStart(int page, int perPage = 6)
	{
		if (perPage <= 0)
			perPage = CARDS_PER_PAGE;

		return page * perPage;
	}
}
