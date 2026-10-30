#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#include "DMDInternal.h"
#include "DMDUtil/Config.h"
#include "DMDUtil/Logger.h"
#include "FrameUtil.h"
#include "OutputFilters.h"
#include "ZeDMD.h"

namespace DMDUtil
{

void DMD::ZeDMDThread()
{
  auto decoder = std::make_unique<OutputDecoder>();
  Config* const pConfig = Config::GetInstance();
  decoder->showNotColorized = pConfig->IsShowNotColorizedFrames();
  decoder->excludeColorized = pConfig->IsExcludeColorizedFramesForZeDMD();
  decoder->targetWidth = m_pZeDMD->GetWidth();
  const int roundedCorners = pConfig->GetRoundedCorners();
  uint16_t width = 0;
  uint16_t height = 0;
  std::vector<uint16_t> rgb565(DMDUTIL_MAX_FRAME_PIXELS);

  OutputLoop(false, 0, nullptr,
             [&](uint8_t slot, uint16_t position, uint16_t skipped)
             {
               if (skipped > 0)
                 Log(DMDUtil_LogLevel_INFO, "ZeDMD: Skipping %d frame(s) before position %d", skipped, position);

               const Update& update = *m_pUpdateBufferQueue[slot];
               if (DecodeUpdate(update, *decoder) == DecodedKind::None) return;

               if (decoder->width != width || decoder->height != height)
               {
                 Log(DMDUtil_LogLevel_INFO, "ZeDMD: Change frame size from %dx%d to %dx%d", width, height,
                     decoder->width, decoder->height);
                 width = decoder->width;
                 height = decoder->height;
                 m_pZeDMD->SetFrameSize(width, height);
               }

               Log(DMDUtil_LogLevel_DEBUG, "ZeDMD: Render frame buffer position %d at real buffer position %d",
                   position, slot);

               if (decoder->rgb565Source)
               {
                 memcpy(rgb565.data(), update.segData, (size_t)width * height * sizeof(uint16_t));
                 ApplyRoundedCornersRGB565(rgb565.data(), width, height, roundedCorners);
                 m_pZeDMD->RenderRgb565(rgb565.data());
               }
               else
               {
                 ApplyRoundedCornersRGB24(decoder->rgb24, width, height, roundedCorners);
                 m_pZeDMD->RenderRgb888(decoder->rgb24);
               }
             });
}

#ifdef DMDUTIL_WITH_PIN2DMD
void DMD::PIN2DMDThread()
{
  auto decoder = std::make_unique<OutputDecoder>();
  Config* const pConfig = Config::GetInstance();
  decoder->showNotColorized = pConfig->IsShowNotColorizedFrames();
  decoder->excludeColorized = pConfig->IsExcludeColorizedFramesForPIN2DMD();
  decoder->targetWidth = m_PIN2DMDWidth;
  const int roundedCorners = pConfig->GetRoundedCorners();
  const uint16_t targetWidth = m_PIN2DMDWidth;
  const uint16_t targetHeight = m_PIN2DMDHeight;
  std::vector<uint8_t> scaled((size_t)targetWidth * targetHeight * 3);
  std::vector<uint8_t> temp;

  OutputLoop(false, 0, nullptr,
             [&](uint8_t slot, uint16_t, uint16_t)
             {
               if (DecodeUpdate(*m_pUpdateBufferQueue[slot], *decoder) == DecodedKind::None) return;
               if (!ScaleFrameToTarget(decoder->rgb24, decoder->width, decoder->height, scaled.data(), targetWidth,
                                       targetHeight, 24, temp))
                 return;
               ApplyRoundedCornersRGB24(scaled.data(), targetWidth, targetHeight, roundedCorners);
               PIN2DMDRenderRaw(targetWidth, targetHeight, scaled.data(), 1);
             });
}
#endif

#ifdef DMDUTIL_WITH_PINDMD3
void DMD::PinDMD3Thread()
{
  auto decoder = std::make_unique<OutputDecoder>();
  Config* const pConfig = Config::GetInstance();
  decoder->showNotColorized = pConfig->IsShowNotColorizedFrames();
  decoder->excludeColorized = pConfig->IsExcludeColorizedFramesForPinDMD3();
  decoder->targetWidth = (uint16_t)m_pPinDMD3->GetWidth();
  const int roundedCorners = pConfig->GetRoundedCorners();
  const uint16_t targetWidth = (uint16_t)m_pPinDMD3->GetWidth();
  const uint16_t targetHeight = (uint16_t)m_pPinDMD3->GetHeight();
  std::vector<uint8_t> scaled((size_t)targetWidth * targetHeight * 3);
  std::vector<uint8_t> temp;

  OutputLoop(false, 0, nullptr,
             [&](uint8_t slot, uint16_t, uint16_t)
             {
               if (DecodeUpdate(*m_pUpdateBufferQueue[slot], *decoder) == DecodedKind::None) return;
               if (!ScaleFrameToTarget(decoder->rgb24, decoder->width, decoder->height, scaled.data(), targetWidth,
                                       targetHeight, 24, temp))
                 return;
               ApplyRoundedCornersRGB24(scaled.data(), targetWidth, targetHeight, roundedCorners);
               m_pPinDMD3->UpdateRGB24(scaled.data());
             });
}
#endif

#ifdef DMDUTIL_WITH_PIXELCADE
void DMD::PixelcadeDMDThread()
{
  auto decoder = std::make_unique<OutputDecoder>();
  Config* const pConfig = Config::GetInstance();
  decoder->showNotColorized = pConfig->IsShowNotColorizedFrames();
  decoder->excludeColorized = pConfig->IsExcludeColorizedFramesForPixelcade();
  decoder->targetWidth = (uint16_t)m_pPixelcadeDMD->GetWidth();
  const int roundedCorners = pConfig->GetRoundedCorners();
  const uint16_t targetWidth = (uint16_t)m_pPixelcadeDMD->GetWidth();
  const uint16_t targetHeight = (uint16_t)m_pPixelcadeDMD->GetHeight();
  const size_t targetLength = (size_t)targetWidth * targetHeight;
  const bool isV2 = m_pPixelcadeDMD->GetIsV2();
  std::vector<uint8_t> scaled(targetLength * 3);
  std::vector<uint16_t> rgb565(targetLength);
  std::vector<uint8_t> temp;

  OutputLoop(false, 0, nullptr,
             [&](uint8_t slot, uint16_t, uint16_t)
             {
               const Update& update = *m_pUpdateBufferQueue[slot];
               if (DecodeUpdate(update, *decoder) == DecodedKind::None) return;

               if (decoder->rgb565Source)
               {
                 if (!ScaleFrameToTarget((const uint8_t*)update.segData, decoder->width, decoder->height,
                                         (uint8_t*)rgb565.data(), targetWidth, targetHeight, 16, temp))
                   return;
               }
               else
               {
                 if (!ScaleFrameToTarget(decoder->rgb24, decoder->width, decoder->height, scaled.data(), targetWidth,
                                         targetHeight, 24, temp))
                   return;
                 if (isV2)
                 {
                   ApplyRoundedCornersRGB24(scaled.data(), targetWidth, targetHeight, roundedCorners);
                   m_pPixelcadeDMD->UpdateRGB24(scaled.data());
                   return;
                 }
                 Rgb888ToRgb565(scaled.data(), rgb565.data(), targetLength);
               }

               ApplyRoundedCornersRGB565(rgb565.data(), targetWidth, targetHeight, roundedCorners);
               m_pPixelcadeDMD->Update(rgb565.data());
             });
}
#endif

#ifdef DMDUTIL_WITH_DMDEXTSTREAM
void DMD::DMDExtStreamThread()
{
  auto decoder = std::make_unique<OutputDecoder>();
  Config* const pConfig = Config::GetInstance();
  decoder->showNotColorized = pConfig->IsShowNotColorizedFrames();
  decoder->excludeColorized = pConfig->IsExcludeColorizedFramesForDMDExtStream();
  decoder->targetWidth = (uint16_t)m_pDMDExtStream->GetWidth();
  const int roundedCorners = pConfig->GetRoundedCorners();
  const uint16_t targetWidth = (uint16_t)m_pDMDExtStream->GetWidth();
  const uint16_t targetHeight = (uint16_t)m_pDMDExtStream->GetHeight();
  const size_t targetLength = (size_t)targetWidth * targetHeight;
  std::vector<uint8_t> scaled(targetLength * 3);
  std::vector<uint8_t> scaledIndexed(targetLength);
  std::vector<uint8_t> temp;
  uint8_t palette16[16 * 3];

  OutputLoop(
      false, m_pDMDExtStream->IsRetryEnabled() ? 1000 : 0,
      [&]()
      {
        if (!m_pDMDExtStream->IsConnected()) m_pDMDExtStream->EnsureConnected();
      },
      [&](uint8_t slot, uint16_t, uint16_t)
      {
        const DecodedKind kind = DecodeUpdate(*m_pUpdateBufferQueue[slot], *decoder);
        if (kind == DecodedKind::None) return;
        if (!m_pDMDExtStream->EnsureConnected()) return;

        m_pDMDExtStream->SetGameName(m_romName);

        if (kind == DecodedKind::Indexed && (decoder->depth == 2 || decoder->depth == 4))
        {
          if (!ScaleFrameToTarget(decoder->indexed, decoder->width, decoder->height, scaledIndexed.data(), targetWidth,
                                  targetHeight, 8, temp))
            return;

          if (decoder->mode == Mode::SerumV1 || decoder->mode == Mode::Vni)
          {
            memset(palette16, 0, sizeof(palette16));
            memcpy(palette16, decoder->palette, (size_t)(1 << decoder->depth) * 3);
            m_pDMDExtStream->SendColoredGray4(scaledIndexed.data(), palette16);
          }
          else
          {
            m_pDMDExtStream->SendColor(decoder->r, decoder->g, decoder->b);
            m_pDMDExtStream->SendGray(scaledIndexed.data(), decoder->depth);
          }
          return;
        }

        if (!ScaleFrameToTarget(decoder->rgb24, decoder->width, decoder->height, scaled.data(), targetWidth,
                                targetHeight, 24, temp))
          return;
        ApplyRoundedCornersRGB24(scaled.data(), targetWidth, targetHeight, roundedCorners);
        m_pDMDExtStream->SendRgb24(scaled.data());
      });
}
#endif

}  // namespace DMDUtil
