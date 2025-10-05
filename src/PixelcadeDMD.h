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
#define PIXELCADE_COMMAND_RGB_LED_MATRIX_ENABLE_V23 0x2E    //For Pixelcade V2 boards with firwmare v23 or later
#define PIXELCADE_COMMAND_V23_INIT 0xEF                     //one time init command for Pixelcade V2 boards with V23+ firmware to set up the framing protocol
#define PIXELCADE_COMMAND_RGB565 0x30
#define PIXELCADE_COMMAND_RGB888 0x40
#define PIXELCADE_FRAME_START_MARKER 0xFE
#define PIXELCADE_FRAME_END_DELIMITER 0xAA
#define PIXELCADE_MAX_DATA_SIZE (128 * 32 * 3)
#define PIXELCADE_COMMAND_READ_TIMEOUT 100
#define PIXELCADE_COMMAND_WRITE_TIMEOUT 100
#define PIXELCADE_MAX_QUEUE_FRAMES 4
#define PIXELCADE_MAX_NO_RESPONSE 20

namespace DMDUtil
{

enum class PixelcadeFrameFormat
{
  RGB565,
  RGB888
};

struct PixelcadeFrame
{
  void* pData;
  PixelcadeFrameFormat format;
};

class PixelcadeDMD
{
 public:
  PixelcadeDMD(struct sp_port* pSerialPort, int width, int height, bool colorSwap, bool isV2, bool isV23);
  ~PixelcadeDMD();

  static PixelcadeDMD* Connect(const char* pDevice = nullptr);
  void Update(uint16_t* pData);
  void UpdateRGB24(uint8_t* pData);

  int GetWidth() const { return m_width; }
  int GetHeight() const { return m_height; }
  bool GetIsV2() const { return m_isV2; }
  bool GetIsV23() const { return m_isV23; }

 private:
  static PixelcadeDMD* Open(const char* pDevice);
  void Run();
  void EnableRgbLedMatrix(int shifterLen32, int rows);
  int BuildFrame(uint8_t* pFrameBuffer, size_t bufferSize, uint8_t command, const uint8_t* pData, uint16_t dataLength);
  int BuildRawCommand(uint8_t* pFrameBuffer, size_t bufferSize, uint8_t command, const uint8_t* pData,
                      uint16_t dataLength);

  struct sp_port* m_pSerialPort;
  int m_width;
  int m_height;
  bool m_colorSwap;
  bool m_isV2;
  bool m_isV23;
  int m_length;

  std::thread* m_pThread;
  std::queue<PixelcadeFrame> m_frames;
  std::mutex m_mutex;
  bool m_running;
};

}  // namespace DMDUtil