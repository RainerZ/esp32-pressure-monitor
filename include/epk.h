// EPK — the firmware version string used to match an A2L to the firmware
//
// CANape and xcpclient compare the EPK in the A2L against the EPK reported by
// the target, and warn when they differ. That check is only worth anything if
// the EPK changes whenever the firmware layout can have changed.
//
// XCP_PROJECT_VERSION is the human-readable part; extra_script.py appends a
// hash of the build inputs, so the EPK changes exactly when a source file,
// the vendored library, or the build configuration changes.

#pragma once

// Bump for a release. The build-input hash is appended automatically.
#define XCP_PROJECT_VERSION "V100"

// The full EPK, e.g. "V100-1a2b3c4d". Valid before XcpInit().
const char *xcpEpk(void);

// Place the EPK in the xcp_epk linker section so xcpclient can read it out of
// the ELF. Call once, before XcpInit().
void xcpCreateEpk(void);
