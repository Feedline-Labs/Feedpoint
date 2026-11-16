/*
 * Feedpoint protocol constants, shared by Feedpoint and its tests.
 * Copyright (C) 2026 Feedline Labs. GPL-3.0-or-later; see LICENSE.
 * The protocol itself is described in PROTOCOL.md.
 */

#ifndef FEEDPOINT_PROTOCOL_H
#define FEEDPOINT_PROTOCOL_H

#define PROTOCOL_VERSION 2

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
    OpReadSync = 12,
    OpGetVersion = 13,
    OpGetTunerGains = 14,
    OpGetTunerGain = 15,
    OpGetSampleRate = 16,
    OpGetCenterFrequency = 17,
    OpGetTunerType = 18,
    OpSetFrequencyCorrection = 19,
    OpGetFrequencyCorrection = 20,
    OpSetBiasTee = 21,
    OpSetBiasTeeGpio = 22,
    OpSetDirectSampling = 23,
    OpGetDirectSampling = 24,
    OpSetOffsetTuning = 25,
    OpGetOffsetTuning = 26,
    OpSetXtalFrequency = 27,
    OpGetXtalFrequency = 28,
    OpSetTunerIfGain = 29,
    OpGetIndexBySerial = 30,
    OpGetDeviceName = 31,
    OpGetUsbStrings = 32
};

/* Tuner types reported by GetTunerType, as librtlsdr numbers them. */
enum TunerType {
    TunerUnknown = 0,
    TunerE4000 = 1,
    TunerFc0012 = 2,
    TunerFc0013 = 3,
    TunerFc2580 = 4,
    TunerR820t = 5,
    TunerR828d = 6
};

/* Feedpoint's own status codes, kept clear of librtlsdr/libusb codes. */
#define STATUS_OK 0
#define STATUS_UNKNOWN_OPERATION -9001
#define STATUS_BAD_REQUEST -9002
#define STATUS_NOT_OPEN -9003
#define STATUS_ALREADY_OPEN -9004
#define STATUS_OUT_OF_MEMORY -9005

#endif
