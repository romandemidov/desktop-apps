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

#ifndef CUPDATEMANAGER_H
#define CUPDATEMANAGER_H

#include "classes/cobject.h"
//#include <ctime>
#include <future>
#include "csocket.h"

using std::string;
using std::wstring;
using std::future;


enum UpdateMode {
    DISABLE=0, SILENT=1, ASK=2
};

class CUpdateManager: public CObject
{
public:
    explicit CUpdateManager(CObject *parent = nullptr);
    ~CUpdateManager();

    void setNewUpdateSetting(const string& _rate);
    void cancelLoading();
    void skipVersion();
    int  getUpdateMode();
    wstring getVersion() const;
    void scheduleRestartForUpdate();
    void handleAppClose();
    void loadUpdates();
    void installUpdates();

private:
    void init();
    void clearTempFiles(const wstring &except = wstring());
    void updateNeededCheking();
    void onLoadCheckFinished(const wstring &filePath);
    void onCheckFinished(bool error, bool updateExist, const wstring &version, const string &changelog);
    void onLoadUpdateFinished(const wstring &filePath);
    void unzipIfNeeded();
    void savePackageData(const string &hash = string(),
                         const wstring &version = wstring(),
                         const wstring &fileName = wstring());

    struct PackageData;
    struct SavedPackageData;
    PackageData      *m_packageData;
    SavedPackageData *m_savedPackageData;

    bool        m_restartForUpdate = false,
                m_lock = false;

    wstring     m_checkUrl,
                m_newVersion;
    int         m_downloadMode;
    future<void> m_future_unzip,
                 m_future_clear;

    CSocket *m_socket = nullptr;
    class CUpdateManagerPrivate;
    CUpdateManagerPrivate *m_pimpl = nullptr;

    enum Mode {
        CHECK_UPDATES=0, DOWNLOAD_CHANGELOG=1, DOWNLOAD_UPDATES=2
    };

    void checkUpdates();
    void progresChanged(const int percent);

private:
    void showUpdateMessage(/*QWidget *parent*/);
    void onCompleteSlot(const int error, const wstring &filePath);
    void showStartInstallMessage(/*QWidget *parent*/);
    void onProgressSlot(const int percent);
};


#endif // CUPDATEMANAGER_H
