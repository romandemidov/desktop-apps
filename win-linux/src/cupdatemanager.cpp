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
#include <QApplication>
#include <QSettings>
#include <QDir>
#include <QDirIterator>
#include <QDataStream>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <iostream>
#include <functional>
#include <vector>
#include "utils.h"
#include "defines.h"
#include "version.h"
#include "clangater.h"
#include "components/cmessage.h"
#include "Network/FileTransporter/include/FileTransporter.h"
#include "cascapplicationmanagerwrapper.h"
#ifdef Q_OS_WIN
# include <QCryptographicHash>
# include <Windows.h>
# include "OfficeUtils.h"
# include "platform_win/updatedialog.h"
# define DAEMON_NAME "/update-daemon.exe"
# define TEMP_DAEMON_NAME "/~update-daemon.exe"
# define VERSION_FILE "/.version_control.lst"
# define DELETE_LIST "/.delete_list.lst"
# define REPLACEMENT_LIST "/.replacement_list.lst"
# define SUCCES_UNPACKED "/.success_unpacked"
#endif

#define UPDATE_PATH "/DesktopEditorsUpdates"
#define MAGIC_NUMBER 0x836A05F0
#define CHECK_ON_STARTUP_MS 9000
#define CMD_ARGUMENT_CHECK_URL L"--updates-appcast-url"
#ifndef URL_APPCAST_UPDATES
# define URL_APPCAST_UPDATES ""
#endif

using std::vector;
using NSNetwork::NSFileTransport::CFileDownloader;


struct UserData {
    const QString *updPath;
    QString lastError;
    QVector<QString> *filesVec;
};

auto currentArch()->QString
{
#ifdef Q_OS_WIN
# ifdef Q_OS_WIN64
    return "_x64";
# else
    return "_x86";
# endif
#else
    return "_x64";
#endif
}

auto criticalMsg(const QString &msg)
{
    wstring lpText = msg.toStdWString();
    MessageBoxW(NULL, lpText.c_str(), TEXT(APP_TITLE), MB_ICONERROR | MB_SERVICE_NOTIFICATION_NT3X | MB_SETFOREGROUND);
}

class CUpdateManager::DialogSchedule : public QObject
{
    Q_OBJECT
public:
    DialogSchedule(QObject *owner);
public slots:
    void addToSchedule(const QString &method);

private:
    QTimer *m_timer = nullptr;
    QVector<QString> m_shedule_vec;
};

CUpdateManager::DialogSchedule::DialogSchedule(QObject *owner) :
    QObject(owner)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(500);
    m_timer->setSingleShot(false);
    connect(m_timer, &QTimer::timeout, this, [=] {
        QWidget *wnd = WindowHelper::currentTopWindow();
        if (wnd && !m_shedule_vec.isEmpty()) {
            QMetaObject::invokeMethod(owner,
                                      m_shedule_vec.first().toLocal8Bit().data(),
                                      Qt::QueuedConnection, Q_ARG(QWidget*, wnd));
            m_shedule_vec.removeFirst();
            if (m_shedule_vec.isEmpty())
                m_timer->stop();
        }
    });
}

void CUpdateManager::DialogSchedule::addToSchedule(const QString &method)
{
    m_shedule_vec.push_back(method);
    if (!m_timer->isActive())
        m_timer->start();
}

auto destroyStartupTimer(QTimer* &timer)->void
{
    if (timer) {
        if (timer->isActive())
            timer->stop();
        timer->deleteLater();
        timer = nullptr;
    }
}

auto getFileHash(const QString &fileName)->QByteArray
{
    QFile file(fileName);
    if (file.open(QFile::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Md5);
        if (hash.addData(&file)) {
            file.close();
            return hash.result();
        }
        file.close();
    }
    return QByteArray();
}

auto generateTmpFileName(const QString &ext)->QString
{
    const QUuid uuid = QUuid::createUuid();
    const QRegularExpression branches = QRegularExpression("[{|}]+");
    return QDir::tempPath() + "/" + QString(FILE_PREFIX) +
           uuid.toString().replace(branches, "") + currentArch() + ext;
}

auto isSuccessUnpacked(const QString &successFilePath, const QString &version)->bool
{
    QFile successFile(successFilePath);
    if (!successFile.open(QFile::ReadOnly))
        return false;
    if (QString(successFile.readAll()).indexOf(version) == -1) {
        successFile.close();
        return false;
    }
    successFile.close();
    return true;
}

void dataReceiver(const wstring &path, BYTE *pBuff, const long &nSize, void *pParam)
{
    UserData *uData = (UserData*)pParam;
    const QString fullPath = *uData->updPath + "/" + QString::fromStdWString(path);
    int pos = fullPath.lastIndexOf('/');
    if (!fullPath.midRef(pos + 1).isEmpty()) {
        // is file
        QVector<QString> *filesVec = uData->filesVec;
        if (filesVec->contains(QString::fromStdWString(L"/" + path))) {
            const QString dirPath = fullPath.mid(0, pos);
            if (!QDir().exists(dirPath) && !QDir().mkpath(dirPath)) {
                uData->lastError = "Cannot create dir: " + dirPath;
                return;
            }
            QFile file(fullPath);
            if (!file.open(QFile::WriteOnly)) {
                uData->lastError = "Cannot write file: " + fullPath;
                return;
            }
            if (file.write((const char*)pBuff, nSize) != nSize) {
                file.close();
                uData->lastError = "Cannot write data to file: " + fullPath;
                return;
            }
            file.close();
        }
    }
}

auto extractFiles(const QString &zipFilePath, const QString &updPath, QVector<QString> *filesVec)->bool
{
    COfficeUtils utils;
    bool result;
    UserData uData;
    uData.updPath = &updPath;
    uData.lastError = "";
    uData.filesVec = filesVec;
    HRESULT res = utils.ExtractFilesToMemory(zipFilePath.toStdWString(), dataReceiver, (void*)&uData, &result);
    if (res != S_OK || !result || !uData.lastError.isEmpty()) {
        criticalMsg(QObject::tr("An error occurred while unpacking zip file! ") + uData.lastError);
        return false;
    }
    return true;
}

auto unzipArchive(const QString &zipFilePath, const QString &updPath, const QString &appPath, const QString &version)->bool
{
    QDir updDir(updPath);
    if (!updDir.exists() && !updDir.mkpath(updPath)) {
        criticalMsg(QObject::tr("An error occurred while creating dir: ") + updPath);
        return false;
    }

    // Extract and save new version control file
    QVector<QString> verFileVec{VERSION_FILE};
    if (!extractFiles(zipFilePath, updPath, &verFileVec))
        return false;

    // Read new version control file
    QMap<QString, QString> updFilesMap;
    {
        QFile newVerFile(updPath + VERSION_FILE);
        if (!newVerFile.open(QFile::ReadOnly)) {
            criticalMsg(QObject::tr("An error occurred while opening file: ") + updPath + VERSION_FILE);
            return false;
        }
        QDataStream in(&newVerFile);
        in.setVersion(QDataStream::Qt_5_9);
        quint32 magic;
        in >> magic;
        if (magic != MAGIC_NUMBER) {
            newVerFile.close();
            criticalMsg(QObject::tr("Invalid version or corrupted file: ") + updPath + VERSION_FILE);
            return false;
        }
        int mapSize;
        in >> updFilesMap;
        in >> mapSize;
        if (updFilesMap.isEmpty() || updFilesMap.size() != mapSize) {
            newVerFile.close();
            criticalMsg(QObject::tr("Invalid or corrupted file: ") + updPath + VERSION_FILE);
            return false;
        }
        newVerFile.close();
    }

    // Read current version control file
    QMap<QString, QString> appFilesMap;
    {
        QFile currVerFile(appPath + VERSION_FILE);
        if (!currVerFile.open(QFile::ReadOnly)) {
            criticalMsg(QObject::tr("An error occurred while opening file: ") + appPath + VERSION_FILE);
            return false;
        }
        QDataStream in(&currVerFile);
        in.setVersion(QDataStream::Qt_5_9);
        quint32 magic;
        in >> magic;
        if (magic != MAGIC_NUMBER) {
            currVerFile.close();
            criticalMsg(QObject::tr("Invalid or corrupted file: ") + appPath + VERSION_FILE);
            return false;
        }
        in >> appFilesMap;
        currVerFile.close();
    }  

    // Create a list of files to delete
    {
        const QString delListFilePath = updPath + DELETE_LIST;
        QFile delListFile(delListFilePath);
        if (!delListFile.open(QFile::WriteOnly)) {
            criticalMsg(QObject::tr("Can't create file: ") + delListFilePath);
            return false;
        }
        QTextStream out(&delListFile);
        foreach (auto &appFile, appFilesMap.keys()) {
            if (!updFilesMap.contains(appFile) && appFile != DAEMON_NAME)
                out << appFile << "\n";
        }
        delListFile.close();
    }

    // Create a list of files to replacement
    QVector<QString> replaceVec;
    {        
        const QString repListFilePath = updPath + REPLACEMENT_LIST;
        QFile repListFile(repListFilePath);
        if (!repListFile.open(QFile::WriteOnly)) {
            criticalMsg(QObject::tr("Can't create file: ") + repListFilePath);
            return false;
        }
        QTextStream out(&repListFile);
        foreach (auto &updFile, updFilesMap.keys()) {
            if (appFilesMap.contains(updFile)) {
                auto updFileHash = updFilesMap.value(updFile);
                if (updFileHash.isEmpty() || updFileHash != appFilesMap.value(updFile)) {
                    out << updFile << "\n";
                    replaceVec.push_back(updFile);
                }
            } else {
                out << updFile << "\n";
                replaceVec.push_back(updFile);
            }
        }
        repListFile.close();
    }

    // Extract files needed for replacement
    if (!extractFiles(zipFilePath, updPath, &replaceVec))
        return false;

    // Сreate a file about successful unpacking for use in subsequent launches
    QFile successFile(updPath + SUCCES_UNPACKED);
    if (!successFile.open(QFile::WriteOnly)) {
        criticalMsg(QObject::tr("An error occurred while creating success unpack file!"));
        return false;
    }
    if (successFile.write(version.toUtf8()) == -1) {
        successFile.close();
        return false;
    }
    successFile.close();
    return true;
}

auto runProcess(const WCHAR *fileName, WCHAR *args)->BOOL
{
//    PROCESS_INFORMATION ProcessInfo;
//    STARTUPINFO StartupInfo;
//    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
//    StartupInfo.cb = sizeof(StartupInfo);
//    if (CreateProcessW(fileName, args, NULL, NULL, FALSE,
//                       CREATE_NO_WINDOW, NULL, NULL,
//                       &StartupInfo, &ProcessInfo)) {
//        //WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
//        CloseHandle(ProcessInfo.hThread);
//        CloseHandle(ProcessInfo.hProcess);
//        return TRUE;
//    }
    SHELLEXECUTEINFO shExInfo = {0};
    shExInfo.cbSize = sizeof(shExInfo);
    shExInfo.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE | SEE_MASK_FLAG_NO_UI;
    shExInfo.hwnd = NULL;
    shExInfo.lpVerb = L"runas";
    shExInfo.lpFile = fileName;
    shExInfo.lpParameters = args;
    shExInfo.lpDirectory = NULL;
    shExInfo.nShow = SW_HIDE;
    shExInfo.hInstApp = NULL;
    if (ShellExecuteEx(&shExInfo)) {
        //WaitForSingleObject(shExInfo.hProcess, INFINITE);
        CloseHandle(shExInfo.hProcess);
        return TRUE;
    }
    return FALSE;
}

class CUpdateManager::CUpdateManagerPrivate
{
public:
    CUpdateManagerPrivate(CUpdateManager *owner, wstring &url)
    {
        m_pDownloader = new CFileDownloader(url, false);
        m_pDownloader->SetEvent_OnComplete([=](int error) {
            const QString filePath = QString::fromStdWString(m_pDownloader->GetFilePath());
            QMetaObject::invokeMethod(owner, "onCompleteSlot", Qt::QueuedConnection,
                                      Q_ARG(int, error), Q_ARG(QString, filePath));
        });
#ifdef Q_OS_WIN
        m_pDownloader->SetEvent_OnProgress([=](int percent) {
            QMetaObject::invokeMethod(owner, "onProgressSlot", Qt::QueuedConnection, Q_ARG(int, percent));
        });
#endif
    }

    ~CUpdateManagerPrivate()
    {
        delete m_pDownloader, m_pDownloader = nullptr;
    }

    void downloadFile(const wstring &url, const wstring &filePath)
    {
        m_pDownloader->Stop();
        m_pDownloader->SetFileUrl(url, false);
        m_pDownloader->SetFilePath(filePath);
        m_pDownloader->Start(0);
    }

    void stop()
    {
        m_pDownloader->Stop();
    }

private:
    CFileDownloader  * m_pDownloader = nullptr;
};

struct CUpdateManager::PackageData {
    QString     fileName;
    wstring     packageUrl,
                packageArgs;
    void clear() {
        fileName.clear();
        packageUrl.clear();
        packageArgs.clear();
    }
};

struct CUpdateManager::SavedPackageData {
    QByteArray hash;
    QString    version,
               fileName;
};

CUpdateManager::CUpdateManager(QObject *parent):
    QObject(parent),
    m_packageData(new PackageData),
    m_savedPackageData(new SavedPackageData),
    m_checkUrl(L""),
    m_downloadMode(Mode::CHECK_UPDATES),
    m_dialogSchedule(new DialogSchedule(this))
{
    // =========== Set updates URL ============
    auto setUrl = [=] {
        if ( InputArgs::contains(CMD_ARGUMENT_CHECK_URL) ) {
            m_checkUrl = InputArgs::argument_value(CMD_ARGUMENT_CHECK_URL);
        } else m_checkUrl = QString(URL_APPCAST_UPDATES).toStdWString();
    };
#ifdef _WIN32
    GET_REGISTRY_SYSTEM(reg_system)
    if (Utils::getWinVersion() > Utils::WinVer::WinXP && reg_system.value("CheckForUpdates", true).toBool())
        setUrl();
#else
    setUrl();
#endif

    m_appPath = qApp->applicationDirPath();
    if ( !m_checkUrl.empty()) {
        m_pimpl = new CUpdateManagerPrivate(this, m_checkUrl);
#ifdef __linux__
        m_pTimer = new QTimer(this);
        m_pTimer->setSingleShot(false);
        connect(m_pTimer, SIGNAL(timeout()), this, SLOT(checkUpdates()));
#endif
        init();
    }
}

CUpdateManager::~CUpdateManager()
{
    if (m_future_clear.valid())
        m_future_clear.wait();
    if (m_future_unzip.valid())
        m_future_unzip.wait();
    delete m_packageData, m_packageData = nullptr;
    delete m_savedPackageData, m_savedPackageData = nullptr;
    delete m_dialogSchedule, m_dialogSchedule = nullptr;
    delete m_pimpl, m_pimpl = nullptr;
}

void CUpdateManager::onCompleteSlot(const int error, const QString &filePath)
{
    if (error == 0) {
        switch (m_downloadMode) {
        case Mode::CHECK_UPDATES:
            onLoadCheckFinished(filePath);
            break;
#ifdef Q_OS_WIN
        case Mode::DOWNLOAD_UPDATES:
            onLoadUpdateFinished(filePath);
            break;
#endif
        default: break;
        }
    } else
    if (error == 1) {
        auto wgt = QApplication::activeWindow();
        if (wgt && wgt->objectName() == "MainWindow" && !wgt->isMinimized())
            CMessage::warning(wgt, tr("Server connection error!"));
    } else {
        // Pause or Stop
    }
}

void CUpdateManager::init()
{
    bool checkOnStartup = true;
    GET_REGISTRY_USER(reg_user);
    reg_user.beginGroup("Updates");
#ifdef _WIN32
    m_savedPackageData->fileName = reg_user.value("Updates/file", QString()).toString();
    m_savedPackageData->hash = reg_user.value("Updates/hash", QByteArray()).toByteArray();
    m_savedPackageData->version = reg_user.value("Updates/version", QString()).toString();
    reg_user.endGroup();
    checkOnStartup = (getUpdateMode() != UpdateMode::DISABLE);
#else
    m_lastCheck = time_t(reg_user.value("Updates/last_check", 0).toLongLong());
    reg_user.endGroup();
    m_currentRate = getUpdateMode();
    checkOnStartup = (m_currentRate != UpdateInterval::NEVER);
#endif
    if (checkOnStartup) {
        m_pCheckOnStartupTimer = new QTimer(this);
        m_pCheckOnStartupTimer->setSingleShot(true);
        m_pCheckOnStartupTimer->setInterval(CHECK_ON_STARTUP_MS);
        connect(m_pCheckOnStartupTimer, &QTimer::timeout, this, &CUpdateManager::updateNeededCheking);
        m_pCheckOnStartupTimer->start();
    }
}

void CUpdateManager::clearTempFiles(const QString &except)
{
    static bool lock = false;
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
#ifdef _WIN32
    if (except.isEmpty())
        savePackageData();
#endif
}

void CUpdateManager::checkUpdates()
{
    destroyStartupTimer(m_pCheckOnStartupTimer);
    m_newVersion.clear();
#ifdef Q_OS_WIN
    m_packageData->clear();
#else
    m_lastCheck = time(nullptr);
    GET_REGISTRY_USER(reg_user);
    reg_user.beginGroup("Updates");
    reg_user.setValue("Updates/last_check", static_cast<qlonglong>(m_lastCheck));
    reg_user.endGroup();
#endif

    m_downloadMode = Mode::CHECK_UPDATES;
    if (m_pimpl)
        m_pimpl->downloadFile(m_checkUrl, generateTmpFileName(".json").toStdWString());
#ifndef Q_OS_WIN
    QTimer::singleShot(3000, this, [=]() {
        updateNeededCheking();
    });
#endif
}

void CUpdateManager::updateNeededCheking()
{
#ifdef Q_OS_WIN
    checkUpdates();
#else
    if (m_pTimer) {
        m_pTimer->stop();
        int interval = 0;
        const time_t DAY_TO_SEC = 24*3600;
        const time_t WEEK_TO_SEC = 7*24*3600;
        const time_t curr_time = time(nullptr);
        const time_t elapsed_time = curr_time - m_lastCheck;
        switch (m_currentRate) {
        case UpdateInterval::DAY:
            if (elapsed_time > DAY_TO_SEC) {
                checkUpdates();
            } else {
                interval = static_cast<int>(DAY_TO_SEC - elapsed_time);
                m_pTimer->setInterval(interval*1000);
                m_pTimer->start();
            }
            break;
        case UpdateInterval::WEEK:
            if (elapsed_time > WEEK_TO_SEC) {
                checkUpdates();
            } else {
                interval = static_cast<int>(WEEK_TO_SEC - elapsed_time);
                m_pTimer->setInterval(interval*1000);
                m_pTimer->start();
            }
            break;
        case UpdateInterval::NEVER:
        default:
            break;
        }
    }
#endif
}

#ifdef Q_OS_WIN
void CUpdateManager::onProgressSlot(const int percent)
{
    if (m_downloadMode == Mode::DOWNLOAD_UPDATES)
        emit progresChanged(percent);
}

void CUpdateManager::savePackageData(const QByteArray &hash, const QString &version, const QString &fileName)
{
    m_savedPackageData->fileName = fileName;
    m_savedPackageData->hash = hash;
    m_savedPackageData->version = version;
    GET_REGISTRY_USER(reg_user);
    reg_user.beginGroup("Updates");
    reg_user.setValue("Updates/file", fileName);
    reg_user.setValue("Updates/hash", hash);
    reg_user.setValue("Updates/version", version);
    reg_user.endGroup();
}

void CUpdateManager::loadUpdates()
{
    if (m_lock)
        return;
    if (!m_savedPackageData->fileName.isEmpty()
            && m_savedPackageData->fileName.indexOf(currentArch()) != -1
            && m_savedPackageData->version == m_newVersion
            && m_savedPackageData->hash == getFileHash(m_savedPackageData->fileName))
    {
        m_packageData->fileName = m_savedPackageData->fileName;
        unzipIfNeeded();

    } else
    if (!m_packageData->packageUrl.empty()) {
        m_downloadMode = Mode::DOWNLOAD_UPDATES;
        if (m_pimpl)
            m_pimpl->downloadFile(m_packageData->packageUrl, generateTmpFileName(".zip").toStdWString());
    }
}

void CUpdateManager::installUpdates()
{
    GET_REGISTRY_USER(reg_user);
    reg_user.beginGroup("Updates");
    const QString ignored_ver = reg_user.value("Updates/ignored_ver").toString();
    reg_user.endGroup();
    if (ignored_ver != getVersion())
        m_dialogSchedule->addToSchedule("showStartInstallMessage");
}

QString CUpdateManager::getVersion() const
{
    return m_newVersion;
}

void CUpdateManager::onLoadUpdateFinished(const QString &filePath)
{
    m_packageData->fileName = filePath;
    savePackageData(getFileHash(m_packageData->fileName), m_newVersion, m_packageData->fileName);
    unzipIfNeeded();
}

void CUpdateManager::unzipIfNeeded()
{
    if (m_lock)
        return;
    m_lock = true;
    const QString updPath = QDir::tempPath() + UPDATE_PATH;
    auto unzip = [=]()->void {
        if (!unzipArchive(m_packageData->fileName, updPath,
                            m_appPath, m_newVersion)) {
            m_lock = false;
            return;
        }
        m_lock = false;
        QMetaObject::invokeMethod(this->m_dialogSchedule,
                                  "addToSchedule",
                                  Qt::QueuedConnection,
                                  Q_ARG(QString, QString("showStartInstallMessage")));
    };

    QDir updDir(updPath);
    if (!updDir.exists() || updDir.isEmpty()) {
        m_future_unzip = std::async(std::launch::async, unzip);
    } else {
        if (isSuccessUnpacked(updPath + SUCCES_UNPACKED, m_newVersion)) {
            m_lock = false;
            m_dialogSchedule->addToSchedule("showStartInstallMessage");
        } else {
            updDir.removeRecursively();
            m_future_unzip = std::async(std::launch::async, unzip);
        }
    }
}

void CUpdateManager::handleAppClose()
{
    if ( m_restartForUpdate ) {
        GET_REGISTRY_SYSTEM(reg_system)
        wstring filePath = (m_appPath + DAEMON_NAME).toStdWString();
        wstring args = L"/LANG=" + reg_system.value("locale", "en").toString().toStdWString();
        args += L" " + m_packageData->packageArgs;
        if (!runProcess(filePath.c_str(), (wchar_t*)args.c_str())) {
            criticalMsg(QString("Unable to start process: %1").arg(m_appPath + DAEMON_NAME));
        }
    } else
        if (m_pimpl)
            m_pimpl->stop();;
}

void CUpdateManager::scheduleRestartForUpdate()
{
    m_restartForUpdate = true;
}
#endif

void CUpdateManager::setNewUpdateSetting(const QString& _rate)
{
    GET_REGISTRY_USER(reg_user);
#ifdef _WIN32
    reg_user.setValue("autoUpdateMode", _rate);
    int mode = (_rate == "silent") ?
                    UpdateMode::SILENT : (_rate == "ask") ?
                        UpdateMode::ASK : UpdateMode::DISABLE;
    if (mode == UpdateMode::DISABLE)
        destroyStartupTimer(m_pCheckOnStartupTimer);
#else
    reg_user.setValue("checkUpdatesInterval", _rate);
    m_currentRate = (_rate == "never") ?
                UpdateInterval::NEVER : (_rate == "day") ?
                    UpdateInterval::DAY : UpdateInterval::WEEK;
    if (m_currentRate == UpdateInterval::NEVER)
        destroyStartupTimer(m_pCheckOnStartupTimer);
    QTimer::singleShot(3000, this, &CUpdateManager::updateNeededCheking);
#endif
}

void CUpdateManager::cancelLoading()
{
    if (m_lock)
        return;
    AscAppManager::sendCommandTo(0, "updates:checking", QString("{\"version\":\"%1\"}").arg(m_newVersion));
    m_downloadMode = Mode::CHECK_UPDATES;
    if (m_pimpl)
        m_pimpl->stop();
}

void CUpdateManager::skipVersion()
{
    GET_REGISTRY_USER(reg_user);
    reg_user.beginGroup("Updates");
    reg_user.setValue("Updates/ignored_ver", m_newVersion);
    reg_user.endGroup();
}

int CUpdateManager::getUpdateMode()
{
    GET_REGISTRY_USER(reg_user);
#ifdef _WIN32
    const QString mode = reg_user.value("autoUpdateMode", "ask").toString();
    return (mode == "silent") ?
                UpdateMode::SILENT : (mode == "ask") ?
                    UpdateMode::ASK : UpdateMode::DISABLE;
#else
    const QString interval = reg_user.value("checkUpdatesInterval", "day").toString();
    return (interval == "never") ?
                UpdateInterval::NEVER : (interval == "day") ?
                    UpdateInterval::DAY : UpdateInterval::WEEK;
#endif
}

void CUpdateManager::onLoadCheckFinished(const QString &filePath)
{
    QFile jsonFile(filePath);
    if ( jsonFile.open(QIODevice::ReadOnly) ) {
        QByteArray ReplyText = jsonFile.readAll();
        jsonFile.close();

        QJsonDocument doc = QJsonDocument::fromJson(ReplyText);
        QJsonObject root = doc.object();

        bool updateExist = false;
        QString version = root.value("version").toString();

        GET_REGISTRY_USER(reg_user);
        reg_user.beginGroup("Updates");
        const QString ignored_ver = reg_user.value("Updates/ignored_ver").toString();
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
#ifdef Q_OS_WIN
            QJsonObject package = root.value("package").toObject();
# ifdef Q_OS_WIN64
            QJsonValue win = package.value("win_64");
# else
            QJsonValue win = package.value("win_32");
# endif
            QJsonObject win_params = win.toObject();
            QJsonObject archive = win_params.value("archive").toObject();
            m_packageData->packageUrl = archive.value("url").toString().toStdWString();
            //m_packageData->packageUrl = win_params.value("url").toString().toStdWString();
            m_packageData->packageArgs = win_params.value("installArguments").toString().toStdWString();
#endif

            // parse release notes
            QJsonObject release_notes = root.value("releaseNotes").toObject();
            const QString lang = CLangater::getCurrentLangCode() == "ru-RU" ? "ru-RU" : "en-EN";
            QJsonValue changelog = release_notes.value(lang);

            m_newVersion = version;
#ifdef Q_OS_WIN
            if (m_newVersion == m_savedPackageData->version
                    && m_savedPackageData->fileName.indexOf(currentArch()) != -1)
                clearTempFiles(m_savedPackageData->fileName);
            else
#endif
                clearTempFiles();
            onCheckFinished(false, true, m_newVersion, changelog.toString());
        } else {
            clearTempFiles();
            onCheckFinished(false, false, "", "");
        }
    } else {
        onCheckFinished(true, false, "", "Error receiving updates...");
    }
}

void CUpdateManager::onCheckFinished(bool error, bool updateExist, const QString &version, const QString &changelog)
{
    Q_UNUSED(changelog);
    if (!error && updateExist) {
        AscAppManager::sendCommandTo(0, "updates:checking", QString("{\"version\":\"%1\"}").arg(version));
#ifdef Q_OS_WIN
        switch (getUpdateMode()) {
        case UpdateMode::SILENT:
            loadUpdates();
            break;
        case UpdateMode::ASK:
            m_dialogSchedule->addToSchedule("showUpdateMessage");
            break;
        }
#else
        m_dialogSchedule->addToSchedule("showUpdateMessage");
#endif
    } else
    if (!error && !updateExist) {
        AscAppManager::sendCommandTo(0, "updates:checking", "{\"version\":\"no\"}");
    } else
    if (error) {
        //qDebug() << "Error while loading check file...";
    }
}

void CUpdateManager::showUpdateMessage(QWidget *parent) {
# ifdef _WIN32
    int result = WinDlg::showDialog(parent,
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
        break;
    }
# else
    CMessage mbox(mainWindow()->handle(), CMessageOpts::moButtons::mbYesDefSkipNo);
    switch (mbox.info(tr("Do you want to install a new version %1 of the program?").arg(version))) {
    case MODAL_RESULT_CUSTOM + 0:
        QDesktopServices::openUrl(QUrl(DOWNLOAD_PAGE, QUrl::TolerantMode));
        break;
    case MODAL_RESULT_CUSTOM + 1: {
        skipVersion();
        AscAppManager::sendCommandTo(0, "updates:checking", "{\"version\":\"no\"}");
        break;
    }
    default:
        break;
    }
# endif
}

#ifdef Q_OS_WIN
void CUpdateManager::showStartInstallMessage(QWidget *parent)
{
    AscAppManager::sendCommandTo(0, "updates:download", "{\"progress\":\"done\"}");
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
    }
}
#endif

#include "cupdatemanager.moc"
