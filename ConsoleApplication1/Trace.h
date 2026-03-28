#pragma once
#include <evntrace.h>

// {DFA99882 - 60F9 - 44B1 - BACC - 7FBDE0CC5587}
#define WPP_CONTROL_GUIDS \
 WPP_DEFINE_CONTROL_GUID( \
 MyConsoleAppTraceGuid, (DFA99882,60F9,44B1,BACC,7FBDE0CC5587), \
 WPP_DEFINE_BIT(DBG_INIT) \
)

#define WPP_Flags_LEVEL_LOGGER(Flags, level)                                  \
    WPP_LEVEL_LOGGER(Flags)

#define WPP_Flags_LEVEL_ENABLED(Flags, level)                                 \
    (WPP_LEVEL_ENABLED(Flags) && \
    WPP_CONTROL(WPP_BIT_ ## Flags).Level >= level)

#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_LEVEL_FLAGS_ENABLED(lvl,flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)

// begin_wpp config
// FUNC TraceEvents(LEVEL, FLAGS, MSG, ...);
// end_wpp