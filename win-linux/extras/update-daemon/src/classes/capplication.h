#ifndef CAPPLICATION_H
#define CAPPLICATION_H

#include <Windows.h>
#include <functional>
#include <map>

typedef std::function<void(void)> FnVoidVoid;
typedef std::map<UINT_PTR, FnVoidVoid> TimersMap;

class CApplication
{
public:
    CApplication();
    ~CApplication();

    int exec();
    void exit(int);
    bool closeTimer(UINT_PTR timer);

    /* callback */
    UINT_PTR setTimer(UINT timeout, FnVoidVoid callback);
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

private:
    static void timerProc(HWND, UINT, UINT_PTR idTimer, DWORD);
    static TimersMap m_timers;
};

#endif // CAPPLICATION_H
