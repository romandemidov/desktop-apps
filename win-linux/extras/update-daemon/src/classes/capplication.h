#ifndef CAPPLICATION_H
#define CAPPLICATION_H

#include <Windows.h>


class CApplication
{
public:
    CApplication();
    ~CApplication();

    int exec();
    void exit(int);

    /* callback */
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
};

#endif // CAPPLICATION_H
