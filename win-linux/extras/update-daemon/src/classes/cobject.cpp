#include "cobject.h"

TimersMap CObject::m_timers = {};

CObject::CObject(CObject *parent)
{

}

CObject::~CObject()
{
    TimersMap::iterator it;
    for (it = m_timers.begin(); it != m_timers.end(); it++)
        KillTimer(NULL, it->first);
}

//void CObject::onDestroy(FnVoidVoid callback)
//{
//    m_destroy_callback = callback;
//}

bool CObject::closeTimer(UINT_PTR timer)
{
    TimersMap::iterator it = m_timers.find(timer);
    if (it != m_timers.end()) {
        m_timers.erase(it);
        return KillTimer(NULL, timer);
    }
    return false;
}

UINT_PTR CObject::setTimer(UINT timeout, FnVoidVoid callback)
{
    UINT_PTR idTimer = SetTimer(NULL, 0, timeout, (TIMERPROC)timerProc);
    if (idTimer)
        m_timers[idTimer] = callback;
    return idTimer;
}

//int CObject::processEvents(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
//{
//    int res = -1;

//    switch (msg) {
//    case WM_DESTROY:
//        if (m_destroy_callback)
//            m_destroy_callback();
//        break;
//    default:
//        break;
//    }
//    return res;
//}

void CObject::timerProc(HWND, UINT, UINT_PTR idTimer, DWORD)
{
    FnVoidVoid callback = m_timers[idTimer];
    if (callback)
        callback();
}
