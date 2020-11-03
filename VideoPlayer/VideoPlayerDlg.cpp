
// VideoPlayerDlg.cpp: 实现文件
//

#include "pch.h"
#include "framework.h"
#include "VideoPlayer.h"
#include "VideoPlayerDlg.h"
#include "afxdialogex.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// 用于应用程序“关于”菜单项的 CAboutDlg 对话框

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg();

// 对话框数据
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

// 实现
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CVideoPlayerDlg 对话框



CVideoPlayerDlg::CVideoPlayerDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_VIDEOPLAYER_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	m_logsfile = nullptr;

	m_hThread = nullptr;
	m_dwThreaID = 0L;
	
}

CVideoPlayerDlg::~CVideoPlayerDlg()
{
	if (m_logsfile != nullptr)
	{
		fclose(m_logsfile);
		m_logsfile = nullptr;
	}

	ReleaseFfplaycore();
}

void CVideoPlayerDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CVideoPlayerDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_BUTTON_TEST, &CVideoPlayerDlg::OnBnClickedButtonTest)
	ON_BN_CLICKED(IDC_BUTTON_PLAY, &CVideoPlayerDlg::OnBnClickedButtonPlay)
END_MESSAGE_MAP()


// CVideoPlayerDlg 消息处理程序

BOOL CVideoPlayerDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// 将“关于...”菜单项添加到系统菜单中。

	// IDM_ABOUTBOX 必须在系统命令范围内。
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != nullptr)
	{
		BOOL bNameValid;
		CString strAboutMenu;
		bNameValid = strAboutMenu.LoadString(IDS_ABOUTBOX);
		ASSERT(bNameValid);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// 设置此对话框的图标。  当应用程序主窗口不是对话框时，框架将自动
	//  执行此操作
	SetIcon(m_hIcon, TRUE);			// 设置大图标
	SetIcon(m_hIcon, FALSE);		// 设置小图标

	// TODO: 在此添加额外的初始化代码
	InitLogs();

	return TRUE;  // 除非将焦点设置到控件，否则返回 TRUE
}

void CVideoPlayerDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialogEx::OnSysCommand(nID, lParam);
	}
}

// 如果向对话框添加最小化按钮，则需要下面的代码
//  来绘制该图标。  对于使用文档/视图模型的 MFC 应用程序，
//  这将由框架自动完成。

void CVideoPlayerDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 用于绘制的设备上下文

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// 使图标在工作区矩形中居中
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// 绘制图标
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

//当用户拖动最小化窗口时系统调用此函数取得光标
//显示。
HCURSOR CVideoPlayerDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}



void CVideoPlayerDlg::OnBnClickedButtonTest()
{
	// TODO: 在此添加控件通知处理程序代码
	AllocConsole();
	freopen("CONOUT$", "w+t", stdout);
	freopen("CONIN$", "r+t", stdin);

	printf("%s", avcodec_configuration());

	// 初始化SDL
	if ((SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) == -1))
	{
		// 初始化失败，打出错误
		printf("Failed to SDL_INIT_VIDEO|SDL_INIT_AUDIO:%s ", SDL_GetError());
	}
	else
	{
		printf("succeed to SDL_INIT_VIDEO|SDL_INIT_AUDIO\n");
	}
	SDL_Quit();
}

void CVideoPlayerDlg::InitLogs()
{
	char  szLogFile[256] = { 0 };

	TCHAR tszDir[MAX_PATH] = { 0 };
	GetModuleFileName(NULL, tszDir, MAX_PATH);
	char szDir[MAX_PATH] = { 0 };
	W2M(tszDir, szDir);
	char *pdest;
	int  ch = '\\';
	//pdest = my_strrchr(szDir, ch);
	pdest = strrchr(szDir, ch);
	int result = pdest - szDir + 1;
	char szTemp[MAX_PATH] = { 0 };
	memcpy(szTemp, szDir, result);

	long nDate = GetCurrentYMD();
	sprintf(szLogFile, "%s\\%ld.log", szTemp, nDate / 100);

	m_logsfile = fopen(szLogFile, "a");

//	g_logf(m_logsfile, "Start Logs!");
}


void CVideoPlayerDlg::OnBnClickedButtonPlay()
{
	// TODO: 在此添加控件通知处理程序代码
	g_logf(m_logsfile, "\n");
	g_logf(m_logsfile, "------------Start Play!------------");
	m_hThread = CreateThread(nullptr, 0, thread_start_play, this, 0, &m_dwThreaID);
}
