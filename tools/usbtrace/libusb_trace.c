// SPDX-License-Identifier: MIT

#define _GNU_SOURCE

#include <libusb.h>

#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static unsigned long next_sequence(void)
{
    static unsigned long sequence;
    return __atomic_add_fetch(&sequence, 1UL, __ATOMIC_RELAXED);
}

static void* resolve_symbol(const char* name)
{
    void* symbol = dlsym(RTLD_NEXT, name);
    if (symbol == NULL) {
        const char* error = dlerror();
        dprintf(STDERR_FILENO, "libusb trace: cannot resolve %s: %s\n", name,
                error != NULL ? error : "unknown error");
        _exit(127);
    }
    return symbol;
}

static FILE* open_trace(void)
{
    const char* path = getenv("ZZRK_LIBUSB_TRACE");
    if (path == NULL || path[0] == '\0') {
        return stderr;
    }
    FILE* trace = fopen(path, "a");
    return trace != NULL ? trace : stderr;
}

static void close_trace(FILE* trace)
{
    fflush(trace);
    if (trace != stderr) {
        fclose(trace);
    }
}

static void write_hex(FILE* trace, const unsigned char* data, int length)
{
    for (int index = 0; index < length; ++index) {
        fprintf(trace, "%02x", data[index]);
    }
}

int libusb_bulk_transfer(libusb_device_handle* handle,
                         unsigned char endpoint, unsigned char* data,
                         int length, int* actual_length,
                         unsigned int timeout)
{
    typedef int (*bulk_fn)(libusb_device_handle*, unsigned char,
                           unsigned char*, int, int*, unsigned int);
    static bulk_fn real_bulk;
    if (real_bulk == NULL) {
        real_bulk = (bulk_fn)resolve_symbol("libusb_bulk_transfer");
    }

    const int result = real_bulk(handle, endpoint, data, length, actual_length,
                                 timeout);
    const int saved_errno = errno;
    const int actual = actual_length != NULL ? *actual_length : -1;
    int captured = (endpoint & LIBUSB_ENDPOINT_IN) != 0U ? actual : length;
    if (captured < 0) {
        captured = 0;
    }
    if (captured > length) {
        captured = length;
    }

    FILE* trace = open_trace();
    fprintf(trace,
            "seq=%lu kind=bulk direction=%s endpoint=0x%02x requested=%d "
            "timeout_ms=%u result=%d actual=%d data=",
            next_sequence(),
            (endpoint & LIBUSB_ENDPOINT_IN) != 0U ? "in" : "out", endpoint,
            length, timeout, result, actual);
    if (data != NULL) {
        write_hex(trace, data, captured);
    }
    fputc('\n', trace);
    close_trace(trace);
    errno = saved_errno;
    return result;
}

int libusb_control_transfer(libusb_device_handle* handle,
                            uint8_t request_type, uint8_t request,
                            uint16_t value, uint16_t index,
                            unsigned char* data, uint16_t length,
                            unsigned int timeout)
{
    typedef int (*control_fn)(libusb_device_handle*, uint8_t, uint8_t,
                              uint16_t, uint16_t, unsigned char*, uint16_t,
                              unsigned int);
    static control_fn real_control;
    if (real_control == NULL) {
        real_control = (control_fn)resolve_symbol("libusb_control_transfer");
    }

    const int result = real_control(handle, request_type, request, value, index,
                                    data, length, timeout);
    const int saved_errno = errno;
    int captured = (request_type & LIBUSB_ENDPOINT_IN) != 0U ? result : length;
    if (captured < 0) {
        captured = 0;
    }
    if (captured > (int)length) {
        captured = (int)length;
    }

    FILE* trace = open_trace();
    fprintf(trace,
            "seq=%lu kind=control request_type=0x%02x request=0x%02x "
            "value=0x%04x index=0x%04x requested=%u timeout_ms=%u "
            "result=%d data=",
            next_sequence(), request_type, request, value, index,
            (unsigned int)length, timeout, result);
    if (data != NULL) {
        write_hex(trace, data, captured);
    }
    fputc('\n', trace);
    close_trace(trace);
    errno = saved_errno;
    return result;
}

#define TRACE_SIMPLE_ONE_INT(function_name)                                    \
    int function_name(libusb_device_handle* handle, int value)                 \
    {                                                                           \
        typedef int (*function_fn)(libusb_device_handle*, int);                 \
        static function_fn real_function;                                       \
        if (real_function == NULL) {                                             \
            real_function = (function_fn)resolve_symbol(#function_name);         \
        }                                                                        \
        const int result = real_function(handle, value);                         \
        const int saved_errno = errno;                                           \
        FILE* trace = open_trace();                                              \
        fprintf(trace,                                                           \
                "seq=%lu kind=%s value=%d result=%d\n", next_sequence(),       \
                #function_name, value, result);                                  \
        close_trace(trace);                                                      \
        errno = saved_errno;                                                     \
        return result;                                                           \
    }

TRACE_SIMPLE_ONE_INT(libusb_set_configuration)
TRACE_SIMPLE_ONE_INT(libusb_detach_kernel_driver)
TRACE_SIMPLE_ONE_INT(libusb_set_auto_detach_kernel_driver)

int libusb_set_interface_alt_setting(libusb_device_handle* handle,
                                     int interface_number,
                                     int alternate_setting)
{
    typedef int (*set_alt_fn)(libusb_device_handle*, int, int);
    static set_alt_fn real_set_alt;
    if (real_set_alt == NULL) {
        real_set_alt =
            (set_alt_fn)resolve_symbol("libusb_set_interface_alt_setting");
    }
    const int result =
        real_set_alt(handle, interface_number, alternate_setting);
    const int saved_errno = errno;
    FILE* trace = open_trace();
    fprintf(trace,
            "seq=%lu kind=libusb_set_interface_alt_setting interface=%d "
            "alternate=%d result=%d\n",
            next_sequence(), interface_number, alternate_setting, result);
    close_trace(trace);
    errno = saved_errno;
    return result;
}

int libusb_clear_halt(libusb_device_handle* handle, unsigned char endpoint)
{
    typedef int (*clear_halt_fn)(libusb_device_handle*, unsigned char);
    static clear_halt_fn real_clear_halt;
    if (real_clear_halt == NULL) {
        real_clear_halt = (clear_halt_fn)resolve_symbol("libusb_clear_halt");
    }
    const int result = real_clear_halt(handle, endpoint);
    const int saved_errno = errno;
    FILE* trace = open_trace();
    fprintf(trace,
            "seq=%lu kind=libusb_clear_halt endpoint=0x%02x result=%d\n",
            next_sequence(), endpoint, result);
    close_trace(trace);
    errno = saved_errno;
    return result;
}

int libusb_reset_device(libusb_device_handle* handle)
{
    typedef int (*reset_fn)(libusb_device_handle*);
    static reset_fn real_reset;
    if (real_reset == NULL) {
        real_reset = (reset_fn)resolve_symbol("libusb_reset_device");
    }
    const int result = real_reset(handle);
    const int saved_errno = errno;
    FILE* trace = open_trace();
    fprintf(trace, "seq=%lu kind=libusb_reset_device result=%d\n",
            next_sequence(), result);
    close_trace(trace);
    errno = saved_errno;
    return result;
}
