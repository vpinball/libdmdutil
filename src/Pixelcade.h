/*
 * Portions of this code was derived from DMDExt
 *
 * https://github.com/freezy/dmd-extensions/blob/master/LibDmd/Output/Pixelcade/Pixelcade.cs
 */

#pragma once

#include "libserialport.h"

#include <cstdint>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

#define PIXELCADE_RESPONSE_ESTABLE_CONNECTION 0x00
#define PIXELCADE_COMMAND_RGB_LED_MATRIX_FRAME 0x1F
#define PIXELCADE_COMMAND_RGB_LED_MATRIX_ENABLE 0x1E
#define PIXELCADE_COMMAND_READ_TIMEOUT 100
#define PIXELCADE_COMMAND_WRITE_TIMEOUT 50
#define PIXELCADE_MAX_QUEUE_FRAMES 4

namespace DMDUtil {

class Pixelcade
{
public:
   Pixelcade(struct sp_port* pSerialPort, int width, int height);
   ~Pixelcade();

   static Pixelcade* Connect(const char* pDevice, int width, int height);
   void Update(uint16_t* pData);

private:
   static Pixelcade* Open(const char* pDevice, int width, int height);
   void Run();
   void Stop();
   void EnableRgbLedMatrix(int shifterLen32, int rows);

   struct sp_port* m_pSerialPort;
   int m_width;
   int m_height;
   int m_length;

   std::thread* m_pThread;
   std::queue<uint16_t*> m_frames;
   std::queue<uint16_t*> m_framePool;
   std::mutex m_mutex;
   std::condition_variable m_condVar;
   bool m_running;
};

}