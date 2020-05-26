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
#include <QDebug>

#include "defines.h"
#include "version.h"

#include "FileDownloader/FileDownloader.h"
#include "../DesktopEditor/common/File.h"

#include "cascapplicationmanagerwrapper.h"
#include "cmessage.h"

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
        wstring m_url;

        void run() override
        {
            if ( !(m_url.length() > 0) ) {
                emit complete(-1, L"");
                return;
            }

            std::shared_ptr<CFileDownloader> _downloader = std::make_shared<CFileDownloader>(m_url, false);

#ifdef Q_OS_WIN
            wchar_t _wbuf[L_tmpnam_s];
            if ( !_wtmpnam_s(_wbuf, L_tmpnam_s) ) {
                _downloader->SetFilePath(_wbuf);
            }
#else
            string xml_tmpname = tmpnam(nullptr);
            _downloader->SetFilePath(NSFile::CUtf8Converter::GetUnicodeStringFromUTF8((BYTE*)xml_tmpname.c_str(), static_cast<long>(xml_tmpname.length())));
#endif
            _downloader->SetEvent_OnComplete(std::bind(&CThreadProc::callback_download_complete, this, _1, _downloader->GetFilePath()));
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

    public:
        void setUrl(const std::wstring& url)
        {
            m_url = {url};
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
    impl() {}

    string remote_version;
};

CAppUpdater::CAppUpdater()
    : pimpl{new impl}
{

}

CAppUpdater::~CAppUpdater()
{
    QObject::disconnect(m_toaster.get());

    if ( m_toaster->isRunning() ) {
        m_toaster->exit();
        m_toaster->abort();
        m_toaster->wait();
    }

    m_toaster->deleteLater();
}

void CAppUpdater::checkUpdates()
{
    if ( !m_toaster ) {
        m_toaster = std::make_shared<CThreadProc>();

//        QObject::connect(m_toaster.get(), &CThreadProc::finished, m_toaster.get(), &CThreadProc::deleteLater);
        QObject::connect(m_toaster.get(), &CThreadProc::complete, this, std::bind(&CAppUpdater::slot_complete, this, _1, _2), Qt::QueuedConnection);
//        QObject::connect(m_toaster.get(), &CThreadProc::complete, this, std::bind(&CAppUpdater::parse_app_cast, this, _1), Qt::QueuedConnection);
    }

    if ( m_toaster->isRunning() ) return;

    m_toaster->setUrl(WSTR(URL_APPCAST_UPDATES));
    m_toaster->start();
}

void CAppUpdater::slot_complete(int e, const std::wstring& xmlname)
{
    if ( e == 0 ) {
        parse_app_cast(xmlname);

        if ( pimpl->remote_version.compare(VER_FILEVERSION_STR) > 0 ) {
            emit hasUpdates(QString::fromStdString(pimpl->remote_version));

//            CMessage mess(AscAppManager::topWindow()->handle(), CMessageOpts::moButtons::mbYesDefNoCancel);
//            mess.warning("Update found");
//                    win_sparkle_check_update_with_ui_and_install()
//            _re_package_url = "sparkle:os=\"windows-x64[\w\W]+url=\"(https?\:\/\/[^\"]+)";
        } else {
            emit noUpdates();
        }
    }
}

void CAppUpdater::parse_app_cast(const std::wstring& xmlname)
{
    std::ifstream t{NSFile::CUtf8Converter::GetUtf8StringFromUnicode(xmlname)};
    std::string xmlcontent((std::istreambuf_iterator<char>(t)),
                        std::istreambuf_iterator<char>());

    if ( xmlcontent.length() > 0 ) {
#ifdef Q_OS_WIN
# ifdef Q_OS_WIN64
#  define UPDATE_TARGET_OS "windows-x64"
# else
#  define UPDATE_TARGET_OS "windows-x86"
# endif
#elif defined(Q_OS_LINUX)
# define UPDATE_TARGET_OS "linux-64"
#endif

        std::regex _regex{"sparkle:os=\"" UPDATE_TARGET_OS "\"\\ssparkle:version=\"((\\d{1,3})(?:.(\\d+))?(?:.(\\d+))?(?:.(\\d+))?)"};
        std::smatch _match;
        if ( regex_search(xmlcontent, _match, _regex) ) {
            pimpl->remote_version = {_match.str(1)};
        }
    }

    NSFile::CFileBinary::Remove(xmlname);
}

#include "cappupdater.moc"
