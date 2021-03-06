//this file is part of eMule
//Copyright (C)2002-2008 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / http://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
//
//This program is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with this program; if not, write to the Free Software
//Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#include "stdafx.h"
#include "emule.h"
#include "emuledlg.h"
#include "SharedFilesWnd.h"
#include "PPgDirectories.h"
#include "otherfunctions.h"
#include "InputBox.h"
#include "SharedFileList.h"
#include "Preferences.h"
#include "HelpIDs.h"
#include "UserMsgs.h"
#include "opcodes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


IMPLEMENT_DYNAMIC(CPPgDirectories, CPropertyPage)

BEGIN_MESSAGE_MAP(CPPgDirectories, CPropertyPage)
	ON_BN_CLICKED(IDC_SELTEMPDIR, OnBnClickedSeltempdir)
	ON_BN_CLICKED(IDC_SELINCDIR, OnBnClickedSelincdir)
	ON_EN_CHANGE(IDC_INCFILES, OnSettingsChange)
	ON_EN_CHANGE(IDC_TEMPFILES, OnSettingsChange)
	ON_BN_CLICKED(IDC_UNCADD, OnBnClickedAddUNC)
	ON_BN_CLICKED(IDC_UNCREM, OnBnClickedRemUNC)
	ON_WM_HELPINFO()
	ON_BN_CLICKED(IDC_SELTEMPDIRADD, OnBnClickedSeltempdiradd)
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CPPgDirectories::CPPgDirectories()
	: CPropertyPage(CPPgDirectories::IDD)
{
	m_icoBrowse = NULL;
}

CPPgDirectories::~CPPgDirectories()
{
}

void CPPgDirectories::DoDataExchange(CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SHARESELECTOR, m_ShareSelector);
	DDX_Control(pDX, IDC_UNCLIST, m_ctlUncPaths);
}

BOOL CPPgDirectories::OnInitDialog()
{
	CWaitCursor curWait; // initialization of that dialog may take a while..
	CPropertyPage::OnInitDialog();
	InitWindowStyles(this);

	((CEdit*)GetDlgItem(IDC_INCFILES))->SetLimitText(MAX_PATH);

	AddBuddyButton(GetDlgItem(IDC_INCFILES)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_SELINCDIR));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_SELINCDIR), m_icoBrowse);

	AddBuddyButton(GetDlgItem(IDC_TEMPFILES)->m_hWnd, ::GetDlgItem(m_hWnd, IDC_SELTEMPDIR));
	InitAttachedBrowseButton(::GetDlgItem(m_hWnd, IDC_SELTEMPDIR), m_icoBrowse);

	m_ctlUncPaths.InsertColumn(0, GetResString(IDS_UNCFOLDERS), LVCFMT_LEFT, 280);
	m_ctlUncPaths.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP);

	GetDlgItem(IDC_SELTEMPDIRADD)->ShowWindow(thePrefs.IsExtControlsEnabled() ? SW_SHOW : SW_HIDE);

	LoadSettings();
	Localize();

	return TRUE;  // return TRUE unless you set the focus to a control
				  // EXCEPTION: OCX Property Pages should return FALSE
}

void CPPgDirectories::LoadSettings()
{
	SetDlgItemText(IDC_INCFILES, thePrefs.m_strIncomingDir);

	CString tempfolders;
	for (int i = 0; i < thePrefs.tempdir.GetCount(); i++) {
		tempfolders.Append(thePrefs.GetTempDir(i));
		if (i + 1 < thePrefs.tempdir.GetCount())
			tempfolders += _T('|');
	}
	SetDlgItemText(IDC_TEMPFILES, tempfolders);

	m_ShareSelector.SetSharedDirectories(&thePrefs.shareddir_list);
	FillUncList();
}

void CPPgDirectories::OnBnClickedSelincdir()
{
	TCHAR buffer[MAX_PATH] = {};
	GetDlgItemText(IDC_INCFILES, buffer, _countof(buffer));
	if (SelectDir(GetSafeHwnd(), buffer, GetResString(IDS_SELECT_INCOMINGDIR)))
		SetDlgItemText(IDC_INCFILES, buffer);
}

void CPPgDirectories::OnBnClickedSeltempdir()
{
	TCHAR buffer[MAX_PATH] = {};
	GetDlgItemText(IDC_TEMPFILES, buffer, _countof(buffer));
	if (SelectDir(GetSafeHwnd(), buffer, GetResString(IDS_SELECT_TEMPDIR)))
		SetDlgItemText(IDC_TEMPFILES, buffer);
}

BOOL CPPgDirectories::OnApply()
{
	bool testtempdirchanged = false;
	CString testincdirchanged = thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR);

	CString strIncomingDir;
	GetDlgItemText(IDC_INCFILES, strIncomingDir);
	MakeFoldername(strIncomingDir);
	if (strIncomingDir.IsEmpty()) {
		strIncomingDir = thePrefs.GetDefaultDirectory(EMULE_INCOMINGDIR, true); // will create the directory here if it doesnt exists
		SetDlgItemText(IDC_INCFILES, strIncomingDir);
	} else if (thePrefs.IsInstallationDirectory(strIncomingDir)) {
		LocMessageBox(IDS_WRN_INCFILE_RESERVED, MB_OK, 0);
		return FALSE;
	} else if (strIncomingDir.CompareNoCase(testincdirchanged) != 0 && strIncomingDir.CompareNoCase(thePrefs.GetDefaultDirectory(EMULE_INCOMINGDIR, false)) != 0) {
		// if the user chooses a non-default directory which already contains files, inform him that all those files
		// will be shared
		CFileFind ff;
		CString strSearchPath(strIncomingDir + _T("\\*"));
		bool bEnd = !ff.FindFile(strSearchPath, 0);
		bool bExistingFile = false;
		while (!bEnd) {
			bEnd = !ff.FindNextFile();
			if (ff.IsDirectory() || ff.IsDots() || ff.IsSystem() || ff.IsTemporary() || ff.GetLength() == 0 || ff.GetLength() > MAX_EMULE_FILE_SIZE)
				continue;

			// ignore real LNK files
			if (ExtensionIs(ff.GetFileName(), _T(".lnk"))) {
				SHFILEINFO info;
				if (SHGetFileInfo(ff.GetFilePath(), 0, &info, sizeof(info), SHGFI_ATTRIBUTES) && (info.dwAttributes & SFGAO_LINK))
					if (!thePrefs.GetResolveSharedShellLinks())
						continue;
			}

			// ignore real THUMBS.DB files -- seems that lot of ppl have 'thumbs.db' files without the 'System' file attribute
			if (ff.GetFileName().CompareNoCase(_T("thumbs.db")) == 0)
				continue;

			bExistingFile = true;
			break;
		}
		if (bExistingFile && LocMessageBox(IDS_WRN_INCFILE_EXISTS, MB_OKCANCEL | MB_ICONINFORMATION, 0) == IDCANCEL)
			return FALSE;
	}

	// checking specified tempdir(s)
	CString strTempDir;
	GetDlgItemText(IDC_TEMPFILES, strTempDir);
	if (strTempDir.IsEmpty()) {
		strTempDir = thePrefs.GetDefaultDirectory(EMULE_TEMPDIR, true); // will create the directory here if it doesnt exists
		SetDlgItemText(IDC_TEMPFILES, strTempDir);
	}

	int curPos = 0;
	CStringArray temptempfolders;
	CString atmp = strTempDir.Tokenize(_T("|"), curPos);
	while (!atmp.IsEmpty()) {
		atmp.Trim();
		if (!atmp.IsEmpty()) {
			if (CompareDirectories(strIncomingDir, atmp) == 0) {
				LocMessageBox(IDS_WRN_INCTEMP_SAME, MB_OK, 0);
				return FALSE;
			}
			if (thePrefs.IsInstallationDirectory(atmp)) {
				LocMessageBox(IDS_WRN_TEMPFILES_RESERVED, MB_OK, 0);
				return FALSE;
			}
			bool doubled = false;
			for (int i = 0; i < temptempfolders.GetCount(); ++i)	// avoid double tempdirs
				if (temptempfolders[i].CompareNoCase(atmp) == 0) {
					doubled = true;
					break;
				}
			if (!doubled) {
				temptempfolders.Add(atmp);
				if (thePrefs.tempdir.GetCount() >= temptempfolders.GetCount()) {
					if (atmp.CompareNoCase(thePrefs.GetTempDir((int)temptempfolders.GetCount() - 1)) != 0)
						testtempdirchanged = true;
				} else testtempdirchanged = true;

			}
		}
		atmp = strTempDir.Tokenize(_T("|"), curPos);
	}

	if (temptempfolders.IsEmpty())
		temptempfolders.Add(strTempDir = thePrefs.GetDefaultDirectory(EMULE_TEMPDIR, true));

	if (temptempfolders.GetCount() != thePrefs.tempdir.GetCount())
		testtempdirchanged = true;

	// applying tempdirs
	if (testtempdirchanged) {
		thePrefs.tempdir.RemoveAll();
		for (int i = 0; i < temptempfolders.GetCount(); i++) {
			CString toadd = temptempfolders[i];
			MakeFoldername(toadd);
			if (!PathFileExists(toadd))
				CreateDirectory(toadd, NULL);
			if (PathFileExists(toadd))
				thePrefs.tempdir.Add(toadd);
		}
	}
	if (thePrefs.tempdir.IsEmpty())
		thePrefs.tempdir.Add(thePrefs.GetDefaultDirectory(EMULE_TEMPDIR, true));

	thePrefs.m_strIncomingDir = strIncomingDir;
	MakeFoldername(thePrefs.m_strIncomingDir);

	thePrefs.shareddir_list.RemoveAll();
	m_ShareSelector.GetSharedDirectories(&thePrefs.shareddir_list);
	for (int i = 0; i < m_ctlUncPaths.GetItemCount(); i++)
		thePrefs.shareddir_list.AddTail(m_ctlUncPaths.GetItemText(i, 0));

	// check shared directories for reserved folder names
	for (POSITION pos = thePrefs.shareddir_list.GetHeadPosition(); pos != NULL;) {
		POSITION posLast = pos;
		if (!thePrefs.IsShareableDirectory(thePrefs.shareddir_list.GetNext(pos)))
			thePrefs.shareddir_list.RemoveAt(posLast);
	}

	// on changing incoming dir, update incoming dirs of category of the same path
	if (testincdirchanged.CompareNoCase(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR)) != 0) {
		thePrefs.GetCategory(0)->strIncomingPath = thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR);
		bool dontaskagain = false;
		for (int cat = 1; cat <= thePrefs.GetCatCount() - 1; ++cat) {
			const CString &oldpath = thePrefs.GetCatPath(cat);
			if (oldpath.Left(testincdirchanged.GetLength()).CompareNoCase(testincdirchanged) == 0) {

				if (!dontaskagain) {
					dontaskagain = true;
					if (LocMessageBox(IDS_UPDATECATINCOMINGDIRS, MB_YESNO, 0) == IDNO)
						break;
				}
				thePrefs.GetCategory(cat)->strIncomingPath = thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR) + oldpath.Mid(testincdirchanged.GetLength());
			}
		}
		thePrefs.SaveCats();
	}


	if (testtempdirchanged)
		LocMessageBox(IDS_SETTINGCHANGED_RESTART, MB_OK, 0);

	theApp.emuledlg->sharedfileswnd->Reload();

	SetModified(0);
	return CPropertyPage::OnApply();
}

BOOL CPPgDirectories::OnCommand(WPARAM wParam, LPARAM lParam)
{
	if (wParam == UM_ITEMSTATECHANGED)
		SetModified();
	else if (wParam == ID_HELP) {
		OnHelp();
		return TRUE;
	}
	return CPropertyPage::OnCommand(wParam, lParam);
}

void CPPgDirectories::Localize()
{
	if (m_hWnd) {
		SetWindowText(GetResString(IDS_PW_DIR));

		SetDlgItemText(IDC_INCOMING_FRM, GetResString(IDS_PW_INCOMING));
		SetDlgItemText(IDC_TEMP_FRM, GetResString(IDS_PW_TEMP));
		SetDlgItemText(IDC_SHARED_FRM, GetResString(IDS_PW_SHARED));
	}
}

void CPPgDirectories::FillUncList()
{
	m_ctlUncPaths.DeleteAllItems();

	for (POSITION pos = thePrefs.shareddir_list.GetHeadPosition(); pos != NULL;) {
		CString folder = thePrefs.shareddir_list.GetNext(pos);
		if (PathIsUNC(folder))
			m_ctlUncPaths.InsertItem(0, folder);
	}
}

void CPPgDirectories::OnBnClickedAddUNC()
{
	InputBox inputbox;
	inputbox.SetLabels(GetResString(IDS_UNCFOLDERS), GetResString(IDS_UNCFOLDERS), _T("\\\\Server\\Share"));
	if (inputbox.DoModal() != IDOK)
		return;
	CString unc = inputbox.GetInput();

	// basic unc-check
	if (!PathIsUNC(unc)) {
		LocMessageBox(IDS_ERR_BADUNC, MB_ICONERROR, 0);
		return;
	}

	if (unc.Right(1) == _T("\\"))
		unc.Truncate(unc.GetLength() - 1);

	for (POSITION pos = thePrefs.shareddir_list.GetHeadPosition(); pos != NULL;)
		if (unc.CompareNoCase(thePrefs.shareddir_list.GetNext(pos)) == 0)
			return;

	for (int ipos = 0; ipos < m_ctlUncPaths.GetItemCount(); ++ipos)
		if (unc.CompareNoCase(m_ctlUncPaths.GetItemText(ipos, 0)) == 0)
			return;

	m_ctlUncPaths.InsertItem(m_ctlUncPaths.GetItemCount(), unc);
	SetModified();
}

void CPPgDirectories::OnBnClickedRemUNC()
{
	int index = m_ctlUncPaths.GetSelectionMark();
	if (index == -1 || m_ctlUncPaths.GetSelectedCount() == 0)
		return;
	m_ctlUncPaths.DeleteItem(index);
	SetModified();
}

void CPPgDirectories::OnHelp()
{
	theApp.ShowHelp(eMule_FAQ_Preferences_Directories);
}

BOOL CPPgDirectories::OnHelpInfo(HELPINFO* /*pHelpInfo*/)
{
	OnHelp();
	return TRUE;
}

void CPPgDirectories::OnBnClickedSeltempdiradd()
{
	CString paths;
	GetDlgItemText(IDC_TEMPFILES, paths);

	TCHAR buffer[MAX_PATH] = {};
	//GetDlgItemText(IDC_TEMPFILES, buffer, _countof(buffer));

	if (SelectDir(GetSafeHwnd(), buffer, GetResString(IDS_SELECT_TEMPDIR))) {
		paths.AppendFormat(_T("|%s"), (LPCTSTR)buffer);
		SetDlgItemText(IDC_TEMPFILES, paths);
	}
}

void CPPgDirectories::OnDestroy()
{
	CPropertyPage::OnDestroy();
	if (m_icoBrowse) {
		VERIFY(DestroyIcon(m_icoBrowse));
		m_icoBrowse = NULL;
	}
}