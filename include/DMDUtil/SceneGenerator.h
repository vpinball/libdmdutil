#pragma once

#ifdef _MSC_VER
#define DMDUTILAPI __declspec(dllexport)
#define DMDUTILCALLBACK __stdcall
#else
#define DMDUTILAPI __attribute__((visibility("default")))
#define DMDUTILCALLBACK
#endif

#include <cstdint>
#include <string>
#include <vector>

namespace DMDUtil
{

struct SceneData
{
  int sceneId;
  int frameCount;
  int durationPerFrame;
  bool interruptable;
};

class DMDUTILAPI SceneGenerator
{
 public:
  SceneGenerator();

  bool parseCSV(const std::string& csv_filename);
  bool generateDump(const std::string& dump_filename, int id = -1);
  bool getSceneInfo(int sceneId, int& frameCount, int& durationPerFrame, bool& interruptable) const;
  bool generateFrame(int sceneId, int frameIndex, uint8_t* buffer);
  void setDepth(int depth);
  void Reset()
  {
    m_sceneData.clear();
    m_templateInitialized = false;
    initializeTemplate();
  }

 private:
  std::vector<SceneData> m_sceneData;

  const unsigned char* getCharFont(char c) const;
  void renderChar(uint8_t* buffer, char c, int x, int y) const;
  void renderString(uint8_t* buffer, const std::string& str, int x, int y) const;

  struct TextTemplate
  {
    uint8_t fullFrame[4096];
  };
  TextTemplate m_template;
  bool m_templateInitialized;

  void initializeTemplate();

  int m_depth = 2;  // Default depth for rendering
};

}  // namespace DMDUtil
