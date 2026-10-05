
add_executable(cmcp_core_test core_test.c)
target_link_libraries(cmcp_core_test PRIVATE CMCP)
add_test(NAME cmcp_core COMMAND cmcp_core_test)
add_executable(cmcp_core_no_vfs_test core_test.c ../mcp.c ../cJSON.c ../serial_transport.c)
target_include_directories(cmcp_core_no_vfs_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(cmcp_core_no_vfs_test PRIVATE C_MCP_ENABLE_VFS=0)
target_compile_definitions(cmcp_core_no_vfs_test PRIVATE C_MCP_ENABLE_SERIAL_LOOP=0)
if(UNIX)
  target_link_libraries(cmcp_core_no_vfs_test PRIVATE m)
endif()
add_test(NAME cmcp_core_no_vfs COMMAND cmcp_core_no_vfs_test)
# Test the real desktop stdio defaults independently of the input-loop option.
add_library(cmcp_serial_test_core STATIC ../mcp.c ../cJSON.c ../serial_transport.c)
target_include_directories(cmcp_serial_test_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(cmcp_serial_test_core PUBLIC C_MCP_ENABLE_SERIAL_LOOP=1)
target_compile_definitions(cmcp_serial_test_core PUBLIC C_MCP_ENABLE_VFS=$<BOOL:${CMCP_ENABLE_VFS}>)
if(UNIX)
  target_link_libraries(cmcp_serial_test_core PUBLIC m)
endif()
# The parser fixture includes serial_transport.c with mocked stdio operations.
# Desktop transport functions are ordinary definitions, not weak overrides.
add_executable(cmcp_serial_transport_test serial_transport_test.c ../mcp.c ../cJSON.c)
target_include_directories(cmcp_serial_transport_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(cmcp_serial_transport_test PRIVATE C_MCP_ENABLE_SERIAL_LOOP=1)
target_compile_definitions(cmcp_serial_transport_test PRIVATE C_MCP_ENABLE_VFS=$<BOOL:${CMCP_ENABLE_VFS}>)
if(UNIX)
  target_link_libraries(cmcp_serial_transport_test PRIVATE m)
endif()
add_test(NAME cmcp_serial_transport COMMAND cmcp_serial_transport_test)
add_executable(cmcp_serial_driver serial_driver.c)
target_link_libraries(cmcp_serial_driver PRIVATE cmcp_serial_test_core)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME cmcp_serial_stdio
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_serial_transport.py
    $<TARGET_FILE:cmcp_serial_driver> -v)
