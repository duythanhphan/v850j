/*
 * Helpers for flash programming Renesas V850ES/Jx3-L devices
 *
 * Copyright (c) 2011 Andreas Färber <andreas.faerber@web.de>
 *
 * Licensed under the GNU LGPL version 2.1 or (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>
#include "v850j.h"

#define V850J_TIMEOUT_MS (3000 + 1000)

static uint8_t checksum(uint8_t *data, size_t data_length)
{
    uint8_t checksum = 0x00;
    for (size_t i = 0; i < data_length; i++) {
        checksum -= data[i];
    }
    return checksum;
}

#define RETRY_MAX 5
#define ENDPOINT_OUT 0x02
#define ENDPOINT_IN  0x81

static int send_command_frame(libusb_device_handle *handle, uint8_t command,
                              const uint8_t *buffer, uint8_t buffer_length)
{
    uint8_t buf[3 + 255 + 2];
    buf[0] = V850ESJx3L_SOH;
    buf[1] = (buffer_length == 255) ? 0 : (buffer_length + 1);
    buf[2] = command;
    if (buffer_length > 0)
        memcpy(&buf[3], buffer, buffer_length);
    buf[3 + buffer_length] = checksum(&buf[1], buffer_length + 2);
    buf[3 + buffer_length + 1] = V850ESJx3L_ETX;

    printf("Sending command frame:");
    for(int i = 0; i < buffer_length + 5; i++) {
        printf(" %02" PRIX8, buf[i]);
    }
    printf("\n");

    uint8_t endpoint = ENDPOINT_OUT;
    int transferred;
    int ret;
    int try = 0;
    do {
        ret = libusb_bulk_transfer(handle, endpoint, buf, buffer_length + 5,
                                   &transferred, V850J_TIMEOUT_MS);
        if (ret == LIBUSB_ERROR_PIPE) {
            libusb_clear_halt(handle, endpoint);
        }
        try++;
    } while ((ret == LIBUSB_ERROR_PIPE) && (try < RETRY_MAX));
    if (ret != LIBUSB_SUCCESS) {
        fprintf(stderr, "%s: sending failed: %d\n", __func__, ret);
        return -1;
    }
    return 0;
}

static int receive_data_frame(libusb_device_handle *handle, uint8_t *buffer, size_t *length)
{
    uint8_t buf[2 + 256 + 2];
    uint8_t endpoint = ENDPOINT_IN;
    int transferred = 0;
    int ret;
    int try = 0;
    do {
        ret = libusb_bulk_transfer(handle, endpoint, buf, 2,
                                   &transferred, V850J_TIMEOUT_MS);
        if (ret == LIBUSB_ERROR_PIPE) {
            libusb_clear_halt(handle, endpoint);
        }
        try++;
    } while ((ret == LIBUSB_ERROR_PIPE) && (try < RETRY_MAX));
    if (ret != LIBUSB_SUCCESS) {
        fprintf(stderr, "%s: receiving header failed: %d (transferred %d)\n", __func__, ret, transferred);
        return -1;
    }
    if (buf[0] != V850ESJx3L_STX) {
        fprintf(stderr, "%s: no data frame: %02" PRIX8 "\n", __func__, buf[0]);
        return -1;
    }
    if (transferred < 2) {
        try = 0;
        do {
            ret = libusb_bulk_transfer(handle, endpoint, buf + 1, 1,
                                       &transferred, V850J_TIMEOUT_MS);
            if (ret == LIBUSB_ERROR_PIPE) {
                libusb_clear_halt(handle, endpoint);
            }
            try++;
        } while ((ret == LIBUSB_ERROR_PIPE) && (try < RETRY_MAX));
        if (ret != LIBUSB_SUCCESS) {
            fprintf(stderr, "%s: receiving length failed: %d\n", __func__, ret);
            return -1;
        }
    }
    size_t len = (buf[1] == 0) ? 256 : buf[1];

    int received = 0;
    do {
        try = 0;
        do {
            ret = libusb_bulk_transfer(handle, endpoint, buf + 2 + received, len + 2 - received,
                                       &transferred, V850J_TIMEOUT_MS);
            if (ret == LIBUSB_ERROR_PIPE) {
                libusb_clear_halt(handle, endpoint);
            }
            try++;
        } while ((ret == LIBUSB_ERROR_PIPE) && (try < RETRY_MAX));
        if (ret != LIBUSB_SUCCESS) {
            fprintf(stderr, "%s: receiving data failed: %d\n", __func__, ret);
            return -1;
        }
        if (ret == 0)
            received += transferred;
    } while (received < len + 2);

    printf("Received data frame:");
    for(int i = 0; i < len + 4; i++) {
        printf(" %02" PRIX8, buf[i]);
    }
    printf("\n");

    memcpy(buffer, buf + 2, len);
    *length = len;
    return 0;
}

int v850j_reset(libusb_device_handle *handle)
{
    uint32_t fx = 5000000;
    uint32_t fxx = 4 * fx;
    useconds_t tCOM = (620.0 / fxx) * 1000000 + 15;
    useconds_t t12 = (30000.0 / fxx) * 1000000;
    printf("t12 = %u\n", t12);
    useconds_t t2C = (30000.0 / fxx) * 1000000;
    printf("t2C = %u\n", t2C);

    usleep(tCOM);

    int ret;
    uint8_t endpoint = ENDPOINT_OUT;
    uint8_t x = 0x00;
    int transferred;
    int try = 0;
    do {
        ret = libusb_bulk_transfer(handle, endpoint, &x, 1,
                                   &transferred, V850J_TIMEOUT_MS);
        if (ret == LIBUSB_ERROR_PIPE) {
            libusb_clear_halt(handle, endpoint);
        }
        try++;
    } while ((ret == LIBUSB_ERROR_PIPE) && (try < RETRY_MAX));
    if (ret != LIBUSB_SUCCESS) {
        fprintf(stderr, "%s: sending (i) failed: %d\n", __func__, ret);
        return -1;
    }
    usleep(t12);

    x = 0x00;
    try = 0;
    do {
        ret = libusb_bulk_transfer(handle, endpoint, &x, 1,
                                   &transferred, V850J_TIMEOUT_MS);
        if (ret == LIBUSB_ERROR_PIPE) {
            libusb_clear_halt(handle, endpoint);
        }
        try++;
    } while ((ret == LIBUSB_ERROR_PIPE) && (try < RETRY_MAX));
    if (ret != LIBUSB_SUCCESS) {
        fprintf(stderr, "%s: sending (ii) failed: %d\n", __func__, ret);
        return -1;
    }
    usleep(t2C);

    ret = send_command_frame(handle, V850ESJx3L_RESET, NULL, 0);
    if (ret != 0)
        return ret;
    uint8_t buf[256];
    size_t len;
    ret = receive_data_frame(handle, buf, &len);
    if (ret != 0)
        return ret;
    if (buf[0] != V850ESJx3L_STATUS_ACK) {
        fprintf(stderr, "%s: no ACK: %02" PRIX8 "\n", __func__, buf[0]);
        return -1;
    }
    return 0;
}

int v850j_get_silicon_signature(libusb_device_handle *handle)
{
    int ret;
    ret = send_command_frame(handle, V850ESJx3L_SILICON_SIGNATURE, NULL, 0);
    if (ret != 0)
        return ret;
    uint8_t buf[256];
    size_t len;
    ret = receive_data_frame(handle, buf, &len);
    if (ret != 0)
        return ret;
    if (buf[0] != V850ESJx3L_STATUS_ACK) {
        fprintf(stderr, "%s: no ACK: %02" PRIX8 "\n", __func__, buf[0]);
        return -1;
    }
    ret = receive_data_frame(handle, buf, &len);
    if (ret != 0)
        return ret;
    return 0;
}