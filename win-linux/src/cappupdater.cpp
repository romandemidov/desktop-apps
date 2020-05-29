/*
 * (c) Copyright Ascensio System SIA 2010-2019
 *
 * This program is a free software product. You can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License (AGPL)
 * version 3 as published by the Free Software Foundation. In accordance with
 * Section 7(a) of the GNU AGPL its Section 15 shall be amended to the effect
 * that Ascensio System SIA expressly excludes the warranty of non-infringement
 * of any third-party rights.
 *
 * This program is distributed WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR  PURPOSE. For
 * details, see the GNU AGPL at: http://www.gnu.org/licenses/agpl-3.0.html
 *
 * You can contact Ascensio System SIA at 20A-12 Ernesta Birznieka-Upisha
 * street, Riga, Latvia, EU, LV-1050.
 *
 * The  interactive user interfaces in modified source and object code versions
 * of the Program must display Appropriate Legal Notices, as required under
 * Section 5 of the GNU AGPL version 3.
 *
 * Pursuant to Section 7(b) of the License you must retain the original Product
 * logo when distributing the program. Pursuant to Section 7(e) we decline to
 * grant you any rights under trademark law for use of our trademarks.
 *
 * All the Product's GUI elements, including illustrations and icon sets, as
 * well as technical writing content are licensed under the terms of the
 * Creative Commons Attribution-ShareAlike 4.0 International. See the License
 * terms at http://creativecommons.org/licenses/by-sa/4.0/legalcode
 *
 */

#include "cappupdater.h"
#include <thread>
#include <functional>
#include <fstream>
#include <streambuf>
#include <regex>

#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QFileInfo>
#include <QDebug>

#include "defines.h"
#include "version.h"

#include "cascapplicationmanagerwrapper.h"

#ifdef Q_OS_WIN
# include <wininet.h>
# pragma comment(lib, "Wininet")
# pragma comment(lib, "Ole32.lib")
//# undef GetTempPath
#endif

#include "FileDownloader/FileDownloader.h"
#include "../DesktopEditor/common/File.h"

#define	MAX_SIZE    256
#define UPDATES_DOWNLOAD_APPCAST    1
#define UPDATES_DOWNLOAD_PACKAGE    2
#define UPDATES_PACKAGE_TEMP_NAME   "asc_update_package.exe"

#include <fstream>

#ifndef URL_APPCAST_UPDATES
# define URL_APPCAST_UPDATES ""
#endif

using namespace std::placeholders;

namespace {
    class CThreadProc: public QThread
    {
        Q_OBJECT

        struct sTick
        {
            bool started = false,
                complete = false;
            int error = 0;

            void start()
            {
                started = !(complete = false);
                error = 0;
            }

            void stop(int e = 0)
            {
                started = !(complete = true);
                error = e;
            }
        };

    private:
        sTick m_ct;
        wstring m_url,
                m_temp_path;

        void run() override
        {
            if ( m_url.empty() ) {
                emit complete(-1, L"");
                return;
            }

            std::shared_ptr<CFileDownloader> _downloader = std::make_shared<CFileDownloader>(m_url, false);

            wstring _file_path = NSFile::CFileBinary::CreateTempFileWithUniqueName(m_temp_path, L"ascu_");
            _downloader->SetFilePath(_file_path);

//            long _file_size = get_file_size(m_url);

            _downloader->SetEvent_OnComplete(std::bind(&CThreadProc::callback_download_complete, this, _1, _downloader->GetFilePath()));
//            _downloader->SetEvent_OnProgress(std::bind(&CThreadProc::callback_download_progress, this, _1));
            _downloader->Start(0);

            m_ct.start();
            while (!m_ct.complete) {
                msleep(10);
            }
        }

        void callback_download_complete(int e, wstring path)
        {
            m_ct.stop(e);
            emit complete(0, path);
        }

        auto callback_download_progress(int percent)
        {

        }

        auto get_file_size(const std::wstring& url) -> long
        {
            std::wstring strResult{L"-1"};

            HINTERNET hInternetSession = InternetOpen(L"Mozilla/5.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
            if ( hInternetSession ) {
                HINTERNET hInternetOpenURL = InternetOpenUrl(hInternetSession, url.c_str(), L"", -1, INTERNET_FLAG_RESYNCHRONIZE, 0);
                if ( hInternetOpenURL ) {
    //                if ( TRUE == QueryStatusCode(hInternetOpenURL, TRUE) )
                    {
                        wchar_t arrResult[MAX_SIZE]{0};
                        DWORD dwLengthDataSize{sizeof(arrResult)};
                        if ( HttpQueryInfo(hInternetOpenURL, HTTP_QUERY_CONTENT_LENGTH, &arrResult, &dwLengthDataSize, nullptr) ) {
                            strResult = arrResult;
                        }
                    }

                    InternetCloseHandle(hInternetOpenURL);
                }

                InternetCloseHandle(hInternetSession);
            }

            return std::stol(strResult);
        }

    public:
        ~CThreadProc() override {
//            if ( isRunning() ) {
//                exit();
//                wait();
//            }

            qDebug() << "thread destructor";
        }

        void setUrl(const std::wstring& url)
        {
            m_url = {url};
        }

        void setTempPath(const std::wstring& path)
        {
            m_temp_path = path;
        }

        void abort()
        {
            m_ct.complete = true;
            m_ct.stop(1);
        }

    public slots:
    signals:
        void complete(int, const std::wstring&);
    };
}

class CAppUpdater::impl {
public:
    impl() : temp_path{NSFile::CFileBinary::GetTempPath()}
    {}

    QString remote_version,
            updates_url,
            install_args;
    wstring temp_path;
};

CAppUpdater::CAppUpdater()
    : pimpl{new impl}
{

}

CAppUpdater::~CAppUpdater()
{
    if ( m_toaster ) {
        QObject::disconnect(m_toaster.get());

        if ( m_toaster->isRunning() ) {
            m_toaster->exit();
            m_toaster->abort();
            m_toaster->wait();
        }

        m_toaster->deleteLater();
    }
}

void CAppUpdater::checkUpdates()
{
    if ( !m_toaster.get() ) {
//        m_toaster = std::make_unique<CThreadProc>();
        m_toaster = std::unique_ptr<CThreadProc, thread_deleter>(new CThreadProc);
    }

    if ( !m_toaster->isRunning() ) {
//        QObject::connect(m_toaster.get(), &CThreadProc::finished, m_toaster.get(), &CThreadProc::deleteLater);
        QObject::connect(m_toaster.get(), &CThreadProc::complete, this,
                                    std::bind(&CAppUpdater::slot_complete, this, _1, _2, UPDATES_DOWNLOAD_APPCAST), Qt::QueuedConnection);

        m_toaster->setUrl(WSTR(URL_APPCAST_UPDATES));
        m_toaster->setTempPath(pimpl->temp_path);
        m_toaster->start();
    }
}

void CAppUpdater::slot_complete(int e, const std::wstring& xmlname, int target)
{
    if ( e == 0 ) {
        QTimer::singleShot(0, [=] {
            if ( target == UPDATES_DOWNLOAD_APPCAST ) {
                parse_app_cast(xmlname);

                if ( pimpl->remote_version.compare(VER_FILEVERSION_STR) > 0 ) {
                    emit hasUpdates(pimpl->remote_version);
                } else {
                    wstring _file_path = pimpl->temp_path + L"/" + WSTR(UPDATES_PACKAGE_TEMP_NAME);
                    if ( NSFile::CFileBinary::Exists(_file_path) )
                        NSFile::CFileBinary::Remove(_file_path);

                    emit noUpdates();
                }
            } else
            if ( target == UPDATES_DOWNLOAD_PACKAGE ) {
                QFileInfo _fi(QString::fromStdWString(xmlname));
                QStringList _args{pimpl->install_args};

                QFile::rename(_fi.absoluteFilePath(), _fi.absolutePath() + "/" + UPDATES_PACKAGE_TEMP_NAME);
                QProcess::startDetached(_fi.absolutePath() + "/" + UPDATES_PACKAGE_TEMP_NAME, _args, _fi.absolutePath());
            }
        });
    }

    m_toaster.reset();
//    CThreadProc * pointer = m_toaster.release();
//    delete pointer, pointer = nullptr;
}

void CAppUpdater::parse_app_cast(const std::wstring& xmlname)
{
    std::ifstream t{NSFile::CUtf8Converter::GetUtf8StringFromUnicode(xmlname)};
    std::string xmlcontent((std::istreambuf_iterator<char>(t)),
                        std::istreambuf_iterator<char>());
    t.close();

    if ( xmlcontent.length() > 0 ) {
#ifdef Q_OS_WIN
# ifdef Q_OS_WIN64
#  define UPDATE_TARGET_OS "win_64"
# else
#  define UPDATE_TARGET_OS "win_32"
# endif
#elif defined(Q_OS_LINUX)
# define UPDATE_TARGET_OS "linux-64"
#endif
        QJsonParseError jerror;
        QByteArray stringdata = QString::fromStdString(xmlcontent).toUtf8();
        QJsonDocument jdoc = QJsonDocument::fromJson(stringdata, &jerror);

        if( jerror.error == QJsonParseError::NoError ) {
            QJsonObject objRoot = jdoc.object();
            pimpl->remote_version = objRoot["version"].toString();

            if ( objRoot.contains("package") &&
                    objRoot["package"].toObject().contains(UPDATE_TARGET_OS) )
            {
                QJsonObject objPackage = objRoot["package"].toObject()[UPDATE_TARGET_OS].toObject();
                pimpl->updates_url = objPackage["url"].toString();
                pimpl->install_args = objPackage["installArguments"].toString();
            }
        }
    }

    NSFile::CFileBinary::Remove(xmlname);
}

auto CAppUpdater::hasUpdatePackage() const -> bool
{
    return !pimpl->updates_url.isEmpty();
}

auto CAppUpdater::download() -> void
{
    if ( !m_toaster.get() ) {
//        m_toaster = std::make_unique<CThreadProc>();
        m_toaster = std::unique_ptr<CThreadProc, thread_deleter>(new CThreadProc);
    }

    if ( !m_toaster->isRunning() ) {
        QObject::disconnect(m_toaster.get());
        QObject::connect(m_toaster.get(), &CThreadProc::complete, this, std::bind(&CAppUpdater::slot_complete, this, _1, _2, UPDATES_DOWNLOAD_PACKAGE), Qt::QueuedConnection);

        m_toaster->setUrl(pimpl->updates_url.toStdWString());
        m_toaster->setTempPath(pimpl->temp_path);
        m_toaster->start();
    }
}

auto CAppUpdater::install() -> void
{

}

#include "cappupdater.moc"
