// EPK construction, isolated in its own translation unit
//
// epk_generated.h is written into $BUILD_DIR by extra_script.py and holds a
// hash of the build inputs. Keeping it out of every other file means a source
// change recompiles only this unit and relinks, rather than rebuilding
// everything that would otherwise see a changed macro.

#include "epk.h"

#include "xcplib.hpp"

#include "epk_generated.h" // XCP_EPK_BUILD_ID, generated at build time

#define XCP_EPK XCP_PROJECT_VERSION "-" XCP_EPK_BUILD_ID

// XCP_EPK_MAX_LENGTH is 31 characters plus the terminator. A longer EPK would
// be silently truncated, which would break the very A2L match it exists for.
static_assert(sizeof(XCP_EPK) <= 32, "EPK exceeds XCP_EPK_MAX_LENGTH (31 characters)");

const char *xcpEpk(void) { return XCP_EPK; }

void xcpCreateEpk(void) { XcpCreateEpk(XCP_EPK); }
