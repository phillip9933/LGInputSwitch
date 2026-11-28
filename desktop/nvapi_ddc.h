#pragma once

// NVAPI I²C layout and interface IDs follow NVIDIA's MIT-licensed SDK.
// https://github.com/NVIDIA/nvapi (nvapi.h and nvapi_interface.h).
// Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. See licenses/NVIDIA-NVAPI-MIT.txt.
#include "../core/ddc_packet.h"
#include <windows.h>
#include <cstddef>
#include <cstdint>

class NvidiaDdc
{
    struct Gpu;
    using U32 = std::uint32_t;
    struct I2cInfo
    {
        U32 version, displayMask;
        std::uint8_t isDdcPort, address;
        std::uint8_t *registerAddress;
        U32 registerSize;
        std::uint8_t *data;
        U32 size, deprecatedSpeed;
        int speed;
        std::uint8_t portId;
        U32 hasPortId;
    };
    static_assert(sizeof(void *) != 8 ||
                  (sizeof(I2cInfo) == 64 && offsetof(I2cInfo, hasPortId) == 56));

    using Query = void *(__cdecl *)(U32);
    using Init = int(__cdecl *)();
    using DisplayId = int(__cdecl *)(const char *, U32 *);
    using Output = int(__cdecl *)(U32, Gpu **, U32 *);
    using Reverse = int(__cdecl *)(Gpu *, U32, U32 *);
    using Write = int(__cdecl *)(Gpu *, I2cInfo *);
    HMODULE library_ = nullptr;
    Init unload_ = nullptr;
    DisplayId displayId_ = nullptr;
    Output output_ = nullptr;
    Reverse reverse_ = nullptr;
    Write write_ = nullptr;
    bool initialized_ = false;

  public:
    struct Target
    {
        Gpu *gpu = nullptr;
        U32 mask = 0;
        explicit operator bool() const { return gpu && mask; }
        bool operator==(const Target &other) const { return gpu == other.gpu && mask == other.mask; }
    };
    NvidiaDdc()
    {
        library_ = LoadLibraryExW(sizeof(void *) == 8 ? L"nvapi64.dll" : L"nvapi.dll",
                                  nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library_)
            return;
        const auto query = reinterpret_cast<Query>(GetProcAddress(library_, "nvapi_QueryInterface"));
        if (!query)
            return;
        const auto initialize = reinterpret_cast<Init>(query(0x0150e828));
        unload_ = reinterpret_cast<Init>(query(0xd22bdd7e));
        displayId_ = reinterpret_cast<DisplayId>(query(0xae457190));
        output_ = reinterpret_cast<Output>(query(0x112ba1a5));
        reverse_ = reinterpret_cast<Reverse>(query(0x08f2bab4));
        write_ = reinterpret_cast<Write>(query(0xe812eb07));
        initialized_ = initialize && unload_ && displayId_ && output_ && reverse_ && write_ &&
                       initialize() == 0;
    }
    NvidiaDdc(const NvidiaDdc &) = delete;
    NvidiaDdc &operator=(const NvidiaDdc &) = delete;
    ~NvidiaDdc()
    {
        if (initialized_)
            unload_();
        if (library_)
            FreeLibrary(library_);
    }
    Target resolve(const char *display) const
    {
        U32 id = 0, confirmedId = 0;
        Target target;
        if (!initialized_ || displayId_(display, &id) != 0 || !id ||
            output_(id, &target.gpu, &target.mask) != 0 || !target ||
            (target.mask & (target.mask - 1)) != 0 ||
            reverse_(target.gpu, target.mask, &confirmedId) != 0 || confirmedId != id)
            return {};
        return target;
    }
    lg::SwitchResult send(const Target &target, const lg::InputCommand &command) const
    {
        auto packet = lg::EncodeDdcWrite(command);
        if (!initialized_ || !target || !packet)
            return {lg::SwitchStatus::InvalidArgument, 87, "Invalid NVIDIA output or input command."};
        I2cInfo transaction{};
        transaction.version = sizeof(I2cInfo) | (3u << 16);
        transaction.displayMask = target.mask;
        transaction.isDdcPort = 1;
        transaction.address = packet->front();
        transaction.data = packet->data() + 1;
        transaction.size = static_cast<U32>(packet->size() - 1);
        transaction.deprecatedSpeed = 0xffff;
        const int status = write_(target.gpu, &transaction);
        if (status != 0)
            return {lg::SwitchStatus::TransportFailure, status, "NVIDIA driver rejected the DDC/CI write."};
        return {lg::SwitchStatus::CommandSent, 0, "NVIDIA command sent; physical switch unverified."};
    }
};
