#ifndef CSOCKET_H
#define CSOCKET_H

#include "cobject.h"

typedef std::function<void(COPYDATASTRUCT*)> FnVoidData;

class CSocket : public CObject
{
public:
    CSocket(CObject *parent = nullptr, LPCWSTR client = NULL, LPCWSTR server = NULL);
    virtual ~CSocket();

    /* callback */
    bool sendMessage(COPYDATASTRUCT *user_data);
    void onMessageReceived(FnVoidData callback);

protected:
    virtual int processEvents(HWND, UINT, WPARAM, LPARAM) override;
    FnVoidData m_received_callback;

private:
    LPCWSTR m_server;
};

#endif // CSOCKET_H
