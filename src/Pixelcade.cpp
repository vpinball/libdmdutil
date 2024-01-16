/*
 * Portions of this code was derived from DMDExt
 *
 * https://github.com/freezy/dmd-extensions/blob/master/LibDmd/Output/Pixelcade/Pixelcade.cs
 */

#include "Pixelcade.h"
#include "FrameUtil.h"
#include "Logger.h"

#include <chrono>
#include <thread>
#include <cstring>

namespace DMDUtil {

Pixelcade::Pixelcade(struct sp_port* pSerialPort, int width, int height)
{
   m_pSerialPort = pSerialPort;
   m_width = width;
   m_height = height;
   m_length = width * height;
   m_pThread = nullptr;
   m_running = false;

   Run();
}

Pixelcade::~Pixelcade()
{
   if (m_pThread) {
      m_running = false;

      m_pThread->join();
      delete m_pThread;
      m_pThread = nullptr;
   }

   while (!m_frames.empty()) {
      free(m_frames.front());
      m_frames.pop();
   }
}

Pixelcade* Pixelcade::Connect(const char* pDevice, int width, int height)
{
   Pixelcade* pPixelcade = nullptr;

   if (pDevice && *pDevice != 0) {
      Log("Connecting to Pixelcade on %s...", pDevice);

      pPixelcade = Open(pDevice, width, height);

      if (!pPixelcade)
         Log("Unable to connect to Pixelcade on %s", pDevice);
   }
   else {
      Log("Searching for Pixelcade...");

      struct sp_port** ppPorts;
      enum sp_return result = sp_list_ports(&ppPorts);
      if (result == SP_OK) {
         for (int i = 0; ppPorts[i]; i++) {
            pPixelcade = Open(sp_get_port_name(ppPorts[i]), width, height);
            if (pPixelcade)
               break;
         }
         sp_free_port_list(ppPorts);
      }

      if (!pPixelcade)
         Log("Unable to find Pixelcade");
   }

   return pPixelcade;
}

Pixelcade* Pixelcade::Open(const char* pDevice, int width, int height)
{
   struct sp_port* pSerialPort = nullptr;
   enum sp_return result = sp_get_port_by_name(pDevice, &pSerialPort);
   if (result != SP_OK)
      return nullptr;

   result = sp_open(pSerialPort, SP_MODE_READ_WRITE);
   if (result != SP_OK) {
     sp_free_port(pSerialPort);
     return nullptr;
   }

   sp_set_dtr(pSerialPort, SP_DTR_OFF);
   sp_set_rts(pSerialPort, SP_RTS_ON);

   std::this_thread::sleep_for(std::chrono::milliseconds(100));

   sp_set_dtr(pSerialPort, SP_DTR_ON);

   std::this_thread::sleep_for(std::chrono::milliseconds(100));

   unsigned char response[29];

   result = sp_blocking_read(pSerialPort, response, 29, PIXELCADE_COMMAND_READ_TIMEOUT);
   if (response[0] != PIXELCADE_RESPONSE_ESTABLE_CONNECTION) {
     sp_close(pSerialPort);
     sp_free_port(pSerialPort);
     //Log("Pixelcade: expected new connection to return 0x0, but got 0x%02d", response[0]);
     return nullptr;
   }

   if (response[1] != 'I' || response[2] != 'O' || response[3] != 'I' || response[4] != 'O') {
     sp_close(pSerialPort);
     sp_free_port(pSerialPort);
     //Log("Pixelcade: expected magic code to equal IOIO but got %c%c%c%c", response[1], response[2], response[3], response[4]);
     return nullptr;
   }

   char hardwareId[9] = {0};
   memcpy(hardwareId, response + 5, 8);
  
   char bootloaderId[9] = {0};
   memcpy(bootloaderId, response + 13, 8);

   char firmware[9] = {0};
   memcpy(firmware, response + 21, 8);

   Log("Pixelcade found: device=%s, Hardware ID=%s, Bootloader ID=%s, Firmware=%s", pDevice, hardwareId, bootloaderId, firmware);

   return new Pixelcade(pSerialPort, width, height);
}

void Pixelcade::Update(uint16_t* pData)
{
   uint16_t* pFrame = (uint16_t*)malloc(m_length * sizeof(uint16_t));
   memcpy(pFrame, pData, m_length * sizeof(uint16_t));

   {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_frames.push(pFrame);
   }
}

void Pixelcade::EnableRgbLedMatrix(int shifterLen32, int rows)
{
   uint8_t data[2] = { PIXELCADE_COMMAND_RGB_LED_MATRIX_ENABLE, (uint8_t)((shifterLen32 & 0x0F) | ((rows == 8 ? 0 : 1) << 4)) };
   sp_blocking_write(m_pSerialPort, data, 2, 0);
}

void Pixelcade::Run()
{
   if (m_running)
      return;

   m_running = true;

   m_pThread = new std::thread([this]() {
      Log("Pixelcade run thread starting");
      EnableRgbLedMatrix(4, 16);

      int errors = 0;

      while (m_running) {
         uint16_t* pFrame = nullptr;

         {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_frames.empty()) {
               pFrame = m_frames.front();
               m_frames.pop();
            }

            while (m_frames.size() > PIXELCADE_MAX_QUEUE_FRAMES) {
               free(m_frames.front());
               m_frames.pop();
            }
         }

         if (pFrame) {
            static uint8_t command = PIXELCADE_COMMAND_RGB_LED_MATRIX_FRAME;
            sp_blocking_write(m_pSerialPort, &command, 1, PIXELCADE_COMMAND_WRITE_TIMEOUT);

            uint8_t planes[128 * 32 * 3 / 2];
            if (m_width == 128 && m_height == 32)
               FrameUtil::SplitIntoRgbPlanes(pFrame, 128 * 32, 128, 16, (uint8_t*)planes);
            else {
               uint16_t scaledFrame[128 * 32];
               FrameUtil::ResizeRgb565Bilinear(pFrame, m_width, m_height, scaledFrame, 128, 32);
               FrameUtil::SplitIntoRgbPlanes(scaledFrame, 128 * 32, 128, 16, (uint8_t*)planes);
            }

            enum sp_return response = sp_blocking_write(m_pSerialPort, planes, 128 * 32 * 3 / 2, PIXELCADE_COMMAND_WRITE_TIMEOUT);

            if (response == SP_ERR_FAIL) {
               char* pMessage = sp_last_error_message();
               Log("Error while transmitting to Pixelcade: %s", pMessage);
               sp_free_error_message(pMessage);
               m_running = false;
            }
            else if ((int)response == 0) {
               if (++errors > PIXELCADE_MAX_NO_RESPONSE) {
                  Log("Error while transmitting to Pixelcade: no response for the past %d frames.", PIXELCADE_MAX_NO_RESPONSE);
                  m_running = false;
               }
            }
         }
         else
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }

      sp_set_dtr(m_pSerialPort, SP_DTR_OFF);
      sp_close(m_pSerialPort);
      sp_free_port(m_pSerialPort);

      m_pSerialPort = nullptr;

      Log("Pixelcade run thread finished");
   });
}

}