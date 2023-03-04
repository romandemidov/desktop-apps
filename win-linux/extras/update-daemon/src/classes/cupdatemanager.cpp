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

#include "cupdatemanager.h"
#include <algorithm>
#include <iostream>
#include <functional>
#include <vector>
#include "utils.h"
#include "../../src/defines.h"
#include "version.h"
#include "classes/cdownloader.h"
#include <Windows.h>
#include <shlwapi.h>
#include <sstream>

#define SENDER_PORT   12010
#define RECEIVER_PORT 12011
#define UPDATE_PATH      L"/DesktopEditorsUpdates"
#define BACKUP_PATH      L"/DesktopEditorsBackup"
#define APP_LAUNCH_NAME  L"/DesktopEditors.exe"
#define DAEMON_NAME      L"/update-daemon.exe"
#define TEMP_DAEMON_NAME L"/~update-daemon.exe"
#define DELETE_LIST      L"/.delete_list.lst"
#define REPLACEMENT_LIST L"/.replacement_list.lst"
#define SUCCES_UNPACKED  L"/.success_unpacked"

using std::vector;


auto currentArch()->wstring
{
#ifdef _WIN64
    return L"_x64";
#else
    return L"_x86";
#endif
}

auto generateTmpFileName(const wstring &ext)->wstring
{
    wstring uuid_wstr;
    UUID uuid = {0};
    RPC_WSTR wszUuid = NULL;
    if (UuidCreate(&uuid) == RPC_S_OK && UuidToStringW(&uuid, &wszUuid) == RPC_S_OK) {
        uuid_wstr = ((wchar_t*)wszUuid);
        RpcStringFreeW(&wszUuid);
    } else
        uuid_wstr = L"00000000-0000-0000-0000-000000000000";
    return File::tempPath() + L"/" + TEXT(FILE_PREFIX) + uuid_wstr + currentArch() + ext;
}

auto isSuccessUnpacked(const wstring &successFilePath, const wstring &version)->bool
{
    list<wstring> lines;
    if (File::readFile(successFilePath, lines)) {
        if (std::find(lines.begin(), lines.end(), version) != lines.end())
            return true;
    }
    return false;
}

auto unzipArchive(const wstring &zipFilePath, const wstring &updPath,
                  const wstring &appPath, const wstring &version, wstring &error)->bool
{
    if (!File::dirExists(updPath) && !File::makePath(updPath)) {
        error = L"An error occurred while creating dir: " + updPath;
        return false;
    }

    // Extract files
    if (!File::unzipArchive(zipFilePath, updPath)) {
        error = L"An error occurred while unpacking zip file!";
        return false;
    }

    auto fillSubpathVector = [&error](const wstring &path, vector<wstring> &vec)->bool {
        list<wstring> filesList;
        wstring _error;
        if (!File::GetFilesList(path, &filesList, _error)) {
            error = L"An error occurred while get files list: " + _error;
            return false;
        }
        for (auto &filePath : filesList) {
            wstring subPath = filePath.substr(path.length());
            vec.push_back(std::move(subPath));
        }
        return true;
    };

    vector<wstring> updVec, appVec;
    if (!fillSubpathVector(appPath, appVec) || !fillSubpathVector(updPath, updVec))
        return false;

#ifdef ALLOW_DELETE_UNUSED_FILES
    // Create a list of files to delete
    {
        list<wstring> delList;
        for (auto &appFile : appVec) {
            auto it_appFile = std::find(updVec.begin(), updVec.end(), appFile);
            if (it_appFile != updVec.end() && appFile != DAEMON_NAME)
                delList.push_back(appFile);
        }
        if (!File::writeToFile(updPath + DELETE_LIST, delList))
            return false;
    }
#endif

    // Create a list of files to replacement
    {
        list<wstring> replList;
        for (auto &updFile : updVec) {
            auto it_appFile = std::find(appVec.begin(), appVec.end(), updFile);
            if (it_appFile != appVec.end()) {
                auto updFileHash = File::getFileHash(updPath + updFile);
                if (updFileHash.empty() || updFileHash != File::getFileHash(appPath + *it_appFile))
                    replList.push_back(updFile);

            } else
                replList.push_back(updFile);
        }
        if (!File::writeToFile(updPath + REPLACEMENT_LIST, replList))
            return false;
    }   

    // Сreate a file about successful unpacking for use in subsequent launches
    {
        list<wstring> successList{version};
        if (!File::writeToFile(updPath + SUCCES_UNPACKED, successList))
            return false;
    }
    return true;
}

class CUpdateManager::CUpdateManagerPrivate
{
public:
    CUpdateManagerPrivate(CUpdateManager *owner)
    {
        m_pDownloader = new CDownloader;
        m_pDownloader->onComplete([=](int error) {
            const wstring filePath = m_pDownloader->GetFilePath();
            owner->onCompleteSlot(error, filePath);
        });
        m_pDownloader->onProgress([=](int percent) {
            owner->onProgressSlot(percent);
        });
    }

    virtual ~CUpdateManagerPrivate()
    {
        delete m_pDownloader, m_pDownloader = nullptr;
    }

    void downloadFile(const wstring &url, const wstring &filePath)
    {
        m_pDownloader->downloadFile(url, filePath);
    }

    void stop()
    {
        m_pDownloader->stop();
    }

private:
    CDownloader  * m_pDownloader = nullptr;
};

CUpdateManager::CUpdateManager(CObject *parent):
    CObject(parent),
    m_downloadMode(Mode::CHECK_UPDATES),
    m_socket(new CSocket(SENDER_PORT, RECEIVER_PORT)),
    m_pimpl(new CUpdateManagerPrivate(this))
{
    init();
}

CUpdateManager::~CUpdateManager()
{
    if (m_future_clear.valid())
        m_future_clear.wait();
    if (m_future_unzip.valid())
        m_future_unzip.wait();
    delete m_pimpl, m_pimpl = nullptr;
    delete m_socket, m_socket = nullptr;
}

void CUpdateManager::onCompleteSlot(const int error, const wstring &filePath)
{
    if (error == 0) {
        switch (m_downloadMode) {
        case Mode::CHECK_UPDATES:
            sendMessage(MSG_LoadCheckFinished, filePath);
            break;
        case Mode::DOWNLOAD_UPDATES:
            sendMessage(MSG_LoadUpdateFinished, filePath);
            break;
        default:
            break;
        }
    } else
    if (error == 1) {
        // Pause or Stop
    } else {
        sendMessage(MSG_OtherError, L"Server connection error!");
    }
}

void CUpdateManager::init()
{
    m_socket->onMessageReceived([=](void *data, size_t size) {
        wstring str((const wchar_t*)data), tmp;
        vector<wstring> params;
        std::wstringstream wss(str);
        while (std::getline(wss, tmp, L'|'))
            params.push_back(std::move(tmp));

        if (params.size() == 4) {
            switch (std::stoi(params[0])) {
            case MSG_CheckUpdates: {
                m_downloadMode = Mode::CHECK_UPDATES;
                if (m_pimpl)
                    m_pimpl->downloadFile(params[1], generateTmpFileName(L".json"));
                break;
            }
            case MSG_LoadUpdates: {
                m_downloadMode = Mode::DOWNLOAD_UPDATES;
                if (m_pimpl)
                    m_pimpl->downloadFile(params[1], generateTmpFileName(L".zip"));
                break;
            }
            case MSG_StopDownload: {
                m_downloadMode = Mode::CHECK_UPDATES;
                if (m_pimpl)
                    m_pimpl->stop();
                break;
            }
            case MSG_UnzipIfNeeded:
                unzipIfNeeded(params[1], params[2]);
                break;

            case MSG_StartReplacingFiles:
                startReplacingFiles();
                break;

            case MSG_ClearTempFiles:
                clearTempFiles(params[1], params[2]);
                break;

            default:
                break;
            }
        }
    });

    m_socket->onError([](const char* error) {
        /*size_t num;
        wchar_t errorDescription[20];
        mbstowcs_s(&num, errorDescription, error, strlen(error) + 1);
        LPTSTR errorDescription = (LPTSTR)_T("Testing error messages...");
        SvcReportEvent(errorDescription);*/
        //Logger::WriteLog("E:/log.txt", error, 0);
    });
}

void CUpdateManager::onProgressSlot(const int percent)
{
    if (m_downloadMode == Mode::DOWNLOAD_UPDATES)
        sendMessage(MSG_Progress, to_wstring(percent));
}

bool CUpdateManager::sendMessage(int cmd, const wstring &param1, const wstring &param2, const wstring &param3)
{
    wstring str = to_wstring(cmd) + L"|" + param1 + L"|" + param2 + L"|" + param3;
    size_t sz = str.size() * sizeof(str.front());
    return m_socket->sendMessage((void*)str.c_str(), sz);
}

void CUpdateManager::unzipIfNeeded(const wstring &filePath, const wstring &newVersion)
{
    if (m_lock)
        return;
    m_lock = true;
    const wstring updPath = File::tempPath() + UPDATE_PATH;
    auto unzip = [=]()->void {
        wstring error(L"unzipArchive() error");
        if (!unzipArchive(filePath, updPath, File::appPath(), newVersion , error)) {
            m_lock = false;
            if (!sendMessage(MSG_OtherError, error)) {
                Logger::WriteLog(DEFAULT_LOG_FILE, DEFAULT_ERROR_MESSAGE);
            }
            return;
        }
        m_lock = false;
        if (!sendMessage(MSG_ShowStartInstallMessage)) {
            Logger::WriteLog(DEFAULT_LOG_FILE, DEFAULT_ERROR_MESSAGE);
        }
    };

    if (!File::dirExists(updPath) || File::dirIsEmpty(updPath)) {
        m_future_unzip = std::async(std::launch::async, unzip);
    } else {
        if (isSuccessUnpacked(updPath + SUCCES_UNPACKED, newVersion)) {
            m_lock = false;
            if (!sendMessage(MSG_ShowStartInstallMessage)) {
                Logger::WriteLog(DEFAULT_LOG_FILE, DEFAULT_ERROR_MESSAGE);
            }
        } else {
            if (!File::removeDirRecursively(updPath)) {
                Logger::WriteLog(DEFAULT_LOG_FILE, DEFAULT_ERROR_MESSAGE);
            }
            m_future_unzip = std::async(std::launch::async, unzip);
        }
    }
}

void CUpdateManager::clearTempFiles(const wstring &prefix, const wstring &except)
{
    m_future_clear = std::async(std::launch::async, [=]() {
        list<wstring> filesList;
        wstring _error;
        if (!File::GetFilesList(File::tempPath(), &filesList, _error)) {
            Logger::WriteLog(DEFAULT_LOG_FILE, DEFAULT_ERROR_MESSAGE);
            return;
        }
        for (auto &filePath : filesList) {
            if (PathMatchSpec(filePath.c_str(), L"*.json") || PathMatchSpec(filePath.c_str(), L"*.zip")) {
                wstring lcFilePath(filePath);
                std::transform(lcFilePath.begin(), lcFilePath.end(), lcFilePath.begin(), ::tolower);
                if (lcFilePath.find(prefix) != wstring::npos && filePath != except)
                    File::removeFile(filePath);
            }
        }
    });
}

void CUpdateManager::restoreFromBackup(const wstring &appPath, const wstring &updPath, const wstring &tmpPath)
{
    // Restore from backup
    if (!File::replaceFolderContents(tmpPath, appPath))
        Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while restore files from backup!");
    else
        File::removeDirRecursively(tmpPath);

    // Restore executable name
    if (!File::replaceFile(appPath + TEMP_DAEMON_NAME, appPath + DAEMON_NAME))
        Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while restore daemon file name!");

    File::removeDirRecursively(updPath);
}

void CUpdateManager::startReplacingFiles()
{
    wstring appPath = File::appPath();
    wstring appFilePath = File::appPath() + DAEMON_NAME;
    wstring updPath = File::tempPath() + UPDATE_PATH;
    wstring tmpPath = File::tempPath() + BACKUP_PATH;
    if (!File::dirExists(updPath)) {
        Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while searching dir: " + updPath);
        return;
    }
    if (File::dirExists(tmpPath) && !File::dirIsEmpty(tmpPath)
            && !File::removeDirRecursively(tmpPath)) {
        Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while deleting Backup dir: " + tmpPath);
        return;
    }
    if (!File::dirExists(tmpPath) && !File::makePath(tmpPath)) {
        Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while creating dir: " + tmpPath);
        return;
    }

    // Remove old update-daemon
    if (File::fileExists(appPath + TEMP_DAEMON_NAME)
            && !File::removeFile(appPath + TEMP_DAEMON_NAME)) {
        Logger::WriteLog(DEFAULT_LOG_FILE, L"Unable to remove temp file: " + appPath + TEMP_DAEMON_NAME);
        return;
    }

    list<wstring> repList;
    if (!File::readFile(updPath + REPLACEMENT_LIST, repList))
        return;

#ifdef ALLOW_DELETE_UNUSED_FILES
    list<wstring> delList;
    if (!File::readFile(updPath + DELETE_LIST, delList))
        return;
#endif

    // Rename current executable
    wstring appFileRenamedPath = appPath + TEMP_DAEMON_NAME;
    if (!File::replaceFile(appFilePath, appFileRenamedPath)) {
        Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while renaming the daemon file!");
        return;
    }

#ifdef ALLOW_DELETE_UNUSED_FILES
    // Replace unused files to Backup
    if (!File::replaceListOfFiles(delList, appPath, tmpPath)) {
        Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while replace unused files! Restoring from the backup will start.");
        restoreFromBackup(appPath, updPath, tmpPath);
        return;
    }
#endif

    // Move update files to app path
    if (!File::replaceListOfFiles(repList, updPath, appPath, tmpPath)) {
        Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while copy files! Restoring from the backup will start.");

        // Remove new update-daemon.exe if exist
        if (File::fileExists(appFilePath))
            File::removeFile(appFilePath);

        restoreFromBackup(appPath, updPath, tmpPath);
        return;
    }

    // Remove Update and Temp dirs
    File::removeDirRecursively(updPath);
    File::removeDirRecursively(tmpPath);

    // Restore executable name if there was no new version
    if (std::find(repList.begin(), repList.end(), DAEMON_NAME) == repList.end())
        if (!File::replaceFile(appFileRenamedPath, appFilePath))
            Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while restore daemon file name: " + appFileRenamedPath);

    // Restart program
    if (!File::runProcess(appPath + APP_LAUNCH_NAME, L""))
        Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while restarting the program!");
}
