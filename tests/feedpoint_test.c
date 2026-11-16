/*
 * Feedpoint tests.
 * Copyright (C) 2026 Feedline Labs. GPL-3.0-or-later; see LICENSE.
 *
 * Each run starts Feedpoint, talks to it over its pipes exactly as a host
 * does, and checks one case against a real RTL-SDR dongle:
 *
 *     feedpoint_test <path to feedpoint> <case>
 *
 * Each case is named after the operation it tests, as PROTOCOL.md names it,
 * or Operation.Situation for one specific situation. Protocol.* cases cover
 * rules that apply to every operation.
 *
 * Exit code 0 = passed, 1 = failed, 77 = skipped (no dongle plugged in).
 * FEEDPOINT_TEST_DEVICE picks the dongle by index (default 0).
 * The bias tee cases put DC power on the antenna port. CMake registers them
 * only when its FEEDPOINT_TEST_BIAS_TEE option is on.
 */

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol.h"

#define EXIT_SKIPPED 77

/* A strong, steady signal near you, in Hz. Change it to suit your location.
   Default: NOAA Weather Radio, 162.400 MHz. */
#define TEST_FREQUENCY 162400000

static const char *feedpointPath;
static const char *caseName;
static uint32_t deviceIndex;

static void fail(const char *message, int line)
{
    fprintf(stderr, "%s failed (line %d): %s\n", caseName, line, message);
    exit(EXIT_FAILURE);
}

#define CHECK(condition) do { if (!(condition)) fail(#condition, __LINE__); } while (0)

/* Fails unless a request returned the expected status. */
#define EXPECT_STATUS(call, expected) do { \
        int32_t actual = (call); \
        if (actual != (expected)) { \
            fprintf(stderr, "%s failed (line %d): %s returned %d, expected %d\n", \
                    caseName, __LINE__, #call, actual, (expected)); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

/* --- starting and stopping Feedpoint -------------------------------------- */

static FILE *toFeedpoint;
static FILE *fromFeedpoint;

#ifdef _WIN32
static HANDLE feedpointProcess;

static void startFeedpoint(void)
{
    SECURITY_ATTRIBUTES inheritable = { sizeof inheritable, NULL, TRUE };
    HANDLE inputRead, inputWrite, outputRead, outputWrite;
    if (!CreatePipe(&inputRead, &inputWrite, &inheritable, 0) ||
        !CreatePipe(&outputRead, &outputWrite, &inheritable, 0))
        fail("could not create pipes", __LINE__);
    SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startup;
    ZeroMemory(&startup, sizeof startup);
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = inputRead;
    startup.hStdOutput = outputWrite;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    char commandLine[4096];
    snprintf(commandLine, sizeof commandLine, "\"%s\"", feedpointPath);
    PROCESS_INFORMATION process;
    if (!CreateProcessA(NULL, commandLine, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process))
        fail("could not start Feedpoint", __LINE__);
    CloseHandle(process.hThread);
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    feedpointProcess = process.hProcess;

    toFeedpoint = _fdopen(_open_osfhandle((intptr_t)inputWrite, 0), "wb");
    fromFeedpoint = _fdopen(_open_osfhandle((intptr_t)outputRead, _O_RDONLY), "rb");
}

/* Closes Feedpoint's input, waits for it to exit and returns its exit code. */
static int stopFeedpoint(void)
{
    fclose(toFeedpoint);
    WaitForSingleObject(feedpointProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(feedpointProcess, &exitCode);
    CloseHandle(feedpointProcess);
    fclose(fromFeedpoint);
    return (int)exitCode;
}
#else
static pid_t feedpointProcess;

static void startFeedpoint(void)
{
    int input[2], output[2];
    if (pipe(input) != 0 || pipe(output) != 0) fail("could not create pipes", __LINE__);

    feedpointProcess = fork();
    if (feedpointProcess < 0) fail("could not start Feedpoint", __LINE__);
    if (feedpointProcess == 0) {
        dup2(input[0], STDIN_FILENO);
        dup2(output[1], STDOUT_FILENO);
        close(input[0]); close(input[1]); close(output[0]); close(output[1]);
        execl(feedpointPath, feedpointPath, (char *)NULL);
        _exit(127);
    }
    close(input[0]);
    close(output[1]);
    toFeedpoint = fdopen(input[1], "wb");
    fromFeedpoint = fdopen(output[0], "rb");
}

/* Closes Feedpoint's input, waits for it to exit and returns its exit code. */
static int stopFeedpoint(void)
{
    fclose(toFeedpoint);
    int status = 0;
    waitpid(feedpointProcess, &status, 0);
    fclose(fromFeedpoint);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
#endif

/* --- requests ------------------------------------------------------------- */

static uint8_t payload[1 << 20];
static uint32_t payloadLength;
static uint32_t payloadPosition;

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

/* Sends one request and reads its response into payload. Returns the status. */
static int32_t request(uint8_t operation, const uint8_t *argument, uint32_t argumentLength)
{
    uint8_t header[5];
    header[0] = operation;
    putUInt32(header + 1, argumentLength);
    fwrite(header, 1, sizeof header, toFeedpoint);
    if (argumentLength) fwrite(argument, 1, argumentLength, toFeedpoint);
    fflush(toFeedpoint);

    uint8_t response[8];
    if (fread(response, 1, sizeof response, fromFeedpoint) != sizeof response)
        fail("Feedpoint stopped answering (is the driver beside it?)", __LINE__);
    payloadLength = getUInt32(response + 4);
    payloadPosition = 0;
    if (payloadLength > sizeof payload) fail("payload larger than the test buffer", __LINE__);
    if (fread(payload, 1, payloadLength, fromFeedpoint) != payloadLength)
        fail("Feedpoint stopped mid-payload", __LINE__);
    return (int32_t)getUInt32(response);
}

static int32_t requestNone(uint8_t operation)
{
    return request(operation, NULL, 0);
}

static int32_t requestValue(uint8_t operation, uint32_t value)
{
    uint8_t argument[4];
    putUInt32(argument, value);
    return request(operation, argument, sizeof argument);
}

static int32_t requestValues(uint8_t operation, uint32_t first, uint32_t second)
{
    uint8_t argument[8];
    putUInt32(argument, first);
    putUInt32(argument + 4, second);
    return request(operation, argument, sizeof argument);
}

static int32_t requestString(uint8_t operation, const char *text)
{
    uint8_t argument[64];
    uint32_t length = (uint32_t)strlen(text);
    CHECK(length <= sizeof argument - 4);
    putUInt32(argument, length);
    memcpy(argument + 4, text, length);
    return request(operation, argument, 4 + length);
}

static uint32_t readUInt32(void)
{
    CHECK(payloadPosition + 4 <= payloadLength);
    uint32_t value = getUInt32(payload + payloadPosition);
    payloadPosition += 4;
    return value;
}

static int32_t readInt32(void)
{
    return (int32_t)readUInt32();
}

/* Reads a payload string into text and returns its length. */
static uint32_t readString(char *text, size_t size)
{
    uint32_t length = readUInt32();
    CHECK(payloadPosition + length <= payloadLength);
    CHECK(length < size);
    memcpy(text, payload + payloadPosition, length);
    text[length] = '\0';
    payloadPosition += length;
    return length;
}

static void checkPayloadRead(void)
{
    CHECK(payloadPosition == payloadLength);
}

/* Sends a Get operation that answers with one u32 and returns the value. */
static uint32_t getValue(uint8_t operation)
{
    EXPECT_STATUS(requestNone(operation), STATUS_OK);
    uint32_t value = readUInt32();
    checkPayloadRead();
    return value;
}

/* --- dongle under test ---------------------------------------------------- */

static void skip(const char *reason)
{
    fprintf(stderr, "%s skipped: %s\n", caseName, reason);
    stopFeedpoint();
    exit(EXIT_SKIPPED);
}

struct UsbStrings {
    char vendor[256], product[256], serial[256];
};

/* Lists the dongles; skips the case unless the dongle under test is there.
   Returns that dongle's strings and the dongle count. */
static uint32_t listDongles(struct UsbStrings *strings)
{
    EXPECT_STATUS(requestNone(OpList), STATUS_OK);
    uint32_t count = readUInt32();
    if (count <= deviceIndex) skip("no RTL-SDR dongle at FEEDPOINT_TEST_DEVICE");

    for (uint32_t index = 0; index < count; index++) {
        struct UsbStrings read;
        readString(read.vendor, sizeof read.vendor);
        readString(read.product, sizeof read.product);
        readString(read.serial, sizeof read.serial);
        if (index == deviceIndex) *strings = read;
    }
    checkPayloadRead();
    return count;
}

static void openDongle(void)
{
    struct UsbStrings strings;
    listDongles(&strings);
    EXPECT_STATUS(requestValue(OpOpen, deviceIndex), STATUS_OK);
}

/* --- 1.0 operations -------------------------------------------------------- */

static void testHello(void)
{
    char text[4096];
    EXPECT_STATUS(requestNone(OpHello), STATUS_OK);
    CHECK(readUInt32() == PROTOCOL_VERSION);
    CHECK(readString(text, sizeof text) > 0);   /* driver path */
    CHECK(readString(text, sizeof text) == 64); /* driver SHA-256, hex */
    checkPayloadRead();
}

static void testList(void)
{
    char text[256];
    EXPECT_STATUS(requestNone(OpList), STATUS_OK);
    uint32_t count = readUInt32();
    for (uint32_t index = 0; index < count * 3; index++)
        readString(text, sizeof text); /* vendor, product, serial */
    checkPayloadRead();
}

static void testOpen(void)
{
    openDongle();
}

static void testOpenAlreadyOpen(void)
{
    openDongle();
    EXPECT_STATUS(requestValue(OpOpen, deviceIndex), STATUS_ALREADY_OPEN);
}

static void testOpenBadRequest(void)
{
    EXPECT_STATUS(requestNone(OpOpen), STATUS_BAD_REQUEST);
}

static void testClose(void)
{
    openDongle();
    EXPECT_STATUS(requestNone(OpClose), STATUS_OK);
    EXPECT_STATUS(requestNone(OpClose), STATUS_NOT_OPEN);
}

static void testSetSampleRate(void)
{
    openDongle();
    EXPECT_STATUS(requestValue(OpSetSampleRate, 2048000), STATUS_OK);
    CHECK(getValue(OpGetSampleRate) == 2048000);
}

static void testSetCenterFrequency(void)
{
    openDongle();
    EXPECT_STATUS(requestValue(OpSetCenterFrequency, TEST_FREQUENCY), STATUS_OK);
    CHECK(getValue(OpGetCenterFrequency) == TEST_FREQUENCY);
}

static void testSetTunerBandwidth(void)
{
    openDongle();
    EXPECT_STATUS(requestValue(OpSetTunerBandwidth, 1000000), STATUS_OK);
    EXPECT_STATUS(requestValue(OpSetTunerBandwidth, 0), STATUS_OK); /* automatic */
}

static void testSetAgcMode(void)
{
    openDongle();
    EXPECT_STATUS(requestValue(OpSetAgcMode, 1), STATUS_OK);
    EXPECT_STATUS(requestValue(OpSetAgcMode, 0), STATUS_OK);
}

static void testSetTunerGainMode(void)
{
    openDongle();
    EXPECT_STATUS(requestValue(OpSetTunerGainMode, 1), STATUS_OK); /* manual */
    EXPECT_STATUS(requestValue(OpSetTunerGainMode, 0), STATUS_OK); /* automatic */
}

static void testSetTunerGain(void)
{
    openDongle();
    EXPECT_STATUS(requestNone(OpGetTunerGains), STATUS_OK);
    uint32_t count = readUInt32();
    CHECK(count > 0);
    payloadPosition += 4 * (count / 2);
    int32_t gain = readInt32(); /* a gain from the middle of the tuner's list */

    EXPECT_STATUS(requestValue(OpSetTunerGainMode, 1), STATUS_OK);
    EXPECT_STATUS(requestValue(OpSetTunerGain, (uint32_t)gain), STATUS_OK);
    CHECK((int32_t)getValue(OpGetTunerGain) == gain);
}

static void testResetBuffer(void)
{
    openDongle();
    EXPECT_STATUS(requestNone(OpResetBuffer), STATUS_OK);
}

static void testReadSync(void)
{
    openDongle();
    EXPECT_STATUS(requestValue(OpSetSampleRate, 2048000), STATUS_OK);
    EXPECT_STATUS(requestValue(OpSetCenterFrequency, TEST_FREQUENCY), STATUS_OK);
    EXPECT_STATUS(requestNone(OpResetBuffer), STATUS_OK);
    EXPECT_STATUS(requestValue(OpReadSync, 16384), STATUS_OK);
    CHECK(payloadLength == 16384);

    /* Live IQ is noise at the very least, never one repeated byte. */
    int varies = 0;
    for (uint32_t i = 1; i < payloadLength && !varies; i++) varies = payload[i] != payload[0];
    CHECK(varies);
}

/* --- 1.1 operations -------------------------------------------------------- */

static void testGetVersion(void)
{
    char version[64];
    EXPECT_STATUS(requestNone(OpGetVersion), STATUS_OK);
    readString(version, sizeof version);
    checkPayloadRead();
    CHECK(strcmp(version, FEEDPOINT_VERSION) == 0);
}

static void testGetTunerGains(void)
{
    openDongle();
    EXPECT_STATUS(requestNone(OpGetTunerGains), STATUS_OK);
    uint32_t count = readUInt32();
    CHECK(count > 0);
    int32_t previous = readInt32();
    for (uint32_t i = 1; i < count; i++) {
        int32_t gain = readInt32();
        CHECK(gain > previous); /* ascending, tenths of a dB */
        previous = gain;
    }
    checkPayloadRead();
}

static void testGetTunerGain(void)
{
    openDongle();
    getValue(OpGetTunerGain);
}

static void testGetSampleRate(void)
{
    openDongle();
    getValue(OpGetSampleRate);
}

static void testGetCenterFrequency(void)
{
    openDongle();
    getValue(OpGetCenterFrequency);
}

static void testGetTunerType(void)
{
    openDongle();
    uint32_t tunerType = getValue(OpGetTunerType);
    CHECK(tunerType >= TunerE4000 && tunerType <= TunerR828d);
}

static void testSetFrequencyCorrection(void)
{
    openDongle();
    int32_t original = (int32_t)getValue(OpGetFrequencyCorrection);
    EXPECT_STATUS(requestValue(OpSetFrequencyCorrection, (uint32_t)(original + 1)), STATUS_OK);
    CHECK((int32_t)getValue(OpGetFrequencyCorrection) == original + 1);
    EXPECT_STATUS(requestValue(OpSetFrequencyCorrection, (uint32_t)(original + 1)), -2); /* unchanged */
    EXPECT_STATUS(requestValue(OpSetFrequencyCorrection, (uint32_t)original), STATUS_OK);
}

static void testGetFrequencyCorrection(void)
{
    openDongle();
    CHECK(getValue(OpGetFrequencyCorrection) == 0);
}

static void testSetBiasTee(void)
{
    openDongle();
    EXPECT_STATUS(requestValue(OpSetBiasTee, 1), STATUS_OK);
    EXPECT_STATUS(requestValue(OpSetBiasTee, 0), STATUS_OK);
}

static void testSetBiasTeeGpio(void)
{
    openDongle();
    EXPECT_STATUS(requestValues(OpSetBiasTeeGpio, 0, 1), STATUS_OK);
    EXPECT_STATUS(requestValues(OpSetBiasTeeGpio, 0, 0), STATUS_OK);
}

static void testSetDirectSampling(void)
{
    openDongle();
    EXPECT_STATUS(requestValue(OpSetDirectSampling, 2), STATUS_OK); /* Q branch */
    CHECK(getValue(OpGetDirectSampling) == 2);
    EXPECT_STATUS(requestValue(OpSetDirectSampling, 0), STATUS_OK);
    CHECK(getValue(OpGetDirectSampling) == 0);
}

static void testGetDirectSampling(void)
{
    openDongle();
    CHECK(getValue(OpGetDirectSampling) == 0);
}

static void testSetOffsetTuning(void)
{
    openDongle();
    uint32_t tunerType = getValue(OpGetTunerType);
    if (tunerType == TunerR820t || tunerType == TunerR828d) {
        /* librtlsdr does not support offset tuning on R820T/R828D tuners. */
        EXPECT_STATUS(requestValue(OpSetOffsetTuning, 1), -2);
        return;
    }
    EXPECT_STATUS(requestValue(OpSetOffsetTuning, 1), STATUS_OK);
    CHECK(getValue(OpGetOffsetTuning) == 1);
    EXPECT_STATUS(requestValue(OpSetOffsetTuning, 0), STATUS_OK);
    CHECK(getValue(OpGetOffsetTuning) == 0);
}

static void testGetOffsetTuning(void)
{
    openDongle();
    CHECK(getValue(OpGetOffsetTuning) == 0);
}

static void testSetXtalFrequency(void)
{
    openDongle();
    EXPECT_STATUS(requestNone(OpGetXtalFrequency), STATUS_OK);
    uint32_t rtlFrequency = readUInt32();
    uint32_t tunerFrequency = readUInt32();

    EXPECT_STATUS(requestValues(OpSetXtalFrequency, rtlFrequency + 100, tunerFrequency), STATUS_OK);
    EXPECT_STATUS(requestNone(OpGetXtalFrequency), STATUS_OK);
    CHECK(readUInt32() == rtlFrequency + 100);
    CHECK(readUInt32() == tunerFrequency);

    EXPECT_STATUS(requestValues(OpSetXtalFrequency, rtlFrequency, tunerFrequency), STATUS_OK);
    EXPECT_STATUS(requestNone(OpGetXtalFrequency), STATUS_OK);
    CHECK(readUInt32() == rtlFrequency);
}

static void testGetXtalFrequency(void)
{
    openDongle();
    EXPECT_STATUS(requestNone(OpGetXtalFrequency), STATUS_OK);
    CHECK(readUInt32() > 0); /* RTL2832 crystal, Hz */
    CHECK(readUInt32() > 0); /* tuner crystal, Hz */
    checkPayloadRead();
}

static void testSetTunerIfGain(void)
{
    /* Applies on E4000 tuners; other tuners have no IF gain stages and
       librtlsdr accepts the call without changing anything. */
    openDongle();
    EXPECT_STATUS(requestValues(OpSetTunerIfGain, 1, 60), STATUS_OK); /* stage 1, 6.0 dB */
}

static void testGetIndexBySerial(void)
{
    struct UsbStrings strings, found;
    uint32_t count = listDongles(&strings);

    EXPECT_STATUS(requestString(OpGetIndexBySerial, strings.serial), STATUS_OK);
    uint32_t index = readUInt32();
    checkPayloadRead();
    CHECK(index < count);

    /* Dongles may share a serial; the index found must carry this one. */
    uint32_t testedIndex = deviceIndex;
    deviceIndex = index;
    listDongles(&found);
    deviceIndex = testedIndex;
    CHECK(strcmp(found.serial, strings.serial) == 0);

    EXPECT_STATUS(requestString(OpGetIndexBySerial, "no-such-feedpoint-serial"), -3); /* not found */
}

static void testGetDeviceName(void)
{
    struct UsbStrings strings;
    char name[256];
    listDongles(&strings);
    EXPECT_STATUS(requestValue(OpGetDeviceName, deviceIndex), STATUS_OK);
    CHECK(readString(name, sizeof name) > 0);
    checkPayloadRead();
}

static void testGetUsbStrings(void)
{
    struct UsbStrings listed, opened;
    listDongles(&listed);
    EXPECT_STATUS(requestValue(OpOpen, deviceIndex), STATUS_OK);

    EXPECT_STATUS(requestNone(OpGetUsbStrings), STATUS_OK);
    readString(opened.vendor, sizeof opened.vendor);
    readString(opened.product, sizeof opened.product);
    readString(opened.serial, sizeof opened.serial);
    checkPayloadRead();
    CHECK(strcmp(opened.vendor, listed.vendor) == 0);
    CHECK(strcmp(opened.product, listed.product) == 0);
    CHECK(strcmp(opened.serial, listed.serial) == 0);
}

/* --- rules for every operation -------------------------------------------- */

static void testProtocolUnknownOperation(void)
{
    EXPECT_STATUS(requestNone(OpUnassigned), STATUS_UNKNOWN_OPERATION);
    EXPECT_STATUS(requestValue(OpGetUsbStrings + 1, 0), STATUS_UNKNOWN_OPERATION);
    EXPECT_STATUS(requestNone(255), STATUS_UNKNOWN_OPERATION);
}

static void testProtocolNotOpen(void)
{
    static const uint8_t needsDevice[] = {
        OpClose, OpSetSampleRate, OpSetCenterFrequency, OpSetTunerBandwidth, OpSetAgcMode,
        OpSetTunerGainMode, OpSetTunerGain, OpResetBuffer, OpReadSync, OpGetTunerGains,
        OpGetTunerGain, OpGetSampleRate, OpGetCenterFrequency, OpGetTunerType,
        OpSetFrequencyCorrection, OpGetFrequencyCorrection, OpSetBiasTee, OpSetBiasTeeGpio,
        OpSetDirectSampling, OpGetDirectSampling, OpSetOffsetTuning, OpGetOffsetTuning,
        OpSetXtalFrequency, OpGetXtalFrequency, OpSetTunerIfGain, OpGetUsbStrings
    };
    for (size_t i = 0; i < sizeof needsDevice; i++)
        EXPECT_STATUS(requestValues(needsDevice[i], 0, 0), STATUS_NOT_OPEN);
}

static void testProtocolInputClosed(void)
{
    openDongle();
    CHECK(stopFeedpoint() == 0);

    startFeedpoint(); /* the dongle is free again */
    EXPECT_STATUS(requestValue(OpOpen, deviceIndex), STATUS_OK);
}

/* CMakeLists.txt registers one test per row, by name. */
static const struct {
    const char *name;
    void (*run)(void);
} cases[] = {
    { "Hello", testHello },
    { "List", testList },
    { "Open", testOpen },
    { "Open.AlreadyOpen", testOpenAlreadyOpen },
    { "Open.BadRequest", testOpenBadRequest },
    { "Close", testClose },
    { "SetSampleRate", testSetSampleRate },
    { "SetCenterFrequency", testSetCenterFrequency },
    { "SetTunerBandwidth", testSetTunerBandwidth },
    { "SetAgcMode", testSetAgcMode },
    { "SetTunerGainMode", testSetTunerGainMode },
    { "SetTunerGain", testSetTunerGain },
    { "ResetBuffer", testResetBuffer },
    { "ReadSync", testReadSync },
    { "GetVersion", testGetVersion },
    { "GetTunerGains", testGetTunerGains },
    { "GetTunerGain", testGetTunerGain },
    { "GetSampleRate", testGetSampleRate },
    { "GetCenterFrequency", testGetCenterFrequency },
    { "GetTunerType", testGetTunerType },
    { "SetFrequencyCorrection", testSetFrequencyCorrection },
    { "GetFrequencyCorrection", testGetFrequencyCorrection },
    { "SetBiasTee", testSetBiasTee },
    { "SetBiasTeeGpio", testSetBiasTeeGpio },
    { "SetDirectSampling", testSetDirectSampling },
    { "GetDirectSampling", testGetDirectSampling },
    { "SetOffsetTuning", testSetOffsetTuning },
    { "GetOffsetTuning", testGetOffsetTuning },
    { "SetXtalFrequency", testSetXtalFrequency },
    { "GetXtalFrequency", testGetXtalFrequency },
    { "SetTunerIfGain", testSetTunerIfGain },
    { "GetIndexBySerial", testGetIndexBySerial },
    { "GetDeviceName", testGetDeviceName },
    { "GetUsbStrings", testGetUsbStrings },
    { "Protocol.UnknownOperation", testProtocolUnknownOperation },
    { "Protocol.NotOpen", testProtocolNotOpen },
    { "Protocol.InputClosed", testProtocolInputClosed },
};

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: feedpoint_test <path to feedpoint> <case>\n");
        return EXIT_FAILURE;
    }
    feedpointPath = argv[1];
    caseName = argv[2];
    const char *device = getenv("FEEDPOINT_TEST_DEVICE");
    deviceIndex = device ? (uint32_t)strtoul(device, NULL, 10) : 0;

#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN); /* a Feedpoint that exits early shows up as a failed read */
#endif

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        if (strcmp(cases[i].name, caseName) != 0) continue;
        startFeedpoint();
        cases[i].run();
        CHECK(stopFeedpoint() == 0);
        return EXIT_SUCCESS;
    }
    fprintf(stderr, "unknown case: %s\n", caseName);
    return EXIT_FAILURE;
}
