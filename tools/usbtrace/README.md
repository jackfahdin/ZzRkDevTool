# libusb API Trace Helper

`libusb_trace.c` is a Linux-only development helper for recording the exact
buffers passed through selected synchronous libusb APIs. It was written for
project-owned RockUSB protocol observations and is not part of the application
or its normal build.

The helper records bulk and control payloads and reports calls which can change
USB state, including reset, configuration, alternate-setting, detach and
clear-halt operations. It observes calls but does not block them. Loading it
does not make an unknown command safe; inspect the target executable and its
arguments before running it against hardware.

Build it from the repository root:

```bash
mkdir -p build/trace
cc -std=c17 -O2 -fPIC -shared -Wall -Wextra -Werror \
  -IthirdParty/libusb/include/libusb-1.0 \
  tools/usbtrace/libusb_trace.c -ldl \
  -o build/trace/libusb_trace.so
```

Set `ZZRK_LIBUSB_TRACE` to a new, nonexistent output path. The recorder opens
the file in append mode once per intercepted call, so reusing a path would mix
runs. A simultaneous `strace` can correlate the API records with Linux usbfs
URB submissions:

```bash
strace -f -ttt -T -v -yy -xx -s 4096 \
  -o /tmp/rockusb-kernel.trace \
  -e trace=openat,close,ioctl \
  timeout -k 2s 15s \
  env LD_PRELOAD="$PWD/build/trace/libusb_trace.so" \
      ZZRK_LIBUSB_TRACE=/tmp/rockusb-api.trace \
      /path/to/audited-tool read-only-command
```

Record the device identity before and after the command, retain stdout/stderr,
and hash the executable, API trace and kernel trace. Reject a capture if the
physical device re-enumerates, the command opens a different USB node, or the
trace contains an operation outside the reviewed sequence.
