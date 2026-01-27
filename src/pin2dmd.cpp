#include <cstdint>
#include <cstring>
#if __has_include(<libusb-1.0/libusb.h>)
#include <libusb-1.0/libusb.h>
#else
#include <libusb.h>
#endif

#include "pin2dmd.h"

//define PIN2DMD vendor id and product id
constexpr uint16_t kVid = 0x0314;
constexpr uint16_t kPid = 0xe457;

//endpoints for PIN2DMD communication
constexpr uint8_t kEpIn = 0x81;
constexpr uint8_t kEpOut = 0x01;

static bool g_pin2dmd = false;
static bool g_pin2dmdXL = false;
static bool g_pin2dmdHD = false;

static libusb_device** g_devices = nullptr;
static libusb_device_handle* g_deviceHandle = nullptr;
static libusb_device_descriptor g_descriptor;
static libusb_context* g_usbContext = nullptr;

static uint8_t g_outputBuffer[65536] = {};

int Pin2dmdInit() {
    static int ret = 0;
    static uint8_t product[256] = {};
    static const char* string = nullptr;

    libusb_init(&g_usbContext); /* initialize the library */

    int device_count = libusb_get_device_list(g_usbContext, &g_devices);
    if (device_count < 0)
    {
        return 0;
    }

    //Now look through the list that we just populated. We are trying to see if any of them match our device.
    int i;
    for (i = 0; i < device_count; i++) {
        libusb_get_device_descriptor(g_devices[i], &g_descriptor);
        if (kVid == g_descriptor.idVendor && kPid == g_descriptor.idProduct) {
            break;
        }
    }

    if (kVid == g_descriptor.idVendor && kPid == g_descriptor.idProduct) {
        ret = libusb_open(g_devices[i], &g_deviceHandle);
        if (ret < 0) {
            libusb_free_device_list(g_devices, 1);
            return ret;
        }
    }
    else {
        libusb_free_device_list(g_devices, 1);
        return 0;
    }

    libusb_free_device_list(g_devices, 1);

    if (g_deviceHandle == nullptr) {
        libusb_exit(g_usbContext);
        return 0;
    }

    ret = libusb_get_string_descriptor_ascii(g_deviceHandle, g_descriptor.iProduct, product, 256);

    if (libusb_claim_interface(g_deviceHandle, 0) < 0)  //claims the interface with the Operating System
    {
        //Closes a device opened since the claim interface is failed.
        libusb_close(g_deviceHandle);
        g_deviceHandle = nullptr;
        libusb_exit(g_usbContext);
        return 0;
    }

    string = (const char*)product;
    if (ret > 0) {
        if (strcmp(string, "PIN2DMD") == 0) {
            g_pin2dmd = true;
            ret = 1;
        }
        else if (strcmp(string, "PIN2DMD XL") == 0) {
            g_pin2dmdXL = true;
            ret = 2;
        }
        else if (strcmp(string, "PIN2DMD HD") == 0) {
            g_pin2dmdHD = true;
            ret = 3;
        }
        else {
            ret = 0;
        }
    }

    return ret;
}

bool Pin2dmdIsConnected()
{
    return (g_pin2dmd || g_pin2dmdXL || g_pin2dmdHD);
}

uint16_t Pin2dmdGetWidth()
{
    if (g_pin2dmdHD) return 256;
    if (g_pin2dmdXL) return 192;
    if (g_pin2dmd) return 128;
    return 0;
}

uint16_t Pin2dmdGetHeight()
{
    if (g_pin2dmdHD) return 64;
    if (g_pin2dmdXL) return 64;
    if (g_pin2dmd) return 32;
    return 0;
}

void Pin2dmdRender(uint16_t width, uint16_t height, uint8_t* buffer, int bitDepth) {
    if (!g_deviceHandle) return;
    if (
        (width == 256 && height == 64 && g_pin2dmdHD) ||
        (width == 192 && height == 64 && (g_pin2dmdXL || g_pin2dmdHD)) ||
        (width == 128 && height <= 32 && (g_pin2dmd || g_pin2dmdXL || g_pin2dmdHD))
    ) {
        int frameSizeInByte = width * height / 8;
        int chunksOf512Bytes = (frameSizeInByte / 512) * bitDepth;

        g_outputBuffer[0] = 0x81;
        g_outputBuffer[1] = 0xc3;
        if (bitDepth == 4 && width == 128 && height == 32) {
            g_outputBuffer[2] = 0xe7; // 4 bit header
            g_outputBuffer[3] = 0x00;
        } else {
            g_outputBuffer[2] = 0xe8; // non 4 bit header
            g_outputBuffer[3] = chunksOf512Bytes; // number of 512 byte chunks
        }

        const int payloadSize = chunksOf512Bytes * 512;
        if ((payloadSize + 4) > (int)sizeof(g_outputBuffer)) return;
        memcpy(&g_outputBuffer[4], buffer, payloadSize);

        // The OutputBuffer to be sent consists of a 4 byte header and a number of chunks of 512 bytes.
        libusb_bulk_transfer(g_deviceHandle, kEpOut, g_outputBuffer, payloadSize + 4, nullptr, 1000);
    }
}

void Pin2dmdRenderRaw(uint16_t width, uint16_t height, uint8_t* buffer, uint32_t frames) {
    if (!g_deviceHandle) return;
    if (
        (width == 256 && height == 64 && g_pin2dmdHD) ||
        (width == 192 && height == 64 && (g_pin2dmdXL || g_pin2dmdHD)) ||
        (width == 128 && height <= 32 && (g_pin2dmd || g_pin2dmdXL || g_pin2dmdHD))
    ) {
        int frameSizeInByte = width * height * 3;
        int chunksOf512Bytes = (frameSizeInByte / 512) * frames;
        int bufferSizeInBytes = frameSizeInByte * frames;
        if ((bufferSizeInBytes + 4) > (int)sizeof(g_outputBuffer)) return;

        g_outputBuffer[0] = 0x52; // RAW mode
        g_outputBuffer[1] = 0x80;
        g_outputBuffer[2] = 0x20;
        g_outputBuffer[3] = chunksOf512Bytes; // number of 512 byte chunks
        memcpy(&g_outputBuffer[4], buffer, bufferSizeInBytes);

        libusb_bulk_transfer(g_deviceHandle, kEpOut, g_outputBuffer, bufferSizeInBytes + 4, nullptr, 1000);
    }
}
