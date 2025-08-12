#pragma once

// Call this from your tray app to switch inputs via the LG alt I2C path.
// Returns 0 on success, non-zero on failure.
extern "C" int SetVcpFeatureWithI2cAddr(
    int adapterIdx,
    int displayIdx,
    unsigned short vcpCode,     // Ignored for LG input switching; must be 0xF4 per original code
    unsigned int valueHex,      // e.g., 0xD0 (DP), 0xD1 (USB-C), 0x90 (HDMI1), 0x91 (HDMI2)
    unsigned int i2cSubaddress  // 0x50 for LG "alt" path
);
