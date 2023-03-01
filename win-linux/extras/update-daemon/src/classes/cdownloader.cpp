#include "cdownloader.h"
#include <Windows.h>
#include <wininet.h>
#include <signal.h>


class CDownloader::DownloadProgress : public IBindStatusCallback
{
public:
    DownloadProgress(CDownloader *owner) :
        m_owner(owner)
    {

    }
    HRESULT __stdcall QueryInterface(const IID &, void **) {
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef(void) {
        return 1;
    }
    ULONG STDMETHODCALLTYPE Release(void) {
        return 1;
    }
    HRESULT STDMETHODCALLTYPE OnStartBinding(DWORD dwReserved, IBinding *pib) {
        return E_NOTIMPL;
    }
    virtual HRESULT STDMETHODCALLTYPE GetPriority(LONG *pnPriority) {
        return E_NOTIMPL;
    }
    virtual HRESULT STDMETHODCALLTYPE OnLowResource(DWORD reserved) {
        return S_OK;
    }
    virtual HRESULT STDMETHODCALLTYPE OnStopBinding(HRESULT hresult, LPCWSTR szError) {
        return E_NOTIMPL;
    }
    virtual HRESULT STDMETHODCALLTYPE GetBindInfo(DWORD *grfBINDF, BINDINFO *pbindinfo) {
        return E_NOTIMPL;
    }
    virtual HRESULT STDMETHODCALLTYPE OnDataAvailable(DWORD grfBSCF, DWORD dwSize, FORMATETC *pformatetc, STGMEDIUM *pstgmed) {
        return E_NOTIMPL;
    }
    virtual HRESULT STDMETHODCALLTYPE OnObjectAvailable(REFIID riid, IUnknown *punk) {
        return E_NOTIMPL;
    }

    virtual HRESULT __stdcall OnProgress(ULONG ulProgress, ULONG ulProgressMax, ULONG ulStatusCode, LPCWSTR szStatusText)
    {
        if (ulProgressMax != 0 && m_owner->m_progress_callback) {
            int percent = static_cast<int>((100.0 * ulProgress) / ulProgressMax);
            m_owner->m_progress_callback(percent);
        }

        if (!m_owner->m_run)
            return E_ABORT;
        return S_OK;
    }

private:
    CDownloader *m_owner;
};

bool CDownloader::m_run = true;
bool CDownloader::m_lock = false;

CDownloader::CDownloader(CObject *parent) :
    CObject(parent)
{
    signal(SIGTERM, &CDownloader::handle_signal);
    signal(SIGABRT, &CDownloader::handle_signal);
    signal(SIGBREAK, &CDownloader::handle_signal);
    signal(SIGINT, &CDownloader::handle_signal);
}

CDownloader::~CDownloader()
{
    m_run = false;
    if (m_future.valid())
        m_future.wait();
}

void CDownloader::handle_signal(int signal)
{
    switch (signal) {
    case SIGTERM:
    case SIGABRT:
    case SIGBREAK:
    case SIGINT:
        m_run = false;
        break;
    }
}

void CDownloader::downloadFile(const std::wstring &url, const std::wstring &filePath)
{
    m_url.clear();
    m_filePath.clear();
    if (url.empty() || filePath.empty() || m_lock)
        return;

    m_url = url;
    m_filePath = filePath;
    start();
}

void CDownloader::start()
{
    if (m_url.empty() || m_filePath.empty() || m_lock)
        return;

    m_run = true;
    m_lock = true;
    m_future = std::async(std::launch::async, [=]() {
        DeleteUrlCacheEntry(m_url.c_str());
        DownloadProgress progress(this);
        HRESULT hr = URLDownloadToFile(NULL, m_url.c_str(), m_filePath.c_str(), 0,
                                       static_cast<IBindStatusCallback*>(&progress));
        if (m_complete_callback)
            m_complete_callback((int)hr);
        m_lock = false;
    });
}

void CDownloader::stop()
{
    m_run = false;
    /*if (!m_url.empty())
        DeleteUrlCacheEntry(m_url.c_str());*/
}

wstring CDownloader::GetFilePath()
{
    return m_filePath;
}

void CDownloader::onComplete(FnVoidInt callback)
{
    m_complete_callback = callback;
}

void CDownloader::onProgress(FnVoidInt callback)
{
    m_progress_callback = callback;
}
