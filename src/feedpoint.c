/*
 * Feedpoint - a small RTL-SDR dongle server.
 * Copyright (C) 2026 Feedline Labs
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See LICENSE.
 *
 * Feedpoint owns one RTL-SDR dongle. It loads librtlsdr at runtime and answers
 * requests on standard input with responses on standard output, one librtlsdr
 * call per request. See PROTOCOL.md. Diagnostics go to standard error.
 */

#if defined(__linux__)
#define _DEFAULT_SOURCE /* readlink */
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
typedef HMODULE LibHandle;
#define LOAD_LIB(path) LoadLibraryA(path)
#define LIB_SYMBOL(lib, name) ((void *)GetProcAddress((lib), (name)))
#define PATH_SEPARATOR '\\'
#else
#include <dlfcn.h>
#include <unistd.h>
typedef void *LibHandle;
#define LOAD_LIB(path) dlopen((path), RTLD_NOW)
#define LIB_SYMBOL(lib, name) dlsym((lib), (name))
#define PATH_SEPARATOR '/'
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#define PROTOCOL_VERSION 1
#define MAX_PATH_LENGTH 4096

/* Operations. Numbers are part of the protocol and never change. */
enum Operation {
    OpUnassigned = 0,
    OpHello = 1,
    OpList = 2,
    OpOpen = 3,
    OpClose = 4,
    OpSetSampleRate = 5,
    OpSetCenterFrequency = 6,
    OpSetTunerBandwidth = 7,
    OpSetAgcMode = 8,
    OpSetTunerGainMode = 9,
    OpSetTunerGain = 10,
    OpResetBuffer = 11,
    OpReadSync = 12
};

/* Feedpoint's own status codes, kept clear of librtlsdr/libusb codes. */
#define STATUS_OK 0
#define STATUS_UNKNOWN_OPERATION -9001
#define STATUS_BAD_REQUEST -9002
#define STATUS_NOT_OPEN -9003
#define STATUS_ALREADY_OPEN -9004
#define STATUS_OUT_OF_MEMORY -9005

/* --- librtlsdr entry points, resolved at runtime --------------------------- */

typedef struct rtlsdr_dev rtlsdr_dev_t;

static uint32_t (*rtlsdr_get_device_count)(void);
static int (*rtlsdr_get_device_usb_strings)(uint32_t, char *, char *, char *);
static int (*rtlsdr_open)(rtlsdr_dev_t **, uint32_t);
static int (*rtlsdr_close)(rtlsdr_dev_t *);
static int (*rtlsdr_set_sample_rate)(rtlsdr_dev_t *, uint32_t);
static int (*rtlsdr_set_center_freq)(rtlsdr_dev_t *, uint32_t);
static int (*rtlsdr_set_tuner_bandwidth)(rtlsdr_dev_t *, uint32_t);
static int (*rtlsdr_set_agc_mode)(rtlsdr_dev_t *, int);
static int (*rtlsdr_set_tuner_gain_mode)(rtlsdr_dev_t *, int);
static int (*rtlsdr_set_tuner_gain)(rtlsdr_dev_t *, int);
static int (*rtlsdr_reset_buffer)(rtlsdr_dev_t *);
static int (*rtlsdr_read_sync)(rtlsdr_dev_t *, void *, int, int *);

static char loadedLibraryPath[MAX_PATH_LENGTH] = "";

#if defined(_WIN32)
static const char *libraryNames[] = { "librtlsdr-v4.dll", "librtlsdr.dll", "rtlsdr.dll", NULL };
#elif defined(__APPLE__)
static const char *libraryNames[] = { "librtlsdr.dylib", "librtlsdr.0.dylib", NULL };
#else
static const char *libraryNames[] = { "librtlsdr-v4.so", "librtlsdr.so", "librtlsdr.so.0", "librtlsdr.so.2", NULL };
#endif

/* Folder this executable lives in, with a trailing separator. */
static void getExecutableFolder(char *folder, size_t size)
{
    folder[0] = '\0';
#if defined(_WIN32)
    DWORD length = GetModuleFileNameA(NULL, folder, (DWORD)size);
    if (length == 0 || length >= size) { folder[0] = '\0'; return; }
#elif defined(__APPLE__)
    uint32_t length = (uint32_t)size;
    if (_NSGetExecutablePath(folder, &length) != 0) { folder[0] = '\0'; return; }
#else
    ssize_t length = readlink("/proc/self/exe", folder, size - 1);
    if (length <= 0) { folder[0] = '\0'; return; }
    folder[length] = '\0';
#endif
    char *lastSeparator = strrchr(folder, PATH_SEPARATOR);
    if (lastSeparator) lastSeparator[1] = '\0'; else folder[0] = '\0';
}

static LibHandle tryLoad(const char *path, const char *note)
{
    LibHandle library = LOAD_LIB(path);
    fprintf(stderr, "Feedpoint: tried %s%s%s\n", path, note, library ? "  (loaded)" : "");
    if (library) snprintf(loadedLibraryPath, sizeof loadedLibraryPath, "%s", path);
    return library;
}

/* Resolution order: RTLSDR_LIBRARY_PATH -> beside this executable -> OS search. */
static LibHandle loadLibrtlsdr(void)
{
    const char *overridePath = getenv("RTLSDR_LIBRARY_PATH");
    if (overridePath && overridePath[0]) {
        LibHandle library = tryLoad(overridePath, "  (RTLSDR_LIBRARY_PATH)");
        if (library) return library;
    }

    char folder[MAX_PATH_LENGTH];
    char path[MAX_PATH_LENGTH];
    getExecutableFolder(folder, sizeof folder);

    for (int i = 0; libraryNames[i]; i++) {
        snprintf(path, sizeof path, "%s%s", folder, libraryNames[i]);
        LibHandle library = tryLoad(path, "");
        if (library) return library;
    }

    for (int i = 0; libraryNames[i]; i++) {
        LibHandle library = tryLoad(libraryNames[i], "  (operating system search path)");
        if (library) return library;
    }

    return NULL;
}

#define RESOLVE(lib, name) \
    if (!(*(void **)&name = LIB_SYMBOL(lib, #name))) { \
        fprintf(stderr, "Feedpoint: %s is missing from %s\n", #name, loadedLibraryPath); \
        return 0; \
    }

static int resolveEntryPoints(LibHandle library)
{
    RESOLVE(library, rtlsdr_get_device_count)
    RESOLVE(library, rtlsdr_get_device_usb_strings)
    RESOLVE(library, rtlsdr_open)
    RESOLVE(library, rtlsdr_close)
    RESOLVE(library, rtlsdr_set_sample_rate)
    RESOLVE(library, rtlsdr_set_center_freq)
    RESOLVE(library, rtlsdr_set_tuner_bandwidth)
    RESOLVE(library, rtlsdr_set_agc_mode)
    RESOLVE(library, rtlsdr_set_tuner_gain_mode)
    RESOLVE(library, rtlsdr_set_tuner_gain)
    RESOLVE(library, rtlsdr_reset_buffer)
    RESOLVE(library, rtlsdr_read_sync)
    return 1;
}

/* --- framing: little-endian integers over stdin/stdout --------------------- */

static int readExact(void *buffer, size_t length)
{
    return fread(buffer, 1, length, stdin) == length;
}

static uint32_t getUInt32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static void putUInt32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
    bytes[2] = (uint8_t)(value >> 16);
    bytes[3] = (uint8_t)(value >> 24);
}

/* Growable response payload. */
static uint8_t *payload;
static uint32_t payloadLength;
static uint32_t payloadCapacity;

static int reservePayload(uint32_t capacity)
{
    if (capacity <= payloadCapacity) return 1;
    uint8_t *grown = realloc(payload, capacity);
    if (!grown) return 0;
    payload = grown;
    payloadCapacity = capacity;
    return 1;
}

static int appendUInt32(uint32_t value)
{
    if (!reservePayload(payloadLength + 4)) return 0;
    putUInt32(payload + payloadLength, value);
    payloadLength += 4;
    return 1;
}

static int appendString(const char *text)
{
    uint32_t length = (uint32_t)strlen(text);
    if (!appendUInt32(length) || !reservePayload(payloadLength + length)) return 0;
    memcpy(payload + payloadLength, text, length);
    payloadLength += length;
    return 1;
}

static void writeResponse(int32_t status)
{
    uint8_t header[8];
    putUInt32(header, (uint32_t)status);
    putUInt32(header + 4, payloadLength);
    fwrite(header, 1, sizeof header, stdout);
    if (payloadLength) fwrite(payload, 1, payloadLength, stdout);
    fflush(stdout);
}

/* --- request handling ------------------------------------------------------ */

static rtlsdr_dev_t *device;

static int32_t handleList(void)
{
    uint32_t count = rtlsdr_get_device_count();
    if (!appendUInt32(count)) return STATUS_OUT_OF_MEMORY;

    for (uint32_t index = 0; index < count; index++) {
        char vendor[256] = "", product[256] = "", serial[256] = "";
        rtlsdr_get_device_usb_strings(index, vendor, product, serial);
        if (!appendString(vendor) || !appendString(product) || !appendString(serial))
            return STATUS_OUT_OF_MEMORY;
    }
    return STATUS_OK;
}

static int32_t handleReadSync(uint32_t byteCount)
{
    if (!reservePayload(byteCount)) return STATUS_OUT_OF_MEMORY;
    int bytesRead = 0;
    int32_t status = rtlsdr_read_sync(device, payload, (int)byteCount, &bytesRead);
    payloadLength = (bytesRead > 0 && (uint32_t)bytesRead <= byteCount) ? (uint32_t)bytesRead : 0;
    return status;
}

static int32_t handle(uint8_t operation, const uint8_t *argument, uint32_t argumentLength)
{
    uint32_t value = argumentLength >= 4 ? getUInt32(argument) : 0;
    int needsValue = operation >= OpOpen && operation != OpClose && operation != OpResetBuffer;

    if (needsValue && argumentLength < 4) return STATUS_BAD_REQUEST;

    if (operation >= OpClose && !device) return STATUS_NOT_OPEN;

    switch (operation) {
    case OpHello:
        if (!appendUInt32(PROTOCOL_VERSION) || !appendString(loadedLibraryPath))
            return STATUS_OUT_OF_MEMORY;
        return STATUS_OK;
    case OpList:
        return handleList();
    case OpOpen:
        if (device) return STATUS_ALREADY_OPEN;
        return rtlsdr_open(&device, value);
    case OpClose: {
        int32_t status = rtlsdr_close(device);
        device = NULL;
        return status;
    }
    case OpSetSampleRate:      return rtlsdr_set_sample_rate(device, value);
    case OpSetCenterFrequency: return rtlsdr_set_center_freq(device, value);
    case OpSetTunerBandwidth:  return rtlsdr_set_tuner_bandwidth(device, value);
    case OpSetAgcMode:         return rtlsdr_set_agc_mode(device, (int)value);
    case OpSetTunerGainMode:   return rtlsdr_set_tuner_gain_mode(device, (int)value);
    case OpSetTunerGain:       return rtlsdr_set_tuner_gain(device, (int32_t)value);
    case OpResetBuffer:        return rtlsdr_reset_buffer(device);
    case OpReadSync:           return handleReadSync(value);
    default:                   return STATUS_UNKNOWN_OPERATION;
    }
}

int main(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    LibHandle library = loadLibrtlsdr();
    if (!library) {
        fprintf(stderr, "Feedpoint: the RTL-SDR driver could not be loaded. "
                        "Copy it next to Feedpoint, or set RTLSDR_LIBRARY_PATH to its full path.\n");
        return 2;
    }
    if (!resolveEntryPoints(library)) return 3;

    /* Request: u8 operation, u32 argument length, argument bytes. */
    uint8_t header[5];
    uint8_t argument[64];

    while (readExact(header, sizeof header)) {
        uint32_t argumentLength = getUInt32(header + 1);
        if (argumentLength > sizeof argument || !readExact(argument, argumentLength)) break;

        payloadLength = 0;
        writeResponse(handle(header[0], argument, argumentLength));
    }

    /* Input closed: the owner has gone away. Release the dongle and exit. */
    if (device) rtlsdr_close(device);
    return 0;
}
