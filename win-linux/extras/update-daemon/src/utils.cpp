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
#include <Windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj_core.h>
#include <combaseapi.h>
#include <comutil.h>
#include <oleauto.h>
#include <iostream>
#include <fstream>
#include <regex>
#include <cstdio>
#include <Wincrypt.h>
#include <vector>
#include <sstream>

#define BUFSIZE 1024
#define MD5LEN  16


wstring Utils::GetLastErrorAsString()
{
    DWORD errorMessageID = ::GetLastError();
    if (errorMessageID == 0)
        return L"";

    LPWSTR messageBuffer = NULL;
    size_t size = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                NULL, errorMessageID,
                                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                (LPWSTR)&messageBuffer, 0, NULL);

    wstring message(messageBuffer, (int)size);
    LocalFree(messageBuffer);
    return message;
}

void Utils::ShowMessage(wstring str, bool showError)
{
    if (showError)
        str += L" " + GetLastErrorAsString();
    MessageBoxW(NULL, str.c_str(), TEXT(VER_PRODUCTNAME_STR),
                MB_ICONERROR | MB_SERVICE_NOTIFICATION_NT3X | MB_SETFOREGROUND);
}

bool File::GetFilesList(const wstring &path, list<wstring> *lst, wstring &error)
{
    WCHAR szDir[MAX_PATH];
    wstring searchPath = path + L"/*";
    wcscpy_s(szDir, MAX_PATH, searchPath.c_str());

    WIN32_FIND_DATA ffd;
    HANDLE hFind = FindFirstFile(szDir, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        error = wstring(L"FindFirstFile invalid handle value: ") + szDir;
        return false;
    }

    do {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!wcscmp(ffd.cFileName, L".")
                    || !wcscmp(ffd.cFileName, L".."))
                continue;
            if (!GetFilesList(path + L"/" + wstring(ffd.cFileName), lst, error)) {
                FindClose(hFind);
                return false;
            }
        } else
            lst->push_back(path + L"/" + wstring(ffd.cFileName));

    } while (FindNextFile(hFind, &ffd) != 0);

    if (GetLastError() != ERROR_NO_MORE_FILES) {
        FindClose(hFind);
        error = wstring(L"Error while FindFile: ") + szDir;
        return false;
    }
    FindClose(hFind);
    return true;
}

bool moveSingleFile(const wstring &source,
                    const wstring &dest,
                    const wstring &temp,
                    bool useTmp)
{
    if (File::fileExists(dest)) {
        if (useTmp) {
            // Create a backup
            if (!File::dirExists(File::parentPath(temp)) && !File::makePath(File::parentPath(temp))) {
                Logger::WriteLog(DEFAULT_LOG_FILE, L"Can't create path: " + File::parentPath(temp));
                return false;
            }
            if (!File::replaceFile(dest, temp)) {
                Logger::WriteLog(DEFAULT_LOG_FILE, L"Can't move file from " + dest + L" to " + temp + L". " + Utils::GetLastErrorAsString());
                return false;
            }
        }
    } else {
        if (!File::dirExists(File::parentPath(dest)) && !File::makePath(File::parentPath(dest))) {
            Logger::WriteLog(DEFAULT_LOG_FILE, L"Can't create path: " + File::parentPath(dest));
            return false;
        }
    }

    if (!File::replaceFile(source, dest)) {
        Logger::WriteLog(DEFAULT_LOG_FILE, L"Can't move file from " + source + L" to " + dest + L". " + Utils::GetLastErrorAsString());
        return false;
    }
    return true;
}

bool File::readFile(const wstring &filePath, list<wstring> &linesList)
{
    std::wifstream file(filePath.c_str(), std::ios_base::in);
    if (!file.is_open()) {
        Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while opening: " + filePath);
        return false;
    }
    wstring line;
    while (std::getline(file, line))
        linesList.push_back(line);

    file.close();
    return true;
}

bool File::writeToFile(const wstring &filePath, list<wstring> &linesList)
{
    std::wofstream file(filePath.c_str(), std::ios_base::out);
    if (!file.is_open()) {
        Logger::WriteLog(DEFAULT_LOG_FILE, L"An error occurred while writing: " + filePath);
        return false;
    }
    for (auto &line : linesList)
        file << line << std::endl;

    file.close();
    return true;
}

bool File::replaceListOfFiles(const list<wstring> &filesList,
                        const wstring &fromDir,
                        const wstring &toDir,
                        const wstring &tmpDir)
{
    bool useTmp = !tmpDir.empty() && fromDir != tmpDir && toDir != tmpDir;
    for (const wstring &relFilePath: filesList) {
        if (!relFilePath.empty()) {
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

bool File::replaceFolderContents(const wstring &fromDir,
                           const wstring &toDir,
                           const wstring &tmpDir)
{
    list<wstring> filesList;
    wstring error;
    if (!GetFilesList(fromDir, &filesList, error))
        return false;

    const size_t sourceLength = fromDir.length();
    bool useTmp = !tmpDir.empty() && fromDir != tmpDir && toDir != tmpDir;
    for (const wstring &sourcePath : filesList) {
        if (!sourcePath.empty()) {
            if (!moveSingleFile(sourcePath,
                                toDir + sourcePath.substr(sourceLength),
                                tmpDir + sourcePath.substr(sourceLength),
                                useTmp)) {
                return false;
            }
        }
    }
    return true;
}

bool File::runProcess(const wstring &fileName, const wstring &args)
{
    PROCESS_INFORMATION ProcessInfo;
    STARTUPINFO StartupInfo;
    ZeroMemory(&StartupInfo, sizeof(StartupInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    if (CreateProcessW(fileName.c_str(), (wchar_t*)args.c_str(),
                       NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL,
                       &StartupInfo, &ProcessInfo)) {
        //WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
        CloseHandle(ProcessInfo.hThread);
        CloseHandle(ProcessInfo.hProcess);
        return true;
    }
    return false;
}

bool File::fileExists(const wstring &filePath)
{
    DWORD attr = ::GetFileAttributes(filePath.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

bool File::dirExists(const wstring &dirName) {
    DWORD attr = ::GetFileAttributes(dirName.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

bool File::dirIsEmpty(const wstring &dirName)
{
    return PathIsDirectoryEmpty(dirName.c_str());
}

bool File::makePath(const wstring &path)
{
    list<wstring> pathsList;
    wstring last_path(path);
    while (!last_path.empty() && !dirExists(last_path)) {
        pathsList.push_front(last_path);
        last_path = parentPath(last_path);
    }
    for (list<wstring>::iterator it = pathsList.begin(); it != pathsList.end(); ++it) {
        if (::CreateDirectory(it->c_str(), NULL) == 0)
            return false;
    }
    return true;
}

bool File::replaceFile(const wstring &oldFilePath, const wstring &newFilePath)
{
    return MoveFileExW(oldFilePath.c_str(), newFilePath.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0 ? true : false;
}

bool File::removeFile(const wstring &filePath)
{
    return DeleteFile(filePath.c_str()) != 0 ? true : false;
}

bool File::removeDirRecursively(const wstring &dir)
{
    WCHAR pFrom[_MAX_PATH + 1];
    swprintf_s(pFrom, sizeof(pFrom)/sizeof(WCHAR), L"%s%c", dir.c_str(), '\0');
    SHFILEOPSTRUCT fop = {
        NULL,
        FO_DELETE,
        pFrom,
        NULL,
        FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT,
        FALSE,
        0,
        NULL
    };
    return (SHFileOperation(&fop) == 0) ? true : false;
}

wstring File::fromNativeSeparators(const wstring &path)
{
    return std::regex_replace(path, std::wregex(L"\\\\"), L"/");
}

wstring File::toNativeSeparators(const wstring &path)
{
    return std::regex_replace(path, std::wregex(L"\\/"), L"\\");
}

wstring File::parentPath(const wstring &path)
{
    wstring::size_type delim = path.find_last_of(L"\\/");
    return (delim == wstring::npos) ? L"" : path.substr(0, delim);
}

wstring File::tempPath()
{
    WCHAR buff[MAX_PATH];
    DWORD res = ::GetTempPathW(MAX_PATH, buff);
    if (res != 0) {
        return fromNativeSeparators(parentPath(wstring(buff)));
    }
    return L"";
}

wstring File::appPath()
{
    WCHAR buff[MAX_PATH];
    DWORD res = ::GetModuleFileName(NULL, buff, MAX_PATH);
    if (res != 0) {
        return fromNativeSeparators(parentPath(wstring(buff)));
    }
    return L"";
}

string File::getFileHash(const wstring &fileName)
{
    //DWORD dwStatus = 0;
    BOOL bResult = FALSE;
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    HANDLE hFile = NULL;
    BYTE rgbFile[BUFSIZE];
    DWORD cbRead = 0;
    //BYTE rgbHash[MD5LEN];
    DWORD cbHash = 0;
    //CHAR rgbDigits[] = "0123456789abcdef";

    hFile = CreateFile(fileName.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        // Error opening file
        //dwStatus = GetLastError();
        return "";
    }

    // Get handle to the crypto provider
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        // CryptAcquireContext failed
        //dwStatus = GetLastError();
        CloseHandle(hFile);
        return "";
    }

    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        // CryptAcquireContext failed
        //dwStatus = GetLastError();
        CloseHandle(hFile);
        CryptReleaseContext(hProv, 0);
        return "";
    }

    while ((bResult = ReadFile(hFile, rgbFile, BUFSIZE, &cbRead, NULL))) {
        if (cbRead == 0)
            break;

        if (!CryptHashData(hHash, rgbFile, cbRead, 0)) {
            // CryptHashData failed
            //dwStatus = GetLastError();
            CryptReleaseContext(hProv, 0);
            CryptDestroyHash(hHash);
            CloseHandle(hFile);
            return "";
        }
    }

    if (!bResult) {
        // ReadFile failed
        //dwStatus = GetLastError();
        CryptReleaseContext(hProv, 0);
        CryptDestroyHash(hHash);
        CloseHandle(hFile);
        return "";
    }

    DWORD cbHashSize = 0, dwCount = sizeof(DWORD);
    if (!CryptGetHashParam( hHash, HP_HASHSIZE, (BYTE*)&cbHashSize, &dwCount, 0)) {
        CryptReleaseContext(hProv, 0);
        CryptDestroyHash(hHash);
        CloseHandle(hFile);
        return "";
    }

    cbHash = MD5LEN;
    std::vector<BYTE> buffer(cbHashSize);
    if (!CryptGetHashParam(hHash, HP_HASHVAL, reinterpret_cast<BYTE*>(&buffer[0]), &cbHashSize, 0)) {
        // CryptGetHashParam failed
        //dwStatus = GetLastError();
        CryptReleaseContext(hProv, 0);
        CryptDestroyHash(hHash);
        CloseHandle(hFile);
        return "";
    }

    std::ostringstream oss;
    for (std::vector<BYTE>::const_iterator it = buffer.begin(); it != buffer.end(); ++it) {
        oss.fill('0');
        oss.width(2);
        oss << std::hex << static_cast<const int>(*it);
    }

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    CloseHandle(hFile);
    return oss.str();
}

bool File::unzipArchive(const wstring &zipFilePath, const wstring &folderPath)
{
    wstring file = toNativeSeparators(zipFilePath);
    wstring path = toNativeSeparators(folderPath);
    _bstr_t lpZipFile(file.c_str());
    _bstr_t lpFolder(path.c_str());

    IShellDispatch *pISD;

    Folder  *pZippedFile = 0L;
    Folder  *pDestination = 0L;

    long FilesCount = 0;
    IDispatch* pItem = 0L;
    FolderItems *pFilesInside = 0L;

    VARIANT Options, OutFolder, InZipFile, Item;
    CoInitialize(NULL);
//    __try {
        if (CoCreateInstance(CLSID_Shell, NULL, CLSCTX_INPROC_SERVER, IID_IShellDispatch, (void**)&pISD) != S_OK) {
            CoUninitialize();
            return false;
        }

        InZipFile.vt = VT_BSTR;
        InZipFile.bstrVal = lpZipFile.GetBSTR();
        pISD->NameSpace(InZipFile, &pZippedFile);
        if (!pZippedFile)
        {
            pISD->Release();
            CoUninitialize();
            return false;
        }

        OutFolder.vt = VT_BSTR;
        OutFolder.bstrVal = lpFolder.GetBSTR();
        pISD->NameSpace(OutFolder, &pDestination);
        if (!pDestination)
        {
            pZippedFile->Release();
            pISD->Release();
            CoUninitialize();
            return false;
        }

        pZippedFile->Items(&pFilesInside);
        if (!pFilesInside)
        {
            pDestination->Release();
            pZippedFile->Release();
            pISD->Release();
            CoUninitialize();
            return false;
        }

        pFilesInside->get_Count(&FilesCount);
        if (FilesCount < 1)
        {
            pFilesInside->Release();
            pDestination->Release();
            pZippedFile->Release();
            pISD->Release();
            CoUninitialize();
            return false;
        }

        pFilesInside->QueryInterface(IID_IDispatch,(void**)&pItem);

        Item.vt = VT_DISPATCH;
        Item.pdispVal = pItem;

        Options.vt = VT_I4;
        Options.lVal = 1024 | 512 | 16 | 4;
        bool retval = pDestination->CopyHere( Item, Options) == S_OK;
        pItem->Release(); pItem = 0L;
        pFilesInside->Release(); pFilesInside = 0L;
        pDestination->Release(); pDestination = 0L;
        pZippedFile->Release(); pZippedFile = 0L;
        pISD->Release(); pISD = 0L;
        CoUninitialize();
        return retval;
//    }
//    __finally
//    {
//        CoUninitialize();
//    }
}

void Logger::WriteLog(const wstring &filePath, const wstring &log)
{   
#ifdef DEBUG_LOG
    std::wofstream file(filePath.c_str(), std::ios::app);
    if (!file.is_open()) {
        return;
    }
    file << log << std::endl;
    file.close();
#endif
}
