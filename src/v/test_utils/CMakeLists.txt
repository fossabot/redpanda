v_cc_library(
  NAME seastar_testing_main
  SRCS seastar_testing_main.cc
  DEPS Seastar::seastar_testing)

add_subdirectory(tests)
