//#include "evntrace.h"

#ifndef _TRACE_H_
#define _TRACE_H_


//{93D6B661 - 6D34 - 498C - 89F9 - 6A2536238F79}
#define WPP_CONTROL_GUIDS                                                       \
    WPP_DEFINE_CONTROL_GUID(                                                    \
        Wikdd, (93D6B661, 6D34, 498C, 89F9, 6A2536238F79),                      \
        WPP_DEFINE_BIT(TRACE_FLAG_GENERAL)     /* bit  0 = 0x00000001 */        \
    )

#define WPP_LEVEL_EVENT_LOGGER(level,event) WPP_LEVEL_LOGGER(event)
#define WPP_LEVEL_EVENT_ENABLED(level,event) (WPP_LEVEL_ENABLED(event) && WPP_CONTROL(WPP_BIT_ ## event).Level >= level)

#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) WPP_LEVEL_LOGGER(flags)
#define WPP_LEVEL_FLAGS_ENABLED(lvl,flags) (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level  >= lvl)

#define WPP_LEVEL_FLAGS_STATUS_LOGGER(lvl, flags, status) WPP_LEVEL_LOGGER(flags)
#define WPP_LEVEL_FLAGS_STATUS_ENABLED(lvl, flags, status) WPP_LEVEL_FLAGS_ENABLED(lvl, flags)

#define TMH_STRINGIFYX(x) #x
#define TMH_STRINGIFY(x) TMH_STRINGIFYX(x)

//
// begin_wpp config
//
// Functions for logging driver related events
//
// FUNC WikddLogTrace{LEVEL=TRACE_LEVEL_VERBOSE, FLAGS=TRACE_FLAG_GENERAL}(MSG, ...);
// FUNC WikddLogInfo{LEVEL=TRACE_LEVEL_INFORMATION, FLAGS=TRACE_FLAG_GENERAL}(MSG, ...);
// FUNC WikddLogWarning{LEVEL=TRACE_LEVEL_WARNING, FLAGS=TRACE_FLAG_GENERAL}(MSG, ...);
// FUNC WikddLogError{LEVEL=TRACE_LEVEL_ERROR, FLAGS=TRACE_FLAG_GENERAL}(MSG, ...);
// FUNC WikddLogCritical{LEVEL=TRACE_LEVEL_CRITICAL, FLAGS=TRACE_FLAG_GENERAL}(MSG, ...);
// 
// USEPREFIX (WikddLogApiFailedNt, "%!STDPREFIX!");
// FUNC WikddLogApiFailedNt{LEVEL=TRACE_LEVEL_ERROR, FLAGS=TRACE_FLAG_GENERAL}(STATUS, MSG, ...);
// USESUFFIX (WikddLogApiFailedNt, " failed with status %!STATUS!", STATUS);
//
//
// end_wpp
//

#endif // !__HP_
