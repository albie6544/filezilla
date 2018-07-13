#include <filezilla.h>
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage_interface.h"
#include "../Mainfrm.h"
#include "../power_management.h"
#include "../xrc_helper.h"
#include <libfilezilla/util.hpp>

#include <wx/statbox.h>

BEGIN_EVENT_TABLE(COptionsPageInterface, COptionsPage)
EVT_CHECKBOX(XRCID("ID_FILEPANESWAP"), COptionsPageInterface::OnLayoutChange)
EVT_CHOICE(XRCID("ID_FILEPANELAYOUT"), COptionsPageInterface::OnLayoutChange)
EVT_CHOICE(XRCID("ID_MESSAGELOGPOS"), COptionsPageInterface::OnLayoutChange)
END_EVENT_TABLE()

bool COptionsPageInterface::LoadPage()
{
	bool failure = false;

	SetCheckFromOption(XRCID("ID_FILEPANESWAP"), OPTION_FILEPANE_SWAP, failure);
	SetChoice(XRCID("ID_FILEPANELAYOUT"), m_pOptions->GetOptionVal(OPTION_FILEPANE_LAYOUT), failure);

	SetChoice(XRCID("ID_MESSAGELOGPOS"), m_pOptions->GetOptionVal(OPTION_MESSAGELOG_POSITION), failure);

#ifndef __WXMAC__
	SetCheckFromOption(XRCID("ID_MINIMIZE_TRAY"), OPTION_MINIMIZE_TRAY, failure);
#endif

	SetCheckFromOption(XRCID("ID_PREVENT_IDLESLEEP"), OPTION_PREVENT_IDLESLEEP, failure);

	SetCheckFromOption(XRCID("ID_SPEED_DISPLAY"), OPTION_SPEED_DISPLAY, failure);

	if (!CPowerManagement::IsSupported()) {
		XRCCTRL(*this, "ID_PREVENT_IDLESLEEP", wxCheckBox)->Hide();
	}

	SetCheckFromOption(XRCID("ID_INTERFACE_SITEMANAGER_ON_STARTUP"), OPTION_INTERFACE_SITEMANAGER_ON_STARTUP, failure);

	int action = m_pOptions->GetOptionVal(OPTION_ALREADYCONNECTED_CHOICE);
	if (action & 2) {
		action = 1 + (action & 1);
	}
	else {
		action = 0;
	}
	SetChoice(XRCID("ID_NEWCONN_ACTION"), action, failure);

	m_pOwner->RememberOldValue(OPTION_MESSAGELOG_POSITION);
	m_pOwner->RememberOldValue(OPTION_FILEPANE_LAYOUT);
	m_pOwner->RememberOldValue(OPTION_FILEPANE_SWAP);

	return !failure;
}

bool COptionsPageInterface::SavePage()
{
	SetOptionFromCheck(XRCID("ID_FILEPANESWAP"), OPTION_FILEPANE_SWAP);
	m_pOptions->SetOption(OPTION_FILEPANE_LAYOUT, GetChoice(XRCID("ID_FILEPANELAYOUT")));

	m_pOptions->SetOption(OPTION_MESSAGELOG_POSITION, GetChoice(XRCID("ID_MESSAGELOGPOS")));

#ifndef __WXMAC__
	SetOptionFromCheck(XRCID("ID_MINIMIZE_TRAY"), OPTION_MINIMIZE_TRAY);
#endif

	SetOptionFromCheck(XRCID("ID_PREVENT_IDLESLEEP"), OPTION_PREVENT_IDLESLEEP);

	SetOptionFromCheck(XRCID("ID_SPEED_DISPLAY"), OPTION_SPEED_DISPLAY);

	SetOptionFromCheck(XRCID("ID_INTERFACE_SITEMANAGER_ON_STARTUP"), OPTION_INTERFACE_SITEMANAGER_ON_STARTUP);

	int action = GetChoice(XRCID("ID_NEWCONN_ACTION"));
	if (!action) {
		action = m_pOptions->GetOptionVal(OPTION_ALREADYCONNECTED_CHOICE) & 1;
	}
	else {
		action += 1;
	}
	m_pOptions->SetOption(OPTION_ALREADYCONNECTED_CHOICE, action);

	return true;
}

void COptionsPageInterface::OnLayoutChange(wxCommandEvent&)
{
	m_pOptions->SetOption(OPTION_FILEPANE_LAYOUT, GetChoice(XRCID("ID_FILEPANELAYOUT")));
	m_pOptions->SetOption(OPTION_FILEPANE_SWAP, GetCheck(XRCID("ID_FILEPANESWAP")) ? 1 : 0);
	m_pOptions->SetOption(OPTION_MESSAGELOG_POSITION, GetChoice(XRCID("ID_MESSAGELOGPOS")));
}

bool COptionsPageInterface::CreateControls(wxWindow* parent)
{
	Create(parent);
	auto outer = new wxBoxSizer(wxVERTICAL);

	auto boxSizer = new wxStaticBoxSizer(wxVERTICAL, this, _("Layout"));
	outer->Add(boxSizer, grow);
	auto box = boxSizer->GetStaticBox();

	auto layout = new wxFlexGridSizer(1, wxDLG_UNIT(this, wxSize(3, 3)));
	boxSizer->Add(layout, 0, wxALL, wxDLG_UNIT(this, wxSize(3, 3)).y);
	auto innerLayout = new wxFlexGridSizer(2, wxDLG_UNIT(this, wxSize(3, 3)));
	layout->Add(innerLayout);
	innerLayout->Add(new wxStaticText(box, -1, _("&Layout of file and directory panes:")), valign);
	auto choice = new wxChoice(box, XRCID("ID_MESSAGELOGPOS"));
	choice->Append(_("Above the file lists"));
	choice->Append(_("Next to the transfer queue"));
	choice->Append(_("As tab in the transfer queue pane"));
	innerLayout->Add(choice, valign);
	innerLayout->Add(new wxStaticText(box, -1, _("Message log positio&n:")), valign);
	choice = new wxChoice(box, XRCID("ID_FILEPANELAYOUT"));
	choice->Append(_("Classic"));
	choice->Append(_("Explorer"));
	choice->Append(_("Widescreen"));
	choice->Append(_("Blackboard"));
	innerLayout->Add(choice, valign);
	layout->Add(new wxCheckBox(box, XRCID("ID_FILEPANESWAP"), _("&Swap local and remote panes")));

	boxSizer = new wxStaticBoxSizer(wxVERTICAL, this, _("Behaviour"));
	outer->Add(boxSizer, grow);
	box = boxSizer->GetStaticBox();

	auto behaviour = new wxFlexGridSizer(1, wxDLG_UNIT(this, wxSize(3, 3)));
	boxSizer->Add(behaviour, 0, wxALL, wxDLG_UNIT(this, wxSize(3, 3)).y);
	behaviour->Add(new wxCheckBox(box, XRCID("ID_MINIMIZE_TRAY"), _("&Minimize to tray")));
	behaviour->Add(new wxCheckBox(box, XRCID("ID_PREVENT_IDLESLEEP"), _("P&revent system from entering idle sleep during transfers and other operations")));
	behaviour->Add(new wxCheckBox(box, XRCID("ID_INTERFACE_SITEMANAGER_ON_STARTUP"), _("S&how the Site Manager on startup")));
	behaviour->Add(new wxStaticText(box, -1, _("When st&arting a new connection while already connected:")));
	choice = new wxChoice(box, XRCID("ID_NEWCONN_ACTION"));
	choice->Append(_("Ask for action"));
	choice->Append(_("Connect in new tab"));
	choice->Append(_("Connect in current tab"));
	behaviour->Add(choice);

	boxSizer = new wxStaticBoxSizer(wxVERTICAL, this, _("Transfer Queue"));
	outer->Add(boxSizer, grow);
	box = boxSizer->GetStaticBox();
	boxSizer->Add(new wxCheckBox(box, XRCID("ID_SPEED_DISPLAY"), _("&Display momentary transfer speed instead of average speed")), 0, wxALL, wxDLG_UNIT(this, wxSize(0, 3)).y);

	SetSizer(outer);

	return true;
}
