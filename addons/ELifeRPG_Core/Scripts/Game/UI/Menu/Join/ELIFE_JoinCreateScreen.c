//------------------------------------------------------------------------------------------------
//! Create screen (spec 5.5): first and last name, an appearance picker with a live preview, and Create. Back shows only
//! when the server allows it (canGoBack). The server validates the names; the client only caps them at NAME_MAX.
class ELIFE_JoinCreateScreen
{
	protected const int NAME_MAX = 20;
	protected const string ERROR_KEY_PREFIX = "ELIFE-Join_";

	protected Widget m_wRoot;
	protected Widget m_wFirstName;
	protected Widget m_wLastName;
	protected Widget m_wAppearance;
	protected Widget m_wBack;
	protected Widget m_wPreview;
	protected Widget m_wErrorRow;
	protected TextWidget m_wError;
	protected SCR_EditBoxComponent m_FirstName;
	protected SCR_EditBoxComponent m_LastName;
	protected SCR_SpinBoxComponent m_Appearance;
	protected SCR_ButtonTextComponent m_BackButton;
	protected SCR_ButtonTextComponent m_CreateButton;

	protected ELIFE_PlayerController m_Controller;
	protected bool m_bBusy;
	protected int m_iAppearance;

	//------------------------------------------------------------------------------------------------
	//! Binds the widgets of an already created JoinCreate layout.
	void Bind(Widget root)
	{
		m_wRoot = root;
		m_wFirstName = m_wRoot.FindAnyWidget("FirstName");
		m_wLastName = m_wRoot.FindAnyWidget("LastName");
		m_wAppearance = m_wRoot.FindAnyWidget("Appearance");
		m_wBack = m_wRoot.FindAnyWidget("ButtonBack");
		m_wPreview = m_wRoot.FindAnyWidget("Preview");
		m_wErrorRow = m_wRoot.FindAnyWidget("CreateErrorRow");
		m_wError = TextWidget.Cast(m_wRoot.FindAnyWidget("CreateError"));

		m_FirstName = SCR_EditBoxComponent.GetEditBoxComponent("FirstName", m_wRoot);
		if (m_FirstName)
			m_FirstName.m_OnChanged.Insert(OnNameChanged);

		m_LastName = SCR_EditBoxComponent.GetEditBoxComponent("LastName", m_wRoot);
		if (m_LastName)
			m_LastName.m_OnChanged.Insert(OnNameChanged);

		m_Appearance = SCR_SpinBoxComponent.GetSpinBoxComponent("Appearance", m_wRoot);
		if (m_Appearance)
			m_Appearance.m_OnChanged.Insert(OnAppearanceChanged);

		TextWidget title = TextWidget.Cast(m_wRoot.FindAnyWidget("CreateTitle"));
		if (title)
			title.SetText("#ELIFE-Join_CreateTitle");

		m_BackButton = SCR_ButtonTextComponent.GetButtonText("ButtonBack", m_wRoot);
		if (m_BackButton)
		{
			m_BackButton.SetText("#ELIFE-Join_Back");
			m_BackButton.m_OnClicked.Insert(OnBackClicked);
		}

		m_CreateButton = SCR_ButtonTextComponent.GetButtonText("ButtonCreate", m_wRoot);
		if (m_CreateButton)
		{
			m_CreateButton.SetText("#ELIFE-Join_Create");
			m_CreateButton.m_OnClicked.Insert(OnCreateClicked);
		}
	}

	//------------------------------------------------------------------------------------------------
	void SetVisible(bool visible)
	{
		if (m_wRoot)
			m_wRoot.SetVisible(visible);
	}

	//------------------------------------------------------------------------------------------------
	//! Shows the payload: names are pre-filled, Back follows canGoBack, and the appearance restarts at 0.
	//! The menu clears busy state before calling this.
	void Show(ELIFE_JoinCreatePayload payload, ELIFE_PlayerController controller)
	{
		if (!m_wRoot)
			return;

		m_Controller = controller;

		int count = ELIFE_JoinPreview.GetAppearanceCount(payload.appearanceCount);
		if (count < 1)
			count = 1;

		m_iAppearance = 0;

		if (m_Appearance)
		{
			m_Appearance.ClearAll();
			for (int i = 1; i <= count; i++)
			{
				m_Appearance.AddItem(i.ToString());
			}

			m_Appearance.SetCurrentItem(0, false, false, false);
		}

		if (m_FirstName)
			m_FirstName.SetValue(payload.firstName);

		if (m_LastName)
			m_LastName.SetValue(payload.lastName);

		if (m_wBack)
			m_wBack.SetVisible(payload.canGoBack == 1);

		SetError(GetErrorText(payload.error));

		RefreshAppearance();
	}

	//------------------------------------------------------------------------------------------------
	//! Disables the inputs and buttons while a request is in flight. Set by ELIFE_JoinMenu.SetBusy.
	void SetBusy(bool busy)
	{
		m_bBusy = busy;

		if (m_wFirstName)
			m_wFirstName.SetEnabled(!busy);

		if (m_wLastName)
			m_wLastName.SetEnabled(!busy);

		if (m_wAppearance)
			m_wAppearance.SetEnabled(!busy);

		if (m_BackButton)
			m_BackButton.SetEnabled(!busy);

		if (m_CreateButton)
			m_CreateButton.SetEnabled(!busy);
	}

	//------------------------------------------------------------------------------------------------
	//! Shows the inline error in vanilla warning style; an empty text hides the row.
	protected void SetError(string text)
	{
		if (m_wError)
			m_wError.SetText(text);

		if (m_wErrorRow)
			m_wErrorRow.SetVisible(text != "");
	}

	//------------------------------------------------------------------------------------------------
	//! Caps a name at NAME_MAX while it is typed. The server still validates the name.
	protected void OnNameChanged(SCR_EditBoxComponent comp, string text)
	{
		if (text.Length() > NAME_MAX)
			comp.SetValue(text.Substring(0, NAME_MAX));
	}

	//------------------------------------------------------------------------------------------------
	protected void OnAppearanceChanged(SCR_SpinBoxComponent comp, int index)
	{
		m_iAppearance = index;
		RefreshAppearance();
	}

	//------------------------------------------------------------------------------------------------
	//! Renders the preview of the current appearance.
	protected void RefreshAppearance()
	{
		ELIFE_JoinPreview.ShowPreview(m_wPreview, ELIFE_JoinPreview.GetAppearancePrefab(m_iAppearance));
	}

	//------------------------------------------------------------------------------------------------
	//! A second click while busy does nothing, so a double-click on Create makes one character.
	protected void OnCreateClicked()
	{
		if (m_bBusy || !m_Controller || !m_FirstName || !m_LastName)
			return;

		string firstName = m_FirstName.GetValue();
		string lastName = m_LastName.GetValue();

		if (firstName.Length() > NAME_MAX || lastName.Length() > NAME_MAX)
		{
			SetError("#ELIFE-Join_NameTooLong");
			return;
		}

		SetBusy(true);
		m_Controller.ELIFE_RequestCreate(firstName, lastName, m_iAppearance);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnBackClicked()
	{
		if (m_bBusy || !m_Controller)
			return;

		SetBusy(true);
		m_Controller.ELIFE_RequestBackToSelect();
	}

	//------------------------------------------------------------------------------------------------
	//! Error keys with the ELIFE-Join_ prefix are shown; anything else is ignored.
	protected string GetErrorText(string messageKey)
	{
		if (messageKey.Length() > ERROR_KEY_PREFIX.Length() && messageKey.Substring(0, ERROR_KEY_PREFIX.Length()) == ERROR_KEY_PREFIX)
			return "#" + messageKey;

		return "";
	}
}
