#include "amdddc_core.h"
#include "adl.h"
#include <windows.h>
#include <cstring>

// ==== Copied & adapted from your amdddc-windows.cpp ====

#define SETWRITESIZE 8
#define SET_VCPCODE_SUBADDRESS 1
#define SET_VCPCODE_OFFSET     4
#define SET_HIGH_OFFSET        5
#define SET_LOW_OFFSET         6
#define SET_CHK_OFFSET         7

// Side-channel code used by the original program for input switching
static const unsigned char VCP_CODE_SWITCH_INPUT = 0xF4;

// Template message (DDC/CI spec)
static unsigned char ucSetCommandWrite[SETWRITESIZE] = { 0x6e,0x51,0x84,0x03,0x00,0x00,0x00,0x00 };

// Ensure ADL is initialized exactly once for this process
static bool EnsureADL()
{
    static bool inited = false;
    if (inited) return true;
    if (!InitADL()) return false;
    inited = true;
    return true;
}

// Local helper: raw I2C write via ADL
static int vWriteI2c(char* lpucSendMsgBuf, int iSendMsgLen, int iAdapterIndex, int iDisplayIndex)
{
    int iRev = 0;
    // int ADL_Display_DDCBlockAccess_Get(int adapter, int display, int iOffset, int iCommand,
    //                                    int iDataSize, char* lpData, int* lpRead, char* lpReadBuffer);
    return adlprocs.ADL_Display_DDCBlockAccess_Get(
        iAdapterIndex,
        iDisplayIndex,
        0,                // iOffset (was nullptr)
        0,                // iCommand (was nullptr)
        iSendMsgLen,
        lpucSendMsgBuf,
        &iRev,
        (char*)0          // lpReadBuffer (was nullptr)
    );
}

// Local helper: builds the payload and writes it
static int vSetVcpCommand(unsigned int subaddress, unsigned char ucVcp, unsigned int ulVal,
    int iAdapterIndex, int iDisplayIndex)
{
    // Build message per your original code/comments
    ucSetCommandWrite[SET_VCPCODE_SUBADDRESS] = (unsigned char)subaddress; // e.g., 0x50
    ucSetCommandWrite[SET_VCPCODE_OFFSET] = ucVcp;                      // 0xF4
    ucSetCommandWrite[SET_LOW_OFFSET] = (unsigned char)(ulVal & 0xFF);       // e.g., 0xD1
    ucSetCommandWrite[SET_HIGH_OFFSET] = (unsigned char)((ulVal >> 8) & 0xFF); // usually 0x00

    // XOR checksum across bytes 0..6
    unsigned char chk = 0;
    for (int i = 0; i < SET_CHK_OFFSET; ++i) chk ^= ucSetCommandWrite[i];
    ucSetCommandWrite[SET_CHK_OFFSET] = chk;

    // Send
    int rc = vWriteI2c((char*)ucSetCommandWrite, SETWRITESIZE, iAdapterIndex, iDisplayIndex);

    // Give the monitor a moment to switch / settle
    Sleep(700);

    return rc;
}

// Public bridge used by the tray app
extern "C" int SetVcpFeatureWithI2cAddr(
    int adapterIdx,
    int displayIdx,
    unsigned short /*vcpCode_ignored*/,
    unsigned int valueHex,
    unsigned int i2cSubaddress)
{
    if (!EnsureADL()) return 1;

    int rc = vSetVcpCommand(i2cSubaddress, VCP_CODE_SWITCH_INPUT, valueHex, adapterIdx, displayIdx);
    return (rc == 0) ? 0 : rc;
}
