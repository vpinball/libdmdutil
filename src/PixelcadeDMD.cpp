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

PixelcadeDMD::PixelcadeDMD(struct sp_port* pSerialPort, int width, int height, bool colorSwap, bool isV2)
{
  m_pSerialPort = pSerialPort;
  m_width = width;
  m_height = height;
  m_colorSwap = colorSwap;
  m_isV2 = isV2;
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
    free(m_frames.front().pData);
    m_frames.pop();
  }
}

PixelcadeDMD* PixelcadeDMD::Connect(const char* pDevice)
{
  PixelcadeDMD* pPixelcadeDMD = nullptr;

  if (pDevice && *pDevice != 0)
  {
    Log(DMDUtil_LogLevel_INFO, "Connecting to Pixelcade on %s...", pDevice);

    pPixelcadeDMD = Open(pDevice);

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
        pPixelcadeDMD = Open(sp_get_port_name(ppPorts[i]));
        if (pPixelcadeDMD) break;
      }
      sp_free_port_list(ppPorts);
    }

    if (!pPixelcadeDMD) Log(DMDUtil_LogLevel_INFO, "Unable to find Pixelcade");
  }

  return pPixelcadeDMD;
}

PixelcadeDMD* PixelcadeDMD::Open(const char* pDevice)
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
    return nullptr;
  }

  if (response[1] != 'I' || response[2] != 'O' || response[3] != 'I' || response[4] != 'O')
  {
    sp_close(pSerialPort);
    sp_free_port(pSerialPort);
    return nullptr;
  }

  char hardwareId[9] = {0};
  memcpy(hardwareId, response + 5, 8);

  char bootloaderId[9] = {0};
  memcpy(bootloaderId, response + 13, 8);

  char firmware[9] = {0};
  memcpy(firmware, response + 21, 8);

  int width = 128;
  int height = 32;
  bool isV2 = false;
  bool colorSwap = false;

  if (firmware[0] == 'P' && firmware[1] != 0 && firmware[2] != 0 && firmware[3] != 0)
  {
    if (firmware[2] == 'X')
    {
      width = 128;
      height = 32;
    }
    else if (firmware[2] == 'M')
    {
      width = 64;
      height = 32;
    }

    isV2 = (firmware[3] == 'R');

    colorSwap = (firmware[4] == 'C') && !isV2;
  }

  Log(DMDUtil_LogLevel_INFO,
      "Pixelcade found: device=%s, Hardware ID=%s, Bootloader ID=%s, Firmware=%s, Size=%dx%d, V2=%d, ColorSwap=%d",
      pDevice, hardwareId, bootloaderId, firmware, width, height, isV2, colorSwap);

  return new PixelcadeDMD(pSerialPort, width, height, colorSwap, isV2);
}

void PixelcadeDMD::Update(uint16_t* pData)
{
  uint16_t* pFrame = (uint16_t*)malloc(m_length * sizeof(uint16_t));
  memcpy(pFrame, pData, m_length * sizeof(uint16_t));

  PixelcadeFrame frame;
  frame.pData = pFrame;
  frame.format = PixelcadeFrameFormat::RGB565;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_frames.push(frame);
  }
}

void PixelcadeDMD::UpdateRGB24(uint8_t* pData)
{
  uint8_t* pFrame = (uint8_t*)malloc(m_length * 3);
  memcpy(pFrame, pData, m_length * 3);

  PixelcadeFrame frame;
  frame.pData = pFrame;
  frame.format = PixelcadeFrameFormat::RGB888;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_frames.push(frame);
  }
}

int PixelcadeDMD::BuildFrame(uint8_t* pFrameBuffer, size_t bufferSize, uint8_t command, const uint8_t* pData,
                             uint16_t dataLength)
{
  const size_t frameSize = 6 + dataLength;

  if (frameSize > bufferSize || dataLength > PIXELCADE_MAX_DATA_SIZE) return -1;

  uint16_t payloadLength = 1 + dataLength;

  pFrameBuffer[0] = PIXELCADE_FRAME_START_MARKER;
  pFrameBuffer[1] = PIXELCADE_FRAME_START_MARKER;
  pFrameBuffer[2] = payloadLength & 0xFF;
  pFrameBuffer[3] = (payloadLength >> 8) & 0xFF;
  pFrameBuffer[4] = command;

  if (dataLength > 0 && pData) memcpy(pFrameBuffer + 5, pData, dataLength);

  pFrameBuffer[5 + dataLength] = PIXELCADE_FRAME_END_DELIMITER;

  return frameSize;
}

void PixelcadeDMD::EnableRgbLedMatrix(int shifterLen32, int rows)
{
  uint8_t configData = (uint8_t)((shifterLen32 & 0x0F) | ((rows == 8 ? 0 : 1) << 4));

  if (m_isV2)
  {
    uint8_t frame[8];
    int frameSize = BuildFrame(frame, sizeof(frame), PIXELCADE_COMMAND_RGB_LED_MATRIX_ENABLE, &configData, 1);
    if (frameSize > 0) sp_blocking_write(m_pSerialPort, frame, frameSize, 0);
  }
  else
  {
    uint8_t data[2] = {PIXELCADE_COMMAND_RGB_LED_MATRIX_ENABLE, configData};
    sp_blocking_write(m_pSerialPort, data, 2, 0);
  }
}

void PixelcadeDMD::Run()
{
  if (m_running) return;

  m_running = true;

  m_pThread = new std::thread(
      [this]()
      {
        Log(DMDUtil_LogLevel_INFO, "PixelcadeDMD run thread starting");

        int shifterLen32 = m_width / 32;
        int rows = m_height;
        EnableRgbLedMatrix(shifterLen32, rows);

        int errors = 0;
        FrameUtil::ColorMatrix colorMatrix =
            (!m_colorSwap) ? FrameUtil::ColorMatrix::Rgb : FrameUtil::ColorMatrix::Rbg;

        const int maxFrameDataSize = m_length * 3;
        uint8_t* pFrameData = new uint8_t[maxFrameDataSize + 10];

        while (m_running)
        {
          PixelcadeFrame frame;
          frame.pData = nullptr;

          {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_frames.empty())
            {
              frame = m_frames.front();
              m_frames.pop();
            }

            while (m_frames.size() > PIXELCADE_MAX_QUEUE_FRAMES)
            {
              free(m_frames.front().pData);
              m_frames.pop();
            }
          }

          if (frame.pData)
          {
            int payloadSize = 0;
            uint8_t command = 0;
            enum sp_return response = SP_ERR_FAIL;

            if (m_isV2)
            {
              if (frame.format == PixelcadeFrameFormat::RGB565)
              {
                command = PIXELCADE_COMMAND_RGB565;
                payloadSize = m_length * 2;
                memcpy(pFrameData + 5, frame.pData, payloadSize);
              }
              else if (frame.format == PixelcadeFrameFormat::RGB888)
              {
                command = PIXELCADE_COMMAND_RGB888;
                payloadSize = m_length * 3;
                memcpy(pFrameData + 5, frame.pData, payloadSize);
              }

              int frameSize = BuildFrame(pFrameData, maxFrameDataSize + 10, command, pFrameData + 5, payloadSize);
              if (frameSize > 0)
                response = sp_blocking_write(m_pSerialPort, pFrameData, frameSize, PIXELCADE_COMMAND_WRITE_TIMEOUT);
            }
            else
            {
              command = PIXELCADE_COMMAND_RGB_LED_MATRIX_FRAME;
              payloadSize = m_length * 3 / 2;
              pFrameData[0] = command;
              FrameUtil::Helper::SplitIntoRgbPlanes((uint16_t*)frame.pData, m_length, m_width, rows / 2,
                                                     pFrameData + 1, colorMatrix);
              response = sp_blocking_write(m_pSerialPort, pFrameData, 1 + payloadSize, PIXELCADE_COMMAND_WRITE_TIMEOUT);
            }

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

            free(frame.pData);
          }
          else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        delete[] pFrameData;

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