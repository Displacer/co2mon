/*
 * co2mon - programming interface to CO2 sensor.
 * Copyright (C) 2015  Oleg Bulatov <oleg@bulatov.me>

 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>

#include "device.h"

static int
is_co2_device(libusb_device *dev)
{
    struct libusb_device_descriptor desc;
    int r;

    r = libusb_get_device_descriptor(dev, &desc);
    if (r < 0)
    {
        fprintf(stderr, "libusb_get_device_descriptor: error %d\n", r);
        return 0;
    }

    if (desc.idVendor == 0x04d9 && desc.idProduct == 0xa052)
    {
        return 1;
    }
    return 0;
}

libusb_device *
co2mon_find_device()
{
    libusb_device **devs;
    libusb_device *dev;
    libusb_device *result = NULL;
    ssize_t cnt;
    int i;

    cnt = libusb_get_device_list(NULL, &devs);
    if (cnt < 0)
    {
        fprintf(stderr, "libusb_get_device_list: error %ld\n", (long)cnt);
        return NULL;
    }

    for (i = 0; (dev = devs[i]) != NULL; ++i)
    {
        if (is_co2_device(dev))
        {
            result = dev;
            libusb_ref_device(dev);
            break;
        }
    }

    libusb_free_device_list(devs, 1);

    return result;
}

void
co2mon_release_device(libusb_device *dev)
{
    libusb_unref_device(dev);
}

libusb_device_handle *
co2mon_open_device(libusb_device *dev)
{
    libusb_device_handle *handle;
    int r = libusb_open(dev, &handle);
    if (r != 0)
    {
      fprintf(stderr, "libusb_open: error %s\n", libusb_strerror(r));
        return NULL;
    }

#ifdef __linux__
    libusb_detach_kernel_driver(handle, 0);
#endif

    r = libusb_claim_interface(handle, 0);
    if (r != 0)
    {
        fprintf(stderr, "libusb_claim_interface: error %d\n", r);
        libusb_close(handle);
        return NULL;
    }

    return handle;
}

void
co2mon_close_device(libusb_device_handle *handle)
{
    libusb_close(handle);
}

int
co2mon_send_magic_table(libusb_device_handle *handle, unsigned char magic_table[8])
{
    int r = libusb_control_transfer(
        handle,
        LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
        LIBUSB_REQUEST_SET_CONFIGURATION,
        0x0300, 0,
        magic_table, sizeof(magic_table),
        2000 /* milliseconds */);
    if (r < 0 || r != sizeof(magic_table))
    {
        fprintf(stderr, "libusb_control_transfer(out, magic_table): error %d\n", r);
        return 0;
    }
    return 1;
}

static void
swap_char(unsigned char *a, unsigned char *b)
{
    unsigned char tmp = *a;
    *a = *b;
    *b = tmp;
}

static void
decode_buf(char result[8], unsigned char buf[8], unsigned char magic_table[8])
{
    const unsigned char magic_word[8] = "Htemp99e";

    int i;
    unsigned char tmp;

    swap_char(&buf[0], &buf[2]);
    swap_char(&buf[1], &buf[4]);
    swap_char(&buf[3], &buf[7]);
    swap_char(&buf[5], &buf[6]);

    for (i = 0; i < 8; ++i)
    {
        buf[i] ^= magic_table[i];
    }

    tmp = (buf[7] << 5);
    result[7] = (buf[6] << 5) | (buf[7] >> 3);
    result[6] = (buf[5] << 5) | (buf[6] >> 3);
    result[5] = (buf[4] << 5) | (buf[5] >> 3);
    result[4] = (buf[3] << 5) | (buf[4] >> 3);
    result[3] = (buf[2] << 5) | (buf[3] >> 3);
    result[2] = (buf[1] << 5) | (buf[2] >> 3);
    result[1] = (buf[0] << 5) | (buf[1] >> 3);
    result[0] = tmp | (buf[0] >> 3);

    for (i = 0; i < 8; ++i)
    {
        result[i] -= (magic_word[i] << 4) | (magic_word[i] >> 4);
    }
}

int
co2mon_read_data(libusb_device_handle *handle, unsigned char magic_table[8], unsigned char result[8])
{
    int actual_length;
    unsigned char data[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    int r = libusb_interrupt_transfer(handle,
        LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_INTERFACE,
        data, sizeof(data), &actual_length,
        5000 /* milliseconds */);
    if (r < 0)
    {
        fprintf(stderr, "libusb_interrupt_transfer(in, data): error %d\n", r);
        return r;
    }
    if (actual_length != sizeof(data))
    {
        fprintf(stderr, "libusb_interrupt_transfer(in, data): trasferred %d bytes, expected %lu bytes\n", actual_length, (unsigned long)sizeof(data));
        return 0;
    }

    decode_buf(result, data, magic_table);
    return actual_length;
}
