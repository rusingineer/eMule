#include "stdafx.h"
#include <locale.h>
#include "emule.h"
#include "StringConversion.h"
#include "WebServer.h"
#include "ClientCredits.h"
#include "ClientList.h"
#include "DownloadQueue.h"
#include "ED2KLink.h"
#include "emuledlg.h"
#include "FriendList.h"
#include "MD5Sum.h"
#include "ini2.h"
#include "Kademlia/Kademlia/Kademlia.h"
#include "KademliaWnd.h"
#include "KadSearchListCtrl.h"
#include "kademlia/kademlia/Entry.h"
#include "KnownFileList.h"
#include "ListenSocket.h"
#include "Log.h"
#include "MenuCmds.h"
#include "OtherFunctions.h"
#include "Preferences.h"
#include "Server.h"
#include "ServerList.h"
#include "ServerWnd.h"
#include "SearchList.h"
#include "SearchDlg.h"
#include "SearchParams.h"
#include "SharedFileList.h"
#include "Sockets.h"
#include "StatisticsDlg.h"
#include "Opcodes.h"
#include "QArray.h"
#include "TransferDlg.h"
#include "UploadQueue.h"
#include "UpDownClient.h"
#include "UserMsgs.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


#define HTTPInit "Server: eMule\r\nConnection: close\r\nContent-Type: text/html\r\n"
#define HTTPInitGZ HTTPInit "Content-Encoding: gzip\r\n"
#define HTTPENCODING _T("utf-8")

#define WEB_SERVER_TEMPLATES_VERSION	7

//SyruS CQArray-Sorting operators
bool operator > (QueueUsers & first, QueueUsers & second)
{
	return (first.sIndex.CompareNoCase(second.sIndex) > 0);
}
bool operator < (QueueUsers & first, QueueUsers & second)
{
	return (first.sIndex.CompareNoCase(second.sIndex) < 0);
}

bool operator > (SearchFileStruct & first, SearchFileStruct & second)
{
	return (first.m_strIndex.CompareNoCase(second.m_strIndex) > 0);
}
bool operator < (SearchFileStruct & first, SearchFileStruct & second)
{
	return (first.m_strIndex.CompareNoCase(second.m_strIndex) < 0);
}

static BOOL	WSdownloadColumnHidden[8];
static BOOL	WSuploadColumnHidden[5];
static BOOL	WSqueueColumnHidden[4];
static BOOL	WSsharedColumnHidden[7];
static BOOL	WSserverColumnHidden[10];
static BOOL	WSsearchColumnHidden[4];

CWebServer::CWebServer()
	: m_Templates(), m_bServerWorking(), m_iSearchSortby(3), m_bSearchAsc()
	, m_nIntruderDetect(), m_bIsTempDisabled(), m_nStartTempDisabledTime(), m_ulCurIP()
{
	CIni ini(thePrefs.GetConfigFile(), _T("WebServer"));

	ini.SerGet(true, WSdownloadColumnHidden, ARRSIZE(WSdownloadColumnHidden), _T("downloadColumnHidden"));
	ini.SerGet(true, WSuploadColumnHidden, ARRSIZE(WSuploadColumnHidden), _T("uploadColumnHidden"));
	ini.SerGet(true, WSqueueColumnHidden, ARRSIZE(WSqueueColumnHidden), _T("queueColumnHidden"));
	ini.SerGet(true, WSsearchColumnHidden, ARRSIZE(WSsearchColumnHidden), _T("searchColumnHidden"));
	ini.SerGet(true, WSsharedColumnHidden, ARRSIZE(WSsharedColumnHidden), _T("sharedColumnHidden"));
	ini.SerGet(true, WSserverColumnHidden, ARRSIZE(WSserverColumnHidden), _T("serverColumnHidden"));

	m_Params.bShowUploadQueue = ini.GetBool(_T("ShowUploadQueue"), false);
	m_Params.bShowUploadQueueBanned = ini.GetBool(_T("ShowUploadQueueBanned"), false);
	m_Params.bShowUploadQueueFriend = ini.GetBool(_T("ShowUploadQueueFriend"), false);

	m_Params.bDownloadSortReverse = ini.GetBool(_T("DownloadSortReverse"), true);
	m_Params.bUploadSortReverse = ini.GetBool(_T("UploadSortReverse"), true);
	m_Params.bQueueSortReverse = ini.GetBool(_T("QueueSortReverse"), true);
	m_Params.bServerSortReverse = ini.GetBool(_T("ServerSortReverse"), true);
	m_Params.bSharedSortReverse = ini.GetBool(_T("SharedSortReverse"), true);

	m_Params.DownloadSort = (DownloadSort)ini.GetInt(_T("DownloadSort"), DOWN_SORT_NAME);
	m_Params.UploadSort = (UploadSort)ini.GetInt(_T("UploadSort"), UP_SORT_FILENAME);
	m_Params.QueueSort = (QueueSort)ini.GetInt(_T("QueueSort"), QU_SORT_FILENAME);
	m_Params.ServerSort = (ServerSort)ini.GetInt(_T("ServerSort"), SERVER_SORT_NAME);
	m_Params.SharedSort = (SharedSort)ini.GetInt(_T("SharedSort"), SHARED_SORT_NAME);
}

CWebServer::~CWebServer()
{
	// save layout settings
	CIni ini(thePrefs.GetConfigFile(), _T("WebServer"));

	ini.WriteBool(_T("ShowUploadQueue"), m_Params.bShowUploadQueue);
	ini.WriteBool(_T("ShowUploadQueueBanned"), m_Params.bShowUploadQueueBanned);
	ini.WriteBool(_T("ShowUploadQueueFriend"), m_Params.bShowUploadQueueFriend);

	ini.WriteBool(_T("DownloadSortReverse"), m_Params.bDownloadSortReverse);
	ini.WriteBool(_T("UploadSortReverse"), m_Params.bUploadSortReverse);
	ini.WriteBool(_T("QueueSortReverse"), m_Params.bQueueSortReverse);
	ini.WriteBool(_T("ServerSortReverse"), m_Params.bServerSortReverse);
	ini.WriteBool(_T("SharedSortReverse"), m_Params.bSharedSortReverse);

	ini.WriteInt(_T("DownloadSort"), m_Params.DownloadSort);
	ini.WriteInt(_T("UploadSort"), m_Params.UploadSort);
	ini.WriteInt(_T("QueueSort"), m_Params.QueueSort);
	ini.WriteInt(_T("ServerSort"), m_Params.ServerSort);
	ini.WriteInt(_T("SharedSort"), m_Params.SharedSort);

	if (m_bServerWorking)
		StopSockets();
}

void CWebServer::SaveWIConfigArray(BOOL array[], int size, LPCTSTR key)
{
	CIni ini(thePrefs.GetConfigFile(), _T("WebServer"));
	ini.SerGet(false, array, size, key);
}

bool CWebServer::ReloadTemplates()
{
	TCHAR *sPrevLocale = _tsetlocale(LC_TIME, NULL);

	_tsetlocale(LC_TIME, _T("English"));
	CTime t = CTime::GetCurrentTime();
	m_Params.sLastModified = t.FormatGmt("%a, %d %b %Y %H:%M:%S GMT");
	m_Params.sETag = MD5Sum(m_Params.sLastModified).GetHashString();
	_tsetlocale(LC_TIME, sPrevLocale);

	const CString sFile = thePrefs.GetTemplate();

	CStdioFile file;
	if (file.Open(sFile, CFile::modeRead | CFile::shareDenyWrite | CFile::typeText)) {
		CString sAll, sLine;
		while (file.ReadString(sLine))
			sAll.AppendFormat(_T("%s\n"), (LPCTSTR)sLine);
		file.Close();

		CString sVersion = _LoadTemplate(sAll, _T("TMPL_VERSION"));
		if (_tstol(sVersion) >= WEB_SERVER_TEMPLATES_VERSION) {
			m_Templates.sHeader = _LoadTemplate(sAll, _T("TMPL_HEADER"));
			m_Templates.sHeaderStylesheet = _LoadTemplate(sAll, _T("TMPL_HEADER_STYLESHEET"));
			m_Templates.sFooter = _LoadTemplate(sAll, _T("TMPL_FOOTER"));
			m_Templates.sServerList = _LoadTemplate(sAll, _T("TMPL_SERVER_LIST"));
			m_Templates.sServerLine = _LoadTemplate(sAll, _T("TMPL_SERVER_LINE"));
			m_Templates.sTransferImages = _LoadTemplate(sAll, _T("TMPL_TRANSFER_IMAGES"));
			m_Templates.sTransferList = _LoadTemplate(sAll, _T("TMPL_TRANSFER_LIST"));
			m_Templates.sTransferDownHeader = _LoadTemplate(sAll, _T("TMPL_TRANSFER_DOWN_HEADER"));
			m_Templates.sTransferDownFooter = _LoadTemplate(sAll, _T("TMPL_TRANSFER_DOWN_FOOTER"));
			m_Templates.sTransferDownLine = _LoadTemplate(sAll, _T("TMPL_TRANSFER_DOWN_LINE"));
			m_Templates.sTransferUpHeader = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_HEADER"));
			m_Templates.sTransferUpFooter = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_FOOTER"));
			m_Templates.sTransferUpLine = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_LINE"));
			m_Templates.sTransferUpQueueShow = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_SHOW"));
			m_Templates.sTransferUpQueueHide = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_HIDE"));
			m_Templates.sTransferUpQueueLine = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_LINE"));
			m_Templates.sTransferUpQueueBannedShow = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_BANNED_SHOW"));
			m_Templates.sTransferUpQueueBannedHide = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_BANNED_HIDE"));
			m_Templates.sTransferUpQueueBannedLine = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_BANNED_LINE"));
			m_Templates.sTransferUpQueueFriendShow = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_FRIEND_SHOW"));
			m_Templates.sTransferUpQueueFriendHide = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_FRIEND_HIDE"));
			m_Templates.sTransferUpQueueFriendLine = _LoadTemplate(sAll, _T("TMPL_TRANSFER_UP_QUEUE_FRIEND_LINE"));
			m_Templates.sSharedList = _LoadTemplate(sAll, _T("TMPL_SHARED_LIST"));
			m_Templates.sSharedLine = _LoadTemplate(sAll, _T("TMPL_SHARED_LINE"));
			m_Templates.sGraphs = _LoadTemplate(sAll, _T("TMPL_GRAPHS"));
			m_Templates.sLog = _LoadTemplate(sAll, _T("TMPL_LOG"));
			m_Templates.sServerInfo = _LoadTemplate(sAll, _T("TMPL_SERVERINFO"));
			m_Templates.sDebugLog = _LoadTemplate(sAll, _T("TMPL_DEBUGLOG"));
			m_Templates.sStats = _LoadTemplate(sAll, _T("TMPL_STATS"));
			m_Templates.sPreferences = _LoadTemplate(sAll, _T("TMPL_PREFERENCES"));
			m_Templates.sLogin = _LoadTemplate(sAll, _T("TMPL_LOGIN"));
			m_Templates.sAddServerBox = _LoadTemplate(sAll, _T("TMPL_ADDSERVERBOX"));
			m_Templates.sSearch = _LoadTemplate(sAll, _T("TMPL_SEARCH"));
			m_Templates.iProgressbarWidth = (uint16)_tstoi(_LoadTemplate(sAll, _T("PROGRESSBARWIDTH")));
			m_Templates.sSearchHeader = _LoadTemplate(sAll, _T("TMPL_SEARCH_RESULT_HEADER"));
			m_Templates.sSearchResultLine = _LoadTemplate(sAll, _T("TMPL_SEARCH_RESULT_LINE"));
			m_Templates.sProgressbarImgs = _LoadTemplate(sAll, _T("PROGRESSBARIMGS"));
			m_Templates.sProgressbarImgsPercent = _LoadTemplate(sAll, _T("PROGRESSBARPERCENTIMG"));
			m_Templates.sCatArrow = _LoadTemplate(sAll, _T("TMPL_CATARROW"));
			m_Templates.sDownArrow = _LoadTemplate(sAll, _T("TMPL_DOWNARROW"));
			m_Templates.sUpArrow = _LoadTemplate(sAll, _T("TMPL_UPARROW"));
			m_Templates.strDownDoubleArrow = _LoadTemplate(sAll, _T("TMPL_DNDOUBLEARROW"));
			m_Templates.strUpDoubleArrow = _LoadTemplate(sAll, _T("TMPL_UPDOUBLEARROW"));
			m_Templates.sKad = _LoadTemplate(sAll, _T("TMPL_KADDLG"));
			m_Templates.sBootstrapLine = _LoadTemplate(sAll, _T("TMPL_BOOTSTRAPLINE"));
			m_Templates.sMyInfoLog = _LoadTemplate(sAll, _T("TMPL_MYINFO"));
			m_Templates.sCommentList = _LoadTemplate(sAll, _T("TMPL_COMMENTLIST"));
			m_Templates.sCommentListLine = _LoadTemplate(sAll, _T("TMPL_COMMENTLIST_LINE"));

			m_Templates.sProgressbarImgsPercent.Replace(_T("[PROGRESSGIFNAME]"), _T("%s"));
			m_Templates.sProgressbarImgsPercent.Replace(_T("[PROGRESSGIFINTERNAL]"), _T("%i"));
			m_Templates.sProgressbarImgs.Replace(_T("[PROGRESSGIFNAME]"), _T("%s"));
			m_Templates.sProgressbarImgs.Replace(_T("[PROGRESSGIFINTERNAL]"), _T("%i"));
			return true;
		}
		if(thePrefs.GetWSIsEnabled() || m_bServerWorking) {
			CString buffer;
			buffer.Format(GetResString(IDS_WS_ERR_LOADTEMPLATE), (LPCTSTR)sFile);
			AddLogLine(true, buffer);
			AfxMessageBox(buffer, MB_OK);
			StopServer();
		}
	} else if (m_bServerWorking) {
		AddLogLine(true, GetResString(IDS_WEB_ERR_CANTLOAD), (LPCTSTR)sFile);
		StopServer();
	}
	return false;
}

CString CWebServer::_LoadTemplate(const CString& sAll, const CString& sTemplateName)
{
	CString sRet;
	int nStart = sAll.Find(_T("<--") + sTemplateName + _T("-->"));
	int nEnd = sAll.Find(_T("<--") + sTemplateName + _T("_END-->"));
	if (nStart != -1 && nEnd != -1 && nStart < nEnd) {
		nStart += sTemplateName.GetLength() + 7;
		sRet = sAll.Mid(nStart, nEnd - nStart - 1);
	} else {
		if (sTemplateName == _T("TMPL_VERSION"))
			AddLogLine(true, (LPCTSTR)GetResString(IDS_WS_ERR_LOADTEMPLATE), (LPCTSTR)sTemplateName);
		if (nStart == -1)
			AddLogLine(false, (LPCTSTR)GetResString(IDS_WEB_ERR_CANTLOAD), (LPCTSTR)sTemplateName);
	}
	return sRet;
}

void CWebServer::RestartSockets()
{	//Cax2 - restarts the server with the new port settings
	StopSockets();
	if (m_bServerWorking)
		StartSockets(this);
}

void CWebServer::StartServer()
{
	if (m_bServerWorking == thePrefs.GetWSIsEnabled())
		return;
	m_bServerWorking = thePrefs.GetWSIsEnabled();

	if (m_bServerWorking) {
		ReloadTemplates();
		if (m_bServerWorking) {
			StartSockets(this);
			m_nIntruderDetect = 0;
			m_nStartTempDisabledTime = 0;
			m_bIsTempDisabled = false;
		}
	} else
		StopSockets();

	UINT uid = (thePrefs.GetWSIsEnabled() && m_bServerWorking ? IDS_ENABLED : IDS_DISABLED);
	AddLogLine(false, _T("%s: %s%s")
		, (LPCTSTR)_GetPlainResString(IDS_PW_WS)
		, (LPCTSTR)_GetPlainResString(uid).MakeLower()
		, (uid == IDS_ENABLED && thePrefs.GetWebUseHttps()) ? _T(" (HTTPS)") : _T(""));
}

void CWebServer::StopServer()
{
	if (m_bServerWorking) {
		StopSockets();
		m_bServerWorking = false;
	}
	thePrefs.SetWSIsEnabled(false);
}

void CWebServer::_RemoveServer(const CString& sIP, int nPort)
{
	CServer* server = theApp.serverlist->GetServerByAddress(sIP, (uint16)nPort);
	if (server != NULL)
		SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_SERVER_REMOVE, (LPARAM)server);
}

void CWebServer::_AddToStatic(const CString& sIP, int nPort)
{
	CServer *server = theApp.serverlist->GetServerByAddress(sIP, (uint16)nPort);
	if (server != NULL)
		SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_ADD_TO_STATIC, (LPARAM)server);
}

void CWebServer::_RemoveFromStatic(const CString& sIP, int nPort)
{
	CServer *server = theApp.serverlist->GetServerByAddress(sIP, (uint16)nPort);
	if (server != NULL)
		SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_REMOVE_FROM_STATIC, (LPARAM)server);
}


void CWebServer::AddStatsLine(UpDown line)
{
	m_Params.PointsForWeb.Add(line);
	if (m_Params.PointsForWeb.GetCount() > WEB_GRAPH_WIDTH)
		m_Params.PointsForWeb.RemoveAt(0);
}

CString CWebServer::_SpecialChars(CString str, bool noquote /*=false*/)
{
	str.Replace(_T("&"), _T("&amp;"));
	str.Replace(_T("<"), _T("&lt;"));
	str.Replace(_T(">"), _T("&gt;"));
	str.Replace(_T("\""), _T("&quot;"));
	if (noquote) {
		str.Replace(_T("'"), _T("&#8217;"));
		str.Replace(_T("\n"), _T("\\n"));
	}
	return str;
}

void CWebServer::_ConnectToServer(const CString& sIP, int nPort)
{
	CServer *server = theApp.serverlist->GetServerByAddress(sIP, (uint16)nPort);
	if (server != NULL)
		SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_CONNECTTOSERVER, (LPARAM)server);
}

void CWebServer::ProcessURL(const ThreadData& Data)
{
	if (!theApp.emuledlg->IsRunning())
		return;

	CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return;

	SetThreadLocale(thePrefs.GetLanguageID());

	//(0.29b)//////////////////////////////////////////////////////////////////
	// Here we are in real trouble! We are accessing the entire emule main thread
	// data without any syncronization!! Either we use the message pump for m_pdlgEmule
	// or use some hundreds of critical sections... For now, an exception handler
	// shoul avoid the worse things.
	//////////////////////////////////////////////////////////////////////////
	CoInitialize(NULL);

#ifndef _DEBUG
	try {
#endif
		bool isUseGzip = thePrefs.GetWebUseGzip();

		srand((unsigned)time(NULL));

		uint32 myip = inet_addr(ipstrA(Data.inadr));

		// check for being banned
		int myfaults = 0;
		DWORD now = ::GetTickCount();
		for (INT_PTR i = pThis->m_Params.badlogins.GetSize(); --i >= 0;) {
			if (now >= pThis->m_Params.badlogins[i].timestamp + MIN2MS(15))
				pThis->m_Params.badlogins.RemoveAt(i); // remove outdated entries
			else
				if (pThis->m_Params.badlogins[i].datalen == myip)
					++myfaults;
		}
		if (myfaults > 4) {
			Data.pSocket->SendContent(HTTPInit, _GetPlainResString(IDS_ACCESSDENIED));
			CoUninitialize();
			return;
		}

		bool justAddLink = false;
		bool login = false;
		CString sSession = _ParseURL(Data.sURL, _T("ses"));
		long lSession = sSession.IsEmpty() ? 0 : _tstol(sSession);

		if (_ParseURL(Data.sURL, _T("w")) == _T("password")) {
			const CString ip = ipstr(Data.inadr);

			if (!_ParseURL(Data.sURL, _T("c")).IsEmpty()) {
				// just sent password to add link remotely. Don't start a session.
				justAddLink = true;
			}

			if (MD5Sum(_ParseURL(Data.sURL, _T("p"))).GetHashString() == thePrefs.GetWSPass()) {
				if (!justAddLink) {
					// user wants to login
					Session ses;
					ses.admin = true;
					ses.startTime = CTime::GetCurrentTime();
					ses.lSession = lSession = GetRandomUInt32();
					ses.lastcat = 0 - thePrefs.GetCatFilter(0);
					pThis->m_Params.Sessions.Add(ses);
				}

				SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_UPDATEMYINFO, 0);

				AddLogLine(true, GetResString(IDS_WEB_ADMINLOGIN) + _T(" (%s)"), (LPCTSTR)ip);
				login = true;
			} else if (thePrefs.GetWSIsLowUserEnabled() && !thePrefs.GetWSLowPass().IsEmpty() && MD5Sum(_ParseURL(Data.sURL, _T("p"))).GetHashString() == thePrefs.GetWSLowPass()) {
				Session ses;
				ses.admin = false;
				ses.startTime = CTime::GetCurrentTime();
				ses.lSession = lSession = GetRandomUInt32();
				pThis->m_Params.Sessions.Add(ses);

				SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_UPDATEMYINFO, 0);

				AddLogLine(true, GetResString(IDS_WEB_GUESTLOGIN) + _T(" (%s)"), (LPCTSTR)ip);
				login = true;
			} else {
				LogWarning(LOG_STATUSBAR, GetResString(IDS_WEB_BADLOGINATTEMPT) + _T(" (%s)"), (LPCTSTR)ip);

				BadLogin newban = { myip, now };	// save failed attempt (ip,time)
				pThis->m_Params.badlogins.Add(newban);
				if (++myfaults > 4) {
					Data.pSocket->SendContent(HTTPInit, _GetPlainResString(IDS_ACCESSDENIED));
					CoUninitialize();
					return;
				}
			}
			isUseGzip = false; // [Julien]
			if (login)	// on login, forget previous failed attempts
				for (INT_PTR i = pThis->m_Params.badlogins.GetSize(); --i >= 0;)
					if (pThis->m_Params.badlogins[i].datalen == myip)
						pThis->m_Params.badlogins.RemoveAt(i);
		}

		TCHAR *gzipOut = NULL;
		DWORD gzipLen = 0;

		CString Out;
		sSession.Format(_T("%ld"), lSession);

		if (_ParseURL(Data.sURL, _T("w")) == _T("logout"))
			_RemoveSession(Data, lSession);

		if (_IsLoggedIn(Data, lSession)) {
			if (_ParseURL(Data.sURL, _T("w")) == _T("close") && IsSessionAdmin(Data, sSession) && thePrefs.GetWebAdminAllowedHiLevFunc()) {
				theApp.m_app_state = APP_STATE_SHUTTINGDOWN;
				_RemoveSession(Data, lSession);

				// send answer ...
				Out += _GetLoginScreen(Data);
				Data.pSocket->SendContent(HTTPInit, Out);

				SendMessage(theApp.emuledlg->m_hWnd, WM_CLOSE, 0, 0);

				CoUninitialize();
				return;
			}

			if (_ParseURL(Data.sURL, _T("w")) == _T("shutdown") && IsSessionAdmin(Data, sSession)) {
				_RemoveSession(Data, lSession);
				// send answer ...
				Out += _GetLoginScreen(Data);
				Data.pSocket->SendContent(HTTPInit, Out);

				SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_WINFUNC, 1);

				CoUninitialize();
				return;
			}

			if (_ParseURL(Data.sURL, _T("w")) == _T("reboot") && IsSessionAdmin(Data, sSession)) {
				_RemoveSession(Data, lSession);

				// send answer ...
				Out += _GetLoginScreen(Data);
				Data.pSocket->SendContent(HTTPInit, Out);

				SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_WINFUNC, 2);

				CoUninitialize();
				return;
			}

			if (_ParseURL(Data.sURL, _T("w")) == _T("commentlist")) {
				CString Out1 = _GetCommentlist(Data);

				if (!Out1.IsEmpty()) {
					Data.pSocket->SendContent(HTTPInit, Out1);

					CoUninitialize();
					return;
				}
			} else if (_ParseURL(Data.sURL, _T("w")) == _T("getfile") && IsSessionAdmin(Data, sSession)) {
				uchar FileHash[16];
				CKnownFile* kf = theApp.sharedfiles->GetFileByID(_GetFileHash(_ParseURL(Data.sURL, _T("filehash")), FileHash));

				if (kf) {
					if (thePrefs.GetMaxWebUploadFileSizeMB() != 0 && kf->GetFileSize() > (uint64)thePrefs.GetMaxWebUploadFileSizeMB() * 1024 * 1024) {
						Data.pSocket->SendReply("HTTP/1.1 403 Forbidden\r\n");

						CoUninitialize();
						return;
					}

					CFile file;
					if (file.Open(kf->GetFilePath(), CFile::modeRead | CFile::shareDenyWrite | CFile::typeBinary)) {
						EMFileSize filesize = kf->GetFileSize();

#define SENDFILEBUFSIZE 2048
						char *buffer = (char *)malloc(SENDFILEBUFSIZE);
						if (!buffer) {
							Data.pSocket->SendReply("HTTP/1.1 500 Internal Server Error\r\n");
							CoUninitialize();
							return;
						}

						CStringA fname(kf->GetFileName());
						CStringA szBuf;
						szBuf.Format("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
							"Content-Description: \"%s\"\r\n"
							"Content-Disposition: attachment; filename=\"%s\";\r\n"
							"Content-Transfer-Encoding: binary\r\n"
							"Content-Length: %I64u\r\n\r\n"
							, (LPCSTR)fname, (LPCSTR)fname, (uint64)filesize);
						Data.pSocket->SendData(szBuf, szBuf.GetLength());

						for (UINT r = 1; filesize > 0ull && r;) {
							r = file.Read(buffer, SENDFILEBUFSIZE);
							filesize -= r;
							Data.pSocket->SendData(buffer, r);
						}
						file.Close();

						free(buffer);
					} else
						Data.pSocket->SendReply("HTTP/1.1 404 File not found\r\n");
					CoUninitialize();
					return;
				}
			}

			Out += _GetHeader(Data, lSession);
			const CString sPage = _ParseURL(Data.sURL, _T("w"));
			if (sPage == _T("server"))
				Out += _GetServerList(Data);
			else if (sPage == _T("shared"))
				Out += _GetSharedFilesList(Data);
			else if (sPage == _T("transfer"))
				Out += _GetTransferList(Data);
			else if (sPage == _T("search"))
				Out += _GetSearch(Data);
			else if (sPage == _T("graphs"))
				Out += _GetGraphs(Data);
			else if (sPage == _T("log"))
				Out += _GetLog(Data);
			if (sPage == _T("sinfo"))
				Out += _GetServerInfo(Data);
			if (sPage == _T("debuglog"))
				Out += _GetDebugLog(Data);
			if (sPage == _T("myinfo"))
				Out += _GetMyInfo(Data);
			if (sPage == _T("stats"))
				Out += _GetStats(Data);
			if (sPage == _T("kad"))
				Out += _GetKadDlg(Data);
			if (sPage == _T("options")) {
				isUseGzip = false;
				Out += _GetPreferences(Data);
			}
			Out += _GetFooter(Data);

			if (sPage.IsEmpty())
				isUseGzip = false;

			if (isUseGzip) {
				bool bOk = false;
				try {
					CStringA strA(wc2utf8(Out));
					uLongf destLen = strA.GetLength() + 1024;
					gzipOut = new TCHAR[destLen];
					if (_GzipCompress((Bytef*)gzipOut, &destLen, (const Bytef*)(LPCSTR)strA, strA.GetLength(), Z_DEFAULT_COMPRESSION) == Z_OK) {
						bOk = true;
						gzipLen = destLen;
					}
				} catch (...) {
					ASSERT(0);
				}
				if (!bOk) {
					isUseGzip = false;
					delete[] gzipOut;
					gzipOut = NULL;
				}
			}
		} else if (justAddLink && login) {
			Out += _GetRemoteLinkAddedOk(Data);
		} else {
			isUseGzip = false;

			if (justAddLink)
				Out += _GetRemoteLinkAddedFailed(Data);
			else
				Out += _GetLoginScreen(Data);
		}

		// send answer ...
		if (!isUseGzip)
			Data.pSocket->SendContent(HTTPInit, Out);
		else
			Data.pSocket->SendContent(HTTPInitGZ, gzipOut, gzipLen);

		delete[] gzipOut;

#ifndef _DEBUG
	} catch (...) {
		AddDebugLogLine(DLP_VERYHIGH, false, _T("*** Unknown exception in CWebServer::ProcessURL"));
		ASSERT(0);
	}
#endif

	CoUninitialize();
}

CString CWebServer::_ParseURLArray(CString &URL, CString fieldname)
{
	CString res;

	while (URL.GetLength() > 0) {
		int pos = URL.MakeLower().Find(fieldname.MakeLower() + _T('='));
		if (pos < 0)
			break;
		const CString &temp = _ParseURL(URL, fieldname);
		if (temp.IsEmpty())
			break;
		res.AppendFormat(_T("%s|"), (LPCTSTR)temp);
		URL.Delete(pos, 10);
	}
	return res;
}

CString CWebServer::_ParseURL(const CString &URL, const CString &fieldname)
{
	int findPos = URL.Find(_T('?'));
	if (findPos >= 0) {
		int findLength = 0;
		CString Parameter = URL.Mid(findPos + 1, URL.GetLength() - findPos - 1);

		// search the fieldname beginning / middle and strip the rest...
		if (Parameter.Find(fieldname + _T('=')) == 0) {
			findPos = 0;
			findLength = fieldname.GetLength() + 1;
		} else
			findPos = -1;
		if (Parameter.Find(_T('&') + fieldname + _T('=')) > -1) {
			findPos = Parameter.Find(_T('&') + fieldname + _T('='));
			findLength = fieldname.GetLength() + 2;
		}
		if (findPos >= 0) {
			Parameter = Parameter.Mid(findPos + findLength, Parameter.GetLength());
			if (Parameter.Find(_T('&')) > -1)
				Parameter.Truncate(Parameter.Find(_T('&')));
			Parameter.Replace(_T('+'), _T(' '));
			// decode value ...
			return OptUtf8ToStr(URLDecode(Parameter, true));
		}
	}
	return CString();
}

CString CWebServer::_GetHeader(const ThreadData& Data, long lSession)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	CString sSession;
	sSession.Format(_T("%ld"), lSession);

	CString Out = pThis->m_Templates.sHeader;

	Out.Replace(_T("[CharSet]"), HTTPENCODING);

	//	Auto-refresh code
	CString sRefresh, strDummyNumber;
	const CString sPage = _ParseURL(Data.sURL, _T("w"));
	bool bAdmin = IsSessionAdmin(Data, sSession);

	CString strCat = _T("&amp;cat=") + _ParseURL(Data.sURL, _T("cat"));

	if (sPage == _T("options") || sPage == _T("stats") || sPage == _T("password"))
		sRefresh = _T("0");
	else
		sRefresh.Format(_T("%d"), thePrefs.GetWebPageRefresh() * SEC2MS(1));

	strDummyNumber.Format(_T("%d"), rand());

	Out.Replace(_T("[admin]"), (bAdmin && thePrefs.GetWebAdminAllowedHiLevFunc()) ? _T("admin") : _T(""));
	Out.Replace(_T("[Session]"), sSession);
	Out.Replace(_T("[RefreshVal]"), sRefresh);
	Out.Replace(_T("[wCommand]"), _ParseURL(Data.sURL, _T("w")) + strCat + _T("&amp;dummy=") + strDummyNumber);
	Out.Replace(_T("[eMuleAppName]"), _T("eMule"));
	Out.Replace(_T("[version]"), theApp.m_strCurVersionLong);
	Out.Replace(_T("[StyleSheet]"), pThis->m_Templates.sHeaderStylesheet);
	Out.Replace(_T("[WebControl]"), _GetPlainResString(IDS_WEB_CONTROL));
	Out.Replace(_T("[Transfer]"), _GetPlainResString(IDS_CD_TRANS));
	Out.Replace(_T("[Server]"), _GetPlainResString(IDS_SV_SERVERLIST));
	Out.Replace(_T("[Shared]"), _GetPlainResString(IDS_SHAREDFILES));
	Out.Replace(_T("[Graphs]"), _GetPlainResString(IDS_GRAPHS));
	Out.Replace(_T("[Log]"), _GetPlainResString(IDS_SV_LOG));
	Out.Replace(_T("[ServerInfo]"), _GetPlainResString(IDS_SV_SERVERINFO));
	Out.Replace(_T("[DebugLog]"), _GetPlainResString(IDS_SV_DEBUGLOG));
	Out.Replace(_T("[MyInfo]"), _GetPlainResString(IDS_MYINFO));
	Out.Replace(_T("[Stats]"), _GetPlainResString(IDS_SF_STATISTICS));
	Out.Replace(_T("[Options]"), _GetPlainResString(IDS_EM_PREFS));
	Out.Replace(_T("[Ed2klink]"), _GetPlainResString(IDS_SW_LINK));
	Out.Replace(_T("[Close]"), _GetPlainResString(IDS_WEB_SHUTDOWN));
	Out.Replace(_T("[Reboot]"), _GetPlainResString(IDS_WEB_REBOOT));
	Out.Replace(_T("[Shutdown]"), _GetPlainResString(IDS_WEB_SHUTDOWNSYSTEM));
	Out.Replace(_T("[WebOptions]"), _GetPlainResString(IDS_WEB_ADMINMENU));
	Out.Replace(_T("[Logout]"), _GetPlainResString(IDS_WEB_LOGOUT));
	Out.Replace(_T("[Search]"), _GetPlainResString(IDS_EM_SEARCH));
	Out.Replace(_T("[Download]"), _GetPlainResString(IDS_SW_DOWNLOAD));
	Out.Replace(_T("[Start]"), _GetPlainResString(IDS_SW_START));
	Out.Replace(_T("[Version]"), _GetPlainResString(IDS_VERSION));
	Out.Replace(_T("[VersionCheck]"), thePrefs.GetVersionCheckURL());
	Out.Replace(_T("[Kad]"), _GetPlainResString(IDS_KADEMLIA));

	Out.Replace(_T("[FileIsHashing]"), _GetPlainResString(IDS_HASHING));
	Out.Replace(_T("[FileIsErroneous]"), _GetPlainResString(IDS_ERRORLIKE));
	Out.Replace(_T("[FileIsCompleting]"), _GetPlainResString(IDS_COMPLETING));
	Out.Replace(_T("[FileDetails]"), _GetPlainResString(IDS_FD_TITLE));
	Out.Replace(_T("[FileComments]"), _GetPlainResString(IDS_CMT_SHOWALL));
	Out.Replace(_T("[ClearCompleted]"), _GetPlainResString(IDS_DL_CLEAR));
	Out.Replace(_T("[RunFile]"), _GetPlainResString(IDS_DOWNLOAD));
	Out.Replace(_T("[Resume]"), _GetPlainResString(IDS_DL_RESUME));
	Out.Replace(_T("[Stop]"), _GetPlainResString(IDS_DL_STOP));
	Out.Replace(_T("[Pause]"), _GetPlainResString(IDS_DL_PAUSE));
	Out.Replace(_T("[ConfirmCancel]"), _GetPlainResString(IDS_Q_CANCELDL2));
	Out.Replace(_T("[Cancel]"), _GetPlainResString(IDS_MAIN_BTN_CANCEL));
	Out.Replace(_T("[GetFLC]"), _GetPlainResString(IDS_DOWNLOADMOVIECHUNKS));
	Out.Replace(_T("[Rename]"), _GetPlainResString(IDS_RENAME));
	Out.Replace(_T("[Connect]"), _GetPlainResString(IDS_IRC_CONNECT));
	Out.Replace(_T("[ConfirmRemove]"), _GetPlainResString(IDS_WEB_CONFIRM_REMOVE_SERVER));
	Out.Replace(_T("[ConfirmClose]"), _GetPlainResString(IDS_WEB_MAIN_CLOSE));
	Out.Replace(_T("[ConfirmReboot]"), _GetPlainResString(IDS_WEB_MAIN_REBOOT));
	Out.Replace(_T("[ConfirmShutdown]"), _GetPlainResString(IDS_WEB_MAIN_SHUTDOWN));
	Out.Replace(_T("[RemoveServer]"), _GetPlainResString(IDS_REMOVETHIS));
	Out.Replace(_T("[StaticServer]"), _GetPlainResString(IDS_STATICSERVER));
	Out.Replace(_T("[Friend]"), _GetPlainResString(IDS_PW_FRIENDS));

	Out.Replace(_T("[PriorityVeryLow]"), _GetPlainResString(IDS_PRIOVERYLOW));
	Out.Replace(_T("[PriorityLow]"), _GetPlainResString(IDS_PRIOLOW));
	Out.Replace(_T("[PriorityNormal]"), _GetPlainResString(IDS_PRIONORMAL));
	Out.Replace(_T("[PriorityHigh]"), _GetPlainResString(IDS_PRIOHIGH));
	Out.Replace(_T("[PriorityRelease]"), _GetPlainResString(IDS_PRIORELEASE));
	Out.Replace(_T("[PriorityAuto]"), _GetPlainResString(IDS_PRIOAUTO));

	CString HTTPConState, HTTPConText, HTTPHelp;
	CString HTTPHelpU = _T("0");
	CString HTTPHelpM = _T("0");
	CString HTTPHelpV = _T("0");
	CString HTTPHelpF = _T("0");
	const CString sCmd = _ParseURL(Data.sURL, _T("c")).MakeLower();
	bool disconnectissued = (sCmd == _T("disconnect"));
	bool connectissued = (sCmd == _T("connect"));

	if ((theApp.serverconnect->IsConnecting() && !disconnectissued) || connectissued) {
		HTTPConState = _T("connecting");
		HTTPConText = _GetPlainResString(IDS_CONNECTING);
	} else if (theApp.serverconnect->IsConnected() && !disconnectissued) {
		HTTPConState = theApp.serverconnect->IsLowID() ? _T("low") : _T("high");
		CServer* cur_server = theApp.serverlist->GetServerByAddress(
			theApp.serverconnect->GetCurrentServer()->GetAddress(),
			theApp.serverconnect->GetCurrentServer()->GetPort());

		if (cur_server) {
			HTTPConText = cur_server->GetListName();
			if (HTTPConText.GetLength() > SHORT_LENGTH)
				HTTPConText = HTTPConText.Left(SHORT_LENGTH - 3) + _T("...");

			if (IsSessionAdmin(Data, sSession))
				HTTPConText.AppendFormat(_T(" (<a href=\"?ses=%s&amp;w=server&amp;c=disconnect\">%s</a>)"), (LPCTSTR)sSession, (LPCTSTR)_GetPlainResString(IDS_IRC_DISCONNECT));

			HTTPHelpU = CastItoIShort(cur_server->GetUsers());
			HTTPHelpM = CastItoIShort(cur_server->GetMaxUsers());
			HTTPHelpF = CastItoIShort(cur_server->GetFiles());
			if (cur_server->GetMaxUsers() > 0)
				HTTPHelpV.Format(_T("%.1f "), (100.0 * cur_server->GetUsers()) / cur_server->GetMaxUsers());
			else
				HTTPHelpV = "0.0 ";
		}

	} else {
		HTTPConState = _T("disconnected");
		HTTPConText = _GetPlainResString(IDS_DISCONNECTED);
		if (IsSessionAdmin(Data, sSession))
			HTTPConText.AppendFormat(_T(" (<a href=\"?ses=%s&amp;w=server&amp;c=connect\">%s</a>)"), (LPCTSTR)sSession, (LPCTSTR)_GetPlainResString(IDS_CONNECTTOANYSERVER));
	}
	uint32 allUsers = 0;
	uint32 allFiles = 0;
	for (INT_PTR sc = 0; sc < theApp.serverlist->GetServerCount(); ++sc) {
		const CServer* cur_server = theApp.serverlist->GetServerAt(sc);
		allUsers += cur_server->GetUsers();
		allFiles += cur_server->GetFiles();
	}
	Out.Replace(_T("[AllUsers]"), CastItoIShort(allUsers));
	Out.Replace(_T("[AllFiles]"), CastItoIShort(allFiles));
	Out.Replace(_T("[ConState]"), HTTPConState);
	Out.Replace(_T("[ConText]"), HTTPConText);

	// kad status
	if (Kademlia::CKademlia::IsConnected()) {
		if (Kademlia::CKademlia::IsFirewalled()) {
			HTTPConText = GetResString(IDS_FIREWALLED);
			HTTPConText.AppendFormat(_T(" (<a href=\"?ses=%s&amp;w=kad&amp;c=rcfirewall\">%s</a>"), (LPCTSTR)sSession, (LPCTSTR)GetResString(IDS_KAD_RECHECKFW));
			HTTPConText.AppendFormat(_T(", <a href=\"?ses=%s&amp;w=kad&amp;c=disconnect\">%s</a>)"), (LPCTSTR)sSession, (LPCTSTR)GetResString(IDS_IRC_DISCONNECT));
		} else {
			HTTPConText = GetResString(IDS_CONNECTED);
			HTTPConText.AppendFormat(_T(" (<a href=\"?ses=%s&amp;w=kad&amp;c=disconnect\">%s</a>)"), (LPCTSTR)sSession, (LPCTSTR)GetResString(IDS_IRC_DISCONNECT));
		}
	} else {
		if (Kademlia::CKademlia::IsRunning())
			HTTPConText = GetResString(IDS_CONNECTING);
		else {
			HTTPConText = GetResString(IDS_DISCONNECTED);
			HTTPConText.AppendFormat(_T(" (<a href=\"?ses=%s&amp;w=kad&amp;c=connect\">%s</a>)"), (LPCTSTR)sSession, (LPCTSTR)GetResString(IDS_IRC_CONNECT));
		}
	}
	Out.Replace(_T("[KadConText]"), HTTPConText);

	TCHAR HTTPHeader[100];
	//100/1024 equals to 1/10.24
	if (thePrefs.GetMaxUpload() == UNLIMITED)
		_stprintf(HTTPHeader, _T("%.1f"), theApp.uploadqueue->GetDatarate() / 10.24 / thePrefs.GetMaxGraphUploadRate(true));
	else
		_stprintf(HTTPHeader, _T("%.1f"), theApp.uploadqueue->GetDatarate() / 10.24 / thePrefs.GetMaxUpload());
	Out.Replace(_T("[UploadValue]"), HTTPHeader);

	if (thePrefs.GetMaxDownload() == UNLIMITED)
		_stprintf(HTTPHeader, _T("%.1f"), theApp.downloadqueue->GetDatarate() / 10.24 / thePrefs.GetMaxGraphDownloadRate());
	else
		_stprintf(HTTPHeader, _T("%.1f"), theApp.downloadqueue->GetDatarate() / 10.24 / thePrefs.GetMaxDownload());
	Out.Replace(_T("[DownloadValue]"), HTTPHeader);

	_stprintf(HTTPHeader, _T("%.1f"), (100.0 * theApp.listensocket->GetOpenSockets()) / thePrefs.GetMaxConnections());
	Out.Replace(_T("[ConnectionValue]"), HTTPHeader);
	_stprintf(HTTPHeader, _T("%.1f"), theApp.uploadqueue->GetDatarate() / 1024.0);
	Out.Replace(_T("[CurUpload]"), HTTPHeader);
	_stprintf(HTTPHeader, _T("%.1f"), theApp.downloadqueue->GetDatarate() / 1024.0);
	Out.Replace(_T("[CurDownload]"), HTTPHeader);
	_stprintf(HTTPHeader, _T("%u.0"), theApp.listensocket->GetOpenSockets());
	Out.Replace(_T("[CurConnection]"), HTTPHeader);

	uint32 dwMax = thePrefs.GetMaxUpload();
	if (dwMax == UNLIMITED)
		HTTPHelp = GetResString(IDS_PW_UNLIMITED);
	else
		HTTPHelp.Format(_T("%u"), dwMax);
	Out.Replace(_T("[MaxUpload]"), HTTPHelp);

	dwMax = thePrefs.GetMaxDownload();
	if (dwMax == UNLIMITED)
		HTTPHelp = GetResString(IDS_PW_UNLIMITED);
	else
		HTTPHelp.Format(_T("%u"), dwMax);
	Out.Replace(_T("[MaxDownload]"), HTTPHelp);

	dwMax = thePrefs.GetMaxConnections();
	if (dwMax == UNLIMITED)
		HTTPHelp = GetResString(IDS_PW_UNLIMITED);
	else
		HTTPHelp.Format(_T("%u"), dwMax);
	Out.Replace(_T("[MaxConnection]"), HTTPHelp);
	Out.Replace(_T("[UserValue]"), HTTPHelpV);
	Out.Replace(_T("[MaxUsers]"), HTTPHelpM);
	Out.Replace(_T("[CurUsers]"), HTTPHelpU);
	Out.Replace(_T("[CurFiles]"), HTTPHelpF);
	Out.Replace(_T("[Connection]"), _GetPlainResString(IDS_PW_CONNECTION));
	Out.Replace(_T("[QuickStats]"), _GetPlainResString(IDS_STATUS));

	Out.Replace(_T("[Users]"), _GetPlainResString(IDS_UUSERS));
	Out.Replace(_T("[Files]"), _GetPlainResString(IDS_FILES));
	Out.Replace(_T("[Con]"), _GetPlainResString(IDS_ST_ACTIVEC));
	Out.Replace(_T("[Up]"), _GetPlainResString(IDS_PW_CON_UPLBL));
	Out.Replace(_T("[Down]"), _GetPlainResString(IDS_PW_CON_DOWNLBL));

	if (thePrefs.GetCatCount() > 1)
		InsertCatBox(Out, 0, pThis->m_Templates.sCatArrow, false, false, sSession, _T(""), true);
	else
		Out.Replace(_T("[CATBOXED2K]"), _T(""));

	return Out;
}

CString CWebServer::_GetFooter(const ThreadData& Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	return (pThis == NULL) ? CString() : pThis->m_Templates.sFooter;
}

CString CWebServer::_GetServerList(const ThreadData& Data)
{
	CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString sSession = _ParseURL(Data.sURL, _T("ses"));
	bool bAdmin = IsSessionAdmin(Data, sSession);
	CString sAddServerBox = _GetAddServerBox(Data);

	const CString sCmd = _ParseURL(Data.sURL, _T("c")).MakeLower();
	const CString sIP = _ParseURL(Data.sURL, _T("ip"));
	int nPort = _tstoi(_ParseURL(Data.sURL, _T("port")));
	if (bAdmin) {
		if (sCmd == _T("connect")) {
			if (sIP.IsEmpty())
				SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_CONNECTTOSERVER, 0);
			else
				_ConnectToServer(sIP, nPort);
		} else if (sCmd == _T("disconnect")) {
			if (theApp.serverconnect->IsConnecting())
				SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_STOPCONNECTING, NULL);
			else
				SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_DISCONNECT, 1);
		} else if (sCmd == _T("remove")) {
			if (!sIP.IsEmpty())
				_RemoveServer(sIP, nPort);
		} else if (sCmd == _T("addtostatic")) {
			if (!sIP.IsEmpty())
				_AddToStatic(sIP, nPort);
		} else if (sCmd == _T("removefromstatic")) {
			if (!sIP.IsEmpty())
				_RemoveFromStatic(sIP, nPort);
		} else if (sCmd == _T("priolow")) {
			if (!sIP.IsEmpty()) {
				CServer* server = theApp.serverlist->GetServerByAddress(sIP, (uint16)nPort);
				if (server) {
					server->SetPreference(SRV_PR_LOW);
					SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_UPDATESERVER, (LPARAM)server);
				}
			}
		} else if (sCmd == _T("prionormal")) {
			if (!sIP.IsEmpty()) {
				CServer* server = theApp.serverlist->GetServerByAddress(sIP, (uint16)nPort);
				if (server) {
					server->SetPreference(SRV_PR_NORMAL);
					SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_UPDATESERVER, (LPARAM)server);
				}
			}
		} else if (sCmd == _T("priohigh")) {
			if (!sIP.IsEmpty()) {
				CServer* server = theApp.serverlist->GetServerByAddress(sIP, (uint16)nPort);
				if (server) {
					server->SetPreference(SRV_PR_HIGH);
					SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_UPDATESERVER, (LPARAM)server);
				}
			}
		}
	} else if (sCmd == _T("menu")) {
		int iMenu = _tstol(_ParseURL(Data.sURL, _T("m")));
		bool bValue = _ParseURL(Data.sURL, _T("v")) == _T("1");
		WSserverColumnHidden[iMenu] = bValue;
		SaveWIConfigArray(WSserverColumnHidden, ARRSIZE(WSserverColumnHidden), _T("serverColumnHidden"));
	}

	CString strTmp = _ParseURL(Data.sURL, _T("sortreverse"));
	const CString sSort = _ParseURL(Data.sURL, _T("sort")).MakeLower();

	if (!sSort.IsEmpty()) {
		bool bDirection = false;

		if (sSort == _T("state"))
			pThis->m_Params.ServerSort = SERVER_SORT_STATE;
		else if (sSort == _T("name")) {
			pThis->m_Params.ServerSort = SERVER_SORT_NAME;
			bDirection = true;
		} else if (sSort == _T("ip"))
			pThis->m_Params.ServerSort = SERVER_SORT_IP;
		else if (sSort == _T("description")) {
			pThis->m_Params.ServerSort = SERVER_SORT_DESCRIPTION;
			bDirection = true;
		} else if (sSort == _T("ping"))
			pThis->m_Params.ServerSort = SERVER_SORT_PING;
		else if (sSort == _T("users"))
			pThis->m_Params.ServerSort = SERVER_SORT_USERS;
		else if (sSort == _T("files"))
			pThis->m_Params.ServerSort = SERVER_SORT_FILES;
		else if (sSort == _T("priority"))
			pThis->m_Params.ServerSort = SERVER_SORT_PRIORITY;
		else if (sSort == _T("failed"))
			pThis->m_Params.ServerSort = SERVER_SORT_FAILED;
		else if (sSort == _T("limit"))
			pThis->m_Params.ServerSort = SERVER_SORT_LIMIT;
		else if (sSort == _T("version"))
			pThis->m_Params.ServerSort = SERVER_SORT_VERSION;

		if (strTmp.IsEmpty())
			pThis->m_Params.bServerSortReverse = bDirection;
	}
	if (!strTmp.IsEmpty())
		pThis->m_Params.bServerSortReverse = (strTmp == _T("true"));

	CString Out = pThis->m_Templates.sServerList;

	Out.Replace(_T("[AddServerBox]"), sAddServerBox);
	Out.Replace(_T("[Session]"), sSession);

	strTmp = (pThis->m_Params.bServerSortReverse) ? _T("&amp;sortreverse=false") : _T("&amp;sortreverse=true");

	if (pThis->m_Params.ServerSort == SERVER_SORT_STATE)
		Out.Replace(_T("[SortState]"), strTmp);
	else
		Out.Replace(_T("[SortState]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_NAME)
		Out.Replace(_T("[SortName]"), strTmp);
	else
		Out.Replace(_T("[SortName]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_IP)
		Out.Replace(_T("[SortIP]"), strTmp);
	else
		Out.Replace(_T("[SortIP]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_DESCRIPTION)
		Out.Replace(_T("[SortDescription]"), strTmp);
	else
		Out.Replace(_T("[SortDescription]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_PING)
		Out.Replace(_T("[SortPing]"), strTmp);
	else
		Out.Replace(_T("[SortPing]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_USERS)
		Out.Replace(_T("[SortUsers]"), strTmp);
	else
		Out.Replace(_T("[SortUsers]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_FILES)
		Out.Replace(_T("[SortFiles]"), strTmp);
	else
		Out.Replace(_T("[SortFiles]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_PRIORITY)
		Out.Replace(_T("[SortPriority]"), strTmp);
	else
		Out.Replace(_T("[SortPriority]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_FAILED)
		Out.Replace(_T("[SortFailed]"), strTmp);
	else
		Out.Replace(_T("[SortFailed]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_LIMIT)
		Out.Replace(_T("[SortLimit]"), strTmp);
	else
		Out.Replace(_T("[SortLimit]"), _T(""));
	if (pThis->m_Params.ServerSort == SERVER_SORT_VERSION)
		Out.Replace(_T("[SortVersion]"), strTmp);
	else
		Out.Replace(_T("[SortVersion]"), _T(""));
	Out.Replace(_T("[ServerList]"), _GetPlainResString(IDS_SV_SERVERLIST));

	const TCHAR *pcSortIcon = (pThis->m_Params.bServerSortReverse) ? pThis->m_Templates.sUpArrow : pThis->m_Templates.sDownArrow;

	_GetPlainResString(strTmp, IDS_SL_SERVERNAME);
	if (!WSserverColumnHidden[0]) {
		Out.Replace(_T("[ServernameI]"), (pThis->m_Params.ServerSort == SERVER_SORT_NAME) ? pcSortIcon : _T(""));
		Out.Replace(_T("[ServernameH]"), strTmp);
	} else {
		Out.Replace(_T("[ServernameI]"), _T(""));
		Out.Replace(_T("[ServernameH]"), _T(""));
	}
	Out.Replace(_T("[ServernameM]"), strTmp);

	_GetPlainResString(strTmp, IDS_IP);
	if (!WSserverColumnHidden[1]) {
		Out.Replace(_T("[AddressI]"), (pThis->m_Params.ServerSort == SERVER_SORT_IP) ? pcSortIcon : _T(""));
		Out.Replace(_T("[AddressH]"), strTmp);
	} else {
		Out.Replace(_T("[AddressI]"), _T(""));
		Out.Replace(_T("[AddressH]"), _T(""));
	}
	Out.Replace(_T("[AddressM]"), strTmp);

	_GetPlainResString(strTmp, IDS_DESCRIPTION);
	if (!WSserverColumnHidden[2]) {
		Out.Replace(_T("[DescriptionI]"), (pThis->m_Params.ServerSort == SERVER_SORT_DESCRIPTION) ? pcSortIcon : _T(""));
		Out.Replace(_T("[DescriptionH]"), strTmp);
	} else {
		Out.Replace(_T("[DescriptionI]"), _T(""));
		Out.Replace(_T("[DescriptionH]"), _T(""));
	}
	Out.Replace(_T("[DescriptionM]"), strTmp);

	_GetPlainResString(strTmp, IDS_PING);
	if (!WSserverColumnHidden[3]) {
		Out.Replace(_T("[PingI]"), (pThis->m_Params.ServerSort == SERVER_SORT_PING) ? pcSortIcon : _T(""));
		Out.Replace(_T("[PingH]"), strTmp);
	} else {
		Out.Replace(_T("[PingI]"), _T(""));
		Out.Replace(_T("[PingH]"), _T(""));
	}
	Out.Replace(_T("[PingM]"), strTmp);

	_GetPlainResString(strTmp, IDS_UUSERS);
	if (!WSserverColumnHidden[4]) {
		Out.Replace(_T("[UsersI]"), (pThis->m_Params.ServerSort == SERVER_SORT_USERS) ? pcSortIcon : _T(""));
		Out.Replace(_T("[UsersH]"), strTmp);
	} else {
		Out.Replace(_T("[UsersI]"), _T(""));
		Out.Replace(_T("[UsersH]"), _T(""));
	}
	Out.Replace(_T("[UsersM]"), strTmp);

	_GetPlainResString(strTmp, IDS_FILES);
	if (!WSserverColumnHidden[5]) {
		Out.Replace(_T("[FilesI]"), (pThis->m_Params.ServerSort == SERVER_SORT_FILES) ? pcSortIcon : _T(""));
		Out.Replace(_T("[FilesH]"), strTmp);
	} else {
		Out.Replace(_T("[FilesI]"), _T(""));
		Out.Replace(_T("[FilesH]"), _T(""));
	}
	Out.Replace(_T("[FilesM]"), strTmp);

	_GetPlainResString(strTmp, IDS_PRIORITY);
	if (!WSserverColumnHidden[6]) {
		Out.Replace(_T("[PriorityI]"), (pThis->m_Params.ServerSort == SERVER_SORT_PRIORITY) ? pcSortIcon : _T(""));
		Out.Replace(_T("[PriorityH]"), strTmp);
	} else {
		Out.Replace(_T("[PriorityI]"), _T(""));
		Out.Replace(_T("[PriorityH]"), _T(""));
	}
	Out.Replace(_T("[PriorityM]"), strTmp);

	_GetPlainResString(strTmp, IDS_UFAILED);
	if (!WSserverColumnHidden[7]) {
		Out.Replace(_T("[FailedI]"), (pThis->m_Params.ServerSort == SERVER_SORT_FAILED) ? pcSortIcon : _T(""));
		Out.Replace(_T("[FailedH]"), strTmp);
	} else {
		Out.Replace(_T("[FailedI]"), _T(""));
		Out.Replace(_T("[FailedH]"), _T(""));
	}
	Out.Replace(_T("[FailedM]"), strTmp);

	_GetPlainResString(strTmp, IDS_SERVER_LIMITS);
	if (!WSserverColumnHidden[8]) {
		Out.Replace(_T("[LimitI]"), (pThis->m_Params.ServerSort == SERVER_SORT_LIMIT) ? pcSortIcon : _T(""));
		Out.Replace(_T("[LimitH]"), strTmp);
	} else {
		Out.Replace(_T("[LimitI]"), _T(""));
		Out.Replace(_T("[LimitH]"), _T(""));
	}
	Out.Replace(_T("[LimitM]"), strTmp);

	_GetPlainResString(strTmp, IDS_SV_SERVERINFO);
	if (!WSserverColumnHidden[9]) {
		Out.Replace(_T("[VersionI]"), (pThis->m_Params.ServerSort == SERVER_SORT_VERSION) ? pcSortIcon : _T(""));
		Out.Replace(_T("[VersionH]"), strTmp);
	} else {
		Out.Replace(_T("[VersionI]"), _T(""));
		Out.Replace(_T("[VersionH]"), _T(""));
	}
	Out.Replace(_T("[VersionM]"), strTmp);

	Out.Replace(_T("[Actions]"), _GetPlainResString(IDS_WEB_ACTIONS));

	CArray<ServerEntry> ServerArray;

	// Populating array
	for (INT_PTR sc = 0; sc < theApp.serverlist->GetServerCount(); ++sc) {
		CServer* cur_file = theApp.serverlist->GetServerAt(sc);
		ServerEntry Entry;
		Entry.sServerName = _SpecialChars(cur_file->GetListName());
		Entry.sServerIP = cur_file->GetAddress();
		Entry.nServerPort = cur_file->GetPort();
		Entry.sServerDescription = _SpecialChars(cur_file->GetDescription());
		Entry.nServerPing = cur_file->GetPing();
		Entry.nServerUsers = cur_file->GetUsers();
		Entry.nServerMaxUsers = cur_file->GetMaxUsers();
		Entry.nServerFiles = cur_file->GetFiles();
		Entry.bServerStatic = cur_file->IsStaticMember();
		if (cur_file->GetPreference() == SRV_PR_HIGH) {
			Entry.sServerPriority = _GetPlainResString(IDS_PRIOHIGH);
			Entry.nServerPriority = 2;
		} else if (cur_file->GetPreference() == SRV_PR_NORMAL) {
			Entry.sServerPriority = _GetPlainResString(IDS_PRIONORMAL);
			Entry.nServerPriority = 1;
		} else if (cur_file->GetPreference() == SRV_PR_LOW) {
			Entry.sServerPriority = _GetPlainResString(IDS_PRIOLOW);
			Entry.nServerPriority = 0;
		}
		Entry.nServerFailed = cur_file->GetFailedCount();
		Entry.nServerSoftLimit = cur_file->GetSoftFiles();
		Entry.nServerHardLimit = cur_file->GetHardFiles();
		Entry.sServerVersion = cur_file->GetVersion();
		if (inet_addr(CStringA(Entry.sServerIP)) != INADDR_NONE) {
			int counter = 0;
			CString newip;
			for (int j = 0; j < 4; ++j) {
				CString temp = Entry.sServerIP.Tokenize(_T("."), counter);
				if (temp.GetLength() == 1)
					newip += _T("00");
				else if (temp.GetLength() == 2)
					newip += _T('0');
				//else if (temp.GetLength() == 3)
				newip += temp;
			}
			Entry.sServerFullIP = newip;
		} else
			Entry.sServerFullIP = Entry.sServerIP;
		if (cur_file->GetFailedCount() > 0)
			Entry.sServerState = _T("failed");
		else
			Entry.sServerState = _T("disconnected");
		if (theApp.serverconnect->IsConnecting()) {
			Entry.sServerState = _T("connecting");
		} else if (theApp.serverconnect->IsConnected()) {
			if (theApp.serverconnect->GetCurrentServer()->GetFullIP() == cur_file->GetFullIP()) {
				if (!theApp.serverconnect->IsLowID())
					Entry.sServerState = _T("high");
				else
					Entry.sServerState = _T("low");
			}
		}
		ServerArray.Add(Entry);
	}
	// Sorting (simple bubble sort, we don't have tons of data here)
	bool bSorted = true;
	for (int nMax = 0; bSorted && nMax < ServerArray.GetCount() * 2; nMax++) {
		bSorted = false;
		for (int i = 0; i < ServerArray.GetCount() - 1; i++) {
			bool bSwap = false;
			switch (pThis->m_Params.ServerSort) {
			case SERVER_SORT_STATE:
				bSwap = ServerArray[i].sServerState.CompareNoCase(ServerArray[i + 1].sServerState) > 0;
				break;
			case SERVER_SORT_NAME:
				bSwap = ServerArray[i].sServerName.CompareNoCase(ServerArray[i + 1].sServerName) < 0;
				break;
			case SERVER_SORT_IP:
				bSwap = ServerArray[i].sServerFullIP < ServerArray[i + 1].sServerFullIP;
				break;
			case SERVER_SORT_DESCRIPTION:
				bSwap = ServerArray[i].sServerDescription.CompareNoCase(ServerArray[i + 1].sServerDescription) < 0;
				break;
			case SERVER_SORT_PING:
				bSwap = ServerArray[i].nServerPing < ServerArray[i + 1].nServerPing;
				break;
			case SERVER_SORT_USERS:
				bSwap = ServerArray[i].nServerUsers < ServerArray[i + 1].nServerUsers;
				break;
			case SERVER_SORT_FILES:
				bSwap = ServerArray[i].nServerFiles < ServerArray[i + 1].nServerFiles;
				break;
			case SERVER_SORT_PRIORITY:
				bSwap = ServerArray[i].nServerPriority < ServerArray[i + 1].nServerPriority;
				break;
			case SERVER_SORT_FAILED:
				bSwap = ServerArray[i].nServerFailed < ServerArray[i + 1].nServerFailed;
				break;
			case SERVER_SORT_LIMIT:
				bSwap = ServerArray[i].nServerSoftLimit < ServerArray[i + 1].nServerSoftLimit;
				break;
			case SERVER_SORT_VERSION:
				bSwap = ServerArray[i].sServerVersion < ServerArray[i + 1].sServerVersion;
				break;
			}
			if (pThis->m_Params.bServerSortReverse)
				bSwap = !bSwap;
			if (bSwap) {
				bSorted = true;
				ServerEntry TmpEntry = ServerArray[i];
				ServerArray[i] = ServerArray[i + 1];
				ServerArray[i + 1] = TmpEntry;
			}
		}
	}

	// Displaying
	const TCHAR	*pcTmp, *pcTmp2;
	CString sList, HTTPProcessData, sServerPort, ed2k;
	CString OutE = pThis->m_Templates.sServerLine; // List Entry Templates

	OutE.Replace(_T("[admin]"), (bAdmin ? _T("admin") : _T("")));
	OutE.Replace(_T("[session]"), sSession);

	for (int i = 0; i < ServerArray.GetCount(); i++) {
		HTTPProcessData = OutE;	// Copy Entry Line to Temp

		sServerPort.Format(_T("%i"), ServerArray[i].nServerPort);
		ed2k.Format(_T("ed2k://|server|%s|%s|/"), (LPCTSTR)ServerArray[i].sServerIP, (LPCTSTR)sServerPort);

		if (ServerArray[i].sServerIP == _ParseURL(Data.sURL, _T("ip")) && sServerPort == _ParseURL(Data.sURL, _T("port")))
			pcTmp = _T("checked");
		else
			pcTmp = _T("checked_no");
		HTTPProcessData.Replace(_T("[LastChangedDataset]"), pcTmp);

		if (ServerArray[i].bServerStatic) {
			pcTmp = _T("staticsrv");
			pcTmp2 = _T("static");
		} else {
			pcTmp = _T("");
			pcTmp2 = _T("none");
		}
		HTTPProcessData.Replace(_T("[isstatic]"), pcTmp);
		HTTPProcessData.Replace(_T("[ServerType]"), pcTmp2);

		const TCHAR *pcSrvPriority;
		switch (ServerArray[i].nServerPriority) {
		case 0:
			pcSrvPriority = _T("Low");
			break;
		case 1:
			pcSrvPriority = _T("Normal");
			break;
		case 2:
			pcSrvPriority = _T("High");
			break;
		default:
			pcSrvPriority = _T("");
		}

		HTTPProcessData.Replace(_T("[ed2k]"), ed2k);
		HTTPProcessData.Replace(_T("[ip]"), ServerArray[i].sServerIP);
		HTTPProcessData.Replace(_T("[port]"), sServerPort);
		HTTPProcessData.Replace(_T("[server-priority]"), pcSrvPriority);

		// DonGato: reduced large servernames or descriptions
		if (!WSserverColumnHidden[0]) {
			if (ServerArray[i].sServerName.GetLength() > SHORT_LENGTH) {
				CString s;
				s.Format(_T("<acronym title=\"%s\">%s...</acronym>"), (LPCTSTR)ServerArray[i].sServerName, (LPCTSTR)ServerArray[i].sServerName.Left(SHORT_LENGTH - 3));
				HTTPProcessData.Replace(_T("[Servername]"), s);
			} else
				HTTPProcessData.Replace(_T("[Servername]"), ServerArray[i].sServerName);
		} else
			HTTPProcessData.Replace(_T("[Servername]"), _T(""));
		if (!WSserverColumnHidden[1]) {
			CString sAddr;
			sAddr.Format(_T("%s:%d"), (LPCTSTR)ServerArray[i].sServerIP, ServerArray[i].nServerPort);
			HTTPProcessData.Replace(_T("[Address]"), sAddr);
		} else
			HTTPProcessData.Replace(_T("[Address]"), _T(""));
		if (!WSserverColumnHidden[2]) {
			if (ServerArray[i].sServerDescription.GetLength() > SHORT_LENGTH) {
				CString s;
				s.Format(_T("<acronym title=\"%s\">%s...</acronym>"), (LPCTSTR)ServerArray[i].sServerDescription, (LPCTSTR)ServerArray[i].sServerDescription.Left(SHORT_LENGTH - 3));
				HTTPProcessData.Replace(_T("[Description]"), s);
			} else
				HTTPProcessData.Replace(_T("[Description]"), ServerArray[i].sServerDescription);
		} else
			HTTPProcessData.Replace(_T("[Description]"), _T(""));
		if (!WSserverColumnHidden[3]) {
			CString sPing;
			sPing.Format(_T("%d"), ServerArray[i].nServerPing);
			HTTPProcessData.Replace(_T("[Ping]"), sPing);
		} else
			HTTPProcessData.Replace(_T("[Ping]"), _T(""));

		if (!WSserverColumnHidden[4]) {
			CString sT;
			if (ServerArray[i].nServerUsers > 0) {
				sT = CastItoIShort(ServerArray[i].nServerUsers);
				if (ServerArray[i].nServerMaxUsers > 0)
					sT.AppendFormat(_T(" (%s)"), (LPCTSTR)CastItoIShort(ServerArray[i].nServerMaxUsers));
			}
			HTTPProcessData.Replace(_T("[Users]"), sT);
		} else
			HTTPProcessData.Replace(_T("[Users]"), _T(""));
		if (!WSserverColumnHidden[5] && (ServerArray[i].nServerFiles > 0)) {
			HTTPProcessData.Replace(_T("[Files]"), CastItoIShort(ServerArray[i].nServerFiles));
		} else
			HTTPProcessData.Replace(_T("[Files]"), _T(""));
		if (!WSserverColumnHidden[6])
			HTTPProcessData.Replace(_T("[Priority]"), ServerArray[i].sServerPriority);
		else
			HTTPProcessData.Replace(_T("[Priority]"), _T(""));
		if (!WSserverColumnHidden[7]) {
			CString sFailed; sFailed.Format(_T("%d"), ServerArray[i].nServerFailed);
			HTTPProcessData.Replace(_T("[Failed]"), sFailed);
		} else
			HTTPProcessData.Replace(_T("[Failed]"), _T(""));
		if (!WSserverColumnHidden[8]) {
			CString strTemp;
			strTemp.Format(_T("%s (%s)"), (LPCTSTR)CastItoIShort(ServerArray[i].nServerSoftLimit),
				(LPCTSTR)CastItoIShort(ServerArray[i].nServerHardLimit));
			HTTPProcessData.Replace(_T("[Limit]"), strTemp);
		} else
			HTTPProcessData.Replace(_T("[Limit]"), _T(""));
		if (!WSserverColumnHidden[9]) {
			if (ServerArray[i].sServerVersion.GetLength() > SHORT_LENGTH_MIN) {
				CString s;
				s.Format(_T("<acronym title=\"%s\">%s...</acronym>"), (LPCTSTR)ServerArray[i].sServerVersion, (LPCTSTR)ServerArray[i].sServerVersion.Left(SHORT_LENGTH_MIN - 3));
				HTTPProcessData.Replace(_T("[Version]"), s);
			} else
				HTTPProcessData.Replace(_T("[Version]"), ServerArray[i].sServerVersion);
		} else
			HTTPProcessData.Replace(_T("[Version]"), _T(""));
		HTTPProcessData.Replace(_T("[ServerState]"), ServerArray[i].sServerState);
		sList += HTTPProcessData;
	}
	Out.Replace(_T("[ServersList]"), sList);
	Out.Replace(_T("[Session]"), sSession);

	return Out;
}

CString CWebServer::_GetTransferList(const ThreadData& Data)
{
	CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString sSession = _ParseURL(Data.sURL, _T("ses"));
	long lSession = _tstol(_ParseURL(Data.sURL, _T("ses")));
	bool bAdmin = IsSessionAdmin(Data, sSession);

	// cat
	int cat;
	const CString catp = _ParseURL(Data.sURL, _T("cat"));
	if (catp.IsEmpty())
		cat = _GetLastUserCat(Data, lSession);
	else {
		cat = _tstoi(catp);
		_SetLastUserCat(Data, lSession, cat);
	}
	// commands
	CString sCat;
	if (cat != 0)
		sCat.Format(_T("&amp;cat=%i"), cat);

	CString Out;
	if (thePrefs.GetCatCount() > 1)
		InsertCatBox(Out, cat, CString(), true, true, sSession, CString());
	else
		Out.Replace(_T("[CATBOX]"), _T(""));


	const CString sClear = _ParseURL(Data.sURL, _T("clearcompleted"));
	if (bAdmin && !sClear.IsEmpty()) {
		if (sClear.CompareNoCase(_T("all")) == 0)
			theApp.emuledlg->SendMessage(WEB_CLEAR_COMPLETED, (WPARAM)0, (LPARAM)cat);
		else {
			if (!sClear.IsEmpty()) {
				uchar FileHash[16];
				_GetFileHash(sClear, FileHash);
				uchar* pFileHash = new uchar[16];

				md4cpy(pFileHash, FileHash);
				theApp.emuledlg->SendMessage(WEB_CLEAR_COMPLETED, (WPARAM)1, reinterpret_cast<LPARAM>(pFileHash));
			}
		}
	}

	CString HTTPTemp = _ParseURL(Data.sURL, _T("ed2k"));

	if (bAdmin && !HTTPTemp.IsEmpty())
		theApp.emuledlg->SendMessage(WEB_ADDDOWNLOADS, (WPARAM)(LPCTSTR)HTTPTemp, cat);

	HTTPTemp = _ParseURL(Data.sURL, _T("c")).MakeLower();

	if (HTTPTemp == _T("menudown")) {
		int iMenu = _tstol(_ParseURL(Data.sURL, _T("m")));
		bool bValue = _tstol(_ParseURL(Data.sURL, _T("v"))) != 0;
		WSdownloadColumnHidden[iMenu] = bValue;

		CIni ini(thePrefs.GetConfigFile(), _T("WebServer"));

		SaveWIConfigArray(WSdownloadColumnHidden, ARRSIZE(WSdownloadColumnHidden), _T("downloadColumnHidden"));
	} else if (HTTPTemp == _T("menuup")) {
		int iMenu = _tstol(_ParseURL(Data.sURL, _T("m")));
		bool bValue = _tstol(_ParseURL(Data.sURL, _T("v"))) != 0;
		WSuploadColumnHidden[iMenu] = bValue;
		SaveWIConfigArray(WSuploadColumnHidden, ARRSIZE(WSuploadColumnHidden), _T("uploadColumnHidden"));
	} else if (HTTPTemp == _T("menuqueue")) {
		int iMenu = _tstol(_ParseURL(Data.sURL, _T("m")));
		bool bValue = _tstol(_ParseURL(Data.sURL, _T("v"))) != 0;
		WSqueueColumnHidden[iMenu] = bValue;
		SaveWIConfigArray(WSqueueColumnHidden, ARRSIZE(WSqueueColumnHidden), _T("queueColumnHidden"));
	} else if (HTTPTemp == _T("menuprio")) {
		if (bAdmin) {
			const CString sPrio = _ParseURL(Data.sURL, _T("p")).MakeLower();
			int prio;
			if (sPrio == _T("low"))
				prio = PR_LOW;
			else if (sPrio == _T("high"))
				prio = PR_HIGH;
			else if (sPrio == _T("normal"))
				prio = PR_NORMAL;
			else //if (sPrio == _T("auto"))
				prio = PR_AUTO; //make auto the default
			SendMessage(theApp.emuledlg->m_hWnd, WEB_CATPRIO, (WPARAM)cat, (LPARAM)prio);
		}
	}

	if (bAdmin) {
		const CString sOp = _ParseURL(Data.sURL, _T("op")).MakeLower();

		if (!sOp.IsEmpty()) {
			const CString sFile = _ParseURL(Data.sURL, _T("file"));

			if (!sFile.IsEmpty()) {
				uchar FileHash[16];
				CPartFile *found_file = theApp.downloadqueue->GetFileByID(_GetFileHash(sFile, FileHash));
				if (found_file) {	// SyruS all actions require a found file (removed double-check inside)
					if (sOp == _T("stop"))
						found_file->StopFile();
					else if (sOp == _T("pause"))
						found_file->PauseFile();
					else if (sOp == _T("resume"))
						found_file->ResumeFile();
					else if (sOp == _T("cancel")) {
						found_file->DeleteFile();
						SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_UPD_CATTABS, 0);
					} else if (sOp == _T("getflc"))
						found_file->GetPreviewPrio();
					else if (sOp == _T("rename")) {
						const CString sNewName = _ParseURL(Data.sURL, _T("name"));
						theApp.emuledlg->SendMessage(WEB_FILE_RENAME, (WPARAM)found_file, (LPARAM)(LPCTSTR)sNewName);
					} else if (sOp == _T("priolow")) {
						found_file->SetAutoDownPriority(false);
						found_file->SetDownPriority(PR_LOW);
					} else if (sOp == _T("prionormal")) {
						found_file->SetAutoDownPriority(false);
						found_file->SetDownPriority(PR_NORMAL);
					} else if (sOp == _T("priohigh")) {
						found_file->SetAutoDownPriority(false);
						found_file->SetDownPriority(PR_HIGH);
					} else if (sOp == _T("prioauto")) {
						found_file->SetAutoDownPriority(true);
						found_file->SetDownPriority(PR_HIGH);
					} else if (sOp == _T("setcat")) {
						const CString newcat = _ParseURL(Data.sURL, _T("filecat"));
						if (!newcat.IsEmpty())
							found_file->SetCategory(_tstol(newcat));
					}
				}
			} else {
				const CString sUser = _ParseURL(Data.sURL, _T("userhash"));
				uchar UserHash[16];

				if (_GetFileHash(sUser, UserHash) != 0) {
					CUpDownClient* cur_client = theApp.clientlist->FindClientByUserHash(UserHash);
					if (cur_client) {
						if (sOp == _T("addfriend"))
							SendMessage(theApp.emuledlg->m_hWnd, WEB_ADDREMOVEFRIEND, (WPARAM)cur_client, (LPARAM)1);
						else if (sOp == _T("removefriend")) {
							const CFriend *f = theApp.friendlist->SearchFriend(UserHash, 0, 0);
							if (f)
								SendMessage(theApp.emuledlg->m_hWnd, WEB_ADDREMOVEFRIEND, (WPARAM)f, (LPARAM)0);
						}
					}
				}
			}
		}
	}

	CString strTmp = _ParseURL(Data.sURL, _T("sortreverse"));
	const CString sSort = _ParseURL(Data.sURL, _T("sort")).MakeLower();
	if (!sSort.IsEmpty()) {
		bool bDirection = false;
		if (sSort == _T("dstate"))
			pThis->m_Params.DownloadSort = DOWN_SORT_STATE;
		else if (sSort == _T("dtype"))
			pThis->m_Params.DownloadSort = DOWN_SORT_TYPE;
		else if (sSort == _T("dname")) {
			pThis->m_Params.DownloadSort = DOWN_SORT_NAME;
			bDirection = true;
		} else if (sSort == _T("dsize"))
			pThis->m_Params.DownloadSort = DOWN_SORT_SIZE;
		else if (sSort == _T("dtransferred"))
			pThis->m_Params.DownloadSort = DOWN_SORT_TRANSFERRED;
		else if (sSort == _T("dspeed"))
			pThis->m_Params.DownloadSort = DOWN_SORT_SPEED;
		else if (sSort == _T("dprogress"))
			pThis->m_Params.DownloadSort = DOWN_SORT_PROGRESS;
		else if (sSort == _T("dsources"))
			pThis->m_Params.DownloadSort = DOWN_SORT_SOURCES;
		else if (sSort == _T("dpriority"))
			pThis->m_Params.DownloadSort = DOWN_SORT_PRIORITY;
		else if (sSort == _T("dcategory")) {
			pThis->m_Params.DownloadSort = DOWN_SORT_CATEGORY;
			bDirection = true;
		} else if (sSort == _T("uuser")) {
			pThis->m_Params.UploadSort = UP_SORT_USER;
			bDirection = true;
		} else if (sSort == _T("uclient"))
			pThis->m_Params.UploadSort = UP_SORT_CLIENT;
		else if (sSort == _T("uversion"))
			pThis->m_Params.UploadSort = UP_SORT_VERSION;
		else if (sSort == _T("ufilename")) {
			pThis->m_Params.UploadSort = UP_SORT_FILENAME;
			bDirection = true;
		} else if (sSort == _T("utransferred"))
			pThis->m_Params.UploadSort = UP_SORT_TRANSFERRED;
		else if (sSort == _T("uspeed"))
			pThis->m_Params.UploadSort = UP_SORT_SPEED;
		else if (sSort == _T("qclient"))
			pThis->m_Params.QueueSort = QU_SORT_CLIENT;
		else if (sSort == _T("quser")) {
			pThis->m_Params.QueueSort = QU_SORT_USER;
			bDirection = true;
		} else if (sSort == _T("qversion"))
			pThis->m_Params.QueueSort = QU_SORT_VERSION;
		else if (sSort == _T("qfilename")) {
			pThis->m_Params.QueueSort = QU_SORT_FILENAME;
			bDirection = true;
		} else if (sSort == _T("qscore"))
			pThis->m_Params.QueueSort = QU_SORT_SCORE;

		if (!strTmp.IsEmpty())
			bDirection = (strTmp.CompareNoCase(_T("true")) == 0);
		
		switch (sSort[0]) {
		case _T('d'):
			pThis->m_Params.bDownloadSortReverse = bDirection;
			break;
		case _T('u'):
			pThis->m_Params.bUploadSortReverse = bDirection;
			break;
		case _T('q'):
			pThis->m_Params.bQueueSortReverse = bDirection;
		}
	}

	HTTPTemp = _ParseURL(Data.sURL, _T("showuploadqueue"));
	if (HTTPTemp == _T("true"))
		pThis->m_Params.bShowUploadQueue = true;
	else if (HTTPTemp == _T("false"))
		pThis->m_Params.bShowUploadQueue = false;

	HTTPTemp = _ParseURL(Data.sURL, _T("showuploadqueuebanned")).MakeLower();
	if (HTTPTemp == _T("true"))
		pThis->m_Params.bShowUploadQueueBanned = true;
	else if (HTTPTemp == _T("false"))
		pThis->m_Params.bShowUploadQueueBanned = false;

	HTTPTemp = _ParseURL(Data.sURL, _T("showuploadqueuefriend")).MakeLower();
	if (HTTPTemp == _T("true"))
		pThis->m_Params.bShowUploadQueueFriend = true;
	else if (HTTPTemp == _T("false"))
		pThis->m_Params.bShowUploadQueueFriend = false;

	Out += pThis->m_Templates.sTransferImages;
	Out += pThis->m_Templates.sTransferList;
	Out.Replace(_T("[DownloadHeader]"), pThis->m_Templates.sTransferDownHeader);
	Out.Replace(_T("[DownloadFooter]"), pThis->m_Templates.sTransferDownFooter);
	Out.Replace(_T("[UploadHeader]"), pThis->m_Templates.sTransferUpHeader);
	Out.Replace(_T("[UploadFooter]"), pThis->m_Templates.sTransferUpFooter);
	InsertCatBox(Out, cat, pThis->m_Templates.sCatArrow, true, true, sSession, _T(""));

	strTmp = (pThis->m_Params.bDownloadSortReverse) ? _T("&amp;sortreverse=false") : _T("&amp;sortreverse=true");

	if (pThis->m_Params.DownloadSort == DOWN_SORT_STATE)
		Out.Replace(_T("[SortDState]"), strTmp);
	else
		Out.Replace(_T("[SortDState]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_TYPE)
		Out.Replace(_T("[SortDType]"), strTmp);
	else
		Out.Replace(_T("[SortDType]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_NAME)
		Out.Replace(_T("[SortDName]"), strTmp);
	else
		Out.Replace(_T("[SortDName]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_SIZE)
		Out.Replace(_T("[SortDSize]"), strTmp);
	else
		Out.Replace(_T("[SortDSize]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_TRANSFERRED)
		Out.Replace(_T("[SortDTransferred]"), strTmp);
	else
		Out.Replace(_T("[SortDTransferred]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_SPEED)
		Out.Replace(_T("[SortDSpeed]"), strTmp);
	else
		Out.Replace(_T("[SortDSpeed]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_PROGRESS)
		Out.Replace(_T("[SortDProgress]"), strTmp);
	else
		Out.Replace(_T("[SortDProgress]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_SOURCES)
		Out.Replace(_T("[SortDSources]"), strTmp);
	else
		Out.Replace(_T("[SortDSources]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_PRIORITY)
		Out.Replace(_T("[SortDPriority]"), strTmp);
	else
		Out.Replace(_T("[SortDPriority]"), _T(""));
	if (pThis->m_Params.DownloadSort == DOWN_SORT_CATEGORY)
		Out.Replace(_T("[SortDCategory]"), strTmp);
	else
		Out.Replace(_T("[SortDCategory]"), _T(""));

	strTmp = (pThis->m_Params.bUploadSortReverse) ? _T("&amp;sortreverse=false") : _T("&amp;sortreverse=true");

	if (pThis->m_Params.UploadSort == UP_SORT_CLIENT)
		Out.Replace(_T("[SortUClient]"), strTmp);
	else
		Out.Replace(_T("[SortUClient]"), _T(""));
	if (pThis->m_Params.UploadSort == UP_SORT_USER)
		Out.Replace(_T("[SortUUser]"), strTmp);
	else
		Out.Replace(_T("[SortUUser]"), _T(""));
	if (pThis->m_Params.UploadSort == UP_SORT_VERSION)
		Out.Replace(_T("[SortUVersion]"), strTmp);
	else
		Out.Replace(_T("[SortUVersion]"), _T(""));
	if (pThis->m_Params.UploadSort == UP_SORT_FILENAME)
		Out.Replace(_T("[SortUFilename]"), strTmp);
	else
		Out.Replace(_T("[SortUFilename]"), _T(""));
	if (pThis->m_Params.UploadSort == UP_SORT_TRANSFERRED)
		Out.Replace(_T("[SortUTransferred]"), strTmp);
	else
		Out.Replace(_T("[SortUTransferred]"), _T(""));
	if (pThis->m_Params.UploadSort == UP_SORT_SPEED)
		Out.Replace(_T("[SortUSpeed]"), strTmp);
	else
		Out.Replace(_T("[SortUSpeed]"), _T(""));

	const TCHAR *pcSortIcon = (pThis->m_Params.bDownloadSortReverse) ? pThis->m_Templates.sUpArrow : pThis->m_Templates.sDownArrow;

	_GetPlainResString(strTmp, IDS_DL_FILENAME);
	if (!WSdownloadColumnHidden[0]) {
		Out.Replace(_T("[DFilenameI]"), (pThis->m_Params.DownloadSort == DOWN_SORT_NAME) ? pcSortIcon : _T(""));
		Out.Replace(_T("[DFilename]"), strTmp);
	} else {
		Out.Replace(_T("[DFilenameI]"), _T(""));
		Out.Replace(_T("[DFilename]"), _T(""));
	}
	Out.Replace(_T("[DFilenameM]"), strTmp);

	_GetPlainResString(strTmp, IDS_DL_SIZE);
	if (!WSdownloadColumnHidden[1]) {
		Out.Replace(_T("[DSizeI]"), (pThis->m_Params.DownloadSort == DOWN_SORT_SIZE) ? pcSortIcon : _T(""));
		Out.Replace(_T("[DSize]"), strTmp);
	} else {
		Out.Replace(_T("[DSizeI]"), _T(""));
		Out.Replace(_T("[DSize]"), _T(""));
	}
	Out.Replace(_T("[DSizeM]"), strTmp);

	_GetPlainResString(strTmp, IDS_DL_TRANSFCOMPL);
	if (!WSdownloadColumnHidden[2]) {
		Out.Replace(_T("[DTransferredI]"), (pThis->m_Params.DownloadSort == DOWN_SORT_TRANSFERRED) ? pcSortIcon : _T(""));
		Out.Replace(_T("[DTransferred]"), strTmp);
	} else {
		Out.Replace(_T("[DTransferredI]"), _T(""));
		Out.Replace(_T("[DTransferred]"), _T(""));
	}
	Out.Replace(_T("[DTransferredM]"), strTmp);

	_GetPlainResString(strTmp, IDS_DL_PROGRESS);
	if (!WSdownloadColumnHidden[3]) {
		Out.Replace(_T("[DProgressI]"), (pThis->m_Params.DownloadSort == DOWN_SORT_PROGRESS) ? pcSortIcon : _T(""));
		Out.Replace(_T("[DProgress]"), strTmp);
	} else {
		Out.Replace(_T("[DProgressI]"), _T(""));
		Out.Replace(_T("[DProgress]"), _T(""));
	}
	Out.Replace(_T("[DProgressM]"), strTmp);

	_GetPlainResString(strTmp, IDS_DL_SPEED);
	if (!WSdownloadColumnHidden[4]) {
		Out.Replace(_T("[DSpeedI]"), (pThis->m_Params.DownloadSort == DOWN_SORT_SPEED) ? pcSortIcon : _T(""));
		Out.Replace(_T("[DSpeed]"), strTmp);
	} else {
		Out.Replace(_T("[DSpeedI]"), _T(""));
		Out.Replace(_T("[DSpeed]"), _T(""));
	}
	Out.Replace(_T("[DSpeedM]"), strTmp);

	_GetPlainResString(strTmp, IDS_DL_SOURCES);
	if (!WSdownloadColumnHidden[5]) {
		Out.Replace(_T("[DSourcesI]"), (pThis->m_Params.DownloadSort == DOWN_SORT_SOURCES) ? pcSortIcon : _T(""));
		Out.Replace(_T("[DSources]"), strTmp);
	} else {
		Out.Replace(_T("[DSourcesI]"), _T(""));
		Out.Replace(_T("[DSources]"), _T(""));
	}
	Out.Replace(_T("[DSourcesM]"), strTmp);

	_GetPlainResString(strTmp, IDS_PRIORITY);
	if (!WSdownloadColumnHidden[6]) {
		Out.Replace(_T("[DPriorityI]"), (pThis->m_Params.DownloadSort == DOWN_SORT_PRIORITY) ? pcSortIcon : _T(""));
		Out.Replace(_T("[DPriority]"), strTmp);
	} else {
		Out.Replace(_T("[DPriorityI]"), _T(""));
		Out.Replace(_T("[DPriority]"), _T(""));
	}
	Out.Replace(_T("[DPriorityM]"), strTmp);

	_GetPlainResString(strTmp, IDS_CAT);
	if (!WSdownloadColumnHidden[7]) {
		Out.Replace(_T("[DCategoryI]"), (pThis->m_Params.DownloadSort == DOWN_SORT_CATEGORY) ? pcSortIcon : _T(""));
		Out.Replace(_T("[DCategory]"), strTmp);
	} else {
		Out.Replace(_T("[DCategoryI]"), _T(""));
		Out.Replace(_T("[DCategory]"), _T(""));
	}
	Out.Replace(_T("[DCategoryM]"), strTmp);

	// add 8th columns here


	pcSortIcon = (pThis->m_Params.bUploadSortReverse) ? pThis->m_Templates.sUpArrow : pThis->m_Templates.sDownArrow;

	_GetPlainResString(strTmp, IDS_QL_USERNAME);
	if (!WSuploadColumnHidden[0]) {
		Out.Replace(_T("[UUserI]"), (pThis->m_Params.UploadSort == UP_SORT_USER) ? pcSortIcon : _T(""));
		Out.Replace(_T("[UUser]"), strTmp);
	} else {
		Out.Replace(_T("[UUserI]"), _T(""));
		Out.Replace(_T("[UUser]"), _T(""));
	}
	Out.Replace(_T("[UUserM]"), strTmp);

	_GetPlainResString(strTmp, IDS_CD_VERSION);
	if (!WSuploadColumnHidden[1]) {
		Out.Replace(_T("[UVersionI]"), (pThis->m_Params.UploadSort == UP_SORT_VERSION) ? pcSortIcon : _T(""));
		Out.Replace(_T("[UVersion]"), strTmp);
	} else {
		Out.Replace(_T("[UVersionI]"), _T(""));
		Out.Replace(_T("[UVersion]"), _T(""));
	}
	Out.Replace(_T("[UVersionM]"), strTmp);

	_GetPlainResString(strTmp, IDS_DL_FILENAME);
	if (!WSuploadColumnHidden[2]) {
		Out.Replace(_T("[UFilenameI]"), (pThis->m_Params.UploadSort == UP_SORT_FILENAME) ? pcSortIcon : _T(""));
		Out.Replace(_T("[UFilename]"), strTmp);
	} else {
		Out.Replace(_T("[UFilenameI]"), _T(""));
		Out.Replace(_T("[UFilename]"), _T(""));
	}
	Out.Replace(_T("[UFilenameM]"), strTmp);

	_GetPlainResString(strTmp, IDS_STATS_SRATIO);
	if (!WSuploadColumnHidden[3]) {
		Out.Replace(_T("[UTransferredI]"), (pThis->m_Params.UploadSort == UP_SORT_TRANSFERRED) ? pcSortIcon : _T(""));
		Out.Replace(_T("[UTransferred]"), strTmp);
	} else {
		Out.Replace(_T("[UTransferredI]"), _T(""));
		Out.Replace(_T("[UTransferred]"), _T(""));
	}
	Out.Replace(_T("[UTransferredM]"), strTmp);

	_GetPlainResString(strTmp, IDS_DL_SPEED);
	if (!WSuploadColumnHidden[4]) {
		Out.Replace(_T("[USpeedI]"), (pThis->m_Params.UploadSort == UP_SORT_SPEED) ? pcSortIcon : _T(""));
		Out.Replace(_T("[USpeed]"), strTmp);
	} else {
		Out.Replace(_T("[USpeedI]"), _T(""));
		Out.Replace(_T("[USpeed]"), _T(""));
	}
	Out.Replace(_T("[USpeedM]"), strTmp);

	Out.Replace(_T("[DownloadList]"), _GetPlainResString(IDS_TW_DOWNLOADS));
	Out.Replace(_T("[UploadList]"), _GetPlainResString(IDS_TW_UPLOADS));
	Out.Replace(_T("[Actions]"), _GetPlainResString(IDS_WEB_ACTIONS));
	Out.Replace(_T("[TotalDown]"), _GetPlainResString(IDS_INFLST_USER_TOTALDOWNLOAD));
	Out.Replace(_T("[TotalUp]"), _GetPlainResString(IDS_INFLST_USER_TOTALUPLOAD));
	Out.Replace(_T("[admin]"), (bAdmin) ? _T("admin") : _T(""));
	InsertCatBox(Out, cat, _T(""), true, true, sSession, _T(""));

	CArray<DownloadFiles> FilesArray;
	CArray<CPartFile*, CPartFile*> partlist;

	theApp.emuledlg->transferwnd->GetDownloadList()->GetDisplayedFiles(&partlist);

	// Populating array
	for (int i = 0; i < partlist.GetCount(); i++) {

		CPartFile* pPartFile = partlist[i];

		if (pPartFile) {
			if (cat < 0) {
				switch (cat) {
				case -1: if (pPartFile->GetCategory() != 0) continue; break;
				case -2: if (!pPartFile->IsPartFile()) continue; break;
				case -3: if (pPartFile->IsPartFile()) continue; break;
				case -4: if (!((pPartFile->GetStatus() == PS_READY || pPartFile->GetStatus() == PS_EMPTY) && pPartFile->GetTransferringSrcCount() == 0)) continue; break;
				case -5: if (!((pPartFile->GetStatus() == PS_READY || pPartFile->GetStatus() == PS_EMPTY) && pPartFile->GetTransferringSrcCount() > 0)) continue; break;
				case -6: if (pPartFile->GetStatus() != PS_ERROR) continue; break;
				case -7: if (pPartFile->GetStatus() != PS_PAUSED && !pPartFile->IsStopped()) continue; break;
				case -8: if (pPartFile->lastseencomplete == 0) continue; break;
				case -9: if (!pPartFile->IsMovie()) continue; break;
				case -10: if (ED2KFT_AUDIO != GetED2KFileTypeID(pPartFile->GetFileName())) continue; break;
				case -11: if (!pPartFile->IsArchive()) continue; break;
				case -12: if (ED2KFT_CDIMAGE != GetED2KFileTypeID(pPartFile->GetFileName())) continue; break;
				case -13: if (ED2KFT_DOCUMENT != GetED2KFileTypeID(pPartFile->GetFileName())) continue; break;
				case -14: if (ED2KFT_IMAGE != GetED2KFileTypeID(pPartFile->GetFileName())) continue; break;
				case -15: if (ED2KFT_PROGRAM != GetED2KFileTypeID(pPartFile->GetFileName())) continue; break;
				case -16: if (ED2KFT_EMULECOLLECTION != GetED2KFileTypeID(pPartFile->GetFileName())) continue; break;
				//JOHNTODO: Not too sure here.. I was going to add Collections but noticed something strange.. Are these supposed to match the list in PartFile around line 5132? Because they do not..
				}
			} else if (cat > 0 && pPartFile->GetCategory() != (UINT)cat)
				continue;

			DownloadFiles dFile;
			dFile.sFileName = _SpecialChars(pPartFile->GetFileName());
			dFile.sFileType = GetWebImageNameForFileType(dFile.sFileName);
			dFile.sFileNameJS = _SpecialChars(pPartFile->GetFileName());	//for javascript
			dFile.m_qwFileSize = pPartFile->GetFileSize();
			dFile.m_qwFileTransferred = pPartFile->GetCompletedSize();
			dFile.m_dblCompleted = pPartFile->GetPercentCompleted();
			dFile.lFileSpeed = pPartFile->GetDatarate();

			if (pPartFile->HasComment() || pPartFile->HasRating())
				dFile.iComment = pPartFile->HasBadRating() ? 2 : 1;
			else
				dFile.iComment = 0;

			dFile.iFileState = pPartFile->getPartfileStatusRang();

			LPCTSTR pFileState;
			switch (pPartFile->GetStatus()) {
			case PS_HASHING:
				pFileState = _T("hashing");
				break;
			case PS_WAITINGFORHASH:
				pFileState = _T("waitinghash");
				break;
			case PS_ERROR:
				pFileState = _T("error");
				break;
			case PS_COMPLETING:
				pFileState = _T("completing");
				break;
			case PS_COMPLETE:
				pFileState = _T("complete");
				break;
			case PS_PAUSED:
				pFileState = pPartFile->IsStopped() ? _T("stopped") : _T("paused");
				break;
			default:
				pFileState = (pPartFile->GetDatarate() > 0) ? _T("downloading") : _T("waiting");
			}
			dFile.sFileState = CString(pFileState);

			dFile.bFileAutoPrio = pPartFile->IsAutoDownPriority();
			dFile.nFilePrio = pPartFile->GetDownPriority();
			int pCat = pPartFile->GetCategory();

			CString strCategory = thePrefs.GetCategory(pCat)->strTitle;
			strCategory.Replace(_T("'"), _T("\\'"));

			dFile.sCategory = strCategory;

			dFile.sFileHash = md4str(pPartFile->GetFileHash());
			dFile.lSourceCount = pPartFile->GetSourceCount();
			dFile.lNotCurrentSourceCount = pPartFile->GetNotCurrentSourcesCount();
			dFile.lTransferringSourceCount = pPartFile->GetTransferringSrcCount();
			dFile.bIsComplete = !pPartFile->IsPartFile();
			dFile.bIsPreview = pPartFile->IsReadyForPreview();
			dFile.bIsGetFLC = pPartFile->GetPreviewPrio();

			if (theApp.GetPublicIP() != 0 && !theApp.IsFirewalled())
				dFile.sED2kLink = pPartFile->GetED2kLink(false, false, false, true, theApp.GetPublicIP());
			else
				dFile.sED2kLink = pPartFile->GetED2kLink();

			dFile.sFileInfo = _SpecialChars(pPartFile->GetInfoSummary(true), false);

			FilesArray.Add(dFile);
		}
	}


	// Sorting (simple bubble sort, we don't have tons of data here)
	bool bSorted = true;
	for (int nMax = 0; bSorted && nMax < FilesArray.GetCount() * 2; ++nMax) {
		bSorted = false;
		for (int i = 0; i < FilesArray.GetCount() - 1; i++) {
			bool bSwap = false;
			switch (pThis->m_Params.DownloadSort) {
			case DOWN_SORT_STATE:
				bSwap = FilesArray[i].iFileState < FilesArray[i + 1].iFileState;
				break;
			case DOWN_SORT_TYPE:
				bSwap = FilesArray[i].sFileType.CompareNoCase(FilesArray[i + 1].sFileType) < 0;
				break;
			case DOWN_SORT_NAME:
				bSwap = FilesArray[i].sFileName.CompareNoCase(FilesArray[i + 1].sFileName) < 0;
				break;
			case DOWN_SORT_SIZE:
				bSwap = FilesArray[i].m_qwFileSize < FilesArray[i + 1].m_qwFileSize;
				break;
			case DOWN_SORT_TRANSFERRED:
				bSwap = FilesArray[i].m_qwFileTransferred < FilesArray[i + 1].m_qwFileTransferred;
				break;
			case DOWN_SORT_SPEED:
				bSwap = FilesArray[i].lFileSpeed < FilesArray[i + 1].lFileSpeed;
				break;
			case DOWN_SORT_PROGRESS:
				bSwap = FilesArray[i].m_dblCompleted < FilesArray[i + 1].m_dblCompleted;
				break;
			case DOWN_SORT_SOURCES:
				bSwap = FilesArray[i].lSourceCount < FilesArray[i + 1].lSourceCount;
				break;
			case DOWN_SORT_PRIORITY:
				bSwap = FilesArray[i].nFilePrio < FilesArray[i + 1].nFilePrio;
				break;
			case DOWN_SORT_CATEGORY:
				bSwap = FilesArray[i].sCategory.CompareNoCase(FilesArray[i + 1].sCategory) < 0;
			}
			if (pThis->m_Params.bDownloadSortReverse)
				bSwap = !bSwap;
			if (bSwap) {
				bSorted = true;
				DownloadFiles TmpFile = FilesArray[i];
				FilesArray[i] = FilesArray[i + 1];
				FilesArray[i + 1] = TmpFile;
			}
		}
	}


	//	uint32	dwClientSoft;
	CArray<UploadUsers> UploadArray;

	for (POSITION pos = theApp.uploadqueue->GetFirstFromUploadList(); pos != NULL;) {
		const CUpDownClient *cur_client = theApp.uploadqueue->GetNextFromUploadList(pos);

		UploadUsers dUser;
		dUser.sUserHash = md4str(cur_client->GetUserHash());
		if (cur_client->GetDatarate() > 0) {
			dUser.sActive = _T("downloading");
			dUser.sClientState = _T("uploading");
		} else {
			dUser.sActive = _T("waiting");
			dUser.sClientState = _T("connecting");
		}

		dUser.sFileInfo = _SpecialChars(GetClientSummary(cur_client), false);
		dUser.sFileInfo.Replace(_T("\\"), _T("\\\\"));
		dUser.sFileInfo.Replace(_T("\n"), _T("<br>"));
		dUser.sFileInfo.Replace(_T("'"), _T("&#8217;"));

		dUser.sClientSoft = GetClientversionImage(cur_client);

		if (cur_client->IsBanned())
			dUser.sClientExtra = _T("banned");
		else if (cur_client->IsFriend())
			dUser.sClientExtra = _T("friend");
		else if (cur_client->Credits()->GetScoreRatio(cur_client->GetIP()) > 1)
			dUser.sClientExtra = _T("credit");
		else
			dUser.sClientExtra = _T("none");

		dUser.sUserName = _SpecialChars(cur_client->GetUserName());

		CString cun(cur_client->GetUserName());
		if (cun.GetLength() > SHORT_LENGTH_MIN)
			dUser.sUserName = _SpecialChars(cun.Left(SHORT_LENGTH_MIN - 3)) + _T("...");

		CKnownFile* file = theApp.sharedfiles->GetFileByID(cur_client->GetUploadFileID());
		if (file)
			dUser.sFileName = _SpecialChars(file->GetFileName());
		else
			dUser.sFileName = _GetPlainResString(IDS_REQ_UNKNOWNFILE);
		dUser.nTransferredDown = cur_client->GetTransferredDown();
		dUser.nTransferredUp = cur_client->GetTransferredUp();
		int iDataRate = cur_client->GetDatarate();
		dUser.nDataRate = ((iDataRate == -1) ? 0 : iDataRate);
		dUser.sClientNameVersion = cur_client->GetClientSoftVer();
		UploadArray.Add(dUser);
	}

	// Sorting (simple bubble sort, we don't have tons of data here)
	bSorted = true;
	for (int nMax = 0; bSorted && nMax < UploadArray.GetCount() * 2; ++nMax) {
		bSorted = false;
		for (int i = 0; i < UploadArray.GetCount() - 1; i++) {
			bool bSwap = false;
			switch (pThis->m_Params.UploadSort) {
			case UP_SORT_CLIENT:
				bSwap = UploadArray[i].sClientSoft.CompareNoCase(UploadArray[i + 1].sClientSoft) < 0;
				break;
			case UP_SORT_USER:
				bSwap = UploadArray[i].sUserName.CompareNoCase(UploadArray[i + 1].sUserName) < 0;
				break;
			case UP_SORT_VERSION:
				bSwap = UploadArray[i].sClientNameVersion.CompareNoCase(UploadArray[i + 1].sClientNameVersion) < 0;
				break;
			case UP_SORT_FILENAME:
				bSwap = UploadArray[i].sFileName.CompareNoCase(UploadArray[i + 1].sFileName) < 0;
				break;
			case UP_SORT_TRANSFERRED:
				bSwap = UploadArray[i].nTransferredUp < UploadArray[i + 1].nTransferredUp;
				break;
			case UP_SORT_SPEED:
				bSwap = UploadArray[i].nDataRate < UploadArray[i + 1].nDataRate;
			}
			if (pThis->m_Params.bUploadSortReverse)
				bSwap = !bSwap;
			if (bSwap) {
				bSorted = true;
				UploadUsers TmpUser = UploadArray[i];
				UploadArray[i] = UploadArray[i + 1];
				UploadArray[i + 1] = TmpUser;
			}
		}
	}

	Out = _CreateTransferList(Out, pThis, Data, &FilesArray, &UploadArray, bAdmin);


	Out.Replace(_T("[Session]"), sSession);
	Out.Replace(_T("[CatSel]"), sCat);

	return Out;
}

CString CWebServer::_CreateTransferList(CString Out, CWebServer *pThis, const ThreadData& Data, void* _FilesArray, void* _UploadArray, bool bAdmin)
{
	CArray<DownloadFiles>* FilesArray = (CArray<DownloadFiles>*)_FilesArray;
	CArray<UploadUsers>* UploadArray = (CArray<UploadUsers>*)_UploadArray;

	// Displaying
	CString sDownList, HTTPProcessData;
	CString OutE = pThis->m_Templates.sTransferDownLine;
	CString HTTPTemp;

	int nCountQueue = 0;
	int nCountQueueBanned = 0;
	int nCountQueueFriend = 0;
	int nCountQueueSecure = 0;
	int nCountQueueBannedSecure = 0;
	int nCountQueueFriendSecure = 0;

	double fTotalSize = 0, fTotalTransferred = 0, fTotalSpeed = 0;

	CQArray<QueueUsers, QueueUsers> QueueArray;
	for (POSITION pos = theApp.uploadqueue->waitinglist.GetHeadPosition(); pos != NULL;) {
		CUpDownClient* cur_client = theApp.uploadqueue->waitinglist.GetNext(pos);
		QueueUsers dUser;

		bool bSecure = (cur_client->Credits()->GetCurrentIdentState(cur_client->GetIP()) == IS_IDENTIFIED);
		if (cur_client->IsBanned()) {
			dUser.sClientExtra = _T("banned");
			nCountQueueBanned++;
			if (bSecure) nCountQueueBannedSecure++;
		} else if (cur_client->IsFriend()) {
			dUser.sClientExtra = _T("friend");
			nCountQueueFriend++;
			if (bSecure) nCountQueueFriendSecure++;
		} else {
			dUser.sClientExtra = _T("none");
			nCountQueue++;
			if (bSecure) nCountQueueSecure++;
		}

		CString usn(cur_client->GetUserName());
		if (usn.GetLength() > SHORT_LENGTH_MIN)
			usn = usn.Left((SHORT_LENGTH_MIN - 3)) + _T("...");
		dUser.sUserName = _SpecialChars(usn);

		dUser.sClientNameVersion = cur_client->GetClientSoftVer();
		CKnownFile* file = theApp.sharedfiles->GetFileByID(cur_client->GetUploadFileID());
		if (file)
			dUser.sFileName = _SpecialChars(file->GetFileName());
		else
			dUser.sFileName = _GetPlainResString(IDS_REQ_UNKNOWNFILE);
		dUser.sClientState = dUser.sClientExtra;
		dUser.sClientStateSpecial = _T("connecting");
		dUser.nScore = cur_client->GetScore(false);

		dUser.sClientSoft = GetClientversionImage(cur_client);
		dUser.sUserHash = md4str(cur_client->GetUserHash());
		//SyruS CQArray-Sorting setting sIndex according to param
		switch (pThis->m_Params.QueueSort) {
		case QU_SORT_CLIENT:
			dUser.sIndex = dUser.sClientSoft;
			break;
		case QU_SORT_USER:
			dUser.sIndex = dUser.sUserName;
			break;
		case QU_SORT_VERSION:
			dUser.sIndex = dUser.sClientNameVersion;
			break;
		case QU_SORT_FILENAME:
			dUser.sIndex = dUser.sFileName;
			break;
		case QU_SORT_SCORE:
			dUser.sIndex.Format(_T("%09u"), dUser.nScore);
			break;
		default:
			dUser.sIndex.Empty();
		}
		QueueArray.Add(dUser);
	}

	int nNextPos = 0;	// position in queue of the user with the highest score -> next upload user
	uint32 nNextScore = 0;	// highest score -> next upload user
	for (int i = 0; i < QueueArray.GetCount(); i++) {
		if (QueueArray[i].nScore > nNextScore) {
			nNextPos = i;
			nNextScore = QueueArray[i].nScore;
		}
	}
	if (theApp.uploadqueue->waitinglist.GetHeadPosition() != 0) {
		QueueArray[nNextPos].sClientState = _T("next");
		QueueArray[nNextPos].sClientStateSpecial = QueueArray[nNextPos].sClientState;
	}

	if ((nCountQueue > 0 && pThis->m_Params.bShowUploadQueue) ||
		(nCountQueueBanned > 0 && pThis->m_Params.bShowUploadQueueBanned) ||
		(nCountQueueFriend > 0 && pThis->m_Params.bShowUploadQueueFriend)) {
#ifdef _DEBUG
		DWORD dwStart = ::GetTickCount();
#endif
		QueueArray.QuickSort(pThis->m_Params.bQueueSortReverse);
#ifdef _DEBUG
		AddDebugLogLine(false, _T("WebServer: Waitingqueue with %u elements sorted in %u ms"), QueueArray.GetSize(), ::GetTickCount() - dwStart);
#endif
	}

	for (int i = 0; i < FilesArray->GetCount(); i++) {
		HTTPProcessData = OutE;

		DownloadFiles dwnlf = FilesArray->GetAt(i);

		if (dwnlf.sFileHash == _ParseURL(Data.sURL, _T("file")))
			HTTPProcessData.Replace(_T("[LastChangedDataset]"), _T("checked"));
		else
			HTTPProcessData.Replace(_T("[LastChangedDataset]"), _T("checked_no"));

		//CPartFile *found_file = theApp.downloadqueue->GetFileByID(_GetFileHash(dwnlf.sFileHash, FileHashA4AF));

		dwnlf.sFileInfo.Replace(_T("\\"), _T("\\\\"));

		CString strFInfo = dwnlf.sFileInfo;
		strFInfo.Replace(_T("'"), _T("&#8217;"));
		strFInfo.Replace(_T("\n"), _T("\\n"));

		dwnlf.sFileInfo.Replace(_T("\n"), _T("<br>"));

		if (!dwnlf.iComment) {
			HTTPProcessData.Replace(_T("[HASCOMMENT]"), _T("<!--"));
			HTTPProcessData.Replace(_T("[HASCOMMENT_END]"), _T("-->"));
		} else {
			HTTPProcessData.Replace(_T("[HASCOMMENT]"), _T(""));
			HTTPProcessData.Replace(_T("[HASCOMMENT_END]"), _T(""));
		}

		if (dwnlf.sFileState.CompareNoCase(_T("downloading")) == 0 || dwnlf.sFileState.CompareNoCase(_T("waiting")) == 0) {
			HTTPProcessData.Replace(_T("[ISACTIVE]"), _T("<!--"));
			HTTPProcessData.Replace(_T("[ISACTIVE_END]"), _T("-->"));
			HTTPProcessData.Replace(_T("[!ISACTIVE]"), _T(""));
			HTTPProcessData.Replace(_T("[!ISACTIVE_END]"), _T(""));
		} else {
			HTTPProcessData.Replace(_T("[ISACTIVE]"), _T(""));
			HTTPProcessData.Replace(_T("[ISACTIVE_END]"), _T(""));
			HTTPProcessData.Replace(_T("[!ISACTIVE]"), _T("<!--"));
			HTTPProcessData.Replace(_T("[!ISACTIVE_END]"), _T("-->"));
		}

		CString ed2k = dwnlf.sED2kLink; //ed2klink
		ed2k.Replace(_T("'"), _T("&#8217;"));
		CString fname = dwnlf.sFileNameJS; //filename
		CString state = dwnlf.sFileState;
		CString fsize; //filesize
		fsize.Format(_T("%I64u"), dwnlf.m_qwFileSize);
		const CString filehash = dwnlf.sFileHash;
		const CString session = _ParseURL(Data.sURL, _T("ses"));

		CString isgetflc;		//getflc
		if (!dwnlf.bIsPreview)
			isgetflc = dwnlf.bIsGetFLC ? _T("enabled") : _T("disabled");

		LPCTSTR downpriority;	//priority
		if (dwnlf.bFileAutoPrio)
			downpriority = _T("Auto");
		else {
			switch (dwnlf.nFilePrio) {
			case 0:
				downpriority = _T("Low");
				break;
			case 1:
				downpriority = _T("Normal");
				break;
			case 2:
				downpriority = _T("High");
				break;
			default:
				downpriority = _T("");
			}
		}

		HTTPProcessData.Replace(_T("[admin]"), (bAdmin) ? _T("admin") : _T(""));
		HTTPProcessData.Replace(_T("[finfo]"), strFInfo);
		HTTPProcessData.Replace(_T("[fcomments]"), (dwnlf.iComment == 0) ? _T("") : _T("yes"));
		HTTPProcessData.Replace(_T("[ed2k]"), _SpecialChars(ed2k));
		HTTPProcessData.Replace(_T("[DownState]"), state);
		HTTPProcessData.Replace(_T("[isgetflc]"), isgetflc);
		HTTPProcessData.Replace(_T("[fname]"), _SpecialChars(fname));
		HTTPProcessData.Replace(_T("[fsize]"), fsize);
		HTTPProcessData.Replace(_T("[session]"), session);
		HTTPProcessData.Replace(_T("[filehash]"), filehash);
		HTTPProcessData.Replace(_T("[down-priority]"), downpriority);
		HTTPProcessData.Replace(_T("[FileType]"), dwnlf.sFileType);
		HTTPProcessData.Replace(_T("[downloadable]"), (bAdmin && (thePrefs.GetMaxWebUploadFileSizeMB() == 0 || dwnlf.m_qwFileSize < ((uint64)thePrefs.GetMaxWebUploadFileSizeMB()) * 1024 * 1024)) ? _T("yes") : _T("no"));

		// comment icon
		if (!dwnlf.iComment)
			HTTPProcessData.Replace(_T("[FileCommentIcon]"), _T("none"));
		else if (dwnlf.iComment == 1)
			HTTPProcessData.Replace(_T("[FileCommentIcon]"), _T("cmtgood"));
		else if (dwnlf.iComment == 2)
			HTTPProcessData.Replace(_T("[FileCommentIcon]"), _T("cmtbad"));


		if (!dwnlf.bIsPreview && dwnlf.bIsGetFLC)
			HTTPProcessData.Replace(_T("[FileIsGetFLC]"), _T("getflc"));
		else
			HTTPProcessData.Replace(_T("[FileIsGetFLC]"), _T("halfnone"));

		if (!WSdownloadColumnHidden[0]) {
			if (dwnlf.sFileName.GetLength() > (SHORT_LENGTH_MAX))
				HTTPProcessData.Replace(_T("[ShortFileName]"), dwnlf.sFileName.Left(SHORT_LENGTH_MAX - 3) + _T("..."));
			else
				HTTPProcessData.Replace(_T("[ShortFileName]"), dwnlf.sFileName);
		} else
			HTTPProcessData.Replace(_T("[ShortFileName]"), _T(""));

		HTTPProcessData.Replace(_T("[FileInfo]"), dwnlf.sFileInfo);
		fTotalSize += dwnlf.m_qwFileSize;

		if (!WSdownloadColumnHidden[1])
			HTTPProcessData.Replace(_T("[2]"), CastItoXBytes(dwnlf.m_qwFileSize));
		else
			HTTPProcessData.Replace(_T("[2]"), _T(""));

		if (!WSdownloadColumnHidden[2]) {
			if (dwnlf.m_qwFileTransferred > 0) {
				fTotalTransferred += dwnlf.m_qwFileTransferred;
				HTTPProcessData.Replace(_T("[3]"), CastItoXBytes(dwnlf.m_qwFileTransferred));
			} else
				HTTPProcessData.Replace(_T("[3]"), _T("-"));
		} else
			HTTPProcessData.Replace(_T("[3]"), _T(""));

		if (!WSdownloadColumnHidden[3])
			HTTPProcessData.Replace(_T("[DownloadBar]"), _GetDownloadGraph(Data, dwnlf.sFileHash));
		else
			HTTPProcessData.Replace(_T("[DownloadBar]"), _T(""));

		HTTPTemp.Empty();

		if (!WSdownloadColumnHidden[4]) {
			if (dwnlf.lFileSpeed > 0.0f) {
				fTotalSpeed += dwnlf.lFileSpeed;
				HTTPTemp.Format(_T("%8.2f"), dwnlf.lFileSpeed / 1024.0);
				HTTPProcessData.Replace(_T("[4]"), HTTPTemp);
			} else
				HTTPProcessData.Replace(_T("[4]"), _T("-"));
		} else
			HTTPProcessData.Replace(_T("[4]"), _T(""));

		if (!WSdownloadColumnHidden[5]) {
			if (dwnlf.lSourceCount > 0) {
				HTTPTemp.Format(_T("%li&nbsp;/&nbsp;%8li&nbsp;(%li)"),
					dwnlf.lSourceCount - dwnlf.lNotCurrentSourceCount,
					dwnlf.lSourceCount,
					dwnlf.lTransferringSourceCount);
				HTTPProcessData.Replace(_T("[5]"), HTTPTemp);
			} else
				HTTPProcessData.Replace(_T("[5]"), _T("-"));
		} else
			HTTPProcessData.Replace(_T("[5]"), _T(""));

		if (!WSdownloadColumnHidden[6] && dwnlf.nFilePrio >= 0 && dwnlf.nFilePrio <= 2) {
			static const UINT uprio[2][3] = {
				{IDS_PRIOLOW, IDS_PRIONORMAL, IDS_PRIOHIGH},
				{IDS_PRIOAUTOLOW, IDS_PRIOAUTONORMAL, IDS_PRIOAUTOHIGH}
			};
			HTTPProcessData.Replace(_T("[PrioVal]"), GetResString(uprio[static_cast<unsigned>(dwnlf.bFileAutoPrio)][dwnlf.nFilePrio]));
		} else
			HTTPProcessData.Replace(_T("[PrioVal]"), _T(""));

		if (!WSdownloadColumnHidden[7])
			HTTPProcessData.Replace(_T("[Category]"), dwnlf.sCategory);
		else
			HTTPProcessData.Replace(_T("[Category]"), _T(""));


		InsertCatBox(HTTPProcessData, 0, _T(""), false, false, session, dwnlf.sFileHash);

		sDownList += HTTPProcessData;
	}

	Out.Replace(_T("[DownloadFilesList]"), sDownList);
	Out.Replace(_T("[TotalDownSize]"), CastItoXBytes(fTotalSize));

	Out.Replace(_T("[TotalDownTransferred]"), CastItoXBytes(fTotalTransferred));

	HTTPTemp.Format(_T("%8.2f"), fTotalSpeed / 1024.0);
	Out.Replace(_T("[TotalDownSpeed]"), HTTPTemp);

	HTTPTemp.Format(_T("%s: %i"), (LPCTSTR)GetResString(IDS_SF_FILE), (int)FilesArray->GetCount());
	Out.Replace(_T("[TotalFiles]"), HTTPTemp);

	HTTPTemp.Format(_T("%i"), pThis->m_Templates.iProgressbarWidth);
	Out.Replace(_T("[PROGRESSBARWIDTHVAL]"), HTTPTemp);

	fTotalSize = 0;
	fTotalTransferred = 0;
	fTotalSpeed = 0;

	CString sUpList;
	const TCHAR	*pcTmp;

	OutE = pThis->m_Templates.sTransferUpLine;
	OutE.Replace(_T("[admin]"), (bAdmin) ? _T("admin") : _T(""));

	for (int i = 0; i < UploadArray->GetCount(); i++) {
		UploadUsers ulu = (*UploadArray)[i];
		HTTPProcessData = OutE;

		HTTPProcessData.Replace(_T("[UserHash]"), ulu.sUserHash);
		HTTPProcessData.Replace(_T("[UpState]"), ulu.sActive);
		HTTPProcessData.Replace(_T("[FileInfo]"), ulu.sFileInfo);
		HTTPProcessData.Replace(_T("[ClientState]"), ulu.sClientState);
		HTTPProcessData.Replace(_T("[ClientSoft]"), ulu.sClientSoft);
		HTTPProcessData.Replace(_T("[ClientExtra]"), ulu.sClientExtra);

		pcTmp = (!WSuploadColumnHidden[0]) ? (LPCTSTR)ulu.sUserName : _T("");
		HTTPProcessData.Replace(_T("[1]"), pcTmp);

		pcTmp = (!WSuploadColumnHidden[1]) ? (LPCTSTR)ulu.sClientNameVersion : _T("");
		HTTPProcessData.Replace(_T("[ClientSoftV]"), pcTmp);

		pcTmp = (!WSuploadColumnHidden[2]) ? (LPCTSTR)ulu.sFileName : _T("");
		HTTPProcessData.Replace(_T("[2]"), pcTmp);

		pcTmp = _T("");
		if (!WSuploadColumnHidden[3]) {
			fTotalSize += ulu.nTransferredDown;
			fTotalTransferred += ulu.nTransferredUp;
			HTTPTemp.Format(_T("%s / %s"), (LPCTSTR)CastItoXBytes(ulu.nTransferredDown), (LPCTSTR)CastItoXBytes(ulu.nTransferredUp));
			pcTmp = HTTPTemp;
		}
		HTTPProcessData.Replace(_T("[3]"), pcTmp);

		pcTmp = _T("");
		if (!WSuploadColumnHidden[4]) {
			fTotalSpeed += ulu.nDataRate;
			HTTPTemp.Format(_T("%8.2f "), max((double)ulu.nDataRate / 1024, 0.0));
			pcTmp = HTTPTemp;
		}
		HTTPProcessData.Replace(_T("[4]"), pcTmp);

		sUpList += HTTPProcessData;
	}
	Out.Replace(_T("[UploadFilesList]"), sUpList);
	HTTPTemp.Format(_T("%s / %s"), (LPCTSTR)CastItoXBytes(fTotalSize), (LPCTSTR)CastItoXBytes(fTotalTransferred));
	Out.Replace(_T("[TotalUpTransferred]"), HTTPTemp);
	HTTPTemp.Format(_T("%8.2f "), max(fTotalSpeed / 1024, 0.0));
	Out.Replace(_T("[TotalUpSpeed]"), HTTPTemp);

	if (pThis->m_Params.bShowUploadQueue) {
		Out.Replace(_T("[UploadQueue]"), pThis->m_Templates.sTransferUpQueueShow);
		Out.Replace(_T("[UploadQueueList]"), _GetPlainResString(IDS_ONQUEUE));

		CString sQueue;

		OutE = pThis->m_Templates.sTransferUpQueueLine;
		OutE.Replace(_T("[admin]"), (bAdmin) ? _T("admin") : _T(""));

		for (int i = 0; i < QueueArray.GetCount(); i++) {
			if (QueueArray[i].sClientExtra == _T("none")) {
				HTTPProcessData = OutE;
				pcTmp = (!WSqueueColumnHidden[0]) ? (LPCTSTR)QueueArray[i].sUserName : _T("");
				HTTPProcessData.Replace(_T("[UserName]"), pcTmp);

				pcTmp = (!WSqueueColumnHidden[1]) ? (LPCTSTR)QueueArray[i].sClientNameVersion : _T("");
				HTTPProcessData.Replace(_T("[ClientSoftV]"), pcTmp);

				pcTmp = (!WSqueueColumnHidden[2]) ? (LPCTSTR)QueueArray[i].sFileName : _T("");
				HTTPProcessData.Replace(_T("[FileName]"), pcTmp);

				TCHAR HTTPTempC[100] = _T("");
				if (!WSqueueColumnHidden[3])
					_stprintf(HTTPTempC, _T("%i"), QueueArray[i].nScore);
				HTTPProcessData.Replace(_T("[Score]"), HTTPTempC);
				HTTPProcessData.Replace(_T("[ClientState]"), QueueArray[i].sClientState);
				HTTPProcessData.Replace(_T("[ClientStateSpecial]"), QueueArray[i].sClientStateSpecial);
				HTTPProcessData.Replace(_T("[ClientSoft]"), QueueArray[i].sClientSoft);
				HTTPProcessData.Replace(_T("[ClientExtra]"), QueueArray[i].sClientExtra);
				HTTPProcessData.Replace(_T("[UserHash]"), QueueArray[i].sUserHash);

				sQueue += HTTPProcessData;
			}
		}
		Out.Replace(_T("[QueueList]"), sQueue);
	} else
		Out.Replace(_T("[UploadQueue]"), pThis->m_Templates.sTransferUpQueueHide);

	if (pThis->m_Params.bShowUploadQueueBanned) {
		Out.Replace(_T("[UploadQueueBanned]"), pThis->m_Templates.sTransferUpQueueBannedShow);
		Out.Replace(_T("[UploadQueueBannedList]"), _GetPlainResString(IDS_BANNED));

		CString sQueueBanned;

		OutE = pThis->m_Templates.sTransferUpQueueBannedLine;

		for (int i = 0; i < QueueArray.GetCount(); i++) {
			if (QueueArray[i].sClientExtra == _T("banned")) {
				HTTPProcessData = OutE;
				pcTmp = (!WSqueueColumnHidden[0]) ? (LPCTSTR)QueueArray[i].sUserName : _T("");
				HTTPProcessData.Replace(_T("[UserName]"), pcTmp);

				pcTmp = (!WSqueueColumnHidden[1]) ? (LPCTSTR)QueueArray[i].sClientNameVersion : _T("");
				HTTPProcessData.Replace(_T("[ClientSoftV]"), pcTmp);

				pcTmp = (!WSqueueColumnHidden[2]) ? (LPCTSTR)QueueArray[i].sFileName : _T("");
				HTTPProcessData.Replace(_T("[FileName]"), pcTmp);

				TCHAR HTTPTempC[20] = _T("");
				if (!WSqueueColumnHidden[3])
					_stprintf(HTTPTempC, _T("%i"), QueueArray[i].nScore);
				HTTPProcessData.Replace(_T("[Score]"), HTTPTempC);

				HTTPProcessData.Replace(_T("[ClientState]"), QueueArray[i].sClientState);
				HTTPProcessData.Replace(_T("[ClientStateSpecial]"), QueueArray[i].sClientStateSpecial);
				HTTPProcessData.Replace(_T("[ClientSoft]"), QueueArray[i].sClientSoft);
				HTTPProcessData.Replace(_T("[ClientExtra]"), QueueArray[i].sClientExtra);
				HTTPProcessData.Replace(_T("[UserHash]"), QueueArray[i].sUserHash);

				sQueueBanned += HTTPProcessData;
			}
		}
		Out.Replace(_T("[QueueListBanned]"), sQueueBanned);
	} else
		Out.Replace(_T("[UploadQueueBanned]"), pThis->m_Templates.sTransferUpQueueBannedHide);

	if (pThis->m_Params.bShowUploadQueueFriend) {
		Out.Replace(_T("[UploadQueueFriend]"), pThis->m_Templates.sTransferUpQueueFriendShow);
		Out.Replace(_T("[UploadQueueFriendList]"), _GetPlainResString(IDS_IRC_ADDTOFRIENDLIST));

		CString sQueueFriend;

		OutE = pThis->m_Templates.sTransferUpQueueFriendLine;

		for (int i = 0; i < QueueArray.GetCount(); i++) {
			if (QueueArray[i].sClientExtra == _T("friend")) {
				HTTPProcessData = OutE;
				pcTmp = (!WSqueueColumnHidden[0]) ? (LPCTSTR)QueueArray[i].sUserName : _T("");
				HTTPProcessData.Replace(_T("[UserName]"), pcTmp);

				pcTmp = (!WSqueueColumnHidden[1]) ? (LPCTSTR)QueueArray[i].sClientNameVersion : _T("");
				HTTPProcessData.Replace(_T("[ClientSoftV]"), pcTmp);

				pcTmp = (!WSqueueColumnHidden[2]) ? (LPCTSTR)QueueArray[i].sFileName : _T("");
				HTTPProcessData.Replace(_T("[FileName]"), pcTmp);

				TCHAR HTTPTempC[100] = _T("");
				if (!WSqueueColumnHidden[3])
					_stprintf(HTTPTempC, _T("%i"), QueueArray[i].nScore);
				HTTPProcessData.Replace(_T("[Score]"), HTTPTempC);

				HTTPProcessData.Replace(_T("[ClientState]"), QueueArray[i].sClientState);
				HTTPProcessData.Replace(_T("[ClientStateSpecial]"), QueueArray[i].sClientStateSpecial);
				HTTPProcessData.Replace(_T("[ClientSoft]"), QueueArray[i].sClientSoft);
				HTTPProcessData.Replace(_T("[ClientExtra]"), QueueArray[i].sClientExtra);
				HTTPProcessData.Replace(_T("[UserHash]"), QueueArray[i].sUserHash);

				sQueueFriend += HTTPProcessData;
			}
		}
		Out.Replace(_T("[QueueListFriend]"), sQueueFriend);
	} else
		Out.Replace(_T("[UploadQueueFriend]"), pThis->m_Templates.sTransferUpQueueFriendHide);


	CString mCounter;
	mCounter.Format(_T("%i"), nCountQueue);
	Out.Replace(_T("[CounterQueue]"), mCounter);
	mCounter.Format(_T("%i"), nCountQueueBanned);
	Out.Replace(_T("[CounterQueueBanned]"), mCounter);
	mCounter.Format(_T("%i"), nCountQueueFriend);
	Out.Replace(_T("[CounterQueueFriend]"), mCounter);
	mCounter.Format(_T("%i"), nCountQueueSecure);
	Out.Replace(_T("[CounterQueueSecure]"), mCounter);
	mCounter.Format(_T("%i"), nCountQueueBannedSecure);
	Out.Replace(_T("[CounterQueueBannedSecure]"), mCounter);
	mCounter.Format(_T("%i"), nCountQueueFriendSecure);
	Out.Replace(_T("[CounterQueueFriendSecure]"), mCounter);
	mCounter.Format(_T("%i"), nCountQueue + nCountQueueBanned + nCountQueueFriend);
	Out.Replace(_T("[CounterAll]"), mCounter);
	mCounter.Format(_T("%i"), nCountQueueSecure + nCountQueueBannedSecure + nCountQueueFriendSecure);
	Out.Replace(_T("[CounterAllSecure]"), mCounter);
	Out.Replace(_T("[ShowUploadQueue]"), _GetPlainResString(IDS_VIEWQUEUE));
	Out.Replace(_T("[ShowUploadQueueList]"), _GetPlainResString(IDS_WEB_SHOW_UPLOAD_QUEUE));

	Out.Replace(_T("[ShowUploadQueueListBanned]"), _GetPlainResString(IDS_WEB_SHOW_UPLOAD_QUEUE_BANNED));
	Out.Replace(_T("[ShowUploadQueueListFriend]"), _GetPlainResString(IDS_WEB_SHOW_UPLOAD_QUEUE_FRIEND));

	CString strTmp = (pThis->m_Params.bQueueSortReverse) ? _T("&amp;sortreverse=false") : _T("&amp;sortreverse=true");

	if (pThis->m_Params.QueueSort == QU_SORT_CLIENT)
		Out.Replace(_T("[SortQClient]"), strTmp);
	else
		Out.Replace(_T("[SortQClient]"), _T(""));
	if (pThis->m_Params.QueueSort == QU_SORT_USER)
		Out.Replace(_T("[SortQUser]"), strTmp);
	else
		Out.Replace(_T("[SortQUser]"), _T(""));
	if (pThis->m_Params.QueueSort == QU_SORT_VERSION)
		Out.Replace(_T("[SortQVersion]"), strTmp);
	else
		Out.Replace(_T("[SortQVersion]"), _T(""));
	if (pThis->m_Params.QueueSort == QU_SORT_FILENAME)
		Out.Replace(_T("[SortQFilename]"), strTmp);
	else
		Out.Replace(_T("[SortQFilename]"), _T(""));
	if (pThis->m_Params.QueueSort == QU_SORT_SCORE)
		Out.Replace(_T("[SortQScore]"), strTmp);
	else
		Out.Replace(_T("[SortQScore]"), _T(""));

	CString pcSortIcon = (pThis->m_Params.bQueueSortReverse) ? pThis->m_Templates.sUpArrow : pThis->m_Templates.sDownArrow;

	_GetPlainResString(strTmp, IDS_QL_USERNAME);
	if (!WSqueueColumnHidden[0]) {
		Out.Replace(_T("[UserNameTitleI]"), (pThis->m_Params.QueueSort == QU_SORT_USER) ? (LPCTSTR)pcSortIcon : _T(""));
		Out.Replace(_T("[UserNameTitle]"), strTmp);
	} else {
		Out.Replace(_T("[UserNameTitleI]"), _T(""));
		Out.Replace(_T("[UserNameTitle]"), _T(""));
	}
	Out.Replace(_T("[UserNameTitleM]"), strTmp);

	_GetPlainResString(strTmp, IDS_CD_CSOFT);
	if (!WSqueueColumnHidden[1]) {
		Out.Replace(_T("[VersionI]"), (pThis->m_Params.QueueSort == QU_SORT_VERSION) ? (LPCTSTR)pcSortIcon : _T(""));
		Out.Replace(_T("[Version]"), strTmp);
	} else {
		Out.Replace(_T("[VersionI]"), _T(""));
		Out.Replace(_T("[Version]"), _T(""));
	}
	Out.Replace(_T("[VersionM]"), strTmp);

	_GetPlainResString(strTmp, IDS_DL_FILENAME);
	if (!WSqueueColumnHidden[2]) {
		Out.Replace(_T("[FileNameTitleI]"), (pThis->m_Params.QueueSort == QU_SORT_FILENAME) ? (LPCTSTR)pcSortIcon : _T(""));
		Out.Replace(_T("[FileNameTitle]"), strTmp);
	} else {
		Out.Replace(_T("[FileNameTitleI]"), _T(""));
		Out.Replace(_T("[FileNameTitle]"), _T(""));
	}
	Out.Replace(_T("[FileNameTitleM]"), strTmp);

	_GetPlainResString(strTmp, IDS_SCORE);
	if (!WSqueueColumnHidden[3]) {
		Out.Replace(_T("[ScoreTitleI]"), (pThis->m_Params.QueueSort == QU_SORT_SCORE) ? (LPCTSTR)pcSortIcon : _T(""));
		Out.Replace(_T("[ScoreTitle]"), strTmp);
	} else {
		Out.Replace(_T("[ScoreTitleI]"), _T(""));
		Out.Replace(_T("[ScoreTitle]"), _T(""));
	}

	Out.Replace(_T("[ScoreTitleM]"), strTmp);

	return Out;
}

CString CWebServer::_GetSharedFilesList(const ThreadData& Data)
{
	CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString sSession = _ParseURL(Data.sURL, _T("ses"));
	bool bAdmin = IsSessionAdmin(Data, sSession);
	const CString strSort = _ParseURL(Data.sURL, _T("sort"));

	CString strTmp = _ParseURL(Data.sURL, _T("sortreverse"));

	if (!strSort.IsEmpty()) {
		bool bDirection = false;

		if (strSort == _T("state"))
			pThis->m_Params.SharedSort = SHARED_SORT_STATE;
		else if (strSort == _T("type"))
			pThis->m_Params.SharedSort = SHARED_SORT_TYPE;
		else if (strSort == _T("name")) {
			pThis->m_Params.SharedSort = SHARED_SORT_NAME;
			bDirection = true;
		} else if (strSort == _T("size"))
			pThis->m_Params.SharedSort = SHARED_SORT_SIZE;
		else if (strSort == _T("transferred"))
			pThis->m_Params.SharedSort = SHARED_SORT_TRANSFERRED;
		else if (strSort == _T("alltimetransferred"))
			pThis->m_Params.SharedSort = SHARED_SORT_ALL_TIME_TRANSFERRED;
		else if (strSort == _T("requests"))
			pThis->m_Params.SharedSort = SHARED_SORT_REQUESTS;
		else if (strSort == _T("alltimerequests"))
			pThis->m_Params.SharedSort = SHARED_SORT_ALL_TIME_REQUESTS;
		else if (strSort == _T("accepts"))
			pThis->m_Params.SharedSort = SHARED_SORT_ACCEPTS;
		else if (strSort == _T("alltimeaccepts"))
			pThis->m_Params.SharedSort = SHARED_SORT_ALL_TIME_ACCEPTS;
		else if (strSort == _T("completes"))
			pThis->m_Params.SharedSort = SHARED_SORT_COMPLETES;
		else if (strSort == _T("priority"))
			pThis->m_Params.SharedSort = SHARED_SORT_PRIORITY;

		if (strTmp.IsEmpty())
			pThis->m_Params.bSharedSortReverse = bDirection;
	}
	if (!strTmp.IsEmpty())
		pThis->m_Params.bSharedSortReverse = (strTmp == _T("true"));

	if (!_ParseURL(Data.sURL, _T("hash")).IsEmpty() && !_ParseURL(Data.sURL, _T("prio")).IsEmpty() && IsSessionAdmin(Data, sSession)) {
		uchar fileid[16];
		CString hash = _ParseURL(Data.sURL, _T("hash"));
		if (hash.GetLength() == 32 && DecodeBase16(hash, hash.GetLength(), fileid, ARRSIZE(fileid))) {
			CKnownFile *cur_file = theApp.sharedfiles->GetFileByID(fileid);

			if (cur_file != 0) {
				cur_file->SetAutoUpPriority(false);
				strTmp = _ParseURL(Data.sURL, _T("prio"));
				if (strTmp == _T("verylow"))
					cur_file->SetUpPriority(PR_VERYLOW);
				else if (strTmp == _T("low"))
					cur_file->SetUpPriority(PR_LOW);
				else if (strTmp == _T("normal"))
					cur_file->SetUpPriority(PR_NORMAL);
				else if (strTmp == _T("high"))
					cur_file->SetUpPriority(PR_HIGH);
				else if (strTmp == _T("release"))
					cur_file->SetUpPriority(PR_VERYHIGH);
				else if (strTmp == _T("auto")) {
					cur_file->SetAutoUpPriority(true);
					cur_file->UpdateAutoUpPriority();
				}

				SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_UPD_SFUPDATE, (LPARAM)cur_file);
			}
		}
	}

	CString sCmd = _ParseURL(Data.sURL, _T("c"));
	if (sCmd.CompareNoCase(_T("menu")) == 0) {
		int iMenu = _tstoi(_ParseURL(Data.sURL, _T("m")));
		bool bValue = _tstoi(_ParseURL(Data.sURL, _T("v"))) != 0;
		WSsharedColumnHidden[iMenu] = bValue;
		SaveWIConfigArray(WSsharedColumnHidden, ARRSIZE(WSsharedColumnHidden), _T("sharedColumnHidden"));
	}
	if (_ParseURL(Data.sURL, _T("reload")) == _T("true"))
		SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_SHARED_FILES_RELOAD, 0);

	CString Out = pThis->m_Templates.sSharedList;

	strTmp = (pThis->m_Params.bSharedSortReverse) ? _T("false") : _T("true");

	//State sorting link
	if (pThis->m_Params.SharedSort == SHARED_SORT_STATE)
		Out.Replace(_T("[SortState]"), _T("sort=state&amp;sortreverse=") + strTmp);
	else
		Out.Replace(_T("[SortState]"), _T("sort=state"));
	//Type sorting link
	if (pThis->m_Params.SharedSort == SHARED_SORT_TYPE)
		Out.Replace(_T("[SortType]"), _T("sort=type&amp;sortreverse=") + strTmp);
	else
		Out.Replace(_T("[SortType]"), _T("sort=type"));
	//Name sorting link
	if (pThis->m_Params.SharedSort == SHARED_SORT_NAME)
		Out.Replace(_T("[SortName]"), _T("sort=name&amp;sortreverse=") + strTmp);
	else
		Out.Replace(_T("[SortName]"), _T("sort=name"));
	//Size sorting Link
	if (pThis->m_Params.SharedSort == SHARED_SORT_SIZE)
		Out.Replace(_T("[SortSize]"), _T("sort=size&amp;sortreverse=") + strTmp);
	else
		Out.Replace(_T("[SortSize]"), _T("sort=size"));
	//Complete Sources sorting Link
	if (pThis->m_Params.SharedSort == SHARED_SORT_COMPLETES)
		Out.Replace(_T("[SortCompletes]"), _T("sort=completes&amp;sortreverse=") + strTmp);
	else
		Out.Replace(_T("[SortCompletes]"), _T("sort=completes"));
	//Priority sorting Link
	if (pThis->m_Params.SharedSort == SHARED_SORT_PRIORITY)
		Out.Replace(_T("[SortPriority]"), _T("sort=priority&amp;sortreverse=") + strTmp);
	else
		Out.Replace(_T("[SortPriority]"), _T("sort=priority"));
	//Transferred sorting link
	if (pThis->m_Params.SharedSort == SHARED_SORT_TRANSFERRED) {
		if (pThis->m_Params.bSharedSortReverse)
			Out.Replace(_T("[SortTransferred]"), _T("sort=alltimetransferred&amp;sortreverse=") + strTmp);
		else
			Out.Replace(_T("[SortTransferred]"), _T("sort=transferred&amp;sortreverse=") + strTmp);
	} else
		if (pThis->m_Params.SharedSort == SHARED_SORT_ALL_TIME_TRANSFERRED) {
			if (pThis->m_Params.bSharedSortReverse)
				Out.Replace(_T("[SortTransferred]"), _T("sort=transferred&amp;sortreverse=") + strTmp);
			else
				Out.Replace(_T("[SortTransferred]"), _T("sort=alltimetransferred&amp;sortreverse=") + strTmp);
		} else
			Out.Replace(_T("[SortTransferred]"), _T("&amp;sort=transferred&amp;sortreverse=false"));
		//Request sorting link
		if (pThis->m_Params.SharedSort == SHARED_SORT_REQUESTS) {
			if (pThis->m_Params.bSharedSortReverse)
				Out.Replace(_T("[SortRequests]"), _T("sort=alltimerequests&amp;sortreverse=") + strTmp);
			else
				Out.Replace(_T("[SortRequests]"), _T("sort=requests&amp;sortreverse=") + strTmp);
		} else
			if (pThis->m_Params.SharedSort == SHARED_SORT_ALL_TIME_REQUESTS) {
				if (pThis->m_Params.bSharedSortReverse)
					Out.Replace(_T("[SortRequests]"), _T("sort=requests&amp;sortreverse=") + strTmp);
				else
					Out.Replace(_T("[SortRequests]"), _T("sort=alltimerequests&amp;sortreverse=") + strTmp);
			} else
				Out.Replace(_T("[SortRequests]"), _T("&amp;sort=requests&amp;sortreverse=false"));
			//Accepts sorting link
			if (pThis->m_Params.SharedSort == SHARED_SORT_ACCEPTS) {
				if (pThis->m_Params.bSharedSortReverse)
					Out.Replace(_T("[SortAccepts]"), _T("sort=alltimeaccepts&amp;sortreverse=") + strTmp);
				else
					Out.Replace(_T("[SortAccepts]"), _T("sort=accepts&amp;sortreverse=") + strTmp);
			} else if (pThis->m_Params.SharedSort == SHARED_SORT_ALL_TIME_ACCEPTS) {
				if (pThis->m_Params.bSharedSortReverse)
					Out.Replace(_T("[SortAccepts]"), _T("sort=accepts&amp;sortreverse=") + strTmp);
				else
					Out.Replace(_T("[SortAccepts]"), _T("sort=alltimeaccepts&amp;sortreverse=") + strTmp);
			} else
				Out.Replace(_T("[SortAccepts]"), _T("&amp;sort=accepts&amp;sortreverse=false"));

			if (_ParseURL(Data.sURL, _T("reload")) == _T("true")) {
				CString strResultLog = _SpecialChars(theApp.emuledlg->GetLastLogEntry());	//Pick-up last line of the log
				strResultLog = strResultLog.TrimRight(_T('\n'));
				int iStringIndex = strResultLog.ReverseFind(_T('\n'));
				if (iStringIndex > 0)
					strResultLog.Delete(0, iStringIndex);
				Out.Replace(_T("[Message]"), strResultLog);
			} else
				Out.Replace(_T("[Message]"), _T(""));

			const TCHAR *pcSortIcon = (pThis->m_Params.bSharedSortReverse) ? pThis->m_Templates.sUpArrow : pThis->m_Templates.sDownArrow;
			const TCHAR *pcIconTmp;

			_GetPlainResString(strTmp, IDS_DL_FILENAME);
			if (!WSsharedColumnHidden[0]) {
				Out.Replace(_T("[FilenameI]"), (pThis->m_Params.SharedSort == SHARED_SORT_NAME) ? pcSortIcon : _T(""));
				Out.Replace(_T("[Filename]"), strTmp);
			} else {
				Out.Replace(_T("[FilenameI]"), _T(""));
				Out.Replace(_T("[Filename]"), _T(""));
			}
			Out.Replace(_T("[FilenameM]"), strTmp);

			_GetPlainResString(strTmp, IDS_SF_TRANSFERRED);
			if (!WSsharedColumnHidden[1]) {
				pcIconTmp = (pThis->m_Params.SharedSort == SHARED_SORT_TRANSFERRED) ? pcSortIcon : _T("");
				if (pThis->m_Params.SharedSort == SHARED_SORT_ALL_TIME_TRANSFERRED)
					pcIconTmp = (pThis->m_Params.bSharedSortReverse) ? pThis->m_Templates.strUpDoubleArrow : pThis->m_Templates.strDownDoubleArrow;
				Out.Replace(_T("[FileTransferredI]"), pcIconTmp);
				Out.Replace(_T("[FileTransferred]"), strTmp);
			} else {
				Out.Replace(_T("[FileTransferredI]"), _T(""));
				Out.Replace(_T("[FileTransferred]"), _T(""));
			}
			Out.Replace(_T("[FileTransferredM]"), strTmp);

			_GetPlainResString(strTmp, IDS_SF_REQUESTS);
			if (!WSsharedColumnHidden[2]) {
				pcIconTmp = (pThis->m_Params.SharedSort == SHARED_SORT_REQUESTS) ? pcSortIcon : _T("");
				if (pThis->m_Params.SharedSort == SHARED_SORT_ALL_TIME_REQUESTS)
					pcIconTmp = (pThis->m_Params.bSharedSortReverse) ? pThis->m_Templates.strUpDoubleArrow : pThis->m_Templates.strDownDoubleArrow;
				Out.Replace(_T("[FileRequestsI]"), pcIconTmp);
				Out.Replace(_T("[FileRequests]"), strTmp);
			} else {
				Out.Replace(_T("[FileRequestsI]"), _T(""));
				Out.Replace(_T("[FileRequests]"), _T(""));
			}
			Out.Replace(_T("[FileRequestsM]"), strTmp);

			_GetPlainResString(strTmp, IDS_SF_ACCEPTS);
			if (!WSsharedColumnHidden[3]) {
				pcIconTmp = (pThis->m_Params.SharedSort == SHARED_SORT_ACCEPTS) ? pcSortIcon : _T("");
				if (pThis->m_Params.SharedSort == SHARED_SORT_ALL_TIME_ACCEPTS)
					pcIconTmp = (pThis->m_Params.bSharedSortReverse) ? pThis->m_Templates.strUpDoubleArrow : pThis->m_Templates.strDownDoubleArrow;
				Out.Replace(_T("[FileAcceptsI]"), pcIconTmp);
				Out.Replace(_T("[FileAccepts]"), strTmp);
			} else {
				Out.Replace(_T("[FileAcceptsI]"), _T(""));
				Out.Replace(_T("[FileAccepts]"), _T(""));
			}
			Out.Replace(_T("[FileAcceptsM]"), strTmp);

			_GetPlainResString(strTmp, IDS_DL_SIZE);
			if (!WSsharedColumnHidden[4]) {
				Out.Replace(_T("[SizeI]"), (pThis->m_Params.SharedSort == SHARED_SORT_SIZE) ? pcSortIcon : _T(""));
				Out.Replace(_T("[Size]"), strTmp);
			} else {
				Out.Replace(_T("[SizeI]"), _T(""));
				Out.Replace(_T("[Size]"), _T(""));
			}
			Out.Replace(_T("[SizeM]"), strTmp);

			_GetPlainResString(strTmp, IDS_COMPLSOURCES);
			if (!WSsharedColumnHidden[5]) {
				Out.Replace(_T("[CompletesI]"), (pThis->m_Params.SharedSort == SHARED_SORT_COMPLETES) ? pcSortIcon : _T(""));
				Out.Replace(_T("[Completes]"), strTmp);
			} else {
				Out.Replace(_T("[CompletesI]"), _T(""));
				Out.Replace(_T("[Completes]"), _T(""));
			}
			Out.Replace(_T("[CompletesM]"), strTmp);

			_GetPlainResString(strTmp, IDS_PRIORITY);
			if (!WSsharedColumnHidden[6]) {
				Out.Replace(_T("[PriorityI]"), (pThis->m_Params.SharedSort == SHARED_SORT_PRIORITY) ? pcSortIcon : _T(""));
				Out.Replace(_T("[Priority]"), strTmp);
			} else {
				Out.Replace(_T("[PriorityI]"), _T(""));
				Out.Replace(_T("[Priority]"), _T(""));
			}
			Out.Replace(_T("[PriorityM]"), strTmp);

			Out.Replace(_T("[Actions]"), _GetPlainResString(IDS_WEB_ACTIONS));
			Out.Replace(_T("[Reload]"), _GetPlainResString(IDS_SF_RELOAD));
			Out.Replace(_T("[Session]"), sSession);
			Out.Replace(_T("[SharedList]"), _GetPlainResString(IDS_SHAREDFILES));

			CString OutE = pThis->m_Templates.sSharedLine;

			CArray<SharedFiles> SharedArray;

			// Populating array
			for (int ix = 0; ix < theApp.sharedfiles->GetCount(); ix++) {
				CCKey bufKey;
				CKnownFile* cur_file = theApp.sharedfiles->GetFileByIndex(ix);
				if (!cur_file)
					continue;
				//		uint16 nCountLo, nCountHi;
				uint32	dwResId;

				bool bPartFile = cur_file->IsPartFile();

				SharedFiles dFile;
				//dFile.sFileName = _SpecialChars(cur_file->GetFileName());
				dFile.bIsPartFile = cur_file->IsPartFile();
				dFile.sFileName = cur_file->GetFileName();
				if (bPartFile)
					dFile.sFileState = _T("filedown");
				else
					dFile.sFileState = _T("file");
				dFile.sFileType = GetWebImageNameForFileType(dFile.sFileName);

				dFile.m_qwFileSize = cur_file->GetFileSize();

				if (theApp.GetPublicIP() != 0 && !theApp.IsFirewalled())
					dFile.sED2kLink = cur_file->GetED2kLink(false, false, false, true, theApp.GetPublicIP());
				else
					dFile.sED2kLink = cur_file->GetED2kLink();

				dFile.nFileTransferred = cur_file->statistic.GetTransferred();
				dFile.nFileAllTimeTransferred = cur_file->statistic.GetAllTimeTransferred();
				dFile.nFileRequests = cur_file->statistic.GetRequests();
				dFile.nFileAllTimeRequests = cur_file->statistic.GetAllTimeRequests();
				dFile.nFileAccepts = cur_file->statistic.GetAccepts();
				dFile.nFileAllTimeAccepts = cur_file->statistic.GetAllTimeAccepts();
				dFile.sFileHash = md4str(cur_file->GetFileHash());

				if (cur_file->m_nCompleteSourcesCountLo == 0)
					dFile.sFileCompletes.Format(_T("< %u"), cur_file->m_nCompleteSourcesCountHi);
				else if (cur_file->m_nCompleteSourcesCountLo == cur_file->m_nCompleteSourcesCountHi)
					dFile.sFileCompletes.Format(_T("%u"), cur_file->m_nCompleteSourcesCountLo);
				else
					dFile.sFileCompletes.Format(_T("%u - %u"), cur_file->m_nCompleteSourcesCountLo, cur_file->m_nCompleteSourcesCountHi);

				if (cur_file->IsAutoUpPriority()) {
					if (cur_file->GetUpPriority() == PR_NORMAL)
						dwResId = IDS_PRIOAUTONORMAL;
					else if (cur_file->GetUpPriority() == PR_HIGH)
						dwResId = IDS_PRIOAUTOHIGH;
					else if (cur_file->GetUpPriority() == PR_VERYHIGH)
						dwResId = IDS_PRIOAUTORELEASE;
					else	//PR_LOW
						dwResId = IDS_PRIOAUTOLOW;
				} else {
					if (cur_file->GetUpPriority() == PR_LOW)
						dwResId = IDS_PRIOLOW;
					else if (cur_file->GetUpPriority() == PR_NORMAL)
						dwResId = IDS_PRIONORMAL;
					else if (cur_file->GetUpPriority() == PR_HIGH)
						dwResId = IDS_PRIOHIGH;
					else if (cur_file->GetUpPriority() == PR_VERYHIGH)
						dwResId = IDS_PRIORELEASE;
					else	//PR_VERYLOW
						dwResId = IDS_PRIOVERYLOW;
				}
				dFile.sFilePriority = GetResString(dwResId);

				dFile.nFilePriority = cur_file->GetUpPriority();
				dFile.bFileAutoPriority = cur_file->IsAutoUpPriority();
				SharedArray.Add(dFile);
			} //for

			// Sorting (simple bubble sort, we don't have tons of data here)
			bool bSorted = true;

			for (int nMax = 0; bSorted && nMax < SharedArray.GetCount() * 2; nMax++) {
				bSorted = false;
				for (int i = 0; i < SharedArray.GetCount() - 1; i++) {
					bool bSwap = false;
					switch (pThis->m_Params.SharedSort) {
					case SHARED_SORT_STATE:
						bSwap = SharedArray[i].sFileState.CompareNoCase(SharedArray[i + 1].sFileState) > 0;
						break;
					case SHARED_SORT_TYPE:
						bSwap = SharedArray[i].sFileType.CompareNoCase(SharedArray[i + 1].sFileType) > 0;
						break;
					case SHARED_SORT_NAME:
						bSwap = SharedArray[i].sFileName.CompareNoCase(SharedArray[i + 1].sFileName) < 0;
						break;
					case SHARED_SORT_SIZE:
						bSwap = SharedArray[i].m_qwFileSize < SharedArray[i + 1].m_qwFileSize;
						break;
					case SHARED_SORT_TRANSFERRED:
						bSwap = SharedArray[i].nFileTransferred < SharedArray[i + 1].nFileTransferred;
						break;
					case SHARED_SORT_ALL_TIME_TRANSFERRED:
						bSwap = SharedArray[i].nFileAllTimeTransferred < SharedArray[i + 1].nFileAllTimeTransferred;
						break;
					case SHARED_SORT_REQUESTS:
						bSwap = SharedArray[i].nFileRequests < SharedArray[i + 1].nFileRequests;
						break;
					case SHARED_SORT_ALL_TIME_REQUESTS:
						bSwap = SharedArray[i].nFileAllTimeRequests < SharedArray[i + 1].nFileAllTimeRequests;
						break;
					case SHARED_SORT_ACCEPTS:
						bSwap = SharedArray[i].nFileAccepts < SharedArray[i + 1].nFileAccepts;
						break;
					case SHARED_SORT_ALL_TIME_ACCEPTS:
						bSwap = SharedArray[i].nFileAllTimeAccepts < SharedArray[i + 1].nFileAllTimeAccepts;
						break;
					case SHARED_SORT_COMPLETES:
						bSwap = SharedArray[i].dblFileCompletes < SharedArray[i + 1].dblFileCompletes;
						break;
					case SHARED_SORT_PRIORITY:
						//Very low priority is define equal to 4 ! Must adapte sorting code
						if (SharedArray[i].nFilePriority == 4) {
							if (SharedArray[i + 1].nFilePriority == 4)
								bSwap = false;
							else
								bSwap = true;
						} else if (SharedArray[i + 1].nFilePriority == 4) {
							if (SharedArray[i].nFilePriority == 4)
								bSwap = true;
							else
								bSwap = false;
						} else
							bSwap = SharedArray[i].nFilePriority < SharedArray[i + 1].nFilePriority;
						break;
					}
					if (pThis->m_Params.bSharedSortReverse)
						bSwap = !bSwap;
					if (bSwap) {
						bSorted = true;
						SharedFiles TmpFile = SharedArray[i];
						SharedArray[i] = SharedArray[i + 1];
						SharedArray[i + 1] = TmpFile;
					}
				}
			}

			// Displaying
			CString sSharedList;
			for (int i = 0; i < SharedArray.GetCount(); i++) {
				TCHAR HTTPTempC[100] = _T("");
				CString HTTPProcessData(OutE);

				if (SharedArray[i].sFileHash == _ParseURL(Data.sURL, _T("hash")))
					HTTPProcessData.Replace(_T("[LastChangedDataset]"), _T("checked"));
				else
					HTTPProcessData.Replace(_T("[LastChangedDataset]"), _T("checked_no"));

				LPCTSTR sharedpriority;						//priority
				if (SharedArray[i].bFileAutoPriority)
					sharedpriority = _T("Auto");
				else
					switch (SharedArray[i].nFilePriority) {
					case PR_VERYLOW:
						sharedpriority = _T("VeryLow");
						break;
					case PR_LOW:
						sharedpriority = _T("Low");
						break;
					case PR_NORMAL:
						sharedpriority = _T("Normal");
						break;
					case PR_HIGH:
						sharedpriority = _T("High");
						break;
					case PR_VERYHIGH:
						sharedpriority = _T("Release");
						break;
					default:
						sharedpriority = _T("");
					}

				CString ed2k(SharedArray[i].sED2kLink);		//ed2klink
				ed2k.Replace(_T("'"), _T("&#8217;"));
				CString session = sSession;					//session
				CString hash(SharedArray[i].sFileHash);		//hash
				CString fname(SharedArray[i].sFileName);	//filename
				fname.Replace(_T("'"), _T("&#8217;"));

				bool downloadable = false;
				uchar fileid[16];
				if (hash.GetLength() == 32 && DecodeBase16(hash, hash.GetLength(), fileid, ARRSIZE(fileid))) {
					HTTPProcessData.Replace(_T("[hash]"), hash);
					const CKnownFile* cur_file = theApp.sharedfiles->GetFileByID(fileid);
					if (cur_file != NULL) {
						if (cur_file->GetUpPriority() == PR_VERYHIGH)
							HTTPProcessData.Replace(_T("[FileIsPriority]"), _T("release"));
						else
							HTTPProcessData.Replace(_T("[FileIsPriority]"), _T("none"));
						downloadable = !cur_file->IsPartFile() && (thePrefs.GetMaxWebUploadFileSizeMB() == 0 || SharedArray[i].m_qwFileSize < ((uint64)thePrefs.GetMaxWebUploadFileSizeMB()) * 1024 * 1024);
					}
				}

				HTTPProcessData.Replace(_T("[admin]"), (bAdmin) ? _T("admin") : _T(""));
				HTTPProcessData.Replace(_T("[ed2k]"), _SpecialChars(ed2k));
				HTTPProcessData.Replace(_T("[fname]"), _SpecialChars(fname));
				HTTPProcessData.Replace(_T("[session]"), session);
				HTTPProcessData.Replace(_T("[shared-priority]"), sharedpriority); //DonGato: priority change

				HTTPProcessData.Replace(_T("[FileName]"), _SpecialChars(SharedArray[i].sFileName));
				HTTPProcessData.Replace(_T("[FileType]"), SharedArray[i].sFileType);
				HTTPProcessData.Replace(_T("[FileState]"), SharedArray[i].sFileState);


				HTTPProcessData.Replace(_T("[Downloadable]"), downloadable ? _T("yes") : _T("no"));

				HTTPProcessData.Replace(_T("[IFDOWNLOADABLE]"), downloadable ? _T("") : _T("<!--"));
				HTTPProcessData.Replace(_T("[/IFDOWNLOADABLE]"), downloadable ? _T("") : _T("-->"));


				if (!WSsharedColumnHidden[0]) {
					if (SharedArray[i].sFileName.GetLength() > SHORT_LENGTH)
						HTTPProcessData.Replace(_T("[ShortFileName]"), _SpecialChars(SharedArray[i].sFileName.Left(SHORT_LENGTH - 3)) + _T("..."));
					else
						HTTPProcessData.Replace(_T("[ShortFileName]"), _SpecialChars(SharedArray[i].sFileName));
				} else
					HTTPProcessData.Replace(_T("[ShortFileName]"), _T(""));
				if (!WSsharedColumnHidden[1]) {
					HTTPProcessData.Replace(_T("[FileTransferred]"), CastItoXBytes(SharedArray[i].nFileTransferred));
					HTTPProcessData.Replace(_T("[FileAllTimeTransferred]"), _T(" (") + CastItoXBytes(SharedArray[i].nFileAllTimeTransferred) + _T(')'));
				} else {
					HTTPProcessData.Replace(_T("[FileTransferred]"), _T(""));
					HTTPProcessData.Replace(_T("[FileAllTimeTransferred]"), _T(""));
				}
				if (!WSsharedColumnHidden[2]) {
					_stprintf(HTTPTempC, _T("%i"), SharedArray[i].nFileRequests);
					HTTPProcessData.Replace(_T("[FileRequests]"), HTTPTempC);
					_stprintf(HTTPTempC, _T("%i"), SharedArray[i].nFileAllTimeRequests);
					HTTPProcessData.Replace(_T("[FileAllTimeRequests]"), _T(" (") + CString(HTTPTempC) + _T(')'));
				} else {
					HTTPProcessData.Replace(_T("[FileRequests]"), _T(""));
					HTTPProcessData.Replace(_T("[FileAllTimeRequests]"), _T(""));
				}
				if (!WSsharedColumnHidden[3]) {
					_stprintf(HTTPTempC, _T("%i"), SharedArray[i].nFileAccepts);
					HTTPProcessData.Replace(_T("[FileAccepts]"), HTTPTempC);
					_stprintf(HTTPTempC, _T("%i"), SharedArray[i].nFileAllTimeAccepts);
					HTTPProcessData.Replace(_T("[FileAllTimeAccepts]"), _T(" (") + CString(HTTPTempC) + _T(')'));
				} else {
					HTTPProcessData.Replace(_T("[FileAccepts]"), _T(""));
					HTTPProcessData.Replace(_T("[FileAllTimeAccepts]"), _T(""));
				}
				if (!WSsharedColumnHidden[4]) {
					HTTPProcessData.Replace(_T("[FileSize]"), CastItoXBytes(SharedArray[i].m_qwFileSize));
				} else
					HTTPProcessData.Replace(_T("[FileSize]"), _T(""));
				if (!WSsharedColumnHidden[5])
					HTTPProcessData.Replace(_T("[Completes]"), SharedArray[i].sFileCompletes);
				else
					HTTPProcessData.Replace(_T("[Completes]"), _T(""));
				if (!WSsharedColumnHidden[6])
					HTTPProcessData.Replace(_T("[Priority]"), SharedArray[i].sFilePriority);
				else
					HTTPProcessData.Replace(_T("[Priority]"), _T(""));

				HTTPProcessData.Replace(_T("[FileHash]"), SharedArray[i].sFileHash);

				sSharedList += HTTPProcessData;
			}
			Out.Replace(_T("[SharedFilesList]"), sSharedList);
			Out.Replace(_T("[Session]"), sSession);

			return Out;
}

CString CWebServer::_GetGraphs(const ThreadData& Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	CString Out = pThis->m_Templates.sGraphs;

	CString strGraphDownload, strGraphUpload, strGraphCons;

	for (int i = 0; i < WEB_GRAPH_WIDTH; i++) {
		if (i < pThis->m_Params.PointsForWeb.GetCount()) {
			if (i != 0) {
				strGraphDownload += _T(',');
				strGraphUpload += _T(',');
				strGraphCons += _T(',');
			}
			const UpDown& pt = pThis->m_Params.PointsForWeb[i];
			// download
			strGraphDownload.AppendFormat(_T("%u"), (uint32)(pt.download * 1024));

			// upload
			strGraphUpload.AppendFormat(_T("%u"), (uint32)(pt.upload * 1024));

			// connections
			strGraphCons.AppendFormat(_T("%u"), (uint32)(pt.connections));
		}
	}

	Out.Replace(_T("[GraphDownload]"), strGraphDownload);
	Out.Replace(_T("[GraphUpload]"), strGraphUpload);
	Out.Replace(_T("[GraphConnections]"), strGraphCons);

	Out.Replace(_T("[TxtDownload]"), _GetPlainResString(IDS_TW_DOWNLOADS));
	Out.Replace(_T("[TxtUpload]"), _GetPlainResString(IDS_TW_UPLOADS));
	Out.Replace(_T("[TxtTime]"), _GetPlainResString(IDS_TIME));
	Out.Replace(_T("[KByteSec]"), _GetPlainResString(IDS_KBYTESPERSEC));
	Out.Replace(_T("[TxtConnections]"), _GetPlainResString(IDS_SP_ACTCON));

	Out.Replace(_T("[ScaleTime]"), (LPCTSTR)CastSecondsToHM(((time_t)thePrefs.GetTrafficOMeterInterval()) * WEB_GRAPH_WIDTH));

	CString s1;
	s1.Format(_T("%i"), thePrefs.GetMaxGraphDownloadRate() + 4);
	Out.Replace(_T("[MaxDownload]"), s1);
	s1.Format(_T("%i"), thePrefs.GetMaxGraphUploadRate(true) + 4);
	Out.Replace(_T("[MaxUpload]"), s1);
	s1.Format(_T("%u"), thePrefs.GetMaxConnections() + 20);
	Out.Replace(_T("[MaxConnections]"), s1);

	return Out;
}

CString CWebServer::_GetAddServerBox(const ThreadData& Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString sSession = _ParseURL(Data.sURL, _T("ses"));

	if (!IsSessionAdmin(Data, sSession))
		return CString();

	CString resultlog = _SpecialChars(theApp.emuledlg->GetLastLogEntry()); //Pick-up last line of the log

	CString Out = pThis->m_Templates.sAddServerBox;
	if (_ParseURL(Data.sURL, _T("addserver")) == _T("true")) {
		CString strServerAddress = _ParseURL(Data.sURL, _T("serveraddr")).Trim();
		CString strServerPort = _ParseURL(Data.sURL, _T("serverport")).Trim();
		if (!strServerAddress.IsEmpty() && !strServerPort.IsEmpty()) {
			CString strServerName = _ParseURL(Data.sURL, _T("servername")).Trim();
			if (strServerName.IsEmpty())
				strServerName = strServerAddress;
			CServer* nsrv = new CServer((uint16)_tstoi(strServerPort), strServerAddress);
			nsrv->SetListName(strServerName);
			if (!theApp.emuledlg->serverwnd->serverlistctrl.AddServer(nsrv, true)) {
				delete nsrv;
				Out.Replace(_T("[Message]"), _GetPlainResString(IDS_ERROR));
			} else {
				const CString sPrio = _ParseURL(Data.sURL, _T("priority"));
				if (sPrio == _T("low"))
					nsrv->SetPreference(PR_LOW);
				else if (sPrio == _T("normal"))
					nsrv->SetPreference(PR_NORMAL);
				else if (sPrio == _T("high"))
					nsrv->SetPreference(PR_HIGH);

				SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_UPDATESERVER, (LPARAM)nsrv);

				if (_ParseURL(Data.sURL, _T("addtostatic")) == _T("true")) {
					_AddToStatic(_ParseURL(Data.sURL, _T("serveraddr")), _tstoi(_ParseURL(Data.sURL, _T("serverport"))));
					resultlog.AppendFormat(_T("<br>%s"), (LPCTSTR)_SpecialChars(theApp.emuledlg->GetLastLogEntry())); //Pick-up last line of the log
				}
				resultlog = resultlog.TrimRight(_T('\n'));
				resultlog = resultlog.Mid(resultlog.ReverseFind(_T('\n')));
				Out.Replace(_T("[Message]"), resultlog);
				if (_ParseURL(Data.sURL, _T("connectnow")) == _T("true"))
					_ConnectToServer(_ParseURL(Data.sURL, _T("serveraddr")), _tstoi(_ParseURL(Data.sURL, _T("serverport"))));
			}
		} else
			Out.Replace(_T("[Message]"), _GetPlainResString(IDS_ERROR));
	} else if (_ParseURL(Data.sURL, _T("updateservermetfromurl")) == _T("true")) {
		const CString url = _ParseURL(Data.sURL, _T("servermeturl"));
		SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_UPDATESERVERMETFROMURL, (LPARAM)(LPCTSTR)url);

		resultlog = _SpecialChars(theApp.emuledlg->GetLastLogEntry());
		resultlog = resultlog.TrimRight(_T('\n'));
		resultlog = resultlog.Mid(resultlog.ReverseFind(_T('\n')));
		Out.Replace(_T("[Message]"), resultlog);
	} else
		Out.Replace(_T("[Message]"), _T(""));

	Out.Replace(_T("[AddServer]"), _GetPlainResString(IDS_SV_NEWSERVER));
	Out.Replace(_T("[IP]"), _GetPlainResString(IDS_SV_ADDRESS));
	Out.Replace(_T("[Port]"), _GetPlainResString(IDS_PORT));
	Out.Replace(_T("[Name]"), _GetPlainResString(IDS_SW_NAME));
	Out.Replace(_T("[Static]"), _GetPlainResString(IDS_STATICSERVER));
	Out.Replace(_T("[ConnectNow]"), _GetPlainResString(IDS_IRC_CONNECT));
	Out.Replace(_T("[Priority]"), _GetPlainResString(IDS_PRIORITY));
	Out.Replace(_T("[Low]"), _GetPlainResString(IDS_PRIOLOW));
	Out.Replace(_T("[Normal]"), _GetPlainResString(IDS_PRIONORMAL));
	Out.Replace(_T("[High]"), _GetPlainResString(IDS_PRIOHIGH));
	Out.Replace(_T("[Add]"), _GetPlainResString(IDS_SV_ADD));
	Out.Replace(_T("[UpdateServerMetFromURL]"), _GetPlainResString(IDS_SV_MET));
	Out.Replace(_T("[URL]"), _GetPlainResString(IDS_SV_URL));
	Out.Replace(_T("[Apply]"), _GetPlainResString(IDS_PW_APPLY));
	Out.Replace(_T("[URL_Disconnect]"), IsSessionAdmin(Data, sSession) ? (_T("?ses=") + sSession + _T("&amp;w=server&amp;c=disconnect")) : GetPermissionDenied());
	Out.Replace(_T("[URL_Connect]"), IsSessionAdmin(Data, sSession) ? (_T("?ses=") + sSession + _T("&amp;w=server&amp;c=connect")) : GetPermissionDenied());
	Out.Replace(_T("[Disconnect]"), _GetPlainResString(IDS_IRC_DISCONNECT));
	Out.Replace(_T("[Connect]"), _GetPlainResString(IDS_CONNECTTOANYSERVER));
	Out.Replace(_T("[ServerOptions]"), _GetPlainResString(IDS_FSTAT_CONNECTION));
	Out.Replace(_T("[Execute]"), _GetPlainResString(IDS_IRC_PERFORM));

	return Out;
}

CString CWebServer::_GetLog(const ThreadData& Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString sSession = _ParseURL(Data.sURL, _T("ses"));

	CString Out = pThis->m_Templates.sLog;

	if (_ParseURL(Data.sURL, _T("clear")) == _T("yes") && IsSessionAdmin(Data, sSession))
		theApp.emuledlg->ResetLog();

	Out.Replace(_T("[Clear]"), _GetPlainResString(IDS_PW_RESET));
	Out.Replace(_T("[Log]"), _SpecialChars(theApp.emuledlg->GetAllLogEntries(), false) + _T("<br><a name=\"end\"></a>"));
	Out.Replace(_T("[Session]"), sSession);

	return Out;
}

CString CWebServer::_GetServerInfo(const ThreadData& Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString sSession = _ParseURL(Data.sURL, _T("ses"));

	CString Out = pThis->m_Templates.sServerInfo;

	if (_ParseURL(Data.sURL, _T("clear")) == _T("yes") && IsSessionAdmin(Data, sSession))
		theApp.emuledlg->ResetServerInfo();

	Out.Replace(_T("[Clear]"), _GetPlainResString(IDS_PW_RESET));
	Out.Replace(_T("[ServerInfo]"), _SpecialChars(theApp.emuledlg->GetServerInfoText(), false) + _T("<br><a name=\"end\"></a>"));
	Out.Replace(_T("[Session]"), sSession);

	return Out;
}

CString CWebServer::_GetDebugLog(const ThreadData& Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString sSession = _ParseURL(Data.sURL, _T("ses"));

	CString Out = pThis->m_Templates.sDebugLog;

	if (_ParseURL(Data.sURL, _T("clear")) == _T("yes") && IsSessionAdmin(Data, sSession))
		theApp.emuledlg->ResetDebugLog();

	Out.Replace(_T("[Clear]"), _GetPlainResString(IDS_PW_RESET));
	Out.Replace(_T("[DebugLog]"), _SpecialChars(theApp.emuledlg->GetAllDebugLogEntries(), false) + _T("<br><a name=\"end\"></a>"));
	Out.Replace(_T("[Session]"), sSession);

	return Out;
}

CString CWebServer::_GetMyInfo(const ThreadData& Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	(void)_ParseURL(Data.sURL, _T("ses"));
	CString Out = pThis->m_Templates.sMyInfoLog;

	Out.Replace(_T("[MYINFOLOG]"), theApp.emuledlg->serverwnd->GetMyInfoString());

	return Out;
}

CString CWebServer::_GetKadDlg(const ThreadData& Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	/*if (!thePrefs.GetNetworkKademlia()) {
		CString buffer;
		buffer.Format(_T("<br><center>[KADDEACTIVATED]</center>")  );
		return buffer;
	}*/

	const CString sSession = _ParseURL(Data.sURL, _T("ses"));
	CString Out = pThis->m_Templates.sKad;

	if (!_ParseURL(Data.sURL, _T("bootstrap")).IsEmpty() && IsSessionAdmin(Data, sSession)) {
		CString dest;
		dest.Format(_T("%s:%s"), (LPCTSTR)_ParseURL(Data.sURL, _T("ip")), (LPCTSTR)_ParseURL(Data.sURL, _T("port")));
		SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_KAD_BOOTSTRAP, (LPARAM)(LPCTSTR)dest);
	}

	if (_ParseURL(Data.sURL, _T("c")) == _T("connect") && IsSessionAdmin(Data, sSession))
		SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_KAD_START, 0);

	if (_ParseURL(Data.sURL, _T("c")) == _T("disconnect") && IsSessionAdmin(Data, sSession))
		SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_KAD_STOP, 0);

	if (_ParseURL(Data.sURL, _T("c")) == _T("rcfirewall") && IsSessionAdmin(Data, sSession))
		SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_KAD_RCFW, 0);

	// check the condition if bootstrap is possible
	Out.Replace(_T("[BOOTSTRAPLINE]"), Kademlia::CKademlia::IsConnected() ? _T("") : pThis->m_Templates.sBootstrapLine);

	// Infos
	CString buffer;
	if (Kademlia::CKademlia::IsConnected())
		if (Kademlia::CKademlia::IsFirewalled()) {
			Out.Replace(_T("[KADSTATUS]"), GetResString(IDS_FIREWALLED));
			buffer.Format(_T("<a href=\"?ses=%s&amp;w=kad&amp;c=rcfirewall\">%s</a>"), (LPCTSTR)sSession, (LPCTSTR)GetResString(IDS_KAD_RECHECKFW));
			buffer.AppendFormat(_T("<br><a href=\"?ses=%s&amp;w=kad&amp;c=disconnect\">%s</a>"), (LPCTSTR)sSession, (LPCTSTR)GetResString(IDS_IRC_DISCONNECT));
		} else {
			Out.Replace(_T("[KADSTATUS]"), GetResString(IDS_CONNECTED));
			buffer.Format(_T("<a href=\"?ses=%s&amp;w=kad&amp;c=disconnect\">%s</a>"), (LPCTSTR)sSession, (LPCTSTR)GetResString(IDS_IRC_DISCONNECT));
		}
	else
		if (Kademlia::CKademlia::IsRunning()) {
			Out.Replace(_T("[KADSTATUS]"), GetResString(IDS_CONNECTING));
			buffer.Format(_T("<a href=\"?ses=%s&amp;w=kad&amp;c=disconnect\">%s</a>"), (LPCTSTR)sSession, (LPCTSTR)GetResString(IDS_IRC_DISCONNECT));
		} else {
			Out.Replace(_T("[KADSTATUS]"), GetResString(IDS_DISCONNECTED));
			buffer.Format(_T("<a href=\"?ses=%s&amp;w=kad&amp;c=connect\">%s</a>"), (LPCTSTR)sSession, (LPCTSTR)GetResString(IDS_IRC_CONNECT));
		}

	Out.Replace(_T("[KADACTION]"), buffer);

	// kadstats
	// labels
	buffer.Format(_T("%s<br>%s"), (LPCTSTR)GetResString(IDS_KADCONTACTLAB), (LPCTSTR)GetResString(IDS_KADSEARCHLAB));
	Out.Replace(_T("[KADSTATSLABELS]"), buffer);

	// numbers
	buffer.Format(_T("%u<br>%i"), theApp.emuledlg->kademliawnd->GetContactCount(),
		theApp.emuledlg->kademliawnd->searchList->GetItemCount());
	Out.Replace(_T("[KADSTATSDATA]"), buffer);

	Out.Replace(_T("[BS_IP]"), GetResString(IDS_IP));
	Out.Replace(_T("[BS_PORT]"), GetResString(IDS_PORT));
	Out.Replace(_T("[BOOTSTRAP]"), GetResString(IDS_BOOTSTRAP));
	Out.Replace(_T("[KADSTAT]"), GetResString(IDS_STATSSETUPINFO));
	Out.Replace(_T("[STATUS]"), GetResString(IDS_STATUS));
	Out.Replace(_T("[KAD]"), GetResString(IDS_KADEMLIA));
	Out.Replace(_T("[Session]"), sSession);

	return Out;
}

CString CWebServer::_GetStats(const ThreadData& Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	(void)_ParseURL(Data.sURL, _T("ses"));

	// refresh statistics
	SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_SHOWSTATISTICS, 1);

	CString Out = pThis->m_Templates.sStats;
	// eklmn: new stats
	Out.Replace(_T("[Stats]"), theApp.emuledlg->statisticswnd->stattree.GetHTMLForExport());

	return Out;
}

CString CWebServer::_GetPreferences(const ThreadData& Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	const CString sSession = _ParseURL(Data.sURL, _T("ses"));

	CString Out = pThis->m_Templates.sPreferences;
	Out.Replace(_T("[Session]"), sSession);

	if ((_ParseURL(Data.sURL, _T("saveprefs")) == _T("true")) && IsSessionAdmin(Data, sSession)) {
		CString strTmp = _ParseURL(Data.sURL, _T("gzip")).MakeLower();
		if (strTmp == _T("true") || strTmp == _T("on"))
			thePrefs.SetWebUseGzip(true);
		else if (strTmp == _T("false") || strTmp.IsEmpty())
			thePrefs.SetWebUseGzip(false);

		if (!_ParseURL(Data.sURL, _T("refresh")).IsEmpty())
			thePrefs.SetWebPageRefresh(_tstoi(_ParseURL(Data.sURL, _T("refresh"))));

		strTmp = _ParseURL(Data.sURL, _T("maxcapdown"));
		if (!strTmp.IsEmpty())
			thePrefs.SetMaxGraphDownloadRate(_tstoi(strTmp));
		strTmp = _ParseURL(Data.sURL, _T("maxcapup"));
		if (!strTmp.IsEmpty())
			thePrefs.SetMaxGraphUploadRate(_tstoi(strTmp));

		strTmp = _ParseURL(Data.sURL, _T("maxdown"));
		if (!strTmp.IsEmpty()) {
			uint32 dwSpeed = _tstoi(strTmp);
			thePrefs.SetMaxDownload(dwSpeed > 0 ? dwSpeed : UNLIMITED);
		}
		strTmp = _ParseURL(Data.sURL, _T("maxup"));
		if (!strTmp.IsEmpty()) {
			uint32 dwSpeed = _tstoi(strTmp);
			thePrefs.SetMaxUpload(dwSpeed > 0 ? dwSpeed : UNLIMITED);
		}

		if (!_ParseURL(Data.sURL, _T("maxsources")).IsEmpty())
			thePrefs.SetMaxSourcesPerFile(_tstoi(_ParseURL(Data.sURL, _T("maxsources"))));
		if (!_ParseURL(Data.sURL, _T("maxconnections")).IsEmpty())
			thePrefs.SetMaxConnections(_tstoi(_ParseURL(Data.sURL, _T("maxconnections"))));
		if (!_ParseURL(Data.sURL, _T("maxconnectionsperfive")).IsEmpty())
			thePrefs.SetMaxConsPerFive(_tstoi(_ParseURL(Data.sURL, _T("maxconnectionsperfive"))));
	}

	// Fill form
	Out.Replace(_T("[UseGzipVal]"), thePrefs.GetWebUseGzip() ? _T("checked") : _T(""));

	CString sRefresh;

	sRefresh.Format(_T("%d"), thePrefs.GetWebPageRefresh());
	Out.Replace(_T("[RefreshVal]"), sRefresh);

	sRefresh.Format(_T("%u"), thePrefs.GetMaxSourcePerFileDefault());
	Out.Replace(_T("[MaxSourcesVal]"), sRefresh);

	sRefresh.Format(_T("%u"), thePrefs.GetMaxConnections());
	Out.Replace(_T("[MaxConnectionsVal]"), sRefresh);

	sRefresh.Format(_T("%u"), thePrefs.GetMaxConperFive());
	Out.Replace(_T("[MaxConnectionsPer5Val]"), sRefresh);

	Out.Replace(_T("[KBS]"), _GetPlainResString(IDS_KBYTESPERSEC) + _T(':'));
	Out.Replace(_T("[LimitForm]"), _GetPlainResString(IDS_WEB_CONLIMITS) + _T(':'));
	Out.Replace(_T("[MaxSources]"), _GetPlainResString(IDS_PW_MAXSOURCES) + _T(':'));
	Out.Replace(_T("[MaxConnections]"), _GetPlainResString(IDS_PW_MAXC) + _T(':'));
	Out.Replace(_T("[MaxConnectionsPer5]"), _GetPlainResString(IDS_MAXCON5SECLABEL) + _T(':'));
	Out.Replace(_T("[UseGzipForm]"), _GetPlainResString(IDS_WEB_GZIP_COMPRESSION));
	Out.Replace(_T("[UseGzipComment]"), _GetPlainResString(IDS_WEB_GZIP_COMMENT));

	Out.Replace(_T("[RefreshTimeForm]"), _GetPlainResString(IDS_WEB_REFRESH_TIME));
	Out.Replace(_T("[RefreshTimeComment]"), _GetPlainResString(IDS_WEB_REFRESH_COMMENT));
	Out.Replace(_T("[SpeedForm]"), _GetPlainResString(IDS_SPEED_LIMITS));
	Out.Replace(_T("[SpeedCapForm]"), _GetPlainResString(IDS_CAPACITY_LIMITS));

	Out.Replace(_T("[MaxCapDown]"), _GetPlainResString(IDS_PW_CON_DOWNLBL));
	Out.Replace(_T("[MaxCapUp]"), _GetPlainResString(IDS_PW_CON_UPLBL));
	Out.Replace(_T("[MaxDown]"), _GetPlainResString(IDS_PW_CON_DOWNLBL));
	Out.Replace(_T("[MaxUp]"), _GetPlainResString(IDS_PW_CON_UPLBL));
	Out.Replace(_T("[WebControl]"), _GetPlainResString(IDS_WEB_CONTROL));
	Out.Replace(_T("[eMuleAppName]"), _T("eMule"));
	Out.Replace(_T("[Apply]"), _GetPlainResString(IDS_PW_APPLY));

	CString m_sTestURL;
	m_sTestURL.Format(PORTTESTURL, thePrefs.GetPort(), thePrefs.GetUDPPort(), thePrefs.GetLanguageID());

	// the portcheck will need to do an obfuscated callback too if obfuscation is requested, so we have to provide our userhash so it can create the key
	if (thePrefs.IsClientCryptLayerRequested())
		m_sTestURL.AppendFormat(_T("&obfuscated_test=%s"), (LPCTSTR)md4str(thePrefs.GetUserHash()));

	Out.Replace(_T("[CONNECTIONTESTLINK]"), _SpecialChars(m_sTestURL));
	Out.Replace(_T("[CONNECTIONTESTLABEL]"), GetResString(IDS_CONNECTIONTEST));


	CString sT;
	sT.Format(_T("%u"), thePrefs.GetMaxDownload() == UNLIMITED ? 0 : thePrefs.GetMaxDownload());
	Out.Replace(_T("[MaxDownVal]"), sT);

	sT.Format(_T("%u"), thePrefs.GetMaxUpload() == UNLIMITED ? 0 : thePrefs.GetMaxUpload());
	Out.Replace(_T("[MaxUpVal]"), sT);

	sT.Format(_T("%i"), thePrefs.GetMaxGraphDownloadRate());
	Out.Replace(_T("[MaxCapDownVal]"), sT);

	sT.Format(_T("%i"), thePrefs.GetMaxGraphUploadRate(true));
	Out.Replace(_T("[MaxCapUpVal]"), sT);

	return Out;
}

CString CWebServer::_GetLoginScreen(const ThreadData& Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	(void)_ParseURL(Data.sURL, _T("ses"));

	CString Out = pThis->m_Templates.sLogin;

	Out.Replace(_T("[CharSet]"), HTTPENCODING);
	Out.Replace(_T("[eMuleAppName]"), _T("eMule"));
	Out.Replace(_T("[version]"), theApp.m_strCurVersionLong);
	Out.Replace(_T("[Login]"), _GetPlainResString(IDS_WEB_LOGIN));
	Out.Replace(_T("[EnterPassword]"), _GetPlainResString(IDS_WEB_ENTER_PASSWORD));
	Out.Replace(_T("[LoginNow]"), _GetPlainResString(IDS_WEB_LOGIN_NOW));
	Out.Replace(_T("[WebControl]"), _GetPlainResString(IDS_WEB_CONTROL));

	if (pThis->m_nIntruderDetect >= 1)
		Out.Replace(_T("[FailedLogin]"), _T("<p class=\"failed\">") + _GetPlainResString(IDS_WEB_BADLOGINATTEMPT) + _T("</p>"));
	else
		Out.Replace(_T("[FailedLogin]"), _T("&nbsp;"));

	return Out;
}

// We have to add gz-header and some other stuff
// to standard zlib functions
// in order to use gzip in web pages
int CWebServer::_GzipCompress(Bytef *dest, uLongf *destLen, const Bytef *source, uLong sourceLen, int level)
{
	static const int gz_magic[2] = { 0x1f, 0x8b }; // gzip magic header
	z_stream stream = {};
	stream.zalloc = (alloc_func)NULL;
	stream.zfree = (free_func)NULL;
	stream.opaque = (voidpf)NULL;
	uLong crc = crc32(0L, Z_NULL, 0);
	// init Zlib stream
	// NOTE windowBits is passed < 0 to suppress zlib header
	int err = deflateInit2(&stream, level, Z_DEFLATED, -MAX_WBITS, MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY);
	if (err != Z_OK)
		return err;

	sprintf((char*)dest, "%c%c%c%c%c%c%c%c%c%c", gz_magic[0], gz_magic[1],
		Z_DEFLATED, 0 /*flags*/, 0, 0, 0, 0 /*time*/, 0 /*xflags*/, 255);
	// wire buffers
	stream.next_in = (Bytef*)source;
	stream.avail_in = (uInt)sourceLen;
	stream.next_out = &dest[10];
	stream.avail_out = *destLen - 18;
	// doit
	err = deflate(&stream, Z_FINISH);
	if (err != Z_STREAM_END) {
		deflateEnd(&stream);
		return err;
	}
	err = deflateEnd(&stream);
	crc = crc32(crc, (const Bytef *)source, (uInt)sourceLen);
	//CRC
	*(uLong *)&dest[10 + stream.total_out] = crc;
	// Length
	*(uLong *)&dest[10 + stream.total_out + sizeof(uLong)] = sourceLen;
	*destLen = 10 + stream.total_out + 8;

	return err;
}

bool CWebServer::_IsLoggedIn(const ThreadData& Data, long lSession)
{
	CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return false;

	_RemoveTimeOuts(Data);

	// find our session
	// i should have used CMap there, but i like CArray more ;-)
	if (lSession != 0)
		for (INT_PTR i = pThis->m_Params.Sessions.GetSize(); --i >= 0;)
			if (pThis->m_Params.Sessions[i].lSession == lSession) {
				// if found, also reset expiration time
				pThis->m_Params.Sessions[i].startTime = CTime::GetCurrentTime();
				return true;
			}

	return false;
}

void CWebServer::_RemoveTimeOuts(const ThreadData& Data)
{
	// remove expired sessions
	CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis != NULL)
		pThis->UpdateSessionCount();
}

bool CWebServer::_RemoveSession(const ThreadData& Data, long lSession)
{
	CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL || lSession == 0)
		return false;

	// find our session
	for (INT_PTR i = pThis->m_Params.Sessions.GetSize(); --i >= 0;)
		if (pThis->m_Params.Sessions[i].lSession == lSession) {
			pThis->m_Params.Sessions.RemoveAt(i);
			CString t_ulCurIP;
			t_ulCurIP.Format(_T("%u.%u.%u.%u"), (byte)pThis->m_ulCurIP, (byte)(pThis->m_ulCurIP >> 8), (byte)(pThis->m_ulCurIP >> 16), (byte)(pThis->m_ulCurIP >> 24));
			AddLogLine(true, (LPCTSTR)GetResString(IDS_WEB_SESSIONEND), (LPCTSTR)t_ulCurIP);
			SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_UPDATEMYINFO, 0);
			return true;
		}

	return false;
}

Session CWebServer::GetSessionByID(ThreadData Data, long sessionID)
{
	CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis != NULL && sessionID != 0)
		for (INT_PTR i = pThis->m_Params.Sessions.GetSize(); --i >= 0;)
			if (pThis->m_Params.Sessions[i].lSession == sessionID)
				return pThis->m_Params.Sessions[i];

	return Session{};
}

bool CWebServer::IsSessionAdmin(ThreadData Data, const CString &strSsessionID)
{
	long sessionID = _tstol(strSsessionID);
	CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis != NULL && sessionID != 0)
		for (INT_PTR i = pThis->m_Params.Sessions.GetSize(); --i >= 0;)
			if (pThis->m_Params.Sessions[i].lSession == sessionID)
				return pThis->m_Params.Sessions[i].admin;

	return false;
}

CString CWebServer::GetPermissionDenied()
{
	CString s;
	s.Format(_T("javascript:alert(\'%s\')"), (LPCTSTR)_GetPlainResString(IDS_ACCESSDENIED));
	return s;
}

CString CWebServer::_GetPlainResString(UINT nID, bool noquote)
{
	CString sRet = GetResString(nID);
	sRet.Remove(_T('&'));
	if (noquote) {
		sRet.Replace(_T("'"), _T("&#8217;"));
		sRet.Replace(_T("\n"), _T("\\n"));
	}
	return sRet;
}

void CWebServer::_GetPlainResString(CString& rstrOut, UINT nID, bool noquote)
{
	rstrOut = _GetPlainResString(nID, noquote);
}

// Ornis: creating the progressbar. colored if resources are given/available
CString CWebServer::_GetDownloadGraph(const ThreadData& Data, const CString& filehash)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	CPartFile* pPartFile;
	uchar fileid[16];
	if (filehash.GetLength() != 32 || !DecodeBase16(filehash, filehash.GetLength(), fileid, ARRSIZE(fileid)))
		return CString();

	CString Out;
	CString progresscolor[12];

	pPartFile = theApp.downloadqueue->GetFileByID(fileid);

	if (pPartFile != NULL && (pPartFile->GetStatus() == PS_PAUSED)) {
		//	Color style (paused files)
		progresscolor[0] = _T("p_green.gif");
		progresscolor[1] = _T("p_black.gif");
		progresscolor[2] = _T("p_yellow.gif");
		progresscolor[3] = _T("p_red.gif");
		progresscolor[4] = _T("p_blue1.gif");
		progresscolor[5] = _T("p_blue2.gif");
		progresscolor[6] = _T("p_blue3.gif");
		progresscolor[7] = _T("p_blue4.gif");
		progresscolor[8] = _T("p_blue5.gif");
		progresscolor[9] = _T("p_blue6.gif");
		progresscolor[10] = _T("p_greenpercent.gif");
		progresscolor[11] = _T("transparent.gif");
	} else {
		//	Color style (active files)
		progresscolor[0] = _T("green.gif");
		progresscolor[1] = _T("black.gif");
		progresscolor[2] = _T("yellow.gif");
		progresscolor[3] = _T("red.gif");
		progresscolor[4] = _T("blue1.gif");
		progresscolor[5] = _T("blue2.gif");
		progresscolor[6] = _T("blue3.gif");
		progresscolor[7] = _T("blue4.gif");
		progresscolor[8] = _T("blue5.gif");
		progresscolor[9] = _T("blue6.gif");
		progresscolor[10] = _T("greenpercent.gif");
		progresscolor[11] = _T("transparent.gif");
	}

	if (pPartFile == NULL || !pPartFile->IsPartFile()) {
		Out.AppendFormat(pThis->m_Templates.sProgressbarImgsPercent, (LPCTSTR)progresscolor[10], pThis->m_Templates.iProgressbarWidth);
		Out += _T("<br>");
		Out.AppendFormat(pThis->m_Templates.sProgressbarImgs, (LPCTSTR)progresscolor[0], pThis->m_Templates.iProgressbarWidth);
	} else {
		CString s_ChunkBar = pPartFile->GetProgressString(pThis->m_Templates.iProgressbarWidth);

		// and now make a graph out of the array - need to be in a progressive way
		BYTE lastcolor = 1;
		uint16 lastindex = 0;

		int compl = static_cast<int>((pThis->m_Templates.iProgressbarWidth / 100.0) * pPartFile->GetPercentCompleted());

		Out.AppendFormat(pThis->m_Templates.sProgressbarImgsPercent,
			(LPCTSTR)progresscolor[(compl > 0) ? 10 : 11], (compl > 0) ? compl : 5);
		Out += _T("<br>");

		for (uint16 i = 0; i < pThis->m_Templates.iProgressbarWidth; ++i) {
			if (lastcolor != _tstoi(s_ChunkBar.Mid(i, 1))) {
				if (i > lastindex && lastcolor < _countof(progresscolor))
					Out.AppendFormat(pThis->m_Templates.sProgressbarImgs, (LPCTSTR)progresscolor[lastcolor], i - lastindex);
				lastcolor = (BYTE)_tstoi(s_ChunkBar.Mid(i, 1));
				lastindex = i;
			}
		}
		Out.AppendFormat(pThis->m_Templates.sProgressbarImgs, (LPCTSTR)progresscolor[lastcolor], pThis->m_Templates.iProgressbarWidth - lastindex);
	}
	return Out;
}

CString CWebServer::_GetSearch(const ThreadData& Data)
{
	CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	int cat = _tstoi(_ParseURL(Data.sURL, _T("cat")));
	const CString sSession = _ParseURL(Data.sURL, _T("ses"));
	CString Out = pThis->m_Templates.sSearch;

	if (!_ParseURL(Data.sURL, _T("downloads")).IsEmpty() && IsSessionAdmin(Data, sSession)) {
		const CString downloads = _ParseURLArray(const_cast<CString &>(Data.sURL), _T("downloads"));

		int curPos = 0;
		for (CString resToken = downloads.Tokenize(_T("|"), curPos); !resToken.IsEmpty(); resToken = downloads.Tokenize(_T("|"), curPos))
			if (resToken.GetLength() == 32)
				SendMessage(theApp.emuledlg->m_hWnd, WEB_ADDDOWNLOADS, (LPARAM)(LPCTSTR)resToken, cat);
	}

	CString sCat;
	if (cat != 0)
		sCat.Format(_T("%i"), cat);

	CString sCmd = _ParseURL(Data.sURL, _T("c"));
	if (sCmd.CompareNoCase(_T("menu")) == 0) {
		int iMenu = _tstoi(_ParseURL(Data.sURL, _T("m")));
		bool bValue = _tstoi(_ParseURL(Data.sURL, _T("v"))) != 0;
		WSsearchColumnHidden[iMenu] = bValue;

		SaveWIConfigArray(WSsearchColumnHidden, ARRSIZE(WSsearchColumnHidden), _T("searchColumnHidden"));
	}

	if (!_ParseURL(Data.sURL, _T("tosearch")).IsEmpty() && IsSessionAdmin(Data, sSession)) {

		// perform search
		SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_DELETEALLSEARCHES, 0);

		// get method
		CString method = (_ParseURL(Data.sURL, _T("method")));

		SSearchParams* pParams = new SSearchParams;
		pParams->strExpression = _ParseURL(Data.sURL, _T("tosearch"));
		pParams->strFileType = _ParseURL(Data.sURL, _T("type"));
		// for safety: this string is sent to servers and/or kad nodes, validate it!
		if (!pParams->strFileType.IsEmpty()
			&& pParams->strFileType != ED2KFTSTR_ARCHIVE
			&& pParams->strFileType != ED2KFTSTR_AUDIO
			&& pParams->strFileType != ED2KFTSTR_CDIMAGE
			&& pParams->strFileType != ED2KFTSTR_DOCUMENT
			&& pParams->strFileType != ED2KFTSTR_IMAGE
			&& pParams->strFileType != ED2KFTSTR_PROGRAM
			&& pParams->strFileType != ED2KFTSTR_VIDEO
			&& pParams->strFileType != ED2KFTSTR_EMULECOLLECTION)
		{
			ASSERT(0);
			pParams->strFileType.Empty();
		}
		pParams->ullMinSize = _tstoi64(_ParseURL(Data.sURL, _T("min"))) * 1048576ui64;
		pParams->ullMaxSize = _tstoi64(_ParseURL(Data.sURL, _T("max"))) * 1048576ui64;
		if (pParams->ullMaxSize < pParams->ullMinSize)
			pParams->ullMaxSize = 0;

		CString s = _ParseURL(Data.sURL, _T("avail"));
		pParams->uAvailability = s.IsEmpty() ? 0 : _tstoi(s);
		if (pParams->uAvailability > 1000000)
			pParams->uAvailability = 1000000;

		pParams->strExtension = _ParseURL(Data.sURL, _T("ext"));
		if (method == _T("kademlia"))
			pParams->eType = SearchTypeKademlia;
		else if (method == _T("global"))
			pParams->eType = SearchTypeEd2kGlobal;
		else
			pParams->eType = SearchTypeEd2kServer;


		CString strResponse = _GetPlainResString(IDS_SW_SEARCHINGINFO);
		try {
			if (pParams->eType != SearchTypeKademlia) {
				if (!theApp.emuledlg->searchwnd->DoNewEd2kSearch(pParams)) {
					delete pParams;
					pParams = NULL;
					strResponse = _GetPlainResString(IDS_ERR_NOTCONNECTED);
				} else
					Sleep(SEC2MS(2));	// wait for some results to come in (thanks thread)
			} else {
				if (!theApp.emuledlg->searchwnd->DoNewKadSearch(pParams)) {
					delete pParams;
					pParams = NULL;
					strResponse = _GetPlainResString(IDS_ERR_NOTCONNECTEDKAD);
				}
			}
		} catch (...) {
			strResponse = _GetPlainResString(IDS_ERROR);
			ASSERT(0);
			delete pParams;
		}
		Out.Replace(_T("[Message]"), strResponse);

	} else if (!_ParseURL(Data.sURL, _T("tosearch")).IsEmpty() && !IsSessionAdmin(Data, sSession))
		Out.Replace(_T("[Message]"), _GetPlainResString(IDS_ACCESSDENIED));
	else
		Out.Replace(_T("[Message]"), _GetPlainResString(IDS_SW_REFETCHRES));

	CString sSort = _ParseURL(Data.sURL, _T("sort"));
	if (sSort.GetLength() > 0)
		pThis->m_iSearchSortby = _tstoi(sSort);
	sSort = _ParseURL(Data.sURL, _T("sortAsc"));
	if (sSort.GetLength() > 0)
		pThis->m_bSearchAsc = _tstoi(sSort) != 0;

	CString result = pThis->m_Templates.sSearchHeader;

	CQArray<SearchFileStruct, SearchFileStruct> SearchFileArray;
	theApp.searchlist->GetWebList(&SearchFileArray, pThis->m_iSearchSortby);

	SearchFileArray.QuickSort(pThis->m_bSearchAsc);

	for (uint16 i = 0; i < SearchFileArray.GetCount(); ++i) {
		uchar nRed = 255, nGreen = 255, nBlue = 255;
		uchar aFileHash[16];
		SearchFileStruct structFile = SearchFileArray[i];
		DecodeBase16(structFile.m_strFileHash, 32, aFileHash, ARRSIZE(aFileHash));

		LPCTSTR strOverlayImage = _T("none");
		if (theApp.downloadqueue->GetFileByID(aFileHash) != NULL) {
			nBlue = 128;
			nGreen = 128;
		} else {
			CKnownFile *sameFile = theApp.sharedfiles->GetFileByID(aFileHash);
			if (sameFile == NULL)
				sameFile = theApp.knownfiles->FindKnownFileByID(aFileHash);

			if (sameFile == NULL) {
				//strOverlayImage = _T("none");
			} else {
				//strOverlayImage = _T("release");
				nBlue = 128;
				nRed = 128;
			}
		}
		CString strColorPrefix;
		strColorPrefix.Format(_T("<font color=\"#%02x%02x%02x\">"), nRed, nGreen, nBlue);
		static LPCTSTR strColorSuffix = _T("</font>");

		LPCTSTR strSourcesImage;
		if (structFile.m_uSourceCount < 5)
			strSourcesImage = _T("0");
		else if (structFile.m_uSourceCount < 10)
			strSourcesImage = _T("5");
		else if (structFile.m_uSourceCount < 25)
			strSourcesImage = _T("10");
		else if (structFile.m_uSourceCount < 50)
			strSourcesImage = _T("25");
		else
			strSourcesImage = _T("50");

		CString strSources;
		strSources.Format(_T("%u(%u)"), structFile.m_uSourceCount, structFile.m_dwCompleteSourceCount);

		CString strFilename(structFile.m_strFileName);
		strFilename.Replace(_T("'"), _T("\\'"));

		CString strLink;
		strLink.Format(_T("ed2k://|file|%s|%I64u|%s|/"),
			(LPCTSTR)_SpecialChars(strFilename), structFile.m_uFileSize, (LPCTSTR)structFile.m_strFileHash);

		result.AppendFormat(pThis->m_Templates.sSearchResultLine
			, strSourcesImage
			, (LPCTSTR)strLink
			, strOverlayImage
			, (LPCTSTR)GetWebImageNameForFileType(structFile.m_strFileName)
			, WSsearchColumnHidden[0] ? _T("") : (LPCTSTR)(strColorPrefix + StringLimit(structFile.m_strFileName, 70) + strColorSuffix)
			, WSsearchColumnHidden[1] ? _T("") : (LPCTSTR)(strColorPrefix + CastItoXBytes(structFile.m_uFileSize) + strColorSuffix)
			, WSsearchColumnHidden[2] ? _T("") : (LPCTSTR)(strColorPrefix + structFile.m_strFileHash + strColorSuffix)
			, WSsearchColumnHidden[3] ? _T("") : (LPCTSTR)(strColorPrefix + strSources + strColorSuffix)
			, (LPCTSTR)structFile.m_strFileHash
		);
	}

	if (thePrefs.GetCatCount() > 1)
		InsertCatBox(Out, 0, pThis->m_Templates.sCatArrow, false, false, sSession, _T(""));
	else
		Out.Replace(_T("[CATBOX]"), _T(""));

	Out.Replace(_T("[SEARCHINFOMSG]"), _T(""));
	Out.Replace(_T("[RESULTLIST]"), result);
	Out.Replace(_T("[Result]"), GetResString(IDS_SW_RESULT));
	Out.Replace(_T("[Session]"), sSession);
	Out.Replace(_T("[Name]"), _GetPlainResString(IDS_SW_NAME));
	Out.Replace(_T("[Type]"), _GetPlainResString(IDS_TYPE));
	Out.Replace(_T("[Any]"), _GetPlainResString(IDS_SEARCH_ANY));
	Out.Replace(_T("[Audio]"), _GetPlainResString(IDS_SEARCH_AUDIO));
	Out.Replace(_T("[Image]"), _GetPlainResString(IDS_SEARCH_PICS));
	Out.Replace(_T("[Video]"), _GetPlainResString(IDS_SEARCH_VIDEO));
	Out.Replace(_T("[Document]"), _GetPlainResString(IDS_SEARCH_DOC));
	Out.Replace(_T("[CDImage]"), _GetPlainResString(IDS_SEARCH_CDIMG));
	Out.Replace(_T("[Program]"), _GetPlainResString(IDS_SEARCH_PRG));
	Out.Replace(_T("[Archive]"), _GetPlainResString(IDS_SEARCH_ARC));
	Out.Replace(_T("[eMuleCollection]"), _GetPlainResString(IDS_SEARCH_EMULECOLLECTION));
	Out.Replace(_T("[Search]"), _GetPlainResString(IDS_EM_SEARCH));
	Out.Replace(_T("[Unicode]"), _GetPlainResString(IDS_SEARCH_UNICODE));
	Out.Replace(_T("[Size]"), _GetPlainResString(IDS_DL_SIZE));
	Out.Replace(_T("[Start]"), _GetPlainResString(IDS_SW_START));

	Out.Replace(_T("[USESSERVER]"), _GetPlainResString(IDS_SERVER));
	Out.Replace(_T("[USEKADEMLIA]"), _GetPlainResString(IDS_KADEMLIA));
	Out.Replace(_T("[METHOD]"), _GetPlainResString(IDS_METHOD));

	Out.Replace(_T("[SizeMin]"), _GetPlainResString(IDS_SEARCHMINSIZE));
	Out.Replace(_T("[SizeMax]"), _GetPlainResString(IDS_SEARCHMAXSIZE));
	Out.Replace(_T("[Availabl]"), _GetPlainResString(IDS_SEARCHAVAIL));
	Out.Replace(_T("[Extention]"), _GetPlainResString(IDS_SEARCHEXTENTION));
	Out.Replace(_T("[Global]"), _GetPlainResString(IDS_SW_GLOBAL));
	Out.Replace(_T("[MB]"), _GetPlainResString(IDS_MBYTES));
	Out.Replace(_T("[Apply]"), _GetPlainResString(IDS_PW_APPLY));
	Out.Replace(_T("[CatSel]"), sCat);
	Out.Replace(_T("[Ed2klink]"), _GetPlainResString(IDS_SW_LINK));
/*
	CString checked;
	if(thePrefs.GetMethod()==1)
		checked = _T("checked");
	Out.Replace(_T("[checked]"), checked);
*/
	const TCHAR *pcSortIcon = (pThis->m_bSearchAsc) ? pThis->m_Templates.sUpArrow : pThis->m_Templates.sDownArrow;

	CString strTmp;
	_GetPlainResString(strTmp, IDS_DL_FILENAME);
	if (!WSsearchColumnHidden[0]) {
		Out.Replace(_T("[FilenameI]"), (pThis->m_iSearchSortby == 0) ? pcSortIcon : _T(""));
		Out.Replace(_T("[FilenameH]"), strTmp);
	} else {
		Out.Replace(_T("[FilenameI]"), _T(""));
		Out.Replace(_T("[FilenameH]"), _T(""));
	}
	Out.Replace(_T("[FilenameM]"), strTmp);

	_GetPlainResString(strTmp, IDS_DL_SIZE);
	if (!WSsearchColumnHidden[1]) {
		Out.Replace(_T("[FilesizeI]"), (pThis->m_iSearchSortby == 1) ? pcSortIcon : _T(""));
		Out.Replace(_T("[FilesizeH]"), strTmp);
	} else {
		Out.Replace(_T("[FilesizeI]"), _T(""));
		Out.Replace(_T("[FilesizeH]"), _T(""));
	}
	Out.Replace(_T("[FilesizeM]"), strTmp);

	_GetPlainResString(strTmp, IDS_FILEHASH);
	if (!WSsearchColumnHidden[2]) {
		Out.Replace(_T("[FilehashI]"), (pThis->m_iSearchSortby == 2) ? pcSortIcon : _T(""));
		Out.Replace(_T("[FilehashH]"), strTmp);
	} else {
		Out.Replace(_T("[FilehashI]"), _T(""));
		Out.Replace(_T("[FilehashH]"), _T(""));
	}
	Out.Replace(_T("[FilehashM]"), strTmp);

	_GetPlainResString(strTmp, IDS_DL_SOURCES);
	if (!WSsearchColumnHidden[3]) {
		Out.Replace(_T("[SourcesI]"), (pThis->m_iSearchSortby == 3) ? pcSortIcon : _T(""));
		Out.Replace(_T("[SourcesH]"), strTmp);
	} else {
		Out.Replace(_T("[SourcesI]"), _T(""));
		Out.Replace(_T("[SourcesH]"), _T(""));
	}
	Out.Replace(_T("[SourcesM]"), strTmp);

	Out.Replace(_T("[Download]"), _GetPlainResString(IDS_DOWNLOAD));

	Out.Replace(_T("[SORTASCVALUE0]"), (pThis->m_iSearchSortby != 0 || pThis->m_bSearchAsc == 0) ? _T("1") : _T("0"));
	Out.Replace(_T("[SORTASCVALUE1]"), (pThis->m_iSearchSortby != 1 || pThis->m_bSearchAsc == 0) ? _T("1") : _T("0"));
	Out.Replace(_T("[SORTASCVALUE2]"), (pThis->m_iSearchSortby != 2 || pThis->m_bSearchAsc == 0) ? _T("1") : _T("0"));
	Out.Replace(_T("[SORTASCVALUE3]"), (pThis->m_iSearchSortby != 3 || pThis->m_bSearchAsc == 0) ? _T("1") : _T("0"));
	Out.Replace(_T("[SORTASCVALUE4]"), (pThis->m_iSearchSortby != 4 || pThis->m_bSearchAsc == 0) ? _T("1") : _T("0"));
	Out.Replace(_T("[SORTASCVALUE5]"), (pThis->m_iSearchSortby != 5 || pThis->m_bSearchAsc == 0) ? _T("1") : _T("0"));

	return Out;
}

INT_PTR CWebServer::UpdateSessionCount()
{
	if (thePrefs.GetWebTimeoutMins() > 0) {
		INT_PTR oldvalue = m_Params.Sessions.GetSize();
		CTime curTime(CTime::GetCurrentTime());
		for (INT_PTR i = m_Params.Sessions.GetSize(); --i >= 0;) {
			CTimeSpan ts = curTime - m_Params.Sessions[i].startTime;
			if (ts.GetTotalSeconds() > thePrefs.GetWebTimeoutMins()*MIN2S(1))
				m_Params.Sessions.RemoveAt(i);
		}

		if (oldvalue != m_Params.Sessions.GetCount())
			SendMessage(theApp.emuledlg->m_hWnd, WEB_GUI_INTERACTION, WEBGUIIA_UPDATEMYINFO, 0);
	}
	return m_Params.Sessions.GetCount();
}

void CWebServer::InsertCatBox(CString& Out, int preselect, const CString& boxlabel, bool jump, bool extraCats, const CString& sSession, const CString& sFileHash, bool ed2kbox)
{
	CString tempBuf;
	tempBuf.Format(_T("<form action=\"\">%s<select name=\"cat\" size=\"1\"%s>")
		, (LPCTSTR)boxlabel
		, jump ? _T(" onchange=\"GotoCat(this.form.cat.options[this.form.cat.selectedIndex].value)\"") : _T(""));

	for (int i = 0; i < thePrefs.GetCatCount(); ++i) {
		CString strCategory = thePrefs.GetCategory(i)->strTitle;
		strCategory.Replace(_T("'"), _T("\\'"));
		tempBuf.AppendFormat(_T("<option%s value=\"%i\">%s</option>\n"), (i == preselect) ? _T(" selected") : _T(""), i, (LPCTSTR)strCategory);
	}
	if (extraCats) {
		if (thePrefs.GetCatCount() > 1)
			tempBuf += _T("<option>-------------------</option>\n");

		for (int i = 1; i < 16; ++i)
			tempBuf.AppendFormat(_T("<option%s value=\"%i\">%s</option>\n"), (0 - i == preselect) ? _T(" selected") : _T(""), 0 - i, (LPCTSTR)GetSubCatLabel(0 - i));
	}
	tempBuf += _T("</select></form>");
	Out.Replace(ed2kbox ? _T("[CATBOXED2K]") : _T("[CATBOX]"), tempBuf);

	CString tempBuff3, tempBuff4;
	CString tempBuff;

	for (int i = 0; i < thePrefs.GetCatCount(); i++) {
		if (i == preselect) {
			tempBuff3 = _T("checked.gif");
			tempBuff4 = (i == 0) ? GetResString(IDS_ALL) : thePrefs.GetCategory(i)->strTitle;
		} else
			tempBuff3 = _T("checked_no.gif");

		CString strCategory(i ? thePrefs.GetCategory(i)->strTitle : GetResString(IDS_ALL));
		strCategory.Replace(_T("'"), _T("\\'"));

		tempBuff.AppendFormat(_T("<a href=&quot;/?ses=%s&amp;w=transfer&amp;cat=%d&quot;><div class=menuitems><img class=menuchecked src=%s>%s&nbsp;</div></a>"),
			(LPCTSTR)sSession, i, (LPCTSTR)tempBuff3, (LPCTSTR)strCategory);
	}
	if (extraCats) {
		tempBuff.Append(_T("<div class=menuitems>&nbsp;------------------------------&nbsp;</div>"));
		for (int i = 1; i < 16; i++) {
			if ((0 - i) == preselect) {
				tempBuff3 = _T("checked.gif");
				tempBuff4 = GetSubCatLabel(0 - i);
			} else
				tempBuff3 = _T("checked_no.gif");

			tempBuff.AppendFormat(_T("<a href=&quot;/?ses=%s&amp;w=transfer&amp;cat=%d&quot;><div class=menuitems><img class=menuchecked src=%s>%s&nbsp;</div></a>"),
				(LPCTSTR)sSession, 0 - i, (LPCTSTR)tempBuff3, (LPCTSTR)GetSubCatLabel(0 - i));
		}
	}
	Out.Replace(_T("[CatBox]"), tempBuff);
	Out.Replace(_T("[Category]"), tempBuff4);

	tempBuff.Empty();

	//	For each user category index...
	for (int i = 0; i < thePrefs.GetCatCount(); ++i) {
		CPartFile *found_file = NULL;
		if (!sFileHash.IsEmpty()) {
			uchar FileHash[16];
			found_file = theApp.downloadqueue->GetFileByID(_GetFileHash(sFileHash, FileHash));
		}
		//	Get the user category index of 'found_file' in 'preselect'.
		if (found_file)
			preselect = found_file->GetCategory();

		if (i == preselect) {
			tempBuff3 = _T("checked.gif");
			tempBuff4 = (i == 0) ? GetResString(IDS_ALL) : thePrefs.GetCategory(i)->strTitle;
		} else
			tempBuff3 = _T("checked_no.gif");

		CString strCategory(i ? thePrefs.GetCategory(i)->strTitle : GetResString(IDS_CAT_UNASSIGN));
		strCategory.Replace(_T("'"), _T("\\'"));

		tempBuff.AppendFormat(_T("<a href=&quot;/?ses=%s&amp;w=transfer[CatSel]&amp;op=setcat&amp;file=%s&amp;filecat=%d&quot;><div class=menuitems><img class=menuchecked src=%s>%s&nbsp;</div></a>"),
			(LPCTSTR)sSession, (LPCTSTR)sFileHash, i, (LPCTSTR)tempBuff3, (LPCTSTR)strCategory);
	}


	Out.Replace(_T("[SetCatBox]"), tempBuff);
}

CString CWebServer::GetSubCatLabel(int cat)
{
	if (cat < 0 && cat >= -16) {
		static const UINT ids[16] = {
			  IDS_ALLOTHERS, IDS_STATUS_NOTCOMPLETED, IDS_DL_TRANSFCOMPL, IDS_WAITING, IDS_DOWNLOADING
			, IDS_ERRORLIKE, IDS_PAUSED, IDS_SEENCOMPL, IDS_VIDEO, IDS_AUDIO
			, IDS_SEARCH_ARC, IDS_SEARCH_CDIMG, IDS_SEARCH_DOC, IDS_SEARCH_PICS, IDS_SEARCH_PRG
			, IDS_SEARCH_EMULECOLLECTION };
		return _GetPlainResString(ids[-cat - 1]);
	}
	return CString(_T('?'));
}

CString CWebServer::_GetRemoteLinkAddedOk(const ThreadData& Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	int cat = _tstoi(_ParseURL(Data.sURL, _T("cat")));
	const CString HTTPTemp = _ParseURL(Data.sURL, _T("c"));

	theApp.emuledlg->SendMessage(WEB_ADDDOWNLOADS, (WPARAM)(LPCTSTR)HTTPTemp, cat);

	CString Out;
	Out.Format(_T("<status result=\"OK\"><description>%s</description>"), (LPCTSTR)GetResString(IDS_WEB_REMOTE_LINK_ADDED));
	Out.AppendFormat(_T("<filename>%s</filename></status>"), (LPCTSTR)HTTPTemp);

	return Out;
}

CString CWebServer::_GetRemoteLinkAddedFailed(const ThreadData& Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return CString();

	CString Out(_T("<status result=\"FAILED\" reason=\"WRONG_PASSWORD\">"));
	Out.AppendFormat(_T("<description>%s</description></status>"), (LPCTSTR)GetResString(IDS_WEB_REMOTE_LINK_NOT_ADDED));

	return Out;
}

void CWebServer::_SetLastUserCat(const ThreadData& Data, long lSession, int cat)
{
	CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return;

	_RemoveTimeOuts(Data);

	// find our session
	if (lSession != 0)
		for (INT_PTR i = pThis->m_Params.Sessions.GetSize(); --i >= 0;) {
			Session& ses = pThis->m_Params.Sessions[i];
			if (ses.lSession == lSession) {
				// if found, also reset expiration time
				ses.startTime = CTime::GetCurrentTime();
				ses.lastcat = cat;
				return;
			}
		}
}

int CWebServer::_GetLastUserCat(const ThreadData& Data, long lSession)
{
	CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return 0;

	_RemoveTimeOuts(Data);

	if (lSession != 0)
		// find our session
		for (INT_PTR i = pThis->m_Params.Sessions.GetSize(); --i >= 0;) {
			Session& ses = pThis->m_Params.Sessions[i];
			if (ses.lSession == lSession) {
				// if found, also reset expiration time
				ses.startTime = CTime::GetCurrentTime();
				return ses.lastcat;
			}
		}

	return 0;
}

uchar *CWebServer::_GetFileHash(const CString &sHash, uchar *FileHash)
{
	strmd4(sHash, FileHash);
	return FileHash;
}

void CWebServer::ProcessFileReq(const ThreadData &Data)
{
	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	if (pThis == NULL)
		return;
	CString filename(Data.sURL);
	CString contenttype;

	CString ext = filename.Right(5).MakeLower();
	int i = ext.ReverseFind(_T('.'));
	ext.Delete(0, i);
	if (i >= 0 && ext.GetLength() > 2) {
		ext.Delete(0, 1);
		if (ext == _T("bmp") || ext == _T("gif") || ext == _T("jpeg") || ext == _T("jpg") || ext == _T("png"))
			contenttype.Format(_T("Content-Type: image/%s\r\n"), (LPCTSTR)ext);
		//DonQ - additional filetypes
		else if (ext == _T("ico"))
			contenttype = _T("Content-Type: image/x-icon\r\n");
		else if (ext == _T("css"))
			contenttype = _T("Content-Type: text/css\r\n");
		else if (ext == _T("js"))
			contenttype = _T("Content-Type: text/javascript\r\n");
	}

	contenttype.AppendFormat(_T("Last-Modified: %s\r\nETag: %s\r\n"), (LPCTSTR)pThis->m_Params.sLastModified, (LPCTSTR)pThis->m_Params.sETag);

	filename.Replace(_T('/'), _T('\\'));
	if (filename[0] == _T('\\'))
		filename.Delete(0);
	filename = thePrefs.GetMuleDirectory(EMULE_WEBSERVERDIR) + filename;

	CFile file;
	if (file.Open(filename, CFile::modeRead | CFile::shareDenyWrite | CFile::typeBinary)) {
		if (thePrefs.GetMaxWebUploadFileSizeMB() == 0 || file.GetLength() <= thePrefs.GetMaxWebUploadFileSizeMB() * 1024ull * 1024ull) {
			UINT filesize = (UINT)file.GetLength();

			char* buffer = new char[filesize];
			UINT size = file.Read(buffer, filesize);
			file.Close();
			Data.pSocket->SendContent(CStringA(contenttype), buffer, size);
			delete[] buffer;
		} else
			Data.pSocket->SendReply("HTTP/1.1 403 Forbidden\r\n");
	} else
		Data.pSocket->SendReply("HTTP/1.1 404 File not found\r\n");
}

CString CWebServer::GetWebImageNameForFileType(const CString& filename)
{
	LPCTSTR p;
	switch (GetED2KFileTypeID(filename)) {
	case ED2KFT_AUDIO:
		p = _T("audio");
		break;
	case ED2KFT_VIDEO:
		p = _T("video");
		break;
	case ED2KFT_IMAGE:
		p = _T("picture");
		break;
	case ED2KFT_PROGRAM:
		p = _T("program");
		break;
	case ED2KFT_DOCUMENT:
		p = _T("document");
		break;
	case ED2KFT_ARCHIVE:
		p = _T("archive");
		break;
	case ED2KFT_CDIMAGE:
		p = _T("cdimage");
		break;
	case ED2KFT_EMULECOLLECTION:
		p = _T("emulecollection");
		break;
	default: /*ED2KFT_ANY:*/
		p = _T("other");
	}
	return CString(p);
}

CString CWebServer::GetClientSummary(const CUpDownClient* client)
{
	CString buffer;
	// name
	buffer.Format(_T("%s %s\n"), (LPCTSTR)GetResString(IDS_CD_UNAME), client->GetUserName());
	// client version
	buffer.AppendFormat(_T("%s: %s\n"), (LPCTSTR)GetResString(IDS_CD_CSOFT), (LPCTSTR)client->GetClientSoftVer());

	// uploading file
	buffer.AppendFormat(_T("%s "), (LPCTSTR)GetResString(IDS_CD_UPLOADREQ));
	const CKnownFile *file = theApp.sharedfiles->GetFileByID(client->GetUploadFileID());
	ASSERT(file);
	if (file)
		buffer += file->GetFileName();

	// transfering time
	buffer.AppendFormat(_T("\n\n%s: %s\n"), (LPCTSTR)GetResString(IDS_UPLOADTIME), (LPCTSTR)CastSecondsToHM(client->GetUpStartTimeDelay() / SEC2MS(1)));

	// transfered data (up,down,global,session)
	buffer.AppendFormat(_T("%s (%s):\n"), (LPCTSTR)GetResString(IDS_FD_TRANS), (LPCTSTR)GetResString(IDS_STATS_SESSION));
	buffer.AppendFormat(_T(".....%s: %s (%s )\n"), (LPCTSTR)GetResString(IDS_PW_CON_UPLBL), (LPCTSTR)CastItoXBytes(client->GetTransferredUp()), (LPCTSTR)CastItoXBytes(client->GetSessionUp()));
	buffer.AppendFormat(_T(".....%s: %s (%s )\n"), (LPCTSTR)GetResString(IDS_DOWNLOAD), (LPCTSTR)CastItoXBytes(client->GetTransferredDown()), (LPCTSTR)CastItoXBytes(client->GetSessionDown()));

	return buffer;
}

CString CWebServer::GetClientversionImage(const CUpDownClient* client)
{
	TCHAR c;
	switch (client->GetClientSoft()) {
	case SO_EMULE:
	case SO_OLDEMULE:
		c = _T('1');
		break;
	case SO_EDONKEYHYBRID:
		c = _T('h');
		break;
	case SO_AMULE:
		c = _T('a');
		break;
	case SO_SHAREAZA:
		c = _T('s');
		break;
	case SO_MLDONKEY:
		c = _T('m');
		break;
	case SO_LPHANT:
		c = _T('l');
		break;
	case SO_URL:
		c = _T('u');
		break;
	default: //SO_EDONKEY
		c = _T('0');
	}
	return CString(c);
}

CString CWebServer::_GetCommentlist(const ThreadData& Data)
{
	uchar FileHash[16];
	CPartFile* pPartFile = theApp.downloadqueue->GetFileByID(_GetFileHash(_ParseURL(Data.sURL, _T("filehash")), FileHash));
	if (!pPartFile)
		return CString();

	const CWebServer *pThis = reinterpret_cast<CWebServer *>(Data.pThis);
	CString Out = pThis->m_Templates.sCommentList;

	CString comments;
	comments.Format(_T("%s: %s"), (LPCTSTR)GetResString(IDS_COMMENT), (LPCTSTR)pPartFile->GetFileName());
	Out.Replace(_T("[COMMENTS]"), comments);

	CString commentlines;
	// prepare commentsinfo-string
	for (POSITION pos = pPartFile->srclist.GetHeadPosition(); pos != NULL;) {
		const CUpDownClient* cur_src = pPartFile->srclist.GetNext(pos);
		if (cur_src->HasFileRating() || !cur_src->GetFileComment().IsEmpty())
			commentlines.AppendFormat(pThis->m_Templates.sCommentListLine
				, (LPCTSTR)_SpecialChars(cur_src->GetUserName())
				, (LPCTSTR)_SpecialChars(cur_src->GetClientFilename())
				, (LPCTSTR)_SpecialChars(cur_src->GetFileComment())
				, (LPCTSTR)_SpecialChars(GetRateString(cur_src->GetFileRating())));
	}

	const CTypedPtrList<CPtrList, Kademlia::CEntry*>& list = pPartFile->getNotes();
	for (POSITION pos = list.GetHeadPosition(); pos != NULL;) {
		const Kademlia::CEntry* entry = list.GetNext(pos);
		commentlines.AppendFormat(pThis->m_Templates.sCommentListLine
			, _T("")
			, (LPCTSTR)_SpecialChars(entry->GetCommonFileName())
			, (LPCTSTR)_SpecialChars(entry->GetStrTagValue(Kademlia::CKadTagNameString(TAG_DESCRIPTION)))
			, (LPCTSTR)_SpecialChars(GetRateString((UINT)entry->GetIntTagValue(Kademlia::CKadTagNameString(TAG_FILERATING)))));
	}

	Out.Replace(_T("[COMMENTLINES]"), commentlines);

	Out.Replace(_T("[COMMENTS]"), _T(""));
	Out.Replace(_T("[USERNAME]"), GetResString(IDS_QL_USERNAME));
	Out.Replace(_T("[FILENAME]"), GetResString(IDS_DL_FILENAME));
	Out.Replace(_T("[COMMENT]"), GetResString(IDS_COMMENT));
	Out.Replace(_T("[RATING]"), GetResString(IDS_QL_RATING));
	Out.Replace(_T("[CLOSE]"), GetResString(IDS_CW_CLOSE));
	Out.Replace(_T("[CharSet]"), HTTPENCODING);

	return Out;
}