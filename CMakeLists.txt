add_library(CMCP 
  cJSON.c
  http.c
  mcp.c
  stdio_transport.c
  )

target_include_directories(CMCP PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})