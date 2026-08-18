// XCP server, EPK and DAQ clock
//
// Everything XCP-specific that does not have to live next to the measured data
// is collected here, so pressure_monitor.cpp carries only the instrumentation
// that annotates its own variables and tasks.
//
// The whole file compiles away without OPTION_XCP, and extra_script.py then
// also skips building the XCPlite sources entirely.

#ifdef OPTION_XCP

#include <stdio.h>

#include "esp_timer.h"

#include "xcp.h" 


// Hash of the build inputs, written into $BUILD_DIR by extra_script.py
#include "epk_generated.h"

//----------------------------------------------------------------------------------------------------
// Configuration

#define XCP_PROJECT_NAME "pressure_monitor"
#define XCP_USE_TCP false
#define XCP_SERVER_PORT 5555
#define XCP_QUEUE_SIZE 0 // Fixed by OPTION_QUEUE_32_SIZE in xcplib_rtos_cfg.h for the 32 bit FreeRTOS build, this parameter is ignored
#define XCP_LOG_LEVEL 3  // 3 - Info, 4 - Print XCP commands, 5 - Debug

// The EPK identifies this exact build, so a tool can tell whether an A2L still
// describes the firmware in front of it.
#define XCP_EPK XCP_PROJECT_VERSION "-" XCP_EPK_BUILD_ID

// XCP_EPK_MAX_LENGTH is 31 characters plus the terminator. A longer EPK would
// be silently truncated, which would break the very A2L match it exists for.
static_assert(sizeof(XCP_EPK) <= 32, "EPK exceeds XCP_EPK_MAX_LENGTH (31 characters)");

//----------------------------------------------------------------------------------------------------
// DAQ clock
//
// XCP timestamps every measurement sample. The ESP-IDF high resolution timer is
// a free running 64 bit microsecond counter, which matches the 1 us resolution
// the rtos configuration expects. Declared extern "C" because XCPlite takes
// these as C callbacks.

extern "C" void Clock64_Init(void) {}

extern "C" void Clock64_Update(void) {}

extern "C" uint64_t Clock64_Get(void) { return (uint64_t)esp_timer_get_time(); }

//----------------------------------------------------------------------------------------------------
// Interface

const char *xcpEpk(void) { return XCP_EPK; }

const char *xcpStatusText(void) {
    if (XcpIsDaqRunning()) {
        return "XCP measuring";
    }
    if (XcpIsConnected()) {
        return "XCP connected";
    }
    return nullptr;
}

bool xcpInit(void) {

    XcpSetLogLevel(XCP_LOG_LEVEL);

    // Publish the EPK into the xcp_epk section, then hand the identical string
    // to XcpInit so the runtime GET_ID reply matches what xcpclient reads from
    // the ELF. If those two ever disagree, the A2L match check is meaningless.
    XcpCreateEpk(XCP_EPK);
    printf("EPK = %s\n", XCP_EPK);

    // Initialize XCP protocol layer
    const uint8_t bindAny[4] = {0, 0, 0, 0};
    if (!XcpInit(XCP_PROJECT_NAME, XCP_EPK, XCP_MODE_LOCAL)) {
        printf("XcpInit failed\n");
        return false;
    }

    // Register the high resolution 64-bit clock as XCP DAQ clock
    Clock64_Init();
    ApplXcpRegisterGetClockCallback(Clock64_Get);
    ApplXcpRegisterIdleCallback(Clock64_Update);

    // Initialize XCP Ethernet server
    if (!XcpEthServerInit(bindAny, XCP_SERVER_PORT, XCP_USE_TCP, XCP_QUEUE_SIZE)) {
        printf("XcpEthServerInit failed\n");
        return false;
    }

    return true;
}

#endif // OPTION_XCP
