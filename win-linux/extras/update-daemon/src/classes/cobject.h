#ifndef COBJECT_H
#define COBJECT_H

#include <Windows.h>
#include <functional>

typedef std::function<void(void)> FnVoidVoid;

class CObject
{
public:
    CObject(CObject *parent = nullptr);
    ~CObject();
    virtual int processEvents(HWND, UINT, WPARAM, LPARAM);

    /* callback */
    void onDestroy(FnVoidVoid callback);

protected:
    HWND       m_hWnd = nullptr;
    FnVoidVoid m_destroy_callback = nullptr;
};

#endif // COBJECT_H
