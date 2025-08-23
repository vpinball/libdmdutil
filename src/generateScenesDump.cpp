#include <iostream>

#include "DMDUtil/DMDUtil.h"
#include "cargs.h"
#include "serum-decode.h"

static struct cag_option options[] = {
    {.identifier = 'c',
     .access_letters = "c",
     .access_name = "csv",
     .value_name = "VALUE",
     .description = "PUP scenes CSV file to parse"},
    {.identifier = 'o',
     .access_letters = "o",
     .access_name = "output",
     .value_name = "VALUE",
     .description = "Output dump file to generate"},
    {.identifier = 'd',
     .access_letters = "d",
     .access_name = "depth",
     .value_name = "VALUE",
     .description = "Bit depth of the DMD frames (2 or 4) (optional, default is 2)"},
    {.identifier = 'i',
     .access_letters = "i",
     .access_name = "id",
     .value_name = "VALUE",
     .description = "PUP scene trigger ID to generate (optional, default is all scenes)"},
    {.identifier = 'h', .access_letters = "h", .access_name = "help", .description = "Show help"}};

int main(int argc, char* argv[])
{
  char identifier;
  cag_option_context cag_context;

  const char* opt_csv_file = NULL;
  const char* opt_output_file = NULL;
  uint8_t opt_depth = 2;
  int32_t opt_id = -1;

  cag_option_init(&cag_context, options, CAG_ARRAY_SIZE(options), argc, argv);
  while (cag_option_fetch(&cag_context))
  {
    identifier = cag_option_get_identifier(&cag_context);
    switch (identifier)
    {
      case 'c':
        opt_csv_file = cag_option_get_value(&cag_context);
        break;
      case 'o':
        opt_output_file = cag_option_get_value(&cag_context);
        break;
      case 'd':
        opt_depth = atoi(cag_option_get_value(&cag_context));
        break;
      case 'i':
        opt_id = atoi(cag_option_get_value(&cag_context));
        break;
      case 'h':
        std::cerr << "Usage: " << argv[0] << " [OPTION]...\n";
        cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
        return 0;
    }
  }

  if (!opt_csv_file || !opt_output_file)
  {
    std::cerr << "Error: Missing required options\n";
    std::cerr << "Usage: " << argv[0] << " -c MY_PUP_SCENES.csv -o MY_DUMP_FILE.txt\n";
    cag_option_print(options, CAG_ARRAY_SIZE(options), stdout);
    return 1;
  }

  Serum_Scene_SetDepth(opt_depth);

  if (!Serum_Scene_ParseCSV(opt_csv_file))
  {
    std::cerr << "Error: Failed to parse CSV file\n";
    return 1;
  }

  if (!Serum_Scene_GenerateDump(opt_output_file, opt_id))
  {
    std::cerr << "Error: Failed to generate dump file\n";
    return 1;
  }

  std::cout << "DMD dump file generated successfully\n";
  return 0;
}
