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
//#include <algorithm>
//#include <shlwapi.h>
#include <cstdio>
#include "svccontrol.h"
#include "event_message/event_message.h"

using std::string;
using std::to_string;

#define BACKUP_PATH      L"DesktopEditorsBackup"
#define DAEMON_NAME      L"/update-daemon.exe"
#define TEMP_DAEMON_NAME L"/~update-daemon.exe"
#define UPDATE_PATH      L"DesktopEditorsUpdates"
#define APP_LAUNCH_NAME  L"/DesktopEditors.exe"
//#define DELETE_LIST      L"/.delete_list.lst"
#define REPLACEMENT_LIST L"/.replacement_list.lst"

SERVICE_STATUS          gSvcStatus;
SERVICE_STATUS_HANDLE   gSvcStatusHandle;
HANDLE                  gSvcStopEvent = NULL;


VOID WINAPI SvcMain(DWORD argc, LPTSTR *argv);
VOID WINAPI SvcCtrlHandler(DWORD dwCtrl);
DWORD WINAPI SvcWorkerThread(LPVOID lpParam);
VOID ReportSvcStatus(DWORD, DWORD, DWORD);
VOID SvcReportEvent(LPTSTR);

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

void write_txt_file(string file_name, string input) {
    FILE *f = fopen(file_name.c_str(), "a+");
    fprintf(f, "%s\n", input.c_str());
    fclose(f);
}

int __cdecl _tmain (int argc, TCHAR *argv[])
{
    if (lstrcmpi(argv[1], _T("--install")) == 0) {
        SvcControl::SvcInstall();
        if (argv[2])
            SvcControl::DoUpdateSvcDesc(argv[2]);
        return 0;
    } else
    if (lstrcmpi(argv[1], _T("--delete")) == 0) {
        SvcControl::DoDeleteSvc();
        return 0;
    } else
    if (lstrcmpi(argv[1], _T("--start")) == 0) {
        SvcControl::DoStartSvc();
        return 0;
    } else
    if (lstrcmpi(argv[1], _T("--stop")) == 0) {
        SvcControl::DoStopSvc();
        return 0;
    } else
    if (lstrcmpi(argv[1], _T("--enable")) == 0) {
        SvcControl::DoEnableSvc();
        return 0;
    } else
    if (lstrcmpi(argv[1], _T("--disable")) == 0) {
        SvcControl::DoDisableSvc();
        return 0;
    } else
    if (lstrcmpi(argv[1], _T("--description")) == 0) {
        if (argv[2])
            SvcControl::DoUpdateSvcDesc(argv[2]);
        return 0;
    } else
    if (lstrcmpi(argv[1], _T("--update_dacl")) == 0) {
        //SvcControl::DoUpdateSvcDacl(pTrusteeName);
        return 0;
    }

    SERVICE_TABLE_ENTRY DispatchTable[] =
    {
        {(LPTSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)SvcMain},
        {NULL, NULL}
    };

    if (StartServiceCtrlDispatcher(DispatchTable) == 0) {
       Utils::ShowMessage(L"ServiceCtrlDispatcher returned error:", true);
       return GetLastError();
    }

    return 0;
}

VOID WINAPI SvcMain(DWORD argc, LPTSTR *argv)
{
    gSvcStatusHandle = RegisterServiceCtrlHandler(
                            SERVICE_NAME,
                            SvcCtrlHandler);
    if (gSvcStatusHandle == NULL) {
        Utils::ShowMessage(L"RegisterServiceCtrlHandler returned error:", true);
        return;
    }

    // Tell the service controller we are starting
    ZeroMemory(&gSvcStatus, sizeof(gSvcStatus));
    gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    gSvcStatus.dwServiceSpecificExitCode = 0;
    ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);

    // TO_DO: Declare and set any required variables.
    //   Be sure to periodically call ReportSvcStatus() with
    //   SERVICE_START_PENDING. If initialization fails, call
    //   ReportSvcStatus with SERVICE_STOPPED.
    //   Create an event. The control handler function, SvcCtrlHandler,
    //   signals this event when it receives the stop control code.

    OutputDebugString(_T("ServiceMain: Performing Service Start Operations"));

    gSvcStopEvent = CreateEvent(NULL,  // default security attributes
                                TRUE,  // manual reset event
                                FALSE, // not signaled
                                NULL); // no name
    if (gSvcStopEvent == NULL) {
        ReportSvcStatus(SERVICE_STOPPED, GetLastError(), 0);
        Utils::ShowMessage(L"CreateEvent(g_ServiceStopEvent) returned error:", true);
        return;
    }

    // Report running status when initialization is complete.
    ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);

    // Start the thread that will perform the main task of the service
    HANDLE hThread = CreateThread(NULL, 0, SvcWorkerThread, NULL, 0, NULL);
    OutputDebugString(_T("Service: Waiting for Worker Thread to complete"));
    // Wait until worker thread exits effectively signaling that the service needs to stop
    WaitForSingleObject(hThread, INFINITE);
    OutputDebugString(_T("Service: Worker Thread Stop Event signaled"));

    OutputDebugString(_T("Service: Performing Cleanup Operations"));
    //WaitForSingleObject(gSvcStopEvent, INFINITE);
    ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
    CloseHandle(gSvcStopEvent);

    OutputDebugString(_T("Service: Exit"));
}

VOID WINAPI SvcCtrlHandler(DWORD dwCtrl)
{
    switch (dwCtrl) {
    case SERVICE_CONTROL_STOP:
        ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
        // Signal the service to stop.
        SetEvent(gSvcStopEvent);
        ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);
        break;

    case SERVICE_CONTROL_INTERROGATE:
        break;

    default:
        break;
    }
}

DWORD WINAPI SvcWorkerThread(LPVOID lpParam)
{
    OutputDebugString(_T("Service: ServiceWorkerThread: Entry"));
    int i = 0;
    //  Periodically check if the service has been requested to stop
    while (WaitForSingleObject(gSvcStopEvent, 0) != WAIT_OBJECT_0)
    {
        /*
         * Perform main service function here
         */
        write_txt_file("C:\\Program Files\\ONLYOFFICE\\DesktopEditors\\out.txt", "Writing...#" + to_string(i));

        Sleep(3000);
        i++;
    }

    OutputDebugString(_T("Service: ServiceWorkerThread: Exit"));

    return ERROR_SUCCESS;
}

VOID ReportSvcStatus(DWORD dwCurrentState,
                     DWORD dwWin32ExitCode,
                     DWORD dwWaitHint)
{
    static DWORD dwCheckPoint = 1;

    gSvcStatus.dwCurrentState = dwCurrentState;
    gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
    gSvcStatus.dwWaitHint = dwWaitHint;

    if (dwCurrentState == SERVICE_START_PENDING)
        gSvcStatus.dwControlsAccepted = 0;
    else
        gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    if ((dwCurrentState == SERVICE_RUNNING) ||
            (dwCurrentState == SERVICE_STOPPED))
        gSvcStatus.dwCheckPoint = 0;
    else
        gSvcStatus.dwCheckPoint = dwCheckPoint++;

    // Report the status of the service to the SCM.
    if (SetServiceStatus(gSvcStatusHandle, &gSvcStatus) == FALSE) {
        Utils::ShowMessage(L"SetServiceStatus returned error:", true);
    }
}

VOID SvcReportEvent(LPTSTR szFunction)
{
    HANDLE hEventSource;
    LPCTSTR lpszStrings[2];
    TCHAR Buffer[80];

    hEventSource = RegisterEventSource(NULL, SERVICE_NAME);

    if (hEventSource != NULL) {
        StringCchPrintf(Buffer, 80, TEXT("%s failed with %d"), szFunction, GetLastError());

        lpszStrings[0] = SERVICE_NAME;
        lpszStrings[1] = Buffer;

        ReportEvent(hEventSource,        // event log handle
                    EVENTLOG_ERROR_TYPE, // event type
                    0,                   // event category
                    SVC_ERROR,           // event identifier
                    NULL,                // no security identifier
                    2,                   // size of lpszStrings array
                    0,                   // no binary data
                    lpszStrings,         // array of strings
                    NULL);               // no binary data

        DeregisterEventSource(hEventSource);
    }
}
