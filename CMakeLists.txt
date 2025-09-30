cmake_minimum_required(VERSION 3.14)
project(mysqlpcap VERSION 1.0 LANGUAGES CXX)

# Set the C++ Standard for all targets
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add custom module path (as in the original)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# --- Find Dependencies ---
# Find MySQL (assumes this works or you have a FindMySQL.cmake)
find_package(MySQL REQUIRED)

# Find LibPcap using the older, more reliable method (Fix for previous error)
find_library(PCAP_LIBRARY NAMES pcap HINTS /usr/lib /usr/local/lib)
find_path(PCAP_INCLUDE_DIR NAMES pcap.h HINTS /usr/include /usr/local/include)

# Fail if libpcap is not found
if(NOT PCAP_LIBRARY OR NOT PCAP_INCLUDE_DIR)
    message(FATAL_ERROR "Could not find libpcap. Please install libpcap development package (e.g., libpcap-dev or libpcap-devel).")
endif()

# Find other modern packages
find_package(Threads REQUIRED)

# Find PCRE2 using PkgConfig (Fix for current error)
# This assumes the libpcre2-8 development package is installed.

# --- Legacy Find Method for PCRE2 ---
# Find the PCRE2 library file
find_library(PCRE2_LIBRARY NAMES pcre2-8 HINTS /usr/lib /usr/local/lib)
# Find the PCRE2 include directory
find_path(PCRE2_INCLUDE_DIR NAMES pcre2.h HINTS /usr/include /usr/local/include)

# Fail if PCRE2 is not found
if(NOT PCRE2_LIBRARY)
    message(FATAL_ERROR "Could not find PCRE2 (libpcre2-8). Please install the development package or set PCRE2_LIBRARY/PCRE2_INCLUDE_DIR.")
endif()

# --- Main Executable ---
add_executable(mysqlpcap
    mysqlpcap.cc
    mysql_stream.cc
    mysql_packet.cc
    mysql_stream_manager.cc
    query_pattern.cc
    ip_stream.cc
    table_stats.cc
)

# Set properties for the main executable
target_include_directories(mysqlpcap
    PRIVATE
        ${MYSQL_INCLUDE_DIR}
        ${PCAP_INCLUDE_DIR}
        ${PCRE2_INCLUDE_DIRS} 
)

# Link libraries
target_link_libraries(mysqlpcap
    PRIVATE
        ${PCAP_LIBRARY}
        ${MYSQL_LIBRARIES}
        Threads::Threads
        ${PCRE2_LIBRARY} # Link PCRE2 libraries from PkgConfig
)

# --- Test Executables ---
add_executable(test_query_pattern query_pattern.cc)
add_executable(test_table_stats table_stats.cc)

# Set preprocessor definitions
target_compile_definitions(test_query_pattern
    PRIVATE
        TEST_QUERY_PATTERN
)
target_compile_definitions(test_table_stats
    PRIVATE
        TEST_TABLE_STATS
)

# Link test executables
target_link_libraries(test_query_pattern
    PRIVATE
        ${PCRE2_LIBRARY} # Link PCRE2 for the test target
)
