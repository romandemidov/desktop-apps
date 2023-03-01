#ifndef CDOWNLOADER_H
#define CDOWNLOADER_H

#include "cobject.h"
#include <string>
#include <functional>
#include <future>

typedef std::function<void(int)> FnVoidInt;

using std::wstring;

class CDownloader : CObject
{
public:
    CDownloader(CObject *parent = nullptr);
    ~CDownloader();

    void downloadFile(const wstring &url, const wstring &filePath);
    void start();
    void stop();
    wstring GetFilePath();

    /* callback */
    void onComplete(FnVoidInt callback);
    void onProgress(FnVoidInt callback);

private:
    static void handle_signal(int signal);
    FnVoidInt m_complete_callback = nullptr,
              m_progress_callback = nullptr;
    wstring   m_url,
              m_filePath;
    std::future<void> m_future;
    static bool m_run,
                m_lock;
    class DownloadProgress;
    friend DownloadProgress;
};

#endif // CDOWNLOADER_H
