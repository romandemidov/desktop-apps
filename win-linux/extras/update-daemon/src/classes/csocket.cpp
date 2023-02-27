#include "csocket.h"
#include "capplication.h"
//#include <windowsx.h>

#define RETRIES_COUNT 3
#define RETRIES_DELAY_MS 100


CSocket::CSocket(CObject *parent, LPCWSTR client, LPCWSTR server) :
    CObject(parent),
    m_server(server)
{   
    HINSTANCE hInstance = GetModuleHandle(NULL);
    WNDCLASSEX wcx;
    memset(&wcx, 0, sizeof(wcx));
    wcx.cbSize = sizeof(WNDCLASSEX);
    wcx.style = CS_HREDRAW | CS_VREDRAW;
    wcx.hInstance = hInstance;
    wcx.lpfnWndProc = &CApplication::WndProc;
    wcx.cbClsExtra	= 0;
    wcx.cbWndExtra	= 0;
    wcx.lpszClassName = client;
    wcx.hbrBackground = CreateSolidBrush(0x00ffffff);
    wcx.hCursor = LoadCursor(hInstance, IDC_ARROW);
    RegisterClassEx(&wcx);

    m_hWnd = CreateWindowEx(
                WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                client,
                L"",
                WS_MAXIMIZEBOX | WS_THICKFRAME,
                0, 0, 0, 0,
                NULL,
                NULL,
                hInstance,
                NULL);
    SetWindowLongPtr(m_hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    ShowWindow(m_hWnd, SW_HIDE);


//    parent->onDestroy([=]() {
//        delete this;
//    });
}

CSocket::~CSocket()
{
    if (m_hWnd)
        DestroyWindow(m_hWnd);
}

void CSocket::onMessageReceived(FnVoidData callback)
{
    m_received_callback = callback;
}

bool CSocket::sendMessage(COPYDATASTRUCT *user_data)
{
    int retries = RETRIES_COUNT;
    HWND hwnd = NULL;
    while (retries-- > 0 && (hwnd = FindWindow(m_server, NULL)) == NULL) {
        Sleep(RETRIES_DELAY_MS);
    }

    if (hwnd) {
        SendMessage(hwnd, WM_COPYDATA, WPARAM(0), LPARAM((LPVOID)user_data));
        return true;
    }
    return false;
}

int CSocket::processEvents(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_ACTIVATE:
        break;

    case WM_COPYDATA: {
        COPYDATASTRUCT* pcds = (COPYDATASTRUCT*)lParam;
        if (m_received_callback && pcds) {
            m_received_callback(pcds);
        }
        break;
    }

    case WM_NCCALCSIZE:
        break;

    case WM_NCACTIVATE:
        break;

    default:
        break;
    }
    return CObject::processEvents(hWnd, msg, wParam, lParam);
}
