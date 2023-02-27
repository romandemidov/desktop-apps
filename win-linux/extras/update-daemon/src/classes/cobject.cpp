#include "cobject.h"


CObject::CObject(CObject *parent)
{

}

CObject::~CObject()
{

}

void CObject::onDestroy(FnVoidVoid callback)
{
    m_destroy_callback = callback;
}

int CObject::processEvents(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int res = -1;

    switch (msg) {
    case WM_DESTROY:
        if (m_destroy_callback)
            m_destroy_callback();
        break;
    default:
        break;
    }
    return res;
}
