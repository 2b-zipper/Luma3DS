/*
*   This file is part of Luma3DS
*   Copyright (C) 2016-2020 Aurora Wright, TuxSH
*
*   This program is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   This program is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
*       * Requiring preservation of specified reasonable legal notices or
*         author attributions in that material or in the Appropriate Legal
*         Notices displayed by works containing it.
*       * Prohibiting misrepresentation of the origin of that material,
*         or requiring that modified versions of such material be marked in
*         reasonable ways as different from the original version.
*/
#include <string.h>

#include "svc/SendSyncRequest.h"
#include "svc/TranslateHandle.h"
#include "ipc.h"

static inline bool isNdmuWorkaround(const SessionInfo *info, u32 pid)
{
    return info != NULL && strcmp(info->name, "ndm:u") == 0 && hasStartedRosalinaNetworkFuncsOnce && pid >= nbSection0Modules;
}

Result SendSyncRequestHook(Handle handle)
{
    KProcess *currentProcess = currentCoreContext->objectContext.currentProcess;
    KProcessHandleTable *handleTable = handleTableOfProcess(currentProcess);
    u32 pid = idOfProcess(currentProcess);
    KClientSession *clientSession = (KClientSession *)KProcessHandleTable__ToKAutoObject(handleTable, handle);

    u32 *cmdbuf = (u32 *)((u8 *)currentCoreContext->objectContext.currentThread->threadLocalStorage + 0x80);
    bool skip = false;
    Result res = 0;

     // not the exact same test but it should work
    bool isValidClientSession = clientSession != NULL && strcmp(classNameOfAutoObject(&clientSession->syncObject.autoObject), "KClientSession") == 0;

    if(isValidClientSession)
    {
        switch (cmdbuf[0])
        {
            case 0x10042:
            {
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession);
                if(isNdmuWorkaround(info, pid))
                {
                    cmdbuf[0] = 0x10040;
                    cmdbuf[1] = 0;
                    skip = true;
                }

                break;
            }

            case 0x10082:
            {
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession);
                if(info != NULL && (strcmp(info->name, "cfg:u") == 0 || strcmp(info->name, "cfg:s") == 0 || strcmp(info->name, "cfg:i") == 0)) // GetConfigInfoBlk2
                    skip = doLangEmu(&res, cmdbuf);

                break;
            }

            case 0x10800:
            {
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession);
                if(info != NULL && strcmp(info->name, "err:f") == 0) // Throw
                    skip = doErrfThrowHook(cmdbuf);

                break;
            }

            case 0x20000:
            {
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession);
                if(info != NULL && (strcmp(info->name, "cfg:u") == 0 || strcmp(info->name, "cfg:s") == 0 || strcmp(info->name, "cfg:i") == 0)) // SecureInfoGetRegion
                    skip = doLangEmu(&res, cmdbuf);

                break;
            }

            case 0x20002:
            {
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession);
                if(isNdmuWorkaround(info, pid))
                {
                    cmdbuf[0] = 0x20040;
                    cmdbuf[1] = 0;
                    skip = true;
                }

                break;
            }

            case 0x50100:
            {
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession);
                if(info != NULL && (strcmp(info->name, "srv:") == 0 || (GET_VERSION_MINOR(kernelVersion) < 39 && strcmp(info->name, "srv:pm") == 0)))
                {
                    char name[9] = { 0 };
                    memcpy(name, cmdbuf + 1, 8);

                    skip = true;
                    res = SendSyncRequest(handle);
                    if(res == 0)
                    {
                        KClientSession *outClientSession;

                        outClientSession = (KClientSession *)KProcessHandleTable__ToKAutoObject(handleTable, (Handle)cmdbuf[3]);
                        if(outClientSession != NULL)
                        {
                            if(strcmp(classNameOfAutoObject(&outClientSession->syncObject.autoObject), "KClientSession") == 0)
                                SessionInfo_Add(outClientSession->parentSession, name);
                            outClientSession->syncObject.autoObject.vtable->DecrementReferenceCount(&outClientSession->syncObject.autoObject);
                        }
                    }
                    else
                    {
                        // Prior to 11.0 kernel didn't zero-initialize output handles, and thus
                        // you could accidentaly close things like the KAddressArbiter handle by mistake...
                        cmdbuf[3] = 0;
                    }
                }

                break;
            }

            case 0x80040:
            {
                if(!hasStartedRosalinaNetworkFuncsOnce)
                    break;
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession);
                skip = isNdmuWorkaround(info, pid); // SuspendScheduler
                if(skip)
                    cmdbuf[1] = 0;
                break;
            }

            case 0x90000:
            {
                if(!hasStartedRosalinaNetworkFuncsOnce)
                    break;
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession);
                if(isNdmuWorkaround(info, pid)) // ResumeScheduler
                {
                    cmdbuf[0] = 0x90040;
                    cmdbuf[1] = 0;
                    skip = true;
                }
                break;
            }

            case 0x00C0080: // srv: publishToSubscriber
            {
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession);

                if (info != NULL && strcmp(info->name, "srv:") == 0 && cmdbuf[1] == 0x1002)
                {
                    // Wake up application thread
                    PLG__WakeAppThread();
                    cmdbuf[0] = 0xC0040;
                    cmdbuf[1] = 0;
                    skip = true;
                }
                break;
            }

            case 0x00D0080: // APT:ReceiveParameter
            {
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession);

                if (info != NULL && strncmp(info->name, "APT:", 4) == 0 && cmdbuf[1] == 0x300)
                {
                    res = SendSyncRequest(handle);
                    skip = true;

                    if (res >= 0)
                    {
                        u32 plgStatus = PLG_GetStatus();
                        u32 command = cmdbuf[3];

                        if ((plgStatus == PLG_CFG_RUNNING && command == 3) // COMMAND_RESPONSE
                        || (plgStatus == PLG_CFG_INHOME && (command >= 10 || command <= 12)))  // COMMAND_WAKEUP_BY_EXIT || COMMAND_WAKEUP_BY_PAUSE
                            PLG_SignalEvent(PLG_CFG_HOME_EVENT);
                    }
                }
                break;
            }

            case 0x4010082:
            {
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession);
                if(info != NULL && (strcmp(info->name, "cfg:s") == 0 || strcmp(info->name, "cfg:i") == 0)) // GetConfigInfoBlk4
                    skip = doLangEmu(&res, cmdbuf);

                break;
            }

            case 0x08030204:
            {
               SessionInfo* info = SessionInfo_Lookup(clientSession->parentSession); // OpenFileDirectly
               if (!(info != NULL && strcmp(info->name, "fs:USER") == 0))
                  break;

               if (strcmp((char*)(cmdbuf[12] + 12), "logo") != 0)
                  break;

               static const char* sdPath = "/luma/logo.bin";

               u32 origBuf[12];
               memcpy(origBuf, cmdbuf, 12 * sizeof(u32));
               char origPath[0x14];
               memcpy(origPath, (char*)cmdbuf[12], 0x14);

               cmdbuf[2] = 9; // ArchiveId to SDMC
               cmdbuf[3] = 1; // ArchivePathType to EMPTY
               cmdbuf[5] = 3; // FilePathType to ASCII
               strcpy((char*)cmdbuf[12], sdPath); // Replace FilePathData

               res = SendSyncRequest(handle);
               if (cmdbuf[1] != 0) { // File doesn't exist, restore original parameters
                  memcpy(cmdbuf, origBuf, 12 * sizeof(u32));
                  memcpy((char*)cmdbuf[12], origPath, 0x14);
                  skip = false;
               }
               else {
                  skip = true;
               }

               break;
            }

            case 0x4020082:
            {
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession);
                if(info != NULL && (strcmp(info->name, "cfg:s") == 0 || strcmp(info->name, "cfg:i") == 0)) // GetConfigInfoBlk8
                    skip = doLangEmu(&res, cmdbuf);

                break;
            }

            case 0x8010082:
            {
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession);
                if(info != NULL && (strcmp(info->name, "cfg:s") == 0 || strcmp(info->name, "cfg:i") == 0)) // GetConfigInfoBlk4
                    skip = doLangEmu(&res, cmdbuf);

                break;
            }

            case 0x8020082:
            {
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession);
                if(info != NULL && strcmp(info->name, "cfg:i") == 0) // GetConfigInfoBlk8
                    skip = doLangEmu(&res, cmdbuf);

                break;
            }

            case 0x4060000:
            {
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession); // SecureInfoGetRegion
                if(info != NULL && (strcmp(info->name, "cfg:s") == 0 || strcmp(info->name, "cfg:i") == 0))
                    skip = doLangEmu(&res, cmdbuf);

                break;
            }

            case 0x8160000:
            {
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession); // SecureInfoGetRegion
                if(info != NULL && strcmp(info->name, "cfg:i") == 0)
                    skip = doLangEmu(&res, cmdbuf);

                break;
            }

            // For plugin watcher
            case 0x8040142: // FSUSER_DeleteFile
            case 0x8070142: // FSUSER_DeleteDirectoryRecursively
            case 0x60084:   // socket connect
            case 0x10040:   // CAMU_StartCapture
            {
                SessionInfo *info = SessionInfo_Lookup(clientSession->parentSession);
                if(info != NULL && (strcmp(info->name, "fs:USER") == 0 || strcmp(info->name, "soc:U") == 0 || strcmp(info->name, "cam:u") == 0))
                {
                    Handle plgLdrHandle;
                    SessionInfo *plgLdrInfo = SessionInfo_FindFirst("plg:ldr");
                    if(plgLdrInfo != NULL && createHandleForThisProcess(&plgLdrHandle, &plgLdrInfo->session->clientSession.syncObject.autoObject) >= 0)
                    {
                        u32 header = cmdbuf[0];
                        u32 cmdbufOrig[8];

                        memcpy(cmdbufOrig, cmdbuf, sizeof(cmdbufOrig));

                        if(strcmp(info->name, "fs:USER") == 0 && (header == 0x8040142 || header == 0x8070142)) // FSUSER_DeleteFile / FSUSER_DeleteDirectoryRecursively
                        {
                            if(cmdbufOrig[4] != 4 || !cmdbufOrig[5] || !cmdbufOrig[7])
                            {
                                CloseHandle(plgLdrHandle);
                                break;
                            }

                            cmdbuf[0] = IPC_MakeHeader(100, 4, 0);
                            cmdbuf[2] = (header == 0x8040142) ? 0 : 1;
                            cmdbuf[3] = cmdbufOrig[7];
                            cmdbuf[4] = cmdbufOrig[5];
                        }
                        else if(strcmp(info->name, "soc:U") == 0 && header == 0x60084) // socket connect
                        {
                            u32 *addr = (u32 *)cmdbuf[6] + 1;
                            if(0x6000000 > (u32)addr || (u32)addr >= 0x8000000)
                            {
                                CloseHandle(plgLdrHandle);
                                break;
                            }

                            cmdbuf[0] = IPC_MakeHeader(100, 3, 0);
                            cmdbuf[2] = 2;
                            cmdbuf[3] = *addr;
                        }
                        else if(strcmp(info->name, "cam:u") == 0 && header == 0x10040) // CAMU_StartCapture
                        {
                            cmdbuf[0] = IPC_MakeHeader(100, 2, 0);
                            cmdbuf[2] = 3;
                        }

                        cmdbuf[1] = pid;
                        
                        if(SendSyncRequest(plgLdrHandle) >= 0)
                            skip = cmdbuf[2];
                            
                        if(!skip)
                            memcpy(cmdbuf, cmdbufOrig, sizeof(cmdbufOrig));
    
                        CloseHandle(plgLdrHandle);
                    }
                }
    
                break;
            }
        }
    }

    if(clientSession != NULL)
        clientSession->syncObject.autoObject.vtable->DecrementReferenceCount(&clientSession->syncObject.autoObject);

    res = skip ? res : SendSyncRequest(handle);

    return res;
}
