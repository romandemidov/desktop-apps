#ifndef CSOCKET_H
#define CSOCKET_H

#include <future>

typedef std::function<void(void*, size_t)> FnVoidData;
typedef std::function<void(const char*)> FnVoidCharPtr;


enum MsgCommands {
    MSG_CHECK_UPDATES = 0
};

class CSocket
{
public:
    CSocket(int sender_port, int receiver_port);
    ~CSocket();

    /* callback */
    bool sendMessage(void *data, size_t size);
    void onMessageReceived(FnVoidData callback);
    void onError(FnVoidCharPtr callback);

private:
    void postError(const char*);
    FnVoidData m_received_callback;
    FnVoidCharPtr m_error_callback;
    int m_sender_port;
    std::future<void> m_future;
    class CSocketPrv;
    CSocketPrv *pimpl = nullptr;
};

#endif // CSOCKET_H
