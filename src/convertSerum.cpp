#include <cstdarg>
#include <filesystem>
#include <iostream>
#include <string>

#include "cargs.h"
#include "serum-decode.h"

namespace
{
bool EndsWithCaseInsensitive(const std::string& value, const std::string& suffix)
{
  if (suffix.size() > value.size()) return false;
  const size_t offset = value.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); ++i)
  {
    char a = value[offset + i];
    char b = suffix[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

void SERUM_CALLBACK SerumLogCallback(const char* format, va_list args, const void* userData)
{
  (void)userData;
  vfprintf(stdout, format, args);
  fputc('\n', stdout);
}

static struct cag_option options[] = {
    {.identifier = 'i',
     .access_letters = "i",
     .access_name = "input",
     .value_name = "FILE",
     .description = "Input cRZ/cROM file"},
    {.identifier = 'a',
     .access_letters = "a",
     .access_name = "alt-color-path",
     .value_name = "PATH",
     .description = "Alt color base path (optional, defaults from input path)"},
    {.identifier = 'r',
     .access_letters = "r",
     .access_name = "rom",
     .value_name = "NAME",
     .description = "ROM name (optional, defaults from input filename)"},
    {.identifier = 'S', .access_name = "strip-sd", .description = "Remove SD (32p) colorization and keep HD only"},
    {.identifier = 'H', .access_name = "strip-hd", .description = "Remove HD (64p) colorization and keep SD only"},
    {.identifier = 'l', .access_letters = "l", .access_name = "logging", .description = "Enable libserum log output"},
    {.identifier = 'h', .access_letters = "h", .access_name = "help", .description = "Show help"}};
}  // namespace

int main(int argc, char* argv[])
{
  const char* opt_input = nullptr;
  const char* opt_alt_color_path = nullptr;
  const char* opt_rom = nullptr;
  bool opt_strip_sd = false;
  bool opt_strip_hd = false;
  bool opt_logging = false;

  cag_option_context cag_context;
  cag_option_init(&cag_context, options, CAG_ARRAY_SIZE(options), argc, argv);
  while (cag_option_fetch(&cag_context))
  {
    char identifier = cag_option_get_identifier(&cag_context);
    switch (identifier)
    {
      case 'i':
        opt_input = cag_option_get_value(&cag_context);
        break;
      case 'a':
        opt_alt_color_path = cag_option_get_value(&cag_context);
        break;
      case 'r':
        opt_rom = cag_option_get_value(&cag_context);
        break;
      case 'S':
        opt_strip_sd = true;
        break;
      case 'H':
        opt_strip_hd = true;
        break;
      case 'l':
        opt_logging = true;
        break;
      case 'h':
        std::cerr << "Usage: " << argv[0] << " [OPTION]...\n";
        cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
        return 0;
      default:
        break;
    }
  }

  if (!opt_input || opt_input[0] == '\0')
  {
    std::cerr << "Error: Missing --input\n";
    return 1;
  }
  if (opt_strip_sd && opt_strip_hd)
  {
    std::cerr << "Error: --strip-sd and --strip-hd are mutually exclusive\n";
    return 1;
  }

  std::filesystem::path inputPath = std::filesystem::path(opt_input);
  std::string inputName = inputPath.filename().string();
  if (!EndsWithCaseInsensitive(inputName, ".cRZ") && !EndsWithCaseInsensitive(inputName, ".cROM"))
  {
    std::cerr << "Error: input must be .cRZ or .cROM\n";
    return 1;
  }

  std::string romName;
  if (opt_rom && opt_rom[0] != '\0')
  {
    romName = opt_rom;
  }
  else
  {
    romName = inputPath.stem().string();
  }

  std::filesystem::path altColorBasePath;
  if (opt_alt_color_path && opt_alt_color_path[0] != '\0')
  {
    altColorBasePath = std::filesystem::path(opt_alt_color_path);
  }
  else
  {
    const std::filesystem::path parent = inputPath.parent_path();
    if (parent.filename().string() == romName)
    {
      altColorBasePath = parent.parent_path();
    }
    else
    {
      altColorBasePath = parent;
    }
  }

  if (altColorBasePath.empty())
  {
    altColorBasePath = ".";
  }

  uint8_t flags = FLAG_REQUEST_32P_FRAMES | FLAG_REQUEST_64P_FRAMES;
  if (opt_strip_sd)
  {
    flags = FLAG_REQUEST_64P_FRAMES;
  }
  else if (opt_strip_hd)
  {
    flags = FLAG_REQUEST_32P_FRAMES;
  }

  if (opt_logging)
  {
    Serum_SetLogCallback(SerumLogCallback, nullptr);
  }

  Serum_SetGenerateCRomC(true);
  Serum_Frame_Struc* loaded = Serum_Load(altColorBasePath.string().c_str(), romName.c_str(), flags);
  if (!loaded)
  {
    std::cerr << "Error: Serum_Load failed for rom=" << romName << " basePath=" << altColorBasePath << "\n";
    Serum_Dispose();
    return 1;
  }

  const std::filesystem::path outPath = altColorBasePath / romName / (romName + ".cROMc");
  Serum_Dispose();
  std::cout << "Generated cROMc: " << outPath.string() << "\n";
  return 0;
}
