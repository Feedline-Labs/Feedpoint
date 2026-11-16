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

#include "protocol.h"

#define MAX_PATH_LENGTH 4096

/* What each operation needs before it runs, indexed by operation byte.
   An operation with no row needs nothing and is answered as unknown. */
struct OperationRule {
    uint32_t argumentLength; /* minimum argument bytes */
    int needsDevice;
};

static const struct OperationRule operationRules[UINT8_MAX + 1] = {
    [OpHello]                  = { 0, 0 },
    [OpList]                   = { 0, 0 },
    [OpOpen]                   = { 4, 0 },
    [OpClose]                  = { 0, 1 },
    [OpSetSampleRate]          = { 4, 1 },
    [OpSetCenterFrequency]     = { 4, 1 },
    [OpSetTunerBandwidth]      = { 4, 1 },
    [OpSetAgcMode]             = { 4, 1 },
    [OpSetTunerGainMode]       = { 4, 1 },
    [OpSetTunerGain]           = { 4, 1 },
    [OpResetBuffer]            = { 0, 1 },
    [OpReadSync]               = { 4, 1 },
    [OpGetVersion]             = { 0, 0 },
    [OpGetTunerGains]          = { 0, 1 },
    [OpGetTunerGain]           = { 0, 1 },
    [OpGetSampleRate]          = { 0, 1 },
    [OpGetCenterFrequency]     = { 0, 1 },
    [OpGetTunerType]           = { 0, 1 },
    [OpSetFrequencyCorrection] = { 4, 1 },
    [OpGetFrequencyCorrection] = { 0, 1 },
    [OpSetBiasTee]             = { 4, 1 },
    [OpSetBiasTeeGpio]         = { 8, 1 },
    [OpSetDirectSampling]      = { 4, 1 },
    [OpGetDirectSampling]      = { 0, 1 },
    [OpSetOffsetTuning]        = { 4, 1 },
    [OpGetOffsetTuning]        = { 0, 1 },
    [OpSetXtalFrequency]       = { 8, 1 },
    [OpGetXtalFrequency]       = { 0, 1 },
    [OpSetTunerIfGain]         = { 8, 1 },
    [OpGetIndexBySerial]       = { 4, 0 },
    [OpGetDeviceName]          = { 4, 0 },
    [OpGetUsbStrings]          = { 0, 1 },
};

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
static int (*rtlsdr_get_tuner_gains)(rtlsdr_dev_t *, int *);
static int (*rtlsdr_get_tuner_gain)(rtlsdr_dev_t *);
static uint32_t (*rtlsdr_get_sample_rate)(rtlsdr_dev_t *);
static uint32_t (*rtlsdr_get_center_freq)(rtlsdr_dev_t *);
static int (*rtlsdr_get_tuner_type)(rtlsdr_dev_t *);
static int (*rtlsdr_set_freq_correction)(rtlsdr_dev_t *, int);
static int (*rtlsdr_get_freq_correction)(rtlsdr_dev_t *);
static int (*rtlsdr_set_bias_tee)(rtlsdr_dev_t *, int);
static int (*rtlsdr_set_bias_tee_gpio)(rtlsdr_dev_t *, int, int);
static int (*rtlsdr_set_direct_sampling)(rtlsdr_dev_t *, int);
static int (*rtlsdr_get_direct_sampling)(rtlsdr_dev_t *);
static int (*rtlsdr_set_offset_tuning)(rtlsdr_dev_t *, int);
static int (*rtlsdr_get_offset_tuning)(rtlsdr_dev_t *);
static int (*rtlsdr_set_xtal_freq)(rtlsdr_dev_t *, uint32_t, uint32_t);
static int (*rtlsdr_get_xtal_freq)(rtlsdr_dev_t *, uint32_t *, uint32_t *);
static int (*rtlsdr_set_tuner_if_gain)(rtlsdr_dev_t *, int, int);
static int (*rtlsdr_get_index_by_serial)(const char *);
static const char *(*rtlsdr_get_device_name)(uint32_t);
static int (*rtlsdr_get_usb_strings)(rtlsdr_dev_t *, char *, char *, char *);

static char loadedLibraryPath[MAX_PATH_LENGTH] = "";
static char loadedLibrarySha256[65] = "";

/* The one approved driver per platform (see drivers/README.md). No other name
   and no operating system search path: a node runs the proven driver or says
   plainly that it cannot find it. */
#if defined(_WIN32)
static const char *libraryName = "librtlsdr-v4.dll";
#elif defined(__APPLE__)
static const char *libraryName = "librtlsdr-v4.dylib";
#else
static const char *libraryName = "librtlsdr-v4.so";
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

/* RTLSDR_LIBRARY_PATH (for testing) if set, otherwise the approved driver
   beside this executable. Nothing else. */
static LibHandle loadLibrtlsdr(void)
{
    const char *overridePath = getenv("RTLSDR_LIBRARY_PATH");
    if (overridePath && overridePath[0])
        return tryLoad(overridePath, "  (RTLSDR_LIBRARY_PATH)");

    char folder[MAX_PATH_LENGTH];
    char path[MAX_PATH_LENGTH];
    getExecutableFolder(folder, sizeof folder);
    snprintf(path, sizeof path, "%s%s", folder, libraryName);
    return tryLoad(path, "");
}

/* --- SHA-256 of the loaded driver, so the host can tell it is approved ------ */

static const uint32_t sha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256Block(uint32_t state[8], const uint8_t block[64])
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROTR(w[i - 15], 7) ^ ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROTR(w[i - 2], 17) ^ ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25)) + ((e & f) ^ (~e & g)) + sha256K[i] + w[i];
        uint32_t t2 = (ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

/* Hashes the loaded driver file into loadedLibrarySha256 (lowercase hex). */
static void hashLoadedLibrary(void)
{
    FILE *file = fopen(loadedLibraryPath, "rb");
    if (!file) return;

    uint32_t state[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    uint8_t block[128];
    uint64_t totalBytes = 0;
    size_t length;

    while ((length = fread(block, 1, 64, file)) == 64) {
        sha256Block(state, block);
        totalBytes += 64;
    }
    fclose(file);
    totalBytes += length;

    /* Padding: 0x80, zeros, then the bit length in the last 8 bytes. */
    size_t padded = length < 56 ? 64 : 128;
    block[length] = 0x80;
    memset(block + length + 1, 0, padded - length - 1);
    uint64_t bits = totalBytes * 8;
    for (int i = 0; i < 8; i++) block[padded - 1 - i] = (uint8_t)(bits >> (8 * i));
    sha256Block(state, block);
    if (padded == 128) sha256Block(state, block + 64);

    for (int i = 0; i < 8; i++)
        snprintf(loadedLibrarySha256 + i * 8, 9, "%08x", state[i]);
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
    RESOLVE(library, rtlsdr_get_tuner_gains)
    RESOLVE(library, rtlsdr_get_tuner_gain)
    RESOLVE(library, rtlsdr_get_sample_rate)
    RESOLVE(library, rtlsdr_get_center_freq)
    RESOLVE(library, rtlsdr_get_tuner_type)
    RESOLVE(library, rtlsdr_set_freq_correction)
    RESOLVE(library, rtlsdr_get_freq_correction)
    RESOLVE(library, rtlsdr_set_bias_tee)
    RESOLVE(library, rtlsdr_set_bias_tee_gpio)
    RESOLVE(library, rtlsdr_set_direct_sampling)
    RESOLVE(library, rtlsdr_get_direct_sampling)
    RESOLVE(library, rtlsdr_set_offset_tuning)
    RESOLVE(library, rtlsdr_get_offset_tuning)
    RESOLVE(library, rtlsdr_set_xtal_freq)
    RESOLVE(library, rtlsdr_get_xtal_freq)
    RESOLVE(library, rtlsdr_set_tuner_if_gain)
    RESOLVE(library, rtlsdr_get_index_by_serial)
    RESOLVE(library, rtlsdr_get_device_name)
    RESOLVE(library, rtlsdr_get_usb_strings)
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

/* Successful replies carrying one value. */
static int32_t replyUInt32(uint32_t value)
{
    return appendUInt32(value) ? STATUS_OK : STATUS_OUT_OF_MEMORY;
}

static int32_t replyString(const char *text)
{
    return appendString(text) ? STATUS_OK : STATUS_OUT_OF_MEMORY;
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

static int32_t handleGetTunerGains(void)
{
    int count = rtlsdr_get_tuner_gains(device, NULL);
    if (count < 0) return count;

    int *gains = calloc((size_t)count + 1, sizeof *gains); /* +1: never a zero-size allocation */
    if (!gains) return STATUS_OUT_OF_MEMORY;
    rtlsdr_get_tuner_gains(device, gains);

    int32_t status = appendUInt32((uint32_t)count) ? STATUS_OK : STATUS_OUT_OF_MEMORY;
    for (int i = 0; i < count && status == STATUS_OK; i++)
        if (!appendUInt32((uint32_t)gains[i])) status = STATUS_OUT_OF_MEMORY;
    free(gains);
    return status;
}

static int32_t handleGetXtalFrequency(void)
{
    uint32_t rtlFrequency = 0, tunerFrequency = 0;
    int32_t status = rtlsdr_get_xtal_freq(device, &rtlFrequency, &tunerFrequency);
    if (status != STATUS_OK) return status;
    if (!appendUInt32(rtlFrequency) || !appendUInt32(tunerFrequency)) return STATUS_OUT_OF_MEMORY;
    return STATUS_OK;
}

/* Argument: string serial (u32 length, then the bytes). */
static int32_t handleGetIndexBySerial(const uint8_t *argument, uint32_t argumentLength)
{
    char serial[64];
    uint32_t length = getUInt32(argument);
    if (length > argumentLength - 4) return STATUS_BAD_REQUEST;
    memcpy(serial, argument + 4, length);
    serial[length] = '\0';

    int index = rtlsdr_get_index_by_serial(serial);
    return index < 0 ? index : replyUInt32((uint32_t)index);
}

static int32_t handleGetUsbStrings(void)
{
    char vendor[256] = "", product[256] = "", serial[256] = "";
    int32_t status = rtlsdr_get_usb_strings(device, vendor, product, serial);
    if (status != STATUS_OK) return status;
    if (!appendString(vendor) || !appendString(product) || !appendString(serial))
        return STATUS_OUT_OF_MEMORY;
    return STATUS_OK;
}

static int32_t handle(uint8_t operation, const uint8_t *argument, uint32_t argumentLength)
{
    const struct OperationRule *rule = &operationRules[operation];
    if (argumentLength < rule->argumentLength) return STATUS_BAD_REQUEST;
    if (rule->needsDevice && !device) return STATUS_NOT_OPEN;

    uint32_t value = argumentLength >= 4 ? getUInt32(argument) : 0;
    uint32_t second = argumentLength >= 8 ? getUInt32(argument + 4) : 0;

    switch (operation) {
    case OpHello:
        if (!appendUInt32(PROTOCOL_VERSION) || !appendString(loadedLibraryPath) ||
            !appendString(loadedLibrarySha256))
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
    case OpSetSampleRate:          return rtlsdr_set_sample_rate(device, value);
    case OpSetCenterFrequency:     return rtlsdr_set_center_freq(device, value);
    case OpSetTunerBandwidth:      return rtlsdr_set_tuner_bandwidth(device, value);
    case OpSetAgcMode:             return rtlsdr_set_agc_mode(device, (int)value);
    case OpSetTunerGainMode:       return rtlsdr_set_tuner_gain_mode(device, (int)value);
    case OpSetTunerGain:           return rtlsdr_set_tuner_gain(device, (int32_t)value);
    case OpResetBuffer:            return rtlsdr_reset_buffer(device);
    case OpReadSync:               return handleReadSync(value);
    case OpGetVersion:             return replyString(FEEDPOINT_VERSION);
    case OpGetTunerGains:          return handleGetTunerGains();
    case OpGetTunerGain:           return replyUInt32((uint32_t)rtlsdr_get_tuner_gain(device));
    case OpGetSampleRate:          return replyUInt32(rtlsdr_get_sample_rate(device));
    case OpGetCenterFrequency:     return replyUInt32(rtlsdr_get_center_freq(device));
    case OpGetTunerType:           return replyUInt32((uint32_t)rtlsdr_get_tuner_type(device));
    case OpSetFrequencyCorrection: return rtlsdr_set_freq_correction(device, (int32_t)value);
    case OpGetFrequencyCorrection: return replyUInt32((uint32_t)rtlsdr_get_freq_correction(device));
    case OpSetBiasTee:             return rtlsdr_set_bias_tee(device, (int)value);
    case OpSetBiasTeeGpio:         return rtlsdr_set_bias_tee_gpio(device, (int)value, (int)second);
    case OpSetDirectSampling:      return rtlsdr_set_direct_sampling(device, (int)value);
    case OpGetDirectSampling:      return replyUInt32((uint32_t)rtlsdr_get_direct_sampling(device));
    case OpSetOffsetTuning:        return rtlsdr_set_offset_tuning(device, (int)value);
    case OpGetOffsetTuning:        return replyUInt32((uint32_t)rtlsdr_get_offset_tuning(device));
    case OpSetXtalFrequency:       return rtlsdr_set_xtal_freq(device, value, second);
    case OpGetXtalFrequency:       return handleGetXtalFrequency();
    case OpSetTunerIfGain:         return rtlsdr_set_tuner_if_gain(device, (int)value, (int32_t)second);
    case OpGetIndexBySerial:       return handleGetIndexBySerial(argument, argumentLength);
    case OpGetDeviceName:          return replyString(rtlsdr_get_device_name(value));
    case OpGetUsbStrings:          return handleGetUsbStrings();
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
        fprintf(stderr, "Feedpoint: the RTL-SDR driver %s could not be loaded. "
                        "It must sit next to Feedpoint.\n", libraryName);
        return 2;
    }
    if (!resolveEntryPoints(library)) return 3;
    hashLoadedLibrary();

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
