#ifndef COBJECT_H
#define COBJECT_H

#include <Windows.h>
#include <functional>
#include <map>

typedef std::function<void(void)> FnVoidVoid;
typedef std::map<UINT_PTR, FnVoidVoid> TimersMap;


class CObject
{
public:
    CObject(CObject *parent = nullptr);
    ~CObject();

    bool closeTimer(UINT_PTR timer);
//    virtual int processEvents(HWND, UINT, WPARAM, LPARAM);

    /* callback */
    UINT_PTR setTimer(UINT timeout, FnVoidVoid callback);
//    void onDestroy(FnVoidVoid callback);

private:
//    FnVoidVoid m_destroy_callback = nullptr;
    static void timerProc(HWND, UINT, UINT_PTR idTimer, DWORD);
    static TimersMap m_timers;
};

#endif // COBJECT_H
