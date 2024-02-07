/*
 * Portions of this code was derived from DMDExt
 *
 * https://github.com/freezy/dmd-extensions/blob/master/LibDmd/Output/Pixelcade/Pixelcade.cs
 */

#pragma once

#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>

#include "libserialport.h"

#define PIXELCADE_RESPONSE_ESTABLE_CONNECTION 0x00
#define PIXELCADE_COMMAND_RGB_LED_MATRIX_FRAME 0x1F
#define PIXELCADE_COMMAND_RGB_LED_MATRIX_ENABLE 0x1E
#define PIXELCADE_COMMAND_READ_TIMEOUT 100
#define PIXELCADE_COMMAND_WRITE_TIMEOUT 100
#define PIXELCADE_MAX_QUEUE_FRAMES 4
#define PIXELCADE_MAX_NO_RESPONSE 20

namespace DMDUtil
{

class PixelcadeDMD
{
 public:
  PixelcadeDMD(struct sp_port* pSerialPort, int width, int height);
  ~PixelcadeDMD();

  static PixelcadeDMD* Connect(const char* pDevice, int width, int height);
  void Update(uint16_t* pData);

 private:
  static PixelcadeDMD* Open(const char* pDevice, int width, int height);
  void Run();
  void EnableRgbLedMatrix(int shifterLen32, int rows);

  struct sp_port* m_pSerialPort;
  int m_width;
  int m_height;
  int m_length;

  std::thread* m_pThread;
  std::queue<uint16_t*> m_frames;
  std::mutex m_mutex;
  bool m_running;
};

}  // namespace DMDUtil