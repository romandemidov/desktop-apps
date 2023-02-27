#include "capplication.h"
#include "cobject.h"

TimersMap CApplication::m_timers = {};

CApplication::CApplication()
{

}

CApplication::~CApplication()
{
    TimersMap::iterator it;
    for (it = m_timers.begin(); it != m_timers.end(); it++)
        KillTimer(NULL, it->first);
}

int CApplication::exec()
{
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

void CApplication::exit(int code)
{
    PostQuitMessage(code);
}

bool CApplication::closeTimer(UINT_PTR timer)
{
    TimersMap::iterator it = m_timers.find(timer);
    if (it != m_timers.end()) {
        m_timers.erase(it);
        return KillTimer(NULL, timer);
    }
    return false;
}

UINT_PTR CApplication::setTimer(UINT timeout, FnVoidVoid callback)
{
    UINT_PTR idTimer = SetTimer(NULL, 0, timeout, (TIMERPROC)timerProc);
    if (idTimer)
        m_timers[idTimer] = callback;
    return idTimer;
}

LRESULT CApplication::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CObject *object = reinterpret_cast<CObject*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    if (object) {
        int result = object->processEvents(hWnd, msg, wParam, lParam);
        if (result != -1)
            return result;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void CApplication::timerProc(HWND, UINT, UINT_PTR idTimer, DWORD)
{
    FnVoidVoid callback = m_timers[idTimer];
    if (callback)
        callback();
}
