#include "capplication.h"
//#include "cobject.h"


CApplication::CApplication()
{

}

CApplication::~CApplication()
{

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

LRESULT CALLBACK CApplication::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
//    CObject *object = reinterpret_cast<CObject*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
//    if (object) {
//        int result = object->processEvents(hWnd, msg, wParam, lParam);
//        if (result != -1)
//            return result;
//    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}
