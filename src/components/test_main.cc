#include "gmock/gmock.h"
#include "utils/logger.h"
#include "utils/custom_string.h"
int main(int argc, char** argv) {

  bool enable_logger = false;
  for (int i = 1; i != argc; ++i) {
    const std::string opt = argv[i];
    if ("--enable-logger" == opt) {
      enable_logger = true;
      break;
    }
  }
  namespace custom_str = utils::custom_string;
  INIT_LOGGER("log4cxx.properties", enable_logger);
  testing::InitGoogleMock(&argc, argv);
  ::testing::DefaultValue<custom_str::CustomString>::Set(
      custom_str::CustomString(""));
  const int result = RUN_ALL_TESTS();
  DEINIT_LOGGER();
  return result;
}
