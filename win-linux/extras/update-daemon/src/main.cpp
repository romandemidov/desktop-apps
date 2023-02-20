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

#include "utils.h"
#include <algorithm>
#include <shlwapi.h>

#define BACKUP_PATH      L"DesktopEditorsBackup"
#define DAEMON_NAME      L"/update-daemon.exe"
#define TEMP_DAEMON_NAME L"/~update-daemon.exe"
#define UPDATE_PATH      L"DesktopEditorsUpdates"
#define APP_LAUNCH_NAME  L"/DesktopEditors.exe"
#define DELETE_LIST      L"/.delete_list.lst"
#define REPLACEMENT_LIST L"/.replacement_list.lst"
#define VERSION_FILE     L"/.version_control.lst"


void restoreFromBackup(const wstring &appPath, const wstring &updPath, const wstring &tmpPath)
{
    // Restore from backup
    if (!replaceFolderContents(tmpPath, appPath))
        showMessage(L"An error occurred while restore files from backup!");
    else
        removeDirRecursively(tmpPath);

    // Restore executable name
    wstring appFileRenamedPath = appPath + TEMP_DAEMON_NAME;
    wstring appFilePath = appPath + DAEMON_NAME;
    if (!replaceFile(appFileRenamedPath, appFilePath))
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

    list<wstring> delList, repList;
    if (!readFile(updPath + DELETE_LIST, delList)
            || !readFile(updPath + REPLACEMENT_LIST, repList))
        return 1;
    repList.push_back(VERSION_FILE);

    // Rename current executable
    wstring appFileRenamedPath = appPath + TEMP_DAEMON_NAME;
    if (!replaceFile(appFilePath, appFileRenamedPath)) {
        showMessage(L"An error occurred while renaming the daemon file!");
        return 1;
    }

    // Replace unused files to Backup
    if (!replaceListOfFiles(delList, appPath, tmpPath)) {
        showMessage(L"An error occurred while replace unused files! Restoring from the backup will start.");
        restoreFromBackup(appPath, updPath, tmpPath);
        return 1;
    }

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
