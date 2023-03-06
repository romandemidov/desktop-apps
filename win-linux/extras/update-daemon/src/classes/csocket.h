#ifndef CSOCKET_H
#define CSOCKET_H

#include <future>

typedef std::function<void(void*, size_t)> FnVoidData;
typedef std::function<void(const char*)> FnVoidCharPtr;


enum MsgCommands {
    MSG_CheckUpdates = 0,
    MSG_LoadUpdates,
    MSG_LoadCheckFinished,
    MSG_LoadUpdateFinished,
    MSG_UnzipIfNeeded,
    MSG_ShowStartInstallMessage,
    MSG_StartReplacingFiles,
    MSG_ClearTempFiles,
    MSG_Progress,
    MSG_StopDownload,
    MSG_DownloadingError,
    MSG_OtherError
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
    class CSocketPrv;
    CSocketPrv *pimpl = nullptr;
};

#endif // CSOCKET_H
