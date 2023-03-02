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
//#include "clangater.h"
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
//#define DELETE_LIST L"/.delete_list.lst"
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

auto unzipArchive(const wstring &zipFilePath, const wstring &updPath, const wstring &appPath, const wstring &version)->bool
{
    if (!File::dirExists(updPath) && !File::makePath(updPath)) {
        Utils::ShowMessage(L"An error occurred while creating dir: " + updPath);
        return false;
    }

    // Extract files
    if (!File::unzipArchive(zipFilePath, updPath)) {
        Utils::ShowMessage(L"An error occurred while unpacking zip file!");
        return false;
    }

    auto fillSubpathVector = [](const wstring &path, vector<wstring> &vec)->bool {
        list<wstring> filesList;
        wstring error;
        if (!File::GetFilesList(path, &filesList, error)) {
            Utils::ShowMessage(L"An error occurred while get files list!");
            return false;
        }
        for (auto &filePath : filesList) {
            wstring subPath = filePath.substr(path.length());
            vec.push_back(std::move(subPath));
        }
        return true;
    };

    vector<wstring> updVec, appVec;
    if (!fillSubpathVector(appPath, appVec) || fillSubpathVector(updPath, updVec))
        return false;

//    // Create a list of files to delete
//    {
//        const QString delListFilePath = updPath + DELETE_LIST;
//        QFile delListFile(delListFilePath);
//        if (!delListFile.open(QFile::WriteOnly)) {
//            criticalMsg(QObject::tr("Can't create file: ") + delListFilePath);
//            return false;
//        }
//        QTextStream out(&delListFile);
//        foreach (auto &appFile, appVec) {
//            if (!updVec.contains(appFile) && appFile != DAEMON_NAME)
//                out << appFile << "\n";
//        }
//        delListFile.close();
//    }

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

struct CUpdateManager::PackageData {
    wstring     fileName,
                packageUrl,
                packageArgs;
    void clear() {
        fileName.clear();
        packageUrl.clear();
        packageArgs.clear();
    }
};

struct CUpdateManager::SavedPackageData {
    string     hash;
    wstring    version,
               fileName;
};

CUpdateManager::CUpdateManager(CObject *parent):
    CObject(parent),
    m_packageData(new PackageData),
    m_savedPackageData(new SavedPackageData),
    m_checkUrl(L""),
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
    delete m_packageData, m_packageData = nullptr;
    delete m_savedPackageData, m_savedPackageData = nullptr;
    delete m_pimpl, m_pimpl = nullptr;
    delete m_socket, m_socket = nullptr;
}

void CUpdateManager::onCompleteSlot(const int error, const wstring &filePath)
{
    if (error == 0) {
        switch (m_downloadMode) {
        case Mode::CHECK_UPDATES:
            onLoadCheckFinished(filePath);
            break;
        case Mode::DOWNLOAD_UPDATES:
            onLoadUpdateFinished(filePath);
            break;
        default:
            break;
        }
    } else
    if (error == 1) {
        // Pause or Stop
    } else {
        /*auto wgt = QApplication::activeWindow();
        if (wgt && wgt->objectName() == "MainWindow" && !wgt->isMinimized())
            CMessage::warning(wgt, tr("Server connection error!"));*/
    }
}

void CUpdateManager::init()
{
    m_socket->onMessageReceived([](void *data, size_t size) {
        //Logger::WriteLog("E:/log.txt", (const char*)data, size);
        wstring str((const wchar_t*)data), tmp;
        vector<wstring> params;
        std::wstringstream wss(str);
        while (std::getline(wss, tmp, L'|'))
            params.push_back(std::move(tmp));
        if (params.size() == 4) {

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

    UINT_PTR timer1 = 0L;
    timer1 = setTimer(1000, [=]() {
        sendMessage(9, L"gttttttttttttttttttrrrrrryrrrh", L"", L"ledggsfddddddddddffffffffffffffffffffffffffh");

    });
}

void CUpdateManager::clearTempFiles(const wstring &except)
{
    /*static bool lock = false;
    if (!lock) { // for one-time cleaning
        lock = true;
        m_future_clear = std::async(std::launch::async, [=]() {
            QStringList filter{"*.json", "*.zip"};
            QDirIterator it(QDir::tempPath(), filter, QDir::Files | QDir::NoSymLinks |
                            QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString tmp = it.next();
                if (tmp.toLower().indexOf(FILE_PREFIX) != -1 && tmp != except)
                    QDir().remove(tmp);
            }
        });
    }
    if (except.empty())
        savePackageData();*/
}

void CUpdateManager::checkUpdates()
{
    /*destroyStartupTimer(m_pCheckOnStartupTimer);
    m_newVersion.clear();
    m_packageData->clear();

    m_downloadMode = Mode::CHECK_UPDATES;
    if (m_pimpl)
        m_pimpl->downloadFile(m_checkUrl, generateTmpFileName(L".json"));*/
}

void CUpdateManager::updateNeededCheking()
{
    checkUpdates();
}

void CUpdateManager::onProgressSlot(const int percent)
{
    /*if (m_downloadMode == Mode::DOWNLOAD_UPDATES)
        emit progresChanged(percent);*/
}

void CUpdateManager::savePackageData(const string &hash, const wstring &version, const wstring &fileName)
{
    /*m_savedPackageData->fileName = fileName;
    m_savedPackageData->hash = hash;
    m_savedPackageData->version = version;
    GET_REGISTRY_USER(reg_user);
    reg_user.beginGroup("Updates");
    reg_user.setValue("Updates/file", fileName);
    reg_user.setValue("Updates/hash", hash);
    reg_user.setValue("Updates/version", version);
    reg_user.endGroup();*/
}

bool CUpdateManager::sendMessage(int cmd, const wstring &param1, const wstring &param2, const wstring &param3)
{
    wstring str = to_wstring(cmd) + L"|" + param1 + L"|" + param2 + L"|" + param3;
    size_t sz = str.size() * sizeof(str.front());
    return m_socket->sendMessage((void*)str.c_str(), sz);
}

void CUpdateManager::loadUpdates()
{
    if (m_lock)
        return;
    if (!m_savedPackageData->fileName.empty()
            && m_savedPackageData->fileName.find(currentArch()) != wstring::npos
            && m_savedPackageData->version == m_newVersion
            && m_savedPackageData->hash == File::getFileHash(m_savedPackageData->fileName))
    {
        m_packageData->fileName = m_savedPackageData->fileName;
        unzipIfNeeded();

    } else
    if (!m_packageData->packageUrl.empty()) {
        m_downloadMode = Mode::DOWNLOAD_UPDATES;
        if (m_pimpl)
            m_pimpl->downloadFile(m_packageData->packageUrl, generateTmpFileName(L".zip"));
    }
}

void CUpdateManager::installUpdates()
{
    /*GET_REGISTRY_USER(reg_user);
    reg_user.beginGroup("Updates");
    const string ignored_ver = reg_user.value("Updates/ignored_ver").toString();
    reg_user.endGroup();
    if (ignored_ver != getVersion())
        m_dialogSchedule->addToSchedule("showStartInstallMessage");*/
}

wstring CUpdateManager::getVersion() const
{
    return m_newVersion;
}

void CUpdateManager::onLoadUpdateFinished(const wstring &filePath)
{
    /*m_packageData->fileName = filePath;
    savePackageData(File::getFileHash(m_packageData->fileName), m_newVersion, m_packageData->fileName);*/
    unzipIfNeeded();
}

void CUpdateManager::unzipIfNeeded()
{
    if (m_lock)
        return;
    m_lock = true;
    const wstring updPath = File::tempPath() + UPDATE_PATH;
    auto unzip = [=]()->void {
        if (!unzipArchive(m_packageData->fileName, updPath,
                            File::appPath(), m_newVersion)) {
            m_lock = false;
            return;
        }
        m_lock = false;
        /*QMetaObject::invokeMethod(this->m_dialogSchedule,
                                  "addToSchedule",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, QString("showStartInstallMessage")));*/
    };

    if (!File::dirExists(updPath) || File::dirIsEmpty(updPath)) {
        m_future_unzip = std::async(std::launch::async, unzip);
    } else {
        if (isSuccessUnpacked(updPath + SUCCES_UNPACKED, m_newVersion)) {
            m_lock = false;
            //m_dialogSchedule->addToSchedule("showStartInstallMessage");
        } else {
            File::removeDirRecursively(updPath);
            m_future_unzip = std::async(std::launch::async, unzip);
        }
    }
}

void CUpdateManager::handleAppClose()
{
    if ( m_restartForUpdate ) {
        /*GET_REGISTRY_SYSTEM(reg_system)
        wstring filePath = (File::appPath() + DAEMON_NAME).toStdWString();
        wstring args = L"/LANG=" + reg_system.value("locale", "en").toString().toStdWString();
        args += L" " + m_packageData->packageArgs;
        if (!runProcess(filePath.c_str(), (wchar_t*)args.c_str())) {
            criticalMsg(QString("Unable to start process: %1").arg(File::appPath() + DAEMON_NAME));
        }*/
    } else
        if (m_pimpl)
            m_pimpl->stop();
}

void CUpdateManager::scheduleRestartForUpdate()
{
    m_restartForUpdate = true;
}

void CUpdateManager::setNewUpdateSetting(const string& _rate)
{
    /*GET_REGISTRY_USER(reg_user);
    reg_user.setValue("autoUpdateMode", _rate);
    int mode = (_rate == "silent") ?
                    UpdateMode::SILENT : (_rate == "ask") ?
                        UpdateMode::ASK : UpdateMode::DISABLE;
    if (mode == UpdateMode::DISABLE)
        destroyStartupTimer(m_pCheckOnStartupTimer);*/
}

void CUpdateManager::cancelLoading()
{
    if (m_lock)
        return;
    //AscAppManager::sendCommandTo(0, "updates:checking", QString("{\"version\":\"%1\"}").arg(m_newVersion));
    m_downloadMode = Mode::CHECK_UPDATES;
    if (m_pimpl)
        m_pimpl->stop();
}

void CUpdateManager::skipVersion()
{
    /*GET_REGISTRY_USER(reg_user);
    reg_user.beginGroup("Updates");
    reg_user.setValue("Updates/ignored_ver", m_newVersion);
    reg_user.endGroup();*/
}

int CUpdateManager::getUpdateMode()
{
    /*GET_REGISTRY_USER(reg_user);
    const QString mode = reg_user.value("autoUpdateMode", "ask").toString();
    return (mode == "silent") ?
                UpdateMode::SILENT : (mode == "ask") ?
                    UpdateMode::ASK : UpdateMode::DISABLE;*/
    return 0;
}

void CUpdateManager::onLoadCheckFinished(const wstring &filePath)
{
    /*QFile jsonFile(filePath);
    if ( jsonFile.open(QIODevice::ReadOnly) ) {
        QByteArray ReplyText = jsonFile.readAll();
        jsonFile.close();

        QJsonDocument doc = QJsonDocument::fromJson(ReplyText);
        QJsonObject root = doc.object();

        bool updateExist = false;
        QString version = root.value("version").toString();

        GET_REGISTRY_USER(reg_user);
        reg_user.beginGroup("Updates");
        const string ignored_ver = reg_user.value("Updates/ignored_ver").toString();
        reg_user.endGroup();

        const QStringList curr_ver = QString::fromLatin1(VER_FILEVERSION_STR).split('.');
        const QStringList ver = version.split('.');
        for (int i = 0; i < std::min(ver.size(), curr_ver.size()); i++) {
            if (ver.at(i).toInt() > curr_ver.at(i).toInt()) {
                updateExist = (version != ignored_ver);
                break;
            }
        }

        if ( updateExist ) {
        // parse package
            QJsonObject package = root.value("package").toObject();
# ifdef _WIN64
            QJsonValue win = package.value("win_64");
# else
            QJsonValue win = package.value("win_32");
# endif
            QJsonObject win_params = win.toObject();
            QJsonObject archive = win_params.value("archive").toObject();
            m_packageData->packageUrl = archive.value("url").toString().toStdWString();
            //m_packageData->packageUrl = win_params.value("url").toString().toStdWString();
            m_packageData->packageArgs = win_params.value("installArguments").toString().toStdWString();

            // parse release notes
            QJsonObject release_notes = root.value("releaseNotes").toObject();
            const QString lang = CLangater::getCurrentLangCode() == "ru-RU" ? "ru-RU" : "en-EN";
            QJsonValue changelog = release_notes.value(lang);

            m_newVersion = version;
            if (m_newVersion == m_savedPackageData->version
                    && m_savedPackageData->fileName.indexOf(currentArch()) != -1)
                clearTempFiles(m_savedPackageData->fileName);
            else
                clearTempFiles();
            onCheckFinished(false, true, m_newVersion, changelog.toString());
        } else {
            clearTempFiles();
            onCheckFinished(false, false, "", "");
        }
    } else {
        onCheckFinished(true, false, "", "Error receiving updates...");
    }*/
}

void CUpdateManager::onCheckFinished(bool error, bool updateExist, const wstring &version, const string &changelog)
{
    /*Q_UNUSED(changelog);
    if (!error && updateExist) {
        AscAppManager::sendCommandTo(0, "updates:checking", QString("{\"version\":\"%1\"}").arg(version));
        switch (getUpdateMode()) {
        case UpdateMode::SILENT:
            loadUpdates();
            break;
        case UpdateMode::ASK:
            m_dialogSchedule->addToSchedule("showUpdateMessage");
            break;
        }

    } else
    if (!error && !updateExist) {
        AscAppManager::sendCommandTo(0, "updates:checking", "{\"version\":\"no\"}");
    } else
    if (error) {
        //qDebug() << "Error while loading check file...";
    }*/
}

void CUpdateManager::showUpdateMessage(/*QWidget *parent*/) {
    /*int result = WinDlg::showDialog(parent,
                        tr("A new version of %1 is available!").arg(QString(WINDOW_NAME)),
                        tr("%1 %2 is now available (you have %3). "
                           "Would you like to download it now?").arg(QString(WINDOW_NAME),
                                                                    getVersion(),
                                                                    QString(VER_FILEVERSION_STR)),
                        WinDlg::DlgBtns::mbSkipRemindDownload);

    switch (result) {
    case WinDlg::DLG_RESULT_DOWNLOAD:
        loadUpdates();
        break;
    case WinDlg::DLG_RESULT_SKIP: {
        skipVersion();
        AscAppManager::sendCommandTo(0, "updates:checking", "{\"version\":\"no\"}");
        break;
    }
    default:
        const char *str = "Test message...";
        m_socket->sendMessage((void*)str, 15);
        break;
    }*/
}

void CUpdateManager::showStartInstallMessage(/*QWidget *parent*/)
{
    /*AscAppManager::sendCommandTo(0, "updates:download", "{\"progress\":\"done\"}");
    int result = WinDlg::showDialog(parent,
                                    tr("A new version of %1 is available!").arg(QString(WINDOW_NAME)),
                                    tr("%1 %2 is now downloaded (you have %3). "
                                       "Would you like to install it now?").arg(QString(WINDOW_NAME),
                                                                                getVersion(),
                                                                                QString(VER_FILEVERSION_STR)),
                                    WinDlg::DlgBtns::mbSkipRemindSaveandinstall);
    switch (result) {
    case WinDlg::DLG_RESULT_INSTALL: {
        scheduleRestartForUpdate();
        AscAppManager::closeAppWindows();
        break;
    }
    case WinDlg::DLG_RESULT_SKIP: {
        skipVersion();
        AscAppManager::sendCommandTo(0, "updates:checking", "{\"version\":\"no\"}");
        break;
    }
    default:
        break;
    }*/
}

/*
void restoreFromBackup(const wstring &appPath, const wstring &updPath, const wstring &tmpPath)
{
    // Restore from backup
    if (!replaceFolderContents(tmpPath, appPath))
        showMessage(L"An error occurred while restore files from backup!");
    else
        removeDirRecursively(tmpPath);

    // Restore executable name
    if (!replaceFile(appPath + TEMP_DAEMON_NAME, appPath + DAEMON_NAME))
        showMessage(L"An error occurred while restore daemon file name!");

    removeDirRecursively(updPath);
}

int wmain(int argc, wchar_t *argv[])
{
    wstring appFilePath = normailze(wstring(argv[0]));
    wstring appPath = parentPath(appFilePath);
    wstring updPath = tempPath() + UPDATE_PATH;
    wstring tmpPath = tempPath() + BACKUP_PATH;
    if (!dirExists(updPath)) {
        showMessage(L"An error occurred while searching dir: " + updPath);
        return 1;
    }
    if (dirExists(tmpPath) && !PathIsDirectoryEmpty(tmpPath.c_str())
            && !removeDirRecursively(tmpPath)) {
        showMessage(L"An error occurred while deleting Backup dir: " + tmpPath);
        return 1;
    }
    if (!dirExists(tmpPath) && !makePath(tmpPath)) {
        showMessage(L"An error occurred while creating dir: " + tmpPath);
        return 1;
    }

    // Remove old update-daemon
    if (fileExists(appPath + TEMP_DAEMON_NAME)
            && !removeFile(appPath + TEMP_DAEMON_NAME)) {
        showMessage(L"Unable to remove temp file: " + appPath + TEMP_DAEMON_NAME);
        return 1;
    }

    list<wstring> repList;
    if (!readFile(updPath + REPLACEMENT_LIST, repList))
        return 1;

    // Rename current executable
    wstring appFileRenamedPath = appPath + TEMP_DAEMON_NAME;
    if (!replaceFile(appFilePath, appFileRenamedPath)) {
        showMessage(L"An error occurred while renaming the daemon file!");
        return 1;
    }

//    // Replace unused files to Backup
//    if (!replaceListOfFiles(delList, appPath, tmpPath)) {
//        showMessage(L"An error occurred while replace unused files! Restoring from the backup will start.");
//        restoreFromBackup(appPath, updPath, tmpPath);
//        return 1;
//    }

    // Move update files to app path
    if (!replaceListOfFiles(repList, updPath, appPath, tmpPath)) {
        showMessage(L"An error occurred while copy files! Restoring from the backup will start.");

        // Remove new update-daemon.exe if exist
        if (fileExists(appFilePath))
            removeFile(appFilePath);

        restoreFromBackup(appPath, updPath, tmpPath);
        return 1;
    }

    // Remove Update and Temp dirs
    removeDirRecursively(updPath);
    removeDirRecursively(tmpPath);

    // Restore executable name if there was no new version
    if (std::find(repList.begin(), repList.end(), DAEMON_NAME) == repList.end())
        if (!replaceFile(appFileRenamedPath, appFilePath))
            showMessage(L"An error occurred while restore daemon file name: " + appFileRenamedPath);

    // Restart program
    if (!runProcess(appPath + APP_LAUNCH_NAME, L""))
        showMessage(L"An error occurred while restarting the program!");

    return 0;
}

*/
