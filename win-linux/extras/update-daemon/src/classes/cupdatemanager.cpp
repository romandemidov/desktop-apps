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
//#include <QApplication>
//#include <QSettings>
//#include <QDir>
//#include <QDirIterator>
//#include <QDataStream>
//#include <QUuid>
//#include <QJsonDocument>
//#include <QJsonObject>
//#include <QRegularExpression>
//#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <iostream>
#include <functional>
#include <vector>
#include "utils.h"
//#include "defines.h"
#include "version.h"
//#include "clangater.h"
//#include "components/cmessage.h"
#include "classes/cdownloader.h"
//#include "cascapplicationmanagerwrapper.h"
//# include <QCryptographicHash>
# include <Windows.h>
//# include "OfficeUtils.h"
//# include "platform_win/updatedialog.h"
# define DAEMON_NAME "/update-daemon.exe"
# define TEMP_DAEMON_NAME "/~update-daemon.exe"
//# define DELETE_LIST "/.delete_list.lst"
# define REPLACEMENT_LIST "/.replacement_list.lst"
# define SUCCES_UNPACKED L"/.success_unpacked"

//#define CHECK_DIRECTORY
#define SENDER_PORT   12011
#define RECEIVER_PORT 12010
#define UPDATE_PATH L"/DesktopEditorsUpdates"
#define CMD_ARGUMENT_CHECK_URL L"--updates-appcast-url"
#ifndef URL_APPCAST_UPDATES
# define URL_APPCAST_UPDATES ""
#endif

using std::vector;


auto currentArch()->string
{
#ifdef _WIN64
    return "_x64";
#else
    return "_x86";
#endif
}

auto getFileHash(const wstring &fileName)->string
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
    return string();
}

auto generateTmpFileName(const string &ext)->wstring
{
    const QUuid uuid = QUuid::createUuid();
    const QRegularExpression branches = QRegularExpression("[{|}]+");
    return File::tempPath() + L"/" + TEXT(FILE_PREFIX) +
           uuid.toString().replace(branches, "") + currentArch() + ext;
}

auto isSuccessUnpacked(const wstring &successFilePath, const string &version)->bool
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

auto unzipArchive(const wstring &zipFilePath, const wstring &updPath, const wstring &appPath, const string &version)->bool
{
    if (!File::dirExists(updPath) && !File::makePath(updPath)) {
        criticalMsg(QObject::tr("An error occurred while creating dir: ") + updPath);
        return false;
    }

    // Extract files
    COfficeUtils utils;
    HRESULT res = utils.ExtractToDirectory(zipFilePath.toStdWString(), updPath.toStdWString(), nullptr, 0);
    if (res != S_OK) {
        criticalMsg(QObject::tr("An error occurred while unpacking zip file!"));
        return false;
    }

    auto fillSubpathVector = [](const QString &path, QVector<QString> &vec) {
        QStringList filters{"*.*"};
        QDirIterator it(path, filters, QDir::Files | QDir::NoSymLinks | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString subPath = it.next().mid(path.length());
            vec.push_back(std::move(subPath));
        }
    };

    vector<wstring> updVec, appVec;
    fillSubpathVector(appPath, appVec);
    fillSubpathVector(updPath, updVec);

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
        const QString repListFilePath = updPath + REPLACEMENT_LIST;
        QFile repListFile(repListFilePath);
        if (!repListFile.open(QFile::WriteOnly)) {
            criticalMsg(QObject::tr("Can't create file: ") + repListFilePath);
            return false;
        }
        QTextStream out(&repListFile);
        foreach (auto &updFile, updVec) {
            int ind = appVec.indexOf(updFile);
            if (ind != -1) {
                auto updFileHash = getFileHash(updPath + updFile);
                if (updFileHash.isEmpty() || updFileHash != getFileHash(appPath + appVec[ind]))
                    out << updFile << "\n";

            } else
                out << updFile << "\n";
        }
        repListFile.close();
    }   

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

class CUpdateManager::CUpdateManagerPrivate
{
public:
    CUpdateManagerPrivate(CUpdateManager *owner, wstring &url)
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

    ~CUpdateManagerPrivate()
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
    wstring     fileName;
    wstring     packageUrl,
                packageArgs;
    void clear() {
        fileName.clear();
        packageUrl.clear();
        packageArgs.clear();
    }
};

struct CUpdateManager::SavedPackageData {
    string     hash;
    string     version;
    wstring    fileName;
};

CUpdateManager::CUpdateManager(CObject *parent):
    CObject(parent),
    m_packageData(new PackageData),
    m_savedPackageData(new SavedPackageData),
    m_checkUrl(L""),
    m_downloadMode(Mode::CHECK_UPDATES),
    m_socket(new CSocket(SENDER_PORT, RECEIVER_PORT))
{
    m_socket->onMessageReceived([](void *data, size_t size) {
        //qDebug() << "Received data:" << QString::fromUtf8((const char*)data, size);
    });
    // =========== Set updates URL ============
    auto setUrl = [=] {
        if ( InputArgs::contains(CMD_ARGUMENT_CHECK_URL) ) {
            m_checkUrl = InputArgs::argument_value(CMD_ARGUMENT_CHECK_URL);
        } else
            m_checkUrl = TEXT(URL_APPCAST_UPDATES);
    };
    GET_REGISTRY_SYSTEM(reg_system)
    if (Utils::getWinVersion() > Utils::WinVer::WinXP && reg_system.value("CheckForUpdates", true).toBool())
        setUrl();

    m_appPath = File::appPath();
    bool isDirectoryValid = true;
#ifdef CHECK_DIRECTORY
    if (QFileInfo(m_appPath).baseName() != QString(REG_APP_NAME)) {
        isDirectoryValid = false;
        QTimer::singleShot(2000, this, [] {
            criticalMsg(tr("This folder configuration does not allow for "
                           "updates! The folder name should be: ") + QString(REG_APP_NAME));
        });
    }
#endif
    if (!m_checkUrl.empty() && isDirectoryValid) {
        m_pimpl = new CUpdateManagerPrivate(this, m_checkUrl);
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
        auto wgt = QApplication::activeWindow();
        if (wgt && wgt->objectName() == "MainWindow" && !wgt->isMinimized())
            CMessage::warning(wgt, tr("Server connection error!"));
    } else {
        // Pause or Stop
    }
}

void CUpdateManager::init()
{
    GET_REGISTRY_USER(reg_user);
    reg_user.beginGroup("Updates");
    m_savedPackageData->fileName = reg_user.value("Updates/file", QString()).toString();
    m_savedPackageData->hash = reg_user.value("Updates/hash", QByteArray()).toByteArray();
    m_savedPackageData->version = reg_user.value("Updates/version", QString()).toString();
    reg_user.endGroup();
    if (getUpdateMode() != UpdateMode::DISABLE) {
        m_pCheckOnStartupTimer = new QTimer(this);
        m_pCheckOnStartupTimer->setSingleShot(true);
        m_pCheckOnStartupTimer->setInterval(CHECK_ON_STARTUP_MS);
        connect(m_pCheckOnStartupTimer, &QTimer::timeout, this, &CUpdateManager::updateNeededCheking);
        m_pCheckOnStartupTimer->start();
    }
}

void CUpdateManager::clearTempFiles(const wstring &except)
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
    if (except.empty())
        savePackageData();
}

void CUpdateManager::checkUpdates()
{
    destroyStartupTimer(m_pCheckOnStartupTimer);
    m_newVersion.clear();
    m_packageData->clear();

    m_downloadMode = Mode::CHECK_UPDATES;
    if (m_pimpl)
        m_pimpl->downloadFile(m_checkUrl, generateTmpFileName(".json").toStdWString());
}

void CUpdateManager::updateNeededCheking()
{
    checkUpdates();
}

void CUpdateManager::onProgressSlot(const int percent)
{
    if (m_downloadMode == Mode::DOWNLOAD_UPDATES)
        emit progresChanged(percent);
}

void CUpdateManager::savePackageData(const QByteArray &hash, const string &version, const wstring &fileName)
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
    if (!m_savedPackageData->fileName.empty()
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
    const string ignored_ver = reg_user.value("Updates/ignored_ver").toString();
    reg_user.endGroup();
    if (ignored_ver != getVersion())
        m_dialogSchedule->addToSchedule("showStartInstallMessage");
}

string CUpdateManager::getVersion() const
{
    return m_newVersion;
}

void CUpdateManager::onLoadUpdateFinished(const wstring &filePath)
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
    const wstring updPath = File::tempPath() + UPDATE_PATH;
    auto unzip = [=]()->void {
        if (!unzipArchive(m_packageData->fileName, updPath,
                            m_appPath, m_newVersion)) {
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
            m_dialogSchedule->addToSchedule("showStartInstallMessage");
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
        wstring filePath = (m_appPath + DAEMON_NAME).toStdWString();
        wstring args = L"/LANG=" + reg_system.value("locale", "en").toString().toStdWString();
        args += L" " + m_packageData->packageArgs;
        if (!runProcess(filePath.c_str(), (wchar_t*)args.c_str())) {
            criticalMsg(QString("Unable to start process: %1").arg(m_appPath + DAEMON_NAME));
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
                        UpdateMode::ASK : UpdateMode::DISABLE;*/
    if (mode == UpdateMode::DISABLE)
        destroyStartupTimer(m_pCheckOnStartupTimer);
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
}

void CUpdateManager::onLoadCheckFinished(const wstring &filePath)
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
    }
}

void CUpdateManager::onCheckFinished(bool error, bool updateExist, const string &version, const string &changelog)
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
