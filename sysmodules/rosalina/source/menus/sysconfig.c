/*
*   This file is part of Luma3DS
*   Copyright (C) 2016-2021 Aurora Wright, TuxSH
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

#include <3ds.h>
#include "luma_config.h"
#include "menus/sysconfig.h"
#include "menus/config_extra.h"
#include "memory.h"
#include "draw.h"
#include "fmt.h"
#include "utils.h"
#include "ifile.h"
#include "luminance.h"

Menu sysconfigMenu = {
    "System configuration menu",
    {
        { "Control volume", METHOD, .method=&SysConfigMenu_AdjustVolume},
        { "Control Wireless connection", METHOD, .method = &SysConfigMenu_ControlWifi },
        { "Toggle LEDs", METHOD, .method = &SysConfigMenu_ToggleLEDs },
        { "Toggle Wireless", METHOD, .method = &SysConfigMenu_ToggleWireless },
        { "Toggle Power Button", METHOD, .method=&SysConfigMenu_TogglePowerButton },
        { "Toggle power to card slot", METHOD, .method=&SysConfigMenu_ToggleCardIfPower},
        { "Change screen brightness", METHOD, .method = &SysConfigMenu_ChangeScreenBrightness },
        { "Extra Config...", METHOD, .method = &ConfigExtra_DrawDetailedMenu },
        {},
    }
};

bool isConnectionForced = false;
s8 currVolumeSliderOverride = -1;

void SysConfigMenu_ToggleLEDs(void)
{
    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();

    do
    {
        Draw_Lock();
        Draw_DrawMenuFrame("System configuration menu");
        Draw_DrawString(10, 40, COLOR_WHITE, "Press A to toggle, press B to go back.");
        Draw_DrawString(10, 60, COLOR_RED, "WARNING:");
        Draw_DrawString(10, 70, COLOR_WHITE, "  * Entering sleep mode will reset the LED state!");
        Draw_DrawString(10, 80, COLOR_WHITE, "  * LEDs cannot be toggled when the battery is low!");
        Draw_DrawString(10, 100, COLOR_TITLE, "TIP:");
        Draw_DrawString(10, 110, COLOR_WHITE, "  * Press SELECT anywhere in the Rosalina menu\n");
        Draw_DrawString(10, 120, COLOR_WHITE, "    to toggle LEDs!");

        Draw_FlushFramebuffer();
        Draw_Unlock();

        u32 pressed = waitInputWithTimeout(1000);

        if(pressed & KEY_A)
        {
            mcuHwcInit();
            u8 result;
            MCUHWC_ReadRegister(0x28, &result, 1);
            result = ~result;
            MCUHWC_WriteRegister(0x28, &result, 1);
            mcuHwcExit();
        }
        else if(pressed & KEY_B)
            return;
    }
    while(!menuShouldExit);
}

void SysConfigMenu_ToggleWireless(void)
{
    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();

    bool nwmRunning = isServiceUsable("nwm::EXT");

    do
    {
        Draw_Lock();
        Draw_DrawMenuFrame("System configuration menu");
        Draw_DrawString(10, 40, COLOR_WHITE, "Press A to toggle, press B to go back.");

        u8 wireless = (*(vu8 *)((0x10140000 | (1u << 31)) + 0x180));

        if(nwmRunning)
        {
            Draw_DrawString(10, 60, COLOR_WHITE, "Current status:");
            Draw_DrawString(100, 60, (wireless ? COLOR_GREEN : COLOR_RED), (wireless ? " ON " : " OFF"));
            Draw_DrawString(10, 80, COLOR_TITLE, "TIP:");
            Draw_DrawString(10, 90, COLOR_WHITE, "  * Press START anywhere in the Rosalina menu\n");
            Draw_DrawString(10, 100, COLOR_WHITE, "    to toggle Wireless!");
        }
        else
        {
            Draw_DrawString(10, 60, COLOR_RED, "NWM isn't running.");
            Draw_DrawString(10, 70, COLOR_RED, "If you're currently on Test Menu,");
            Draw_DrawString(10, 80, COLOR_RED, "exit then press R+RIGHT to toggle the WiFi.");
            Draw_DrawString(10, 90, COLOR_RED, "Otherwise, simply exit and wait a few seconds.");
        }

        Draw_FlushFramebuffer();
        Draw_Unlock();

        u32 pressed = waitInputWithTimeout(1000);

        if(pressed & KEY_A && nwmRunning)
        {
            nwmExtInit();
            NWMEXT_ControlWirelessEnabled(!wireless);
            nwmExtExit();
        }
        else if(pressed & KEY_B)
            return;
    }
    while(!menuShouldExit);
}

void SysConfigMenu_UpdateStatus(bool control)
{
    MenuItem *item = &sysconfigMenu.items[1];

    if(control)
    {
        item->title = "Control Wireless connection";
        item->method = &SysConfigMenu_ControlWifi;
    }
    else
    {
        item->title = "Disable forced wireless connection";
        item->method = &SysConfigMenu_DisableForcedWifiConnection;
    }
}

static bool SysConfigMenu_ForceWifiConnection(u32 slot)
{
    char ssid[0x20 + 1] = {0};
    isConnectionForced = false;

    if(R_FAILED(acInit()))
        return false;

    acuConfig config = {0};
    ACU_CreateDefaultConfig(&config);
    ACU_SetNetworkArea(&config, 2);
    ACU_SetAllowApType(&config, 1 << slot);
    ACU_SetRequestEulaVersion(&config);

    Handle connectEvent = 0;
    svcCreateEvent(&connectEvent, RESET_ONESHOT);

    bool forcedConnection = false;
    if(R_SUCCEEDED(ACU_ConnectAsync(&config, connectEvent)))
    {
        if(R_SUCCEEDED(svcWaitSynchronization(connectEvent, -1)) && R_SUCCEEDED(ACU_GetSSID(ssid)))
            forcedConnection = true;
    }
    svcCloseHandle(connectEvent);

    if(forcedConnection)
    {
        isConnectionForced = true;
        SysConfigMenu_UpdateStatus(false);
    }
    else
        acExit();

    char infoString[80] = {0};
    u32 infoStringColor = forcedConnection ? COLOR_GREEN : COLOR_RED;
    if(forcedConnection)
        sprintf(infoString, "Succesfully forced a connection to: %s", ssid);
    else
       sprintf(infoString, "Failed to connect to slot %d", (int)slot + 1);

    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();

    do
    {
        Draw_Lock();
        Draw_DrawMenuFrame("System configuration menu");
        Draw_DrawString(10, 40, infoStringColor, infoString);
        Draw_DrawString(10, 50, COLOR_WHITE, "Press B to go back.");

        Draw_FlushFramebuffer();
        Draw_Unlock();

        u32 pressed = waitInputWithTimeout(1000);

        if(pressed & KEY_B)
            break;
    }
    while(!menuShouldExit);

    return forcedConnection;
}

void SysConfigMenu_TogglePowerButton(void)
{
    u32 mcuIRQMask;

    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();

    mcuHwcInit();
    MCUHWC_ReadRegister(0x18, (u8*)&mcuIRQMask, 4);
    mcuHwcExit();

    do
    {
        Draw_Lock();
        Draw_DrawMenuFrame("System configuration menu");
        Draw_DrawString(10, 40, COLOR_WHITE, "Press A to toggle, press B to go back.");

        Draw_DrawString(10, 60, COLOR_WHITE, "Current status:");
        Draw_DrawString(100, 60, (((mcuIRQMask & 0x00000001) == 0x00000001) ? COLOR_RED : COLOR_GREEN), (((mcuIRQMask & 0x00000001) == 0x00000001) ? " DISABLED" : " ENABLED "));

        Draw_FlushFramebuffer();
        Draw_Unlock();

        u32 pressed = waitInputWithTimeout(1000);

        if(pressed & KEY_A)
        {
            mcuHwcInit();
            MCUHWC_ReadRegister(0x18, (u8*)&mcuIRQMask, 4);
            mcuIRQMask ^= 0x00000001;
            MCUHWC_WriteRegister(0x18, (u8*)&mcuIRQMask, 4);
            mcuHwcExit();
        }
        else if(pressed & KEY_B)
            return;
    }
    while(!menuShouldExit);
}

void SysConfigMenu_ControlWifi(void)
{
    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();

    u32 slot = 0;
    char ssids[3][32] = {{0}};

    Result resInit = acInit();
    for (u32 i = 0; i < 3; i++)
    {
        if (R_SUCCEEDED(ACI_LoadNetworkSetting(i)))
            ACI_GetNetworkWirelessEssidSecuritySsid(ssids[i]);
        else
            strcpy(ssids[i], "(not configured)");
    }
    if (R_SUCCEEDED(resInit))
        acExit();

    do
    {
        Draw_Lock();
        Draw_DrawMenuFrame("System configuration menu");
        u32 posY = Draw_DrawString(10, 40, COLOR_WHITE, "Press A to force a connection to slot, B to go back\n\n");

        for (u32 i = 0; i < 3; i++)
        {
            Draw_DrawString(10, posY + SPACING_Y * i, COLOR_TITLE, slot == i ? ">" : " ");
            Draw_DrawFormattedString(30, posY + SPACING_Y * i, COLOR_WHITE, "[%d] %s", (int)i + 1, ssids[i]);
        }

        Draw_FlushFramebuffer();
        Draw_Unlock();

        u32 pressed = waitInputWithTimeout(1000);

        if(pressed & KEY_A)
        {
            if(SysConfigMenu_ForceWifiConnection(slot))
            {
                // Connection successfully forced, return from this menu to prevent ac handle refcount leakage.
                break;
            }

            Draw_Lock();
            Draw_ClearFramebuffer();
            Draw_FlushFramebuffer();
            Draw_Unlock();
        }
        else if(pressed & KEY_DOWN)
        {
            slot = (slot + 1) % 3;
        }
        else if(pressed & KEY_UP)
        {
            slot = (3 + slot - 1) % 3;
        }
        else if(pressed & KEY_B)
            return;
    }
    while(!menuShouldExit);
}

void SysConfigMenu_DisableForcedWifiConnection(void)
{
    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();

    acExit();
    SysConfigMenu_UpdateStatus(true);

    do
    {
        Draw_Lock();
        Draw_DrawMenuFrame("System configuration menu");
        Draw_DrawString(10, 40, COLOR_WHITE, "Forced connection successfully disabled.\nNote: auto-connection may remain broken.");

        u32 pressed = waitInputWithTimeout(1000);
        if(pressed & KEY_B)
            return;
    }
    while(!menuShouldExit);
}

void SysConfigMenu_ToggleCardIfPower(void)
{
    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();

    bool cardIfStatus = false;
    bool updatedCardIfStatus = false;

    do
    {
        Result res = FSUSER_CardSlotGetCardIFPowerStatus(&cardIfStatus);
        if (R_FAILED(res)) cardIfStatus = false;

        Draw_Lock();
        Draw_DrawMenuFrame("System configuration menu");
        u32 posY = Draw_DrawString(10, 40, COLOR_WHITE, "Press A to toggle, press B to go back.\n\n");
        posY = Draw_DrawString(10, posY, COLOR_WHITE, "Inserting or removing a card will reset the status,\nand you'll need to reinsert the cart if you want to\nplay it.\n\n");
        Draw_DrawString(10, posY, COLOR_WHITE, "Current status:");
        Draw_DrawString(100, posY, !cardIfStatus ? COLOR_RED : COLOR_GREEN, !cardIfStatus ? " DISABLED" : " ENABLED ");

        Draw_FlushFramebuffer();
        Draw_Unlock();

        u32 pressed = waitInputWithTimeout(1000);

        if(pressed & KEY_A)
        {
            if (!cardIfStatus)
                res = FSUSER_CardSlotPowerOn(&updatedCardIfStatus);
            else
                res = FSUSER_CardSlotPowerOff(&updatedCardIfStatus);

            if (R_SUCCEEDED(res))
                cardIfStatus = !updatedCardIfStatus;
        }
        else if(pressed & KEY_B)
            return;
    }
    while(!menuShouldExit);
}

static Result SysConfigMenu_ApplyVolumeOverride(void)
{
    // This feature repurposes the functionality used for the camera shutter sound.
    // As such, it interferes with it:
    //     - shutter volume is set to the override instead of its default 100% value
    //     - due to implementation details, having the shutter sound effect play will
    //       make this feature stop working until the volume override is reapplied by
    //       going back to this menu

    // Original credit: profi200

    u8 i2s1Mux;
    u8 i2s2Mux;
    Result res = cdcChkInit();

    if (R_SUCCEEDED(res)) res = CDCCHK_ReadRegisters2(0,  116, &i2s1Mux, 1); // used for shutter sound in TWL mode, and all GBA/DSi/3DS application
    if (R_SUCCEEDED(res)) res = CDCCHK_ReadRegisters2(100, 49, &i2s2Mux, 1); // used for shutter sound in CTR mode and CTR mode library applets

    if (currVolumeSliderOverride >= 0)
    {
        i2s1Mux &= ~0x80;
        i2s2Mux |=  0x20;
    }
    else
    {
        i2s1Mux |=  0x80;
        i2s2Mux &= ~0x20;
    }

    s8 i2s1Volume;
    s8 i2s2Volume;
    if (currVolumeSliderOverride >= 0)
    {
        i2s1Volume = -128 + (((float)currVolumeSliderOverride/100.f) * 108);
        i2s2Volume = i2s1Volume;
    }
    else
    {
        // Restore shutter sound volumes. This sould be sourced from cfg,
        // however the values are the same everwhere
        i2s1Volume =  -3; // -1.5 dB (115.7%, only used by TWL applications when taking photos)
        i2s2Volume = -20; // -10 dB  (100%)
    }

    // Write volume overrides values before writing to the pinmux registers
    if (R_SUCCEEDED(res)) res = CDCCHK_WriteRegisters2(0, 65, &i2s1Volume, 1); // CDC_REG_DAC_L_VOLUME_CTRL
    if (R_SUCCEEDED(res)) res = CDCCHK_WriteRegisters2(0, 66, &i2s1Volume, 1); // CDC_REG_DAC_R_VOLUME_CTRL
    if (R_SUCCEEDED(res)) res = CDCCHK_WriteRegisters2(100, 123, &i2s2Volume, 1);

    if (R_SUCCEEDED(res)) res = CDCCHK_WriteRegisters2(0, 116, &i2s1Mux, 1);
    if (R_SUCCEEDED(res)) res = CDCCHK_WriteRegisters2(100, 49, &i2s2Mux, 1);

    cdcChkExit();
    return res;
}

void SysConfigMenu_LoadConfig(void)
{
    s64 out = -1;
    svcGetSystemInfo(&out, 0x10000, 7);
    currVolumeSliderOverride = (s8)out;
    if (currVolumeSliderOverride >= 0)
        SysConfigMenu_ApplyVolumeOverride();
}

void SysConfigMenu_AdjustVolume(void)
{
    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();

    s8 tempVolumeOverride = currVolumeSliderOverride;
    static s8 backupVolumeOverride = -1;
    if (backupVolumeOverride == -1)
        backupVolumeOverride = tempVolumeOverride >= 0 ? tempVolumeOverride : 85;

    do
    {
        Draw_Lock();
        Draw_DrawMenuFrame("System configuration menu");
        u32 posY = Draw_DrawString(10, 40, COLOR_WHITE, "Y: Toggle volume slider override.\nDPAD/CPAD: Adjust the volume level.\nA: Apply\nB: Go back\n\n");
        Draw_DrawString(10, posY, COLOR_WHITE, "Current status:");
        posY = Draw_DrawString(100, posY, (tempVolumeOverride == -1) ? COLOR_RED : COLOR_GREEN, (tempVolumeOverride == -1) ? " DISABLED" : " ENABLED ");
        if (tempVolumeOverride != -1) {
            posY = Draw_DrawFormattedString(30, posY, COLOR_WHITE, "\nValue: [%d%%]    ", tempVolumeOverride);
        } else {
            posY = Draw_DrawString(30, posY, COLOR_WHITE, "\n                 ");
        }

        Draw_FlushFramebuffer();
        Draw_Unlock();

        u32 pressed = waitInputWithTimeout(1000);

        Draw_Lock();

        if(pressed & KEY_A)
        {
            currVolumeSliderOverride = tempVolumeOverride;
            Result res = SysConfigMenu_ApplyVolumeOverride();
            LumaConfig_SaveSettings();
            if (R_SUCCEEDED(res))
                Draw_DrawString(10, posY, COLOR_GREEN, "\nSuccess!");
            else
                Draw_DrawFormattedString(10, posY, COLOR_RED, "\nFailed: 0x%08lX", res);
        }
        else if(pressed & KEY_B)
            return;
        else if(pressed & KEY_Y)
        {
            Draw_DrawString(10, posY, COLOR_WHITE, "\n                 ");
            if (tempVolumeOverride == -1) {
                tempVolumeOverride = backupVolumeOverride;
            } else {
                backupVolumeOverride = tempVolumeOverride;
                tempVolumeOverride = -1;
            }
        }
        else if ((pressed & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) && tempVolumeOverride != -1)
        {
            Draw_DrawString(10, posY, COLOR_WHITE, "\n                 ");
            if (pressed & KEY_UP)
                tempVolumeOverride++;
            else if (pressed & KEY_DOWN)
                tempVolumeOverride--;
            else if (pressed & KEY_RIGHT)
                tempVolumeOverride+=10;
            else if (pressed & KEY_LEFT)
                tempVolumeOverride-=10;

            if (tempVolumeOverride < 0)
                tempVolumeOverride = 0;
            if (tempVolumeOverride > 100)
                tempVolumeOverride = 100;
        }

        Draw_FlushFramebuffer();
        Draw_Unlock();
    } while(!menuShouldExit);
}

void SysConfigMenu_ChangeScreenBrightness(void)
{
    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();

    u8 sysModel;
    cfguInit();
    CFGU_GetSystemModel(&sysModel);
    cfguExit();
    bool hasTopScreen = (sysModel != 3); // 3 = o2DS

    u32 luminanceTop = getCurrentLuminance(true);
    u32 luminanceBot = getCurrentLuminance(false);
    u32 minLum = getMinLuminancePreset();
    u32 maxLum = getMaxLuminancePreset();
    u32 trueMax = 172;
    u32 trueMin = 6;
    luminanceTop = luminanceTop >= 173 ? trueMax : luminanceTop;

    do
    {
        Draw_Lock();
        Draw_DrawMenuFrame("Screen brightness");
        u32 posY = 40;
        posY = Draw_DrawFormattedString(
            10,
            posY,
            (luminanceTop > maxLum || luminanceBot > maxLum) ? COLOR_RED : COLOR_WHITE,
            "Top: %lu, Bot: %lu\n",
            luminanceTop,
            luminanceBot
        );
        posY = Draw_DrawFormattedString(
            10,
            posY,
            COLOR_WHITE,
            "(min: %lu max: %lu raw: %lu)\n\n",
            minLum,
            maxLum,
            trueMax
        );
        posY = Draw_DrawString(10, posY, COLOR_GREEN, "Controls: \n");
        posY = Draw_DrawString(10, posY, COLOR_WHITE, "Up/Down for +-1, Right/Left for +-10.\n");
        posY = Draw_DrawString(10, posY, COLOR_WHITE, "Hold X/A for Top/Bottom screen only. \n");
        posY = Draw_DrawString(10, posY, COLOR_WHITE, "Hold L or R to use raw max. limit. \n");
        posY = Draw_DrawString(10, posY, COLOR_WHITE, "Press Y to toggle top and bottom backlight.\n\n");
        posY = Draw_DrawString(10, posY, COLOR_TITLE, "Press START to begin, B to exit.\n\n");

        posY = Draw_DrawString(10, posY, COLOR_RED, "WARNING: \n");
        posY = Draw_DrawString(10, posY, COLOR_WHITE, "  * Use over-brighten at your own risk!\n");
        posY = Draw_DrawString(10, posY, COLOR_WHITE, "  * all changes revert after sleep mode.");
        Draw_FlushFramebuffer();
        Draw_Unlock();

        u32 pressed = waitInputWithTimeout(1000);

        if (pressed & KEY_START)
            break;

        if (pressed & KEY_B)
            return;
    }
    while (!menuShouldExit);

    Draw_Lock();

    Draw_RestoreFramebuffer();
    Draw_FreeFramebufferCache();

    svcKernelSetState(0x10000, 2); // unblock gsp
    gspLcdInit(); // assume it doesn't fail. If it does, brightness won't change, anyway.

    // gsp:LCD will normalize the brightness between top/bottom screen, handle PWM, etc.

    s32 lumTop = (s32)luminanceTop;
    s32 lumBot = (s32)luminanceBot;

    do {
        u32 kHeld = HID_PAD;
        u32 pressed = waitInputWithTimeout(1000);
        if (pressed & DIRECTIONAL_KEYS) {
            s32 increment = 0;
            s32 currentMax = (kHeld & (KEY_L | KEY_R)) ? (s32)trueMax : (s32)maxLum;

            if (pressed & KEY_UP)
                increment = 1;
            else if (pressed & KEY_DOWN)
                increment = -1;
            else if (pressed & KEY_RIGHT)
                increment = 10;
            else if (pressed & KEY_LEFT)
                increment = -10;

            if (kHeld & KEY_X) {
                lumTop += increment;
                lumTop = lumTop < (s32)trueMin ? (s32)trueMin : lumTop;
                lumTop = lumTop > currentMax ? currentMax : lumTop;
            } else if (kHeld & KEY_A) {
                lumBot += increment;
                lumBot = lumBot < (s32)trueMin ? (s32)trueMin : lumBot;
                lumBot = lumBot > currentMax ? currentMax : lumBot;
            } else {
                lumTop += increment;
                lumBot += increment;
                lumTop = lumTop < (s32)trueMin ? (s32)trueMin : lumTop;
                lumTop = lumTop > currentMax ? currentMax : lumTop;
                lumBot = lumBot < (s32)trueMin ? (s32)trueMin : lumBot;
                lumBot = lumBot > currentMax ? currentMax : lumBot;
            }

            if ((lumTop < (s32)minLum || lumTop > (s32)maxLum) || (lumBot < (s32)minLum || lumBot > (s32)maxLum)) {
                // This won't work atm, we need to fix the setBrightnessAlt function bug
                //setBrightnessAlt(lumTop, lumBot);
                GSPLCD_SetBrightnessRaw(BIT(GSP_SCREEN_TOP), lumTop);
                GSPLCD_SetBrightnessRaw(BIT(GSP_SCREEN_BOTTOM), lumBot);
            } else {
                if (kHeld & KEY_X)
                    GSPLCD_SetBrightnessRaw(BIT(GSP_SCREEN_TOP), lumTop);
                else if (kHeld & KEY_A)
                    GSPLCD_SetBrightnessRaw(BIT(GSP_SCREEN_BOTTOM), lumBot);
                else {
                    GSPLCD_SetBrightnessRaw(BIT(GSP_SCREEN_TOP), lumTop);
                    GSPLCD_SetBrightnessRaw(BIT(GSP_SCREEN_BOTTOM), lumBot);
                }
            }
        }

        if ((pressed & KEY_Y) && hasTopScreen) {
            u8 result, botStatus, topStatus;
            mcuHwcInit();
            MCUHWC_ReadRegister(0x0F, &result, 1); // https://www.3dbrew.org/wiki/I2C_Registers#Device_3
            mcuHwcExit();
            botStatus = (result >> 5) & 1;
            topStatus = (result >> 6) & 1;

            if (botStatus == 1 && topStatus == 1) {
                GSPLCD_PowerOffBacklight(BIT(GSP_SCREEN_BOTTOM));
            } else if (botStatus == 0 && topStatus == 1) {
                GSPLCD_PowerOnBacklight(BIT(GSP_SCREEN_BOTTOM));
                GSPLCD_PowerOffBacklight(BIT(GSP_SCREEN_TOP));
            } else if (topStatus == 0) {
                GSPLCD_PowerOnBacklight(BIT(GSP_SCREEN_TOP));
            }
        } else if (pressed & KEY_Y) {
            GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTTOM);
        }

        if (pressed & KEY_B)
            break;
    }
    while (!menuShouldExit);

    gspLcdExit();
    svcKernelSetState(0x10000, 2); // block gsp again

    if (R_FAILED(Draw_AllocateFramebufferCache(FB_BOTTOM_SIZE)))
    {
        // Shouldn't happen
        __builtin_trap();
    }
    else
        Draw_SetupFramebuffer();

    Draw_Unlock();
}
