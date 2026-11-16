# Feedpoint tests

The tests start Feedpoint and talk to it over its pipes, exactly as a host
does, against a real RTL-SDR dongle. There are no mocks and no test
libraries: `feedpoint_test.c` is plain C, built by CMake and run by CTest.

## Running them

1. Plug in an RTL-SDR dongle, and close anything that holds it (AirPulse, SDR
   software). Feedpoint cannot open a dongle another program is using.
2. Build the project. The build puts this platform's approved drivers beside
   `feedpoint`, so the tests run the real driver.
3. Run the tests:
   - **Visual Studio** (File > Open > Folder): Test > Test Explorer, then
     Run All.
   - **VS Code** with the CMake Tools extension: the Testing panel, then
     Run Tests.

Each case shows as its own test. Without a dongle, the cases that need one
exit as skipped; Visual Studio's Test Explorer shows that as failed, VS Code
as skipped.

To test a dongle other than the first, set `FEEDPOINT_TEST_DEVICE` to its
index (as List reports it).

Tests that tune use `TEST_FREQUENCY` at the top of `feedpoint_test.c`:
162.400 MHz (NOAA Weather Radio) by default. Change it to a strong, steady
signal near you.

The bias tee cases (SetBiasTee, SetBiasTeeGpio) put DC power on the antenna
port, so they appear only when the CMake option `FEEDPOINT_TEST_BIAS_TEE` is
on. Only turn it on with an antenna or load that can take DC power, never a
signal generator or anything else that is powered. To turn it on:

- **Visual Studio:** Project > CMake Cache > Edit CMake Cache, change
  `FEEDPOINT_TEST_BIAS_TEE:BOOL=OFF` to `ON`, and save.
- **VS Code:** CMake: Edit CMake Cache (UI), then tick
  `FEEDPOINT_TEST_BIAS_TEE`.

## Names

Each test is named after the operation it checks, exactly as
[PROTOCOL.md](../PROTOCOL.md) names it: `SetSampleRate` tests
`OpSetSampleRate`. A test for one specific situation adds it after a dot, as
in `Open.AlreadyOpen`. `Protocol.*` tests cover rules that apply to every
operation: unknown operations, operations before Open, and releasing the
dongle when Feedpoint's input closes.

Set operations that have a matching Get read their value back with it.

## Adding a case

Write a `test…` function in `feedpoint_test.c` and add a row to its `cases`
table. CMake registers one test per row, by name.
