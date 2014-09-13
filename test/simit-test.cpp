#include <gtest/gtest.h>
#include <string>

int main(int argc, char **argv) {
  // If there is just one argument and it is not a gtest option, then filter
  // the tests using that argument surrounded by wildcards.
  std::string newArg;
  if (argc == 2 && !(argv[1][0] == '-' && argv[1][1] == '-')) {
    newArg = std::string("--gtest_filter=");

    char *dotPtr = strchr(argv[1], '.');
    if (!dotPtr) {
      newArg += "*" + std::string(argv[1]) + "*";
    }
    else {
      if (dotPtr[1] == '\0') {
        newArg += std::string(argv[1]) + "*";
      }
      else {
        newArg += std::string(argv[1]);
      }
    }
    argv[1] = (char*)newArg.c_str();
  }

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}