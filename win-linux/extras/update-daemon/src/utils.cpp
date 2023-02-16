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
#include "version.h"
#include <QDirIterator>
#include <Windows.h>

using std::wstring;


QString GetLastErrorAsString()
{
    DWORD errorMessageID = ::GetLastError();
    if (errorMessageID == 0)
        return QString();

    LPSTR messageBuffer = NULL;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                 FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                 NULL, errorMessageID,
                                 MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                 (LPSTR)&messageBuffer, 0, NULL);

    QString message = QString::fromLatin1(messageBuffer, (int)size);
    LocalFree(messageBuffer);
    return message;
}

bool moveSingleFile(const QString &sourceFilePath,
                    const QString &destFilePath,
                    const QString &tmpFilePath,
                    bool useTmp)
{
    wstring source = sourceFilePath.toStdWString();
    wstring dest = destFilePath.toStdWString();
    if (QFile::exists(destFilePath)) {
        if (useTmp) {
            // Create a backup
            if (!QDir().exists(QFileInfo(tmpFilePath).absolutePath())) {
                if (!QDir().mkpath(QFileInfo(tmpFilePath).absolutePath())) {
                    showMessage(QString("Can't create path %1.").arg(QFileInfo(tmpFilePath).absolutePath()));
                    return false;
                }
            }
            wstring temp = tmpFilePath.toStdWString();
            if ((MoveFileExW(dest.c_str(), temp.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) ) {
                showMessage(QString("Can't move file from %1 to %2. %3").arg(destFilePath, tmpFilePath, GetLastErrorAsString()));
                return false;
            }
        }
    } else {
        if (!QDir().mkpath(QFileInfo(destFilePath).absolutePath())) {
            showMessage(QString("Can't create path %1.").arg(QFileInfo(destFilePath).absolutePath()));
            return false;
        }
    }

    if (MoveFileExW(source.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        showMessage(QString("Can't move file from %1 to %2. %3").arg(sourceFilePath, destFilePath, GetLastErrorAsString()));
        return false;
    }
    return true;
}

void showMessage(const QString& str)
{
    wstring lpText = str.toStdWString();
    MessageBoxW(NULL, lpText.c_str(), TEXT(VER_PRODUCTNAME_STR), MB_ICONWARNING | MB_OK | MB_SETFOREGROUND);
}

bool readFile(const QString &filePath, QStringList &list)
{
    QFile file(filePath);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        showMessage("An error occurred while opening " + filePath);
        return false;
    }
    list = QString(file.readAll()).split('\n');
    file.close();
    return true;
}

bool replaceListOfFiles(const QStringList &list,
                        const QString &fromDir,
                        const QString &toDir,
                        const QString &tmpDir,
                        bool useTmp)
{
    useTmp = useTmp && !tmpDir.isEmpty() && fromDir != tmpDir && toDir != tmpDir;
    foreach (const QString &relFilePath, list) {
        if (!relFilePath.isEmpty()) {
            if (!moveSingleFile(fromDir + relFilePath,
                                toDir + relFilePath,
                                tmpDir + relFilePath,
                                useTmp)) {
                return false;
            }
        }
    }
    return true;
}

bool replaceFolderContents(const QString &fromDir,
                           const QString &toDir,
                           const QString &tmpDir,
                           bool useTmp)
{
    QDirIterator it(fromDir, QDirIterator::Subdirectories);
    const int sourceLength = fromDir.length();
    useTmp = useTmp && !tmpDir.isEmpty() && fromDir != tmpDir && toDir != tmpDir;

    while (it.hasNext()) {
        const QString sourcePath = it.next();
        const auto fileInfo = it.fileInfo();
        if (!fileInfo.isHidden()) { // filters dot and dotdot
            const QString destPath = toDir + sourcePath.mid(sourceLength);
            if (fileInfo.isDir()) {
                if (!QDir().exists(destPath)) {
                    if (!QDir().mkpath(destPath)) {
                        showMessage("Can't create path: " + destPath);
                        return false;
                    }
                }
            } else
            if (fileInfo.isFile()) {
                if (!moveSingleFile(sourcePath,
                                    destPath,
                                    tmpDir + sourcePath.mid(sourceLength),
                                    useTmp)) {
                    return false;
                }
            }
        }
    }
    return true;
}
