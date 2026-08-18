// XCP instrumentation, and everything that depends on it
//
// XCP gives real-time observation of the raw signals at task rate, plus live
// calibration. It is optional: build without OPTION_XCP for a plain MQTT node,
// and no XCPlite code is compiled or linked at all.
//
// The application keeps only the instrumentation that has to sit next to what
// it describes: XCP_COMMENT / XCP_UNIT annotate a specific variable,
// DaqCreateEvent / DaqTriggerEvent mark the measurement points inside the
// tasks, and CalSegDeclRef declares the calibration segment the tasks read.
// Everything else -- server startup, the EPK, the DAQ clock, status reporting
// -- lives in xcp.cpp. Without OPTION_XCP the macros below become no-ops, so
// the application code is identical in both builds.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef OPTION_XCP

// The libxcplite C++ API, which defines XCP_COMMENT, XCP_UNIT, CalSegDeclRef,
// DaqCreateEvent, DaqTriggerEvent, DaqCreateAndTriggerEvent and
// xcp_get_frame_addr used by the application.
#include "xcplib.hpp"

// Human-readable part of the EPK; bump for a release. extra_script.py appends a
// hash of the build inputs, so the EPK changes whenever addresses can have
// moved. See the EPK section in README.md.
#define XCP_PROJECT_VERSION "V100"

#else

//----------------------------------------------------------------------------------------------------
// No-op instrumentation for builds without XCP

// static_assert rather than an empty expansion, so the trailing semicolon at
// file scope is consumed by a real declaration.
#define XCP_COMMENT(name, comment) static_assert(true, "")
#define XCP_UNIT(name, unit) static_assert(true, "")
#define XCP_LIMITS(name, min, max) static_assert(true, "")
#define XCP_READ_WRITE(name) static_assert(true, "")

#define DaqCreateEvent(name)                                                                                                                                                       \
    do {                                                                                                                                                                           \
    } while (0)
#define DaqTriggerEvent(name)                                                                                                                                                      \
    do {                                                                                                                                                                           \
    } while (0)
#define DaqCreateAndTriggerEvent(name)                                                                                                                                             \
    do {                                                                                                                                                                           \
    } while (0)

#define xcp_get_frame_addr() ((void *)0)

// Stands in for a calibration segment. Without XCP nothing can modify the
// parameters, so the tasks read the default page directly and no locking is
// needed. The lock()/operator-> shape matches the real handle, so the task code
// does not change.
template <typename T> class XcpCalSegStub {
  public:
    explicit XcpCalSegStub(const T &data) : _data(data) {}

    class Guard {
      public:
        explicit Guard(const T &data) : _data(data) {}
        const T *operator->() const { return &_data; }
        const T &operator*() const { return _data; }

      private:
        const T &_data;
    };

    Guard lock() const { return Guard(_data); }

  private:
    const T &_data;
};

#define CalSegDeclRef(name, handle) static const XcpCalSegStub<struct name> handle(name)

#endif // OPTION_XCP

//----------------------------------------------------------------------------------------------------
// Interface, available in both builds

#ifdef OPTION_XCP

// Start the XCP server: publish the EPK, initialize the protocol layer, install
// the high resolution DAQ clock and start the Ethernet server. Call once, after
// the network is up.
bool xcpInit(void);

// The full EPK, e.g. "V100-1a2b3c4d". Valid before xcpInit().
const char *xcpEpk(void);

// Short status for the display while a tool is attached, or NULL when no XCP
// client is connected. Lets the display report XCP state without knowing XCP.
const char *xcpStatusText(void);

#else

inline bool xcpInit(void) { return true; }
inline const char *xcpEpk(void) { return "no-xcp"; }
inline const char *xcpStatusText(void) { return nullptr; }

#define XCP_COMMENT(a,b)
#define XCP_UNIT(a,b)


#endif
