#ifndef SVCCONTROL_H
#define SVCCONTROL_H

#include "version.h"
#include <Windows.h>
#include <tchar.h>
#include <strsafe.h>

#define SERVICE_NAME TEXT(VER_PRODUCTNAME_STR)

namespace SvcControl
{
VOID SvcInstall(void);
VOID __stdcall DoStartSvc(void);
VOID __stdcall DoUpdateSvcDacl(LPTSTR pTrusteeName);
VOID __stdcall DoStopSvc(void);
VOID __stdcall DoQuerySvc(void);
VOID __stdcall DoUpdateSvcDesc(LPTSTR szDesc);
VOID __stdcall DoDisableSvc(void);
VOID __stdcall DoEnableSvc(void);
VOID __stdcall DoDeleteSvc(void);
}

#endif
