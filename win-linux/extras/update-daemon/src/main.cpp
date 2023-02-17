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

#include <QCoreApplication>
#include <QProcess>
#include "utils.h"

#define BACKUP_PATH      "/Backup"
#define TEMP_DAEMON_NAME "/~update-daemon.exe"
#define UPDATE_PATH      "/DesktopEditorsUpdates"
#define APP_LAUNCH_NAME  "/DesktopEditors.exe"
#define DELETE_LIST      "/delete_list.lst"
#define REPLACEMENT_LIST "/replacement_list.lst"


int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QString appPath = QCoreApplication::applicationDirPath();

    QString updPath = QFileInfo(appPath).dir().absolutePath() + UPDATE_PATH;
    QDir updDir(updPath);
    if (!updDir.exists()) {
        showMessage("An error occurred while searching Update dir!");
        return 1;
    }

    QString tmpPath = QFileInfo(appPath).dir().absolutePath() + BACKUP_PATH;
    QDir tmpDir(tmpPath);
    if (tmpDir.exists() && !tmpDir.isEmpty())
        tmpDir.removeRecursively();
    if (!tmpDir.mkpath(tmpPath)) {
        showMessage("An error occurred while creating Backup dir!");
        return 1;
    }

    QStringList delList, repList;
    if (!readFile(updPath + DELETE_LIST, delList)
            || !readFile(updPath + REPLACEMENT_LIST, repList))
        return 1;

    // Rename current executable
    QString appFilePath = QCoreApplication::applicationFilePath();
    QString appFileRenamedPath = appPath + TEMP_DAEMON_NAME;
    if (!QFile::rename(appFilePath, appFileRenamedPath)) {
        showMessage("An error occurred while renaming the daemon file!");
        return 1;
    }   

    // Replace unused files to Backup
    if (!replaceListOfFiles(delList, appPath, tmpPath)) {
        showMessage(QString("An error occurred while replace unused files! Restoring from the backup will start."));

        // Restore from backup
        if (!replaceFolderContents(tmpPath, appPath))
            showMessage(QString("An error occurred while restore files from backup!"));
        else
            tmpDir.removeRecursively();

        // Restore executable name
        if (!QFile::rename(appFileRenamedPath, appFilePath))
            showMessage("An error occurred while restore daemon file name!");

        updDir.removeRecursively();
        return 1;
    }

    // Move update files to app path
    if (!replaceListOfFiles(repList, updPath, appPath, tmpPath)) {
        showMessage(QString("An error occurred while copy files! Restoring from the backup will start."));

        // Remove new update-daemon.exe if exist
        if (QFile::exists(appFilePath))
            QFile::remove(appFilePath);

        // Restore from backup
        if (!replaceFolderContents(tmpPath, appPath))
            showMessage(QString("An error occurred while restore files from backup!"));
        else
            tmpDir.removeRecursively();

        // Restore executable name
        if (!QFile::rename(appFileRenamedPath, appFilePath))
            showMessage("An error occurred while restore daemon file name!");

        updDir.removeRecursively();
        return 1;
    }

    // Remove Update and Temp dirs
    updDir.removeRecursively();
    tmpDir.removeRecursively();

    // Restart program
    if (!QProcess::startDetached(appPath + APP_LAUNCH_NAME, {}, appPath))
        showMessage("An error occurred while restarting the program!");

    return 0;
}
