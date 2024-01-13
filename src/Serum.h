#pragma once

#include <cstdint>
#include <string>

namespace DMDUtil {

class Serum {
public:
   ~Serum();

   static Serum* Load(const std::string& romName);
   bool Convert(uint8_t* pFrame, uint8_t* pDstFrame, uint8_t* pDstPalette);

private:
   Serum(int width, int height);

   int m_width;
   int m_height;
   int m_length;
   uint8_t* m_pFrame;
   uint8_t m_palette[64 * 3];

   static bool m_isLoaded;
};

}