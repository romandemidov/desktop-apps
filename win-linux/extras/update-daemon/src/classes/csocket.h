#ifndef CSOCKET_H
#define CSOCKET_H

#include <future>

typedef std::function<void(void*, int)> FnVoidData;

class CSocket
{
public:
    CSocket(int sender_port, int receiver_port);
    virtual ~CSocket();

    /* callback */
    bool sendMessage(void *data, size_t size);
    void onMessageReceived(FnVoidData callback);

protected:
    FnVoidData m_received_callback;

private:
    int m_sender_port;
    std::future<void> m_future;
    class CSocketPrv;
    CSocketPrv *pimpl;
};

#endif // CSOCKET_H
