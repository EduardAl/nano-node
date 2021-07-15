add_executable(vban_node daemon.cpp daemon.hpp entry.cpp)

target_link_libraries(
  vban_node
  node
  rpc
  secure
  argon2
  Boost::boost
  ${PLATFORM_LIBS})

if(VBAN_STACKTRACE_BACKTRACE)
  target_link_libraries(vban_node backtrace)
endif()

target_compile_definitions(
  vban_node PRIVATE -DTAG_VERSION_STRING=${TAG_VERSION_STRING}
                    -DGIT_COMMIT_HASH=${GIT_COMMIT_HASH})

set_target_properties(
  vban_node PROPERTIES COMPILE_FLAGS
                       "-DQT_NO_KEYWORDS -DBOOST_ASIO_HAS_STD_ARRAY=1")

add_custom_command(
  TARGET vban_node
  POST_BUILD
  COMMAND vban_node --generate_config node >
          ${PROJECT_BINARY_DIR}/config-node.toml.sample
  COMMAND vban_node --generate_config rpc >
          ${PROJECT_BINARY_DIR}/config-rpc.toml.sample)

if((VBAN_GUI OR RAIBLOCKS_GUI) AND NOT APPLE)
  if(WIN32)
    install(TARGETS vban_node RUNTIME DESTINATION .)
  else()
    install(TARGETS vban_node RUNTIME DESTINATION ./bin)
  endif()
endif()
