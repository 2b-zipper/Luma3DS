#include "menus/plugin_options.h"
#include "menu.h"
#include "plugin.h"
#include "draw.h"

#include <stdio.h>

extern u32 PluginWatcher_WatchLevel;

Menu pluginOptionsMenu = {
    "Plugin options menu",
    {
        { "", METHOD, .method = &PluginChecker__MenuCallback },
        { "", METHOD, .method = &PluginWatcher__MenuCallback },
        { "Set watch level", METHOD, .method = &PluginWatcher_SetWatchLevel },
        {},
    }
};

void PluginWatcher_SetWatchLevel(void)
{
    static const char *watchOptions[] = {
        "Detects file deletion",
        "Detects directory deletion",
        "Detects internet connections",
        "Detects camera access",
    };

    s32 selected = 0;
    s32 nbOptions = sizeof(watchOptions) / sizeof(const char *);
    u32 *watchLv = &PluginWatcher_WatchLevel;

    do
    {
        u32 posY = 20;

        Draw_Lock();

        Draw_DrawString(10, 10, COLOR_TITLE, "Set watch level");

        for(s32 i = 0; i < nbOptions; i++)
        {
            char buf[256];
            const char *checkbox = (*watchLv & (1 << i)) ? "(x)" : "( )";

            sprintf(buf, "%s %s", checkbox, watchOptions[i]);
            Draw_DrawCharacter(10, posY + SPACING_Y, COLOR_TITLE, i == selected ? '>' : ' ');
            posY = Draw_DrawString(30, posY + SPACING_Y, COLOR_WHITE, buf);
        }

        Draw_FlushFramebuffer();
        Draw_Unlock();

        u32 pressed;
        do
        {
            pressed = waitInputWithTimeout(50);
        } while(pressed == 0 && !menuShouldExit);

        if(pressed & KEY_B)
            break;
        else if(pressed & KEY_A)
            *watchLv ^= (1 << selected);
        else if(pressed & KEY_DOWN)
            selected++;
        else if(pressed & KEY_UP)
            selected--;

        if(selected < 0)
            selected = nbOptions - 1;
        else if(selected >= nbOptions)
            selected = 0;

    } while(!menuShouldExit);

}