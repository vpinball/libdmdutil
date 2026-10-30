/*
 * Portions of this code was derived from DMDExt
 *
 * https://github.com/freezy/dmd-extensions/blob/master/LibDmd/Output/PinDmd3/PinDmd3.cs
 */

#include "PinDMD3.h"

#include <cctype>
#include <chrono>
#include <cstring>
#include <thread>

#include "DMDUtil/Logger.h"

namespace DMDUtil
{

PinDMD3::PinDMD3(struct sp_port* pSerialPort, const char* pFirmware)
{
  m_pSerialPort = pSerialPort;
  m_errors = 0;
  strncpy(m_firmware, pFirmware, sizeof(m_firmware) - 1);
  m_firmware[sizeof(m_firmware) - 1] = 0;

  memset(m_frameBuffer, 0, sizeof(m_frameBuffer));
  m_frameBuffer[0] = PINDMD3_COMMAND_RGB24;
  m_frameBuffer[sizeof(m_frameBuffer) - 1] = PINDMD3_COMMAND_RGB24;
}

PinDMD3::~PinDMD3()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_pSerialPort)
  {
    memset(m_frameBuffer + 1, 0, sizeof(m_frameBuffer) - 2);
    sp_blocking_write(m_pSerialPort, m_frameBuffer, sizeof(m_frameBuffer), PINDMD3_COMMAND_WRITE_TIMEOUT);
    Close();
  }
}

void PinDMD3::Close()
{
  sp_flush(m_pSerialPort, SP_BUF_BOTH);
  sp_close(m_pSerialPort);
  sp_free_port(m_pSerialPort);
  m_pSerialPort = nullptr;
}

PinDMD3* PinDMD3::Connect(const char* pDevice)
{
  PinDMD3* pPinDMD3 = nullptr;

  if (pDevice && *pDevice != 0)
  {
    Log(DMDUtil_LogLevel_INFO, "Connecting to PinDMD3 on %s...", pDevice);

    pPinDMD3 = Open(pDevice, false);

    if (!pPinDMD3) Log(DMDUtil_LogLevel_INFO, "Unable to connect to PinDMD3 on %s", pDevice);
  }
  else
  {
    Log(DMDUtil_LogLevel_INFO, "Searching for PinDMD3...");

    struct sp_port** ppPorts;
    enum sp_return result = sp_list_ports(&ppPorts);
    if (result == SP_OK)
    {
      for (int i = 0; ppPorts[i]; i++)
      {
        pPinDMD3 = Open(sp_get_port_name(ppPorts[i]), true);
        if (pPinDMD3) break;
      }
      sp_free_port_list(ppPorts);
    }

    if (!pPinDMD3) Log(DMDUtil_LogLevel_INFO, "Unable to find PinDMD3");
  }

  return pPinDMD3;
}

bool PinDMD3::IsValidFirmware(const char* pFirmware)
{
  static const char prefix[] = "rev-vpin-";
  const size_t prefixLength = sizeof(prefix) - 1;

  if (strlen(pFirmware) <= prefixLength) return false;

  for (size_t i = 0; i < prefixLength; i++)
  {
    if (tolower((unsigned char)pFirmware[i]) != prefix[i]) return false;
  }

  const char* p = pFirmware + prefixLength;
  if (!isdigit((unsigned char)*p)) return false;
  while (isdigit((unsigned char)*p)) p++;
  if (*p == 'R' || *p == 'r') p++;

  return *p == 0;
}

PinDMD3* PinDMD3::Open(const char* pDevice, bool checkFirmware)
{
  struct sp_port* pSerialPort = nullptr;
  enum sp_return result = sp_get_port_by_name(pDevice, &pSerialPort);
  if (result != SP_OK) return nullptr;

  result = sp_open(pSerialPort, SP_MODE_READ_WRITE);
  if (result != SP_OK)
  {
    sp_free_port(pSerialPort);
    return nullptr;
  }

  sp_set_baudrate(pSerialPort, PINDMD3_BAUD_RATE);
  sp_set_bits(pSerialPort, 8);
  sp_set_parity(pSerialPort, SP_PARITY_NONE);
  sp_set_stopbits(pSerialPort, 1);
  sp_set_flowcontrol(pSerialPort, SP_FLOWCONTROL_NONE);
  sp_flush(pSerialPort, SP_BUF_BOTH);

  const uint8_t identify[2] = {0x42, 0x42};
  sp_blocking_write(pSerialPort, identify, sizeof(identify), PINDMD3_COMMAND_WRITE_TIMEOUT);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  uint8_t response[100] = {0};
  int bytesRead = sp_blocking_read(pSerialPort, response, sizeof(response), PINDMD3_COMMAND_READ_TIMEOUT);

  char firmware[64] = {0};
  if (bytesRead > 2)
  {
    size_t length = 0;
    for (int i = 2; i < bytesRead && response[i] != 0 && length < sizeof(firmware) - 1; i++)
    {
      firmware[length++] = (char)response[i];
    }
  }

  if (checkFirmware && !IsValidFirmware(firmware))
  {
    sp_flush(pSerialPort, SP_BUF_BOTH);
    sp_close(pSerialPort);
    sp_free_port(pSerialPort);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return nullptr;
  }

  Log(DMDUtil_LogLevel_INFO, "PinDMD3 found: device=%s, Firmware=%s, Size=%dx%d", pDevice, firmware,
      bytesRead > 1 ? response[0] : 0, bytesRead > 1 ? response[1] : 0);

  const uint8_t init[10] = {0x43, 0x13, 0x55, 0xdb, 0x5c, 0x94, 0x4e, 0x00, 0x00, 0x43};
  sp_blocking_write(pSerialPort, init, sizeof(init), PINDMD3_COMMAND_WRITE_TIMEOUT);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  uint8_t initResponse[20];
  sp_blocking_read(pSerialPort, initResponse, sizeof(initResponse), PINDMD3_COMMAND_READ_TIMEOUT);

  return new PinDMD3(pSerialPort, firmware);
}

void PinDMD3::UpdateRGB24(const uint8_t* pData)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (!m_pSerialPort) return;

  memcpy(m_frameBuffer + 1, pData, PINDMD3_WIDTH * PINDMD3_HEIGHT * 3);

  enum sp_return response =
      sp_blocking_write(m_pSerialPort, m_frameBuffer, sizeof(m_frameBuffer), PINDMD3_COMMAND_WRITE_TIMEOUT);

  if (response > 0)
  {
    if (m_errors > 0)
    {
      Log(DMDUtil_LogLevel_INFO, "Communication to PinDMD3 restored after %d frames", m_errors);
      m_errors = 0;
    }
  }
  else if (response == 0)
  {
    if (m_errors++ == PINDMD3_MAX_NO_RESPONSE)
    {
      Log(DMDUtil_LogLevel_INFO, "Error while transmitting to PinDMD3: no response for the past %d frames",
          PINDMD3_MAX_NO_RESPONSE);
    }
  }
  else
  {
    char* pMessage = sp_last_error_message();
    Log(DMDUtil_LogLevel_INFO, "Error while transmitting to PinDMD3: %s", pMessage);
    sp_free_error_message(pMessage);
    Close();
  }
}

}  // namespace DMDUtil
