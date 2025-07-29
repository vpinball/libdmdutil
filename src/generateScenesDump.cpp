#include <iostream>

#include "DMDUtil/DMDUtil.h"

int main(int argc, char* argv[])
{
  if (argc != 3)
  {
    std::cerr << "Usage: " << argv[0] << " <input.csv> <output.txt>\n";
    return 1;
  }

  DMDUtil::SceneGenerator generator;

  if (!generator.parseCSV(argv[1]))
  {
    std::cerr << "Error: Failed to parse CSV file\n";
    return 1;
  }

  if (!generator.generateDump(argv[2]))
  {
    std::cerr << "Error: Failed to generate dump file\n";
    return 1;
  }

  std::cout << "DMD dump file generated successfully\n";
  return 0;
}
