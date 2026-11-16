# Feedpoint examples

Small hosts that start Feedpoint and talk to it over its pipes. They use only
their language's standard library.

| Example | Shows |
|---|---|
| [tune_and_read.py](tune_and_read.py) | Version, driver and dongle list; tuner type and gain list; tune, set a gain and read the settings back; read IQ and report its level and DC offset |

## Running tune_and_read.py

1. Put `feedpoint` (`feedpoint.exe` on Windows) and its driver in one folder,
   as in the release downloads, and plug in a dongle.
2. Change `FREQUENCY` at the top of the script to a strong signal near you
   (default: NOAA Weather Radio, 162.400 MHz).
3. Run `python tune_and_read.py <path to feedpoint>`.

It prints something like:

```
Feedpoint 1.1.0, protocol 2
Driver C:\feedpoint\librtlsdr-v4.dll
Dongle 0: Realtek RTL2838UHIDIR, serial 00000001
Tuner R820T, 29 gains from 0.0 to 49.6 dB
Tuned to 162.400 MHz at 2,048,000 samples/s, gain 29.7 dB
Read 262,144 bytes: level -21.4 dBFS, DC offset I +0.31 / Q -0.18
```

The level is a rough check that the dongle hears something: it should drop
when you tune to an empty frequency.
