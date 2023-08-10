#include <Windows.h>
#include <CommCtrl.h>
#include "resource.h"
#include "utils.h"
#include "cdownloader.h"

#ifndef URL_INSTALL_X64
# define URL_INSTALL_X64 ""
#endif
#ifndef URL_INSTALL_X86
# define URL_INSTALL_X86 ""
#endif
#ifndef URL_INSTALL_X64_XP
# define URL_INSTALL_X64_XP ""
#endif
#ifndef URL_INSTALL_X86_XP
# define URL_INSTALL_X86_XP ""
#endif


HANDLE hIcon = NULL;

struct UserData
{
    CDownloader *dnl = nullptr;
    wstring *url = nullptr;
};

void startDownloadAndInstall(HWND hDlg, UserData *data);

INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

int WINAPI _tWinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE hPrevInstance, _In_ LPTSTR lpCmdLine, _In_ int nCmdShow)
{
    int num_args = 0;
    if (LPTSTR *args = CommandLineToArgvW(lpCmdLine, &num_args)) {
        for (int i = 0; i < num_args; i++) {
            if (lstrcmpi(args[i], _T("--log")) == 0) {
                NS_Logger::AllowWriteLog();
                break;
            }
        }
        LocalFree(args);
    }

    SYSTEM_INFO info;
    GetSystemInfo(&info);

    wstring url;
    WinVer ver = NS_File::getWinVersion();
    if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
        url = (ver == WinVer::WinXP) ? _T(URL_INSTALL_X64_XP) : _T(URL_INSTALL_X64);
    } else
    if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL) {
        url = (ver == WinVer::WinXP) ? _T(URL_INSTALL_X86_XP) : _T(URL_INSTALL_X86);
    } else {
        NS_Utils::ShowMessage(_T("The application cannot continue because this architecture is not supported."));
        return 0;
    }

    CDownloader dnl;
    UserData data;
    data.dnl = &dnl;
    data.url = &url;

    InitCommonControls();
    HWND hDlg = CreateDialogParam(hInst, MAKEINTRESOURCE(IDD_DIALOG), NULL, DialogProc, (LPARAM)&data);
    ShowWindow(hDlg, nCmdShow);

    BOOL ret;
    MSG msg = {};
    while ((ret = GetMessage(&msg, hDlg, 0, 0)) != 0) {
        if (ret == -1)
            return 0;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

void startDownloadAndInstall(HWND hDlg, UserData *data)
{
    HWND hLabelMsg = GetDlgItem(hDlg, IDC_LABEL_MESSAGE);
    if (!data) {
        if (hLabelMsg)
            SetWindowText(hLabelMsg, _T(LABEL_MESSAGE_TEXT_ERR1));
        return;
    }
    if (data->url->empty()) {
        if (hLabelMsg)
            SetWindowText(hLabelMsg, _T(LABEL_MESSAGE_TEXT_ERR6));
        return;
    }
    size_t pos = data->url->find_last_of(_T("/"));
    if (pos == wstring::npos) {
        if (hLabelMsg)
            SetWindowText(hLabelMsg, _T(LABEL_MESSAGE_TEXT_ERR7));
        return;
    }
    wstring fileName = data->url->substr(pos + 1);
    if (fileName.empty()) {
        if (hLabelMsg)
            SetWindowText(hLabelMsg, _T(LABEL_MESSAGE_TEXT_ERR7));
        return;
    }

    wstring msgText;
    if (hLabelMsg) {
        int len = GetWindowTextLength(hLabelMsg);
        if (len > 0) {
            LPTSTR buff = (LPTSTR)malloc((len + 1) * sizeof(TCHAR));
            if (buff) {
                GetWindowText(hLabelMsg, buff, len + 1);
                msgText = buff;
                free(buff);
            }
        }
        msgText += _T(" ") + fileName;
        SetWindowText(hLabelMsg, msgText.c_str());
    }

    wstring path = NS_File::appPath() + _T("/") + fileName;
    HWND hProgress = GetDlgItem(hDlg, IDC_PROGRESS);
    data->dnl->onProgress([=](int percent) {
        if (hProgress && IsWindow(hProgress))
            PostMessage(hProgress, PBM_SETPOS, percent, 0);
    });
    data->dnl->onComplete([=](int error) {
        if (error == 0) {
            if (hDlg && IsWindow(hDlg))
                ShowWindow(hDlg, SW_HIDE);
            if (!NS_File::runProcess(path, _T(""))) {
                if (hDlg && IsWindow(hDlg))
                    ShowWindow(hDlg, SW_SHOW);
                if (hLabelMsg && IsWindow(hLabelMsg))
                    SetWindowText(hLabelMsg, _T(LABEL_MESSAGE_TEXT_ERR5));
            } else {
                if (hDlg && IsWindow(hDlg))
                    PostMessage(hDlg, WM_CLOSE, 0, 0);
            }
            if (NS_File::fileExists(path))
                NS_File::removeFile(path);
        } else
        if (error == -1) {
            if (hLabelMsg && IsWindow(hLabelMsg))
                SetWindowText(hLabelMsg, _T(LABEL_MESSAGE_TEXT_ERR2));
        } else
        if (error == -2) {
            if (hLabelMsg && IsWindow(hLabelMsg))
                SetWindowText(hLabelMsg, _T(LABEL_MESSAGE_TEXT_ERR3));
        } else {
            if (hLabelMsg && IsWindow(hLabelMsg))
                SetWindowText(hLabelMsg, _T(LABEL_MESSAGE_TEXT_ERR4));
        }
    });
    data->dnl->downloadFile(*data->url, path);
}

INT_PTR CALLBACK DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_INITDIALOG: {
        HWND hwndIcon = GetDlgItem(hDlg, IDC_ICON);
        hIcon = LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_MAINICON), IMAGE_ICON, 64, 64, LR_DEFAULTCOLOR | LR_LOADTRANSPARENT);
        if (hIcon && hwndIcon)
            PostMessage(hwndIcon, STM_SETIMAGE, IMAGE_ICON, (LPARAM)hIcon);

        startDownloadAndInstall(hDlg, (UserData*)lParam);
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BUTTON_CANCEL:
            PostMessage(hDlg, WM_CLOSE, 0, 0);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        if (hIcon)
            DestroyIcon((HICON)hIcon);
        DestroyWindow(hDlg);
        return TRUE;

    case WM_ACTIVATE:
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return TRUE;

    default:
        break;
    }
    return FALSE;
}
