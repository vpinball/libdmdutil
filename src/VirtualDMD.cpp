#include "VirtualDMD.h"

#include <cstring>
#include <sstream>

#include "Logger.h"

namespace DMDUtil
{

VirtualDMD* VirtualDMD::s_instance = nullptr;

VirtualDMD::VirtualDMD(int port, const char* bindAddr) : m_port(port), m_bindAddr(bindAddr)
{
  s_instance = this;
  mg_mgr_init(&m_mgr);
}

VirtualDMD::~VirtualDMD()
{
  StopWebServer();
  s_instance = nullptr;
}

bool VirtualDMD::StartWebServer()
{
  if (m_webServerRunning)
  {
    return true;
  }

  std::string addr = "http://" + m_bindAddr + ":" + std::to_string(m_port);

  if (mg_http_listen(&m_mgr, addr.c_str(), EventHandler, this) == nullptr)
  {
    Log(DMDUtil_LogLevel_ERROR, "Failed to start web server on port %d", m_port);
    return false;
  }

  m_webServerRunning = true;
  m_webServerThread = std::thread(&VirtualDMD::ProcessWebServerEvents, this);

  Log(DMDUtil_LogLevel_INFO, "Virtual DMD web server started on http://%s:%d", m_bindAddr.c_str(), m_port);
  return true;
}

void VirtualDMD::StopWebServer()
{
  if (!m_webServerRunning)
  {
    return;
  }

  m_webServerRunning = false;

  if (m_webServerThread.joinable())
  {
    m_webServerThread.join();
  }

  mg_mgr_free(&m_mgr);
  Log(DMDUtil_LogLevel_INFO, "Virtual DMD web server stopped");
}

void VirtualDMD::ProcessWebServerEvents()
{
  while (m_webServerRunning)
  {
    mg_mgr_poll(&m_mgr, 50);
  }
}

void VirtualDMD::UpdateRGB24Data(const uint8_t* pData, uint16_t width, uint16_t height, bool buffered)
{
  // Call the base class method first to handle any base functionality
  DMD::UpdateRGB24Data(pData, width, height, buffered);

  if (!pData || width <= 0 || height <= 0)
  {
    return;
  }

  std::lock_guard<std::mutex> lock(m_frameMutex);

  size_t dataSize = width * height * 3;
  m_currentFrame.resize(dataSize);
  std::memcpy(m_currentFrame.data(), pData, dataSize);

  m_currentWidth = width;
  m_currentHeight = height;
  m_frameUpdated = true;

  if (m_webServerRunning)
  {
    BroadcastFrameData(pData, width, height);
  }
}

void VirtualDMD::UpdateRGB16Data(const uint16_t* pData, uint16_t width, uint16_t height, bool buffered)
{
  // Call the base class method first to handle any base functionality
  DMD::UpdateRGB16Data(pData, width, height, buffered);

  if (!pData || width <= 0 || height <= 0)
  {
    return;
  }

  std::vector<uint8_t> rgb24Data(width * height * 3);

  for (uint16_t i = 0; i < width * height; i++)
  {
    uint16_t pixel = pData[i];

    uint8_t r = ((pixel >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((pixel >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (pixel & 0x1F) * 255 / 31;

    rgb24Data[i * 3] = r;
    rgb24Data[i * 3 + 1] = g;
    rgb24Data[i * 3 + 2] = b;
  }

  // Update our virtual display (avoid recursion by calling our own method directly)
  std::lock_guard<std::mutex> lock(m_frameMutex);

  size_t dataSize = width * height * 3;
  m_currentFrame.resize(dataSize);
  std::memcpy(m_currentFrame.data(), rgb24Data.data(), dataSize);

  m_currentWidth = width;
  m_currentHeight = height;
  m_frameUpdated = true;

  if (m_webServerRunning)
  {
    BroadcastFrameData(rgb24Data.data(), width, height);
  }
}

void VirtualDMD::UpdateData(const uint8_t* pData, int depth, uint16_t width, uint16_t height, uint8_t r, uint8_t g,
                            uint8_t b, bool buffered)
{
  // Call the base class method first
  DMD::UpdateData(pData, depth, width, height, r, g, b, buffered);

  if (!pData || width <= 0 || height <= 0)
  {
    return;
  }

  // Convert the palette data to RGB24 for display
  std::vector<uint8_t> rgb24Data(width * height * 3);

  for (int y = 0; y < height; y++)
  {
    for (int x = 0; x < width; x++)
    {
      int pixelIndex = y * width + x;
      uint8_t paletteValue = pData[pixelIndex];

      // Convert palette value to intensity
      uint8_t intensity = (paletteValue * 255) / ((1 << depth) - 1);

      // Apply the RGB color
      rgb24Data[pixelIndex * 3] = (intensity * r) / 255;      // Red
      rgb24Data[pixelIndex * 3 + 1] = (intensity * g) / 255;  // Green
      rgb24Data[pixelIndex * 3 + 2] = (intensity * b) / 255;  // Blue
    }
  }

  // Update our virtual display
  std::lock_guard<std::mutex> lock(m_frameMutex);

  size_t dataSize = width * height * 3;
  m_currentFrame.resize(dataSize);
  std::memcpy(m_currentFrame.data(), rgb24Data.data(), dataSize);

  m_currentWidth = width;
  m_currentHeight = height;
  m_frameUpdated = true;

  if (m_webServerRunning)
  {
    BroadcastFrameData(rgb24Data.data(), width, height);
  }
}

void VirtualDMD::QueueUpdate(const std::shared_ptr<Update> dmdUpdate, bool buffered)
{
  // Call the base class method first to handle any base functionality
  DMD::QueueUpdate(dmdUpdate, buffered);

  if (!dmdUpdate || !dmdUpdate->hasData)
  {
    return;
  }

  // Convert the Update data to RGB24 for display
  std::vector<uint8_t> rgb24Data(dmdUpdate->width * dmdUpdate->height * 3);

  // For data mode, we need to convert palette data to RGB
  for (int y = 0; y < dmdUpdate->height; y++)
  {
    for (int x = 0; x < dmdUpdate->width; x++)
    {
      int pixelIndex = y * dmdUpdate->width + x;
      uint8_t paletteValue = dmdUpdate->data[pixelIndex];

      // Simple grayscale conversion based on palette value
      uint8_t intensity = (paletteValue * 255) / ((1 << dmdUpdate->depth) - 1);

      // Apply the RGB color
      rgb24Data[pixelIndex * 3] = (intensity * dmdUpdate->r) / 255;      // Red
      rgb24Data[pixelIndex * 3 + 1] = (intensity * dmdUpdate->g) / 255;  // Green
      rgb24Data[pixelIndex * 3 + 2] = (intensity * dmdUpdate->b) / 255;  // Blue
    }
  }

  // Update our virtual display
  std::lock_guard<std::mutex> lock(m_frameMutex);

  size_t dataSize = dmdUpdate->width * dmdUpdate->height * 3;
  m_currentFrame.resize(dataSize);
  std::memcpy(m_currentFrame.data(), rgb24Data.data(), dataSize);

  m_currentWidth = dmdUpdate->width;
  m_currentHeight = dmdUpdate->height;
  m_frameUpdated = true;

  if (m_webServerRunning)
  {
    BroadcastFrameData(rgb24Data.data(), dmdUpdate->width, dmdUpdate->height);
  }
}

void VirtualDMD::BroadcastFrameData(const uint8_t* pData, uint16_t width, uint16_t height)
{
  if (!pData)
  {
    return;
  }

  std::ostringstream oss;
  oss << "{\"type\":\"frame\",\"width\":" << width << ",\"height\":" << height << ",\"data\":[";

  for (uint32_t i = 0; i < width * height * 3; i++)
  {
    if (i > 0) oss << ",";
    oss << static_cast<int>(pData[i]);
  }
  oss << "]}";

  std::string jsonData = oss.str();

  for (struct mg_connection* c = m_mgr.conns; c != nullptr; c = c->next)
  {
    if (c->is_websocket)
    {
      mg_ws_send(c, jsonData.c_str(), jsonData.length(), WEBSOCKET_OP_TEXT);
    }
  }
}

void VirtualDMD::SendIndexPage(struct mg_connection* c)
{
  const char* html = R"(<!DOCTYPE html>
<html>
<head>
    <title>DMDServer Virtual Display</title>
    <style>
        body {
            margin: 0;
            padding: 20px;
            background: #111;
            color: #fff;
            font-family: 'Courier New', monospace;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            min-height: 100vh;
        }
        
        h1 {
            color: #ff6600;
            text-shadow: 0 0 10px rgba(255, 102, 0, 0.8);
            margin-bottom: 30px;
        }
        
        #dmdContainer {
            border: 3px solid #444;
            border-radius: 15px;
            padding: 20px;
            background: linear-gradient(145deg, #222, #000);
            box-shadow: 
                0 0 30px rgba(255, 140, 0, 0.4),
                inset 0 0 20px rgba(0, 0, 0, 0.8);
            position: relative;
        }
        
        #dmdCanvas {
            border: 2px solid #333;
            border-radius: 5px;
            background: #000;
            image-rendering: pixelated;
            image-rendering: -moz-crisp-edges;
            image-rendering: crisp-edges;
        }
        
        #info {
            margin-top: 25px;
            text-align: center;
            color: #ccc;
        }
        
        #status {
            margin-top: 15px;
            padding: 12px 20px;
            border-radius: 8px;
            font-weight: bold;
            text-transform: uppercase;
            letter-spacing: 1px;
            transition: all 0.3s ease;
        }
        
        .connected { 
            background: linear-gradient(45deg, #004400, #006600);
            color: #00ff00;
            box-shadow: 0 0 15px rgba(0, 255, 0, 0.5);
        }
        
        .disconnected { 
            background: linear-gradient(45deg, #440000, #660000);
            color: #ff0000;
            box-shadow: 0 0 15px rgba(255, 0, 0, 0.5);
        }
        
        #controls {
            margin-top: 20px;
            display: flex;
            gap: 15px;
            align-items: center;
        }
        
        .control-group {
            display: flex;
            align-items: center;
            gap: 8px;
        }
        
        label {
            color: #aaa;
            font-size: 14px;
        }
        
        input[type="range"] {
            width: 120px;
        }
        
        input[type="checkbox"] {
            transform: scale(1.2);
        }
    </style>
</head>
<body>
    <h1>DMDServer Virtual Display</h1>
    <div id="dmdContainer">
        <canvas id="dmdCanvas" width="128" height="32"></canvas>
    </div>
    <div id="info">
        <div>Resolution: <span id="resolution">128x32</span></div>
        <div id="status" class="disconnected">Disconnected</div>
        <div id="controls">
            <div class="control-group">
                <label for="dotSize">Dot Size:</label>
                <input type="range" id="dotSize" min="0.5" max="0.95" step="0.05" value="0.8">
            </div>
            <div class="control-group">
                <label for="brightness">Brightness:</label>
                <input type="range" id="brightness" min="0.3" max="2.0" step="0.1" value="1.0">
            </div>
            <div class="control-group">
                <label for="scanlines">Scanlines:</label>
                <input type="checkbox" id="scanlines" checked>
            </div>
            <div class="control-group">
                <label for="glow">Glow:</label>
                <input type="checkbox" id="glow" checked>
            </div>
        </div>
    </div>

    <script>
        const canvas = document.getElementById('dmdCanvas');
        const ctx = canvas.getContext('2d');
        const statusDiv = document.getElementById('status');
        const resolutionSpan = document.getElementById('resolution');
        
        // Controls
        const dotSizeSlider = document.getElementById('dotSize');
        const brightnessSlider = document.getElementById('brightness');
        const scanlinesCheck = document.getElementById('scanlines');
        const glowCheck = document.getElementById('glow');
        
        let ws = null;
        let currentWidth = 128;
        let currentHeight = 32;
        let currentData = null;
        
        // DMD rendering settings
        let settings = {
            dotSize: 0.8,
            brightness: 1.0,
            scanlines: true,
            glow: true,
            scale: 4
        };
        
        // Add event listeners for controls
        dotSizeSlider.addEventListener('input', (e) => {
            settings.dotSize = parseFloat(e.target.value);
            if (currentData) renderDMD();
        });
        
        brightnessSlider.addEventListener('input', (e) => {
            settings.brightness = parseFloat(e.target.value);
            if (currentData) renderDMD();
        });
        
        scanlinesCheck.addEventListener('change', (e) => {
            settings.scanlines = e.target.checked;
            if (currentData) renderDMD();
        });
        
        glowCheck.addEventListener('change', (e) => {
            settings.glow = e.target.checked;
            if (currentData) renderDMD();
        });
        
        function connect() {
            const wsUrl = 'ws://' + window.location.host + '/ws';
            ws = new WebSocket(wsUrl);
            
            ws.onopen = function() {
                statusDiv.textContent = 'Connected';
                statusDiv.className = 'connected';
            };
            
            ws.onmessage = function(event) {
                try {
                    const data = JSON.parse(event.data);
                    if (data.type === 'frame') {
                        updateDMD(data.width, data.height, data.data);
                    }
                } catch (e) {
                    console.error('Error parsing WebSocket message:', e);
                }
            };
            
            ws.onclose = function() {
                statusDiv.textContent = 'Disconnected';
                statusDiv.className = 'disconnected';
                setTimeout(connect, 2000);
            };
            
            ws.onerror = function(error) {
                console.error('WebSocket error:', error);
            };
        }
        
        function updateDMD(width, height, data) {
            if (width !== currentWidth || height !== currentHeight) {
                currentWidth = width;
                currentHeight = height;
                canvas.width = width * settings.scale;
                canvas.height = height * settings.scale;
                resolutionSpan.textContent = width + 'x' + height;
            }
            
            currentData = data;
            renderDMD();
        }
        
        function renderDMD() {
            if (!currentData) return;
            
            const scale = settings.scale;
            const width = currentWidth;
            const height = currentHeight;
            
            // Clear canvas
            ctx.fillStyle = '#000';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
            
            // Set up glow effect
            if (settings.glow) {
                ctx.shadowBlur = 8;
                ctx.shadowColor = '#ff6600';
            } else {
                ctx.shadowBlur = 0;
            }
            
            // Draw dots
            const dotRadius = (scale * settings.dotSize) / 2;
            
            for (let y = 0; y < height; y++) {
                for (let x = 0; x < width; x++) {
                    const pixelIndex = (y * width + x) * 3;
                    const r = currentData[pixelIndex];
                    const g = currentData[pixelIndex + 1];
                    const b = currentData[pixelIndex + 2];
                    
                    // Calculate intensity
                    const intensity = (r + g + b) / 3 / 255;
                    
                    if (intensity > 0.01) {
                        // Apply brightness
                        const adjustedR = Math.min(255, r * settings.brightness);
                        const adjustedG = Math.min(255, g * settings.brightness);
                        const adjustedB = Math.min(255, b * settings.brightness);
                        
                        const centerX = x * scale + scale / 2;
                        const centerY = y * scale + scale / 2;
                        
                        // Draw the dot with gradient
                        const gradient = ctx.createRadialGradient(
                            centerX, centerY, 0,
                            centerX, centerY, dotRadius
                        );
                        
                        gradient.addColorStop(0, `rgb(${adjustedR}, ${adjustedG}, ${adjustedB})`);
                        gradient.addColorStop(0.7, `rgba(${adjustedR}, ${adjustedG}, ${adjustedB}, 0.8)`);
                        gradient.addColorStop(1, `rgba(${adjustedR * 0.3}, ${adjustedG * 0.3}, ${adjustedB * 0.3}, 0.3)`);
                        
                        ctx.fillStyle = gradient;
                        ctx.beginPath();
                        ctx.arc(centerX, centerY, dotRadius, 0, Math.PI * 2);
                        ctx.fill();
                    }
                }
            }
            
            // Add scanlines effect
            if (settings.scanlines) {
                ctx.shadowBlur = 0;
                ctx.fillStyle = 'rgba(0, 0, 0, 0.3)';
                for (let y = 0; y < canvas.height; y += 2) {
                    ctx.fillRect(0, y, canvas.width, 1);
                }
            }
        }
        
        // Initial setup
        canvas.width = currentWidth * settings.scale;
        canvas.height = currentHeight * settings.scale;
        
        // Clear canvas with black
        ctx.fillStyle = '#000';
        ctx.fillRect(0, 0, canvas.width, canvas.height);
        
        // Connect to WebSocket
        connect();
    </script>
</body>
</html>)";

  mg_http_reply(c, 200, "Content-Type: text/html\r\n", "%s", html);
}

void VirtualDMD::EventHandler(struct mg_connection* c, int ev, void* ev_data)
{
  if (ev == MG_EV_HTTP_MSG)
  {
    struct mg_http_message* hm = (struct mg_http_message*)ev_data;

    if (mg_match(hm->uri, mg_str("/"), nullptr))
    {
      if (s_instance)
      {
        s_instance->SendIndexPage(c);
      }
    }
    else if (mg_match(hm->uri, mg_str("/ws"), nullptr))
    {
      mg_ws_upgrade(c, hm, nullptr);
    }
    else
    {
      mg_http_reply(c, 404, nullptr, "Not found");
    }
  }
  else if (ev == MG_EV_WS_OPEN)
  {
    if (s_instance)
    {
      std::lock_guard<std::mutex> lock(s_instance->m_frameMutex);
      if (s_instance->m_frameUpdated && !s_instance->m_currentFrame.empty())
      {
        s_instance->BroadcastFrameData(s_instance->m_currentFrame.data(), s_instance->m_currentWidth,
                                       s_instance->m_currentHeight);
      }
    }
  }
}

}  // namespace DMDUtil