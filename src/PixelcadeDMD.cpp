/*
 * Portions of this code was derived from DMDExt
 *
 * https://github.com/freezy/dmd-extensions/blob/master/LibDmd/Output/Pixelcade/Pixelcade.cs
 */

#include "PixelcadeDMD.h"

#include <chrono>
#include <cstring>
#include <thread>

#include "FrameUtil.h"
#include "Logger.h"

namespace DMDUtil
{

PixelcadeDMD::PixelcadeDMD(struct sp_port* pSerialPort, int width, int height, bool colorSwap)
{
  m_pSerialPort = pSerialPort;
  m_width = width;
  m_height = height;
  m_colorSwap = colorSwap;
  m_length = width * height;
  m_pThread = nullptr;
  m_running = false;

  Run();
}

PixelcadeDMD::~PixelcadeDMD()
{
  if (m_pThread)
  {
    m_running = false;

    m_pThread->join();
    delete m_pThread;
    m_pThread = nullptr;
  }

  while (!m_frames.empty())
  {
    free(m_frames.front());
    m_frames.pop();
  }
}

PixelcadeDMD* PixelcadeDMD::Connect(const char* pDevice, int width, int height)
{
  PixelcadeDMD* pPixelcadeDMD = nullptr;

  if (pDevice && *pDevice != 0)
  {
    Log(DMDUtil_LogLevel_INFO, "Connecting to Pixelcade on %s...", pDevice);

    pPixelcadeDMD = Open(pDevice, width, height);

    if (!pPixelcadeDMD) Log(DMDUtil_LogLevel_INFO, "Unable to connect to Pixelcade on %s", pDevice);
  }
  else
  {
    Log(DMDUtil_LogLevel_INFO, "Searching for Pixelcade...");

    struct sp_port** ppPorts;
    enum sp_return result = sp_list_ports(&ppPorts);
    if (result == SP_OK)
    {
      for (int i = 0; ppPorts[i]; i++)
      {
        pPixelcadeDMD = Open(sp_get_port_name(ppPorts[i]), width, height);
        if (pPixelcadeDMD) break;
      }
      sp_free_port_list(ppPorts);
    }

    if (!pPixelcadeDMD) Log(DMDUtil_LogLevel_INFO, "Unable to find Pixelcade");
  }

  return pPixelcadeDMD;
}

PixelcadeDMD* PixelcadeDMD::Open(const char* pDevice, int width, int height)
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

  sp_set_baudrate(pSerialPort, 115200);
  sp_set_bits(pSerialPort, 8);
  sp_set_parity(pSerialPort, SP_PARITY_NONE);
  sp_set_stopbits(pSerialPort, 1);
  sp_set_xon_xoff(pSerialPort, SP_XONXOFF_DISABLED);

  sp_set_dtr(pSerialPort, SP_DTR_OFF);
  sp_set_rts(pSerialPort, SP_RTS_ON);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  sp_set_dtr(pSerialPort, SP_DTR_ON);

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  unsigned char response[29];

  result = sp_blocking_read(pSerialPort, response, 29, PIXELCADE_COMMAND_READ_TIMEOUT);
  if (response[0] != PIXELCADE_RESPONSE_ESTABLE_CONNECTION)
  {
    sp_close(pSerialPort);
    sp_free_port(pSerialPort);
    // Log(DMDUtil_LogLevel_INFO, "Pixelcade expected new connection to return 0x0, but got 0x%02d", response[0]);
    return nullptr;
  }

  if (response[1] != 'I' || response[2] != 'O' || response[3] != 'I' || response[4] != 'O')
  {
    sp_close(pSerialPort);
    sp_free_port(pSerialPort);
    // Log(DMDUtil_LogLevel_INFO, "Pixelcade expected magic code to equal IOIO but got %c%c%c%c", response[1],
    // response[2], response[3], response[4]);
    return nullptr;
  }

  char hardwareId[9] = {0};
  memcpy(hardwareId, response + 5, 8);

  char bootloaderId[9] = {0};
  memcpy(bootloaderId, response + 13, 8);

  char firmware[9] = {0};
  memcpy(firmware, response + 21, 8);

  Log(DMDUtil_LogLevel_INFO, "Pixelcade found: device=%s, Hardware ID=%s, Bootloader ID=%s, Firmware=%s", pDevice,
      hardwareId, bootloaderId, firmware);

  return new PixelcadeDMD(pSerialPort, width, height, (firmware[4] == 'C'));
}

void PixelcadeDMD::Update(uint16_t* pData)
{
  uint16_t* pFrame = (uint16_t*)malloc(m_length * sizeof(uint16_t));
  memcpy(pFrame, pData, m_length * sizeof(uint16_t));

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_frames.push(pFrame);
  }
}

void PixelcadeDMD::EnableRgbLedMatrix(int shifterLen32, int rows)
{
  uint8_t data[2] = {PIXELCADE_COMMAND_RGB_LED_MATRIX_ENABLE,
                     (uint8_t)((shifterLen32 & 0x0F) | ((rows == 8 ? 0 : 1) << 4))};
  sp_blocking_write(m_pSerialPort, data, 2, 0);
}

void PixelcadeDMD::Run()
{
  if (m_running) return;

  m_running = true;

  m_pThread = new std::thread(
      [this]()
      {
        Log(DMDUtil_LogLevel_INFO, "PixelcadeDMD run thread starting");
        EnableRgbLedMatrix(4, 16);

        int errors = 0;
        FrameUtil::ColorMatrix colorMatrix = (!m_colorSwap) ? FrameUtil::ColorMatrix::Rgb : FrameUtil::ColorMatrix::Rbg;

        while (m_running)
        {
          uint16_t* pFrame = nullptr;

          {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_frames.empty())
            {
              pFrame = m_frames.front();
              m_frames.pop();
            }

            while (m_frames.size() > PIXELCADE_MAX_QUEUE_FRAMES)
            {
              free(m_frames.front());
              m_frames.pop();
            }
          }

          if (pFrame)
          {
            uint8_t planes[128 * 32 * 3 / 2];
            FrameUtil::Helper::SplitIntoRgbPlanes(pFrame, 128 * 32, 128, 16, (uint8_t*)planes, colorMatrix);

            static uint8_t command = PIXELCADE_COMMAND_RGB_LED_MATRIX_FRAME;
            sp_blocking_write(m_pSerialPort, &command, 1, PIXELCADE_COMMAND_WRITE_TIMEOUT);

            enum sp_return response =
                sp_blocking_write(m_pSerialPort, planes, 128 * 32 * 3 / 2, PIXELCADE_COMMAND_WRITE_TIMEOUT);

            if (response > 0)
            {
              if (errors > 0)
              {
                Log(DMDUtil_LogLevel_INFO, "Communication to Pixelcade restored after %d frames", errors);
                errors = 0;
              }
            }
            else if (response == 0)
            {
              if (errors++ > PIXELCADE_MAX_NO_RESPONSE)
              {
                Log(DMDUtil_LogLevel_INFO, "Error while transmitting to Pixelcade: no response for the past %d frames",
                    PIXELCADE_MAX_NO_RESPONSE);
                m_running = false;
              }
            }
            else if (response == SP_ERR_FAIL)
            {
              char* pMessage = sp_last_error_message();
              Log(DMDUtil_LogLevel_INFO, "Error while transmitting to Pixelcade: %s", pMessage);
              sp_free_error_message(pMessage);
              m_running = false;
            }
          }
          else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        sp_flush(m_pSerialPort, SP_BUF_BOTH);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        sp_set_dtr(m_pSerialPort, SP_DTR_OFF);
        sp_set_rts(m_pSerialPort, SP_RTS_OFF);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        sp_close(m_pSerialPort);
        sp_free_port(m_pSerialPort);

        m_pSerialPort = nullptr;

        Log(DMDUtil_LogLevel_INFO, "PixelcadeDMD run thread finished");
      });
}

}  // namespace DMDUtil