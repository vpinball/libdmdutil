/*
 * Portions of this code was derived from DMDExt
 *
 * https://github.com/freezy/dmd-extensions/blob/master/LibDmd/Output/PinDmd3/PinDmd3.cs
 */

#pragma once

#include <cstdint>
#include <mutex>

#include "libserialport.h"

#define PINDMD3_WIDTH 128
#define PINDMD3_HEIGHT 32
#define PINDMD3_BAUD_RATE 8176000
#define PINDMD3_COMMAND_RGB24 0x02
#define PINDMD3_COMMAND_READ_TIMEOUT 100
#define PINDMD3_COMMAND_WRITE_TIMEOUT 100
#define PINDMD3_MAX_NO_RESPONSE 20

namespace DMDUtil
{

class PinDMD3
{
 public:
  PinDMD3(struct sp_port* pSerialPort, const char* pFirmware);
  ~PinDMD3();

  static PinDMD3* Connect(const char* pDevice = nullptr);
  void UpdateRGB24(const uint8_t* pData);

  int GetWidth() const { return PINDMD3_WIDTH; }
  int GetHeight() const { return PINDMD3_HEIGHT; }
  const char* GetFirmware() const { return m_firmware; }

 private:
  static PinDMD3* Open(const char* pDevice, bool checkFirmware);
  static bool IsValidFirmware(const char* pFirmware);
  void Close();

  struct sp_port* m_pSerialPort;
  char m_firmware[64];
  uint8_t m_frameBuffer[PINDMD3_WIDTH * PINDMD3_HEIGHT * 3 + 2];
  std::mutex m_mutex;
  int m_errors;
};

}  // namespace DMDUtil
