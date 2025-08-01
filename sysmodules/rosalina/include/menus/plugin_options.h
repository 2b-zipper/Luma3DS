#pragma once

#include "menu.h"

extern Menu pluginOptionsMenu;

void PluginLoaderOptions__MenuCallback(void);
void PluginLoaderOptions__UpdateMenu(void);
void PluginChecker__MenuCallback(void);
void PluginChecker__UpdateMenu(void);
void PluginWatcher__MenuCallback(void);
void PluginWatcher__UpdateMenu(void);
void PluginConverter__ToggleUseCacheFlag(void);
void PluginConverter__UpdateMenu(void);

void PluginWatcher_SetWatchLevel(void);