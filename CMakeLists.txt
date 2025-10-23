cmake_minimum_required(VERSION 2.8)
project(mysqlpcap)
set(MYSQLPCAP_VERSION "1.0.4")
set(MYSQLPCAP_VERSION_MAJOR 1)
set(MYSQLPCAP_VERSION_MINOR 4)
configure_file(version.h.in version.h @ONLY)
configure_file(mysqlpcap.spec.in mysqlpcap.spec @ONLY)

# Set the C++ Standard for all targets (Best effort for C++11 in CMake 2.8)
# We rely on the CXXFLAGS environment variable or the compiler's default for C++11.
# The following is a common idiom for older CMake versions:
if(CMAKE_COMPILER_IS_GNU_CXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
endif()

# Add custom module path (as in the original)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

    # Standard find package for systems where MySQL/MariaDB is installed normally
if (NOT JUST_EXPAND_IN)
    # Perform the necessary dependency search for the MySQL development headers
    # and libraries, making it a REQUIRED dependency for a normal build.
    find_package(MySQL REQUIRED)

    if (MySQL_FOUND)
        message(STATUS "Found MySQL dependency. Including headers and libraries.")
        include_directories(${MYSQL_INCLUDE_DIR})
    endif()
else()
    # Confirmation message that the check was skipped.
    message(STATUS "Skipping 'find_package(MySQL)' check as JUST_EXPAND_IN is set.")
endif()

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(BISON REQUIRED)

# Find LibPcap using the older, more reliable method
find_library(PCAP_LIBRARY NAMES pcap HINTS /usr/lib /usr/local/lib)
find_path(PCAP_INCLUDE_DIR NAMES pcap.h HINTS /usr/include /usr/local/include)

# Find PCRE2 using Legacy Find Method
# Find the PCRE2 library file
find_library(PCRE2_LIBRARY NAMES pcre2-8 HINTS /usr/lib /usr/local/lib)
# Find the PCRE2 include directory
find_path(PCRE2_INCLUDE_DIR NAMES pcre2.h HINTS /usr/include /usr/local/include)

BISON_TARGET(SQL_PARSER
    ${CMAKE_CURRENT_SOURCE_DIR}/sql_parser.yy
    ${CMAKE_CURRENT_BINARY_DIR}/sql_parser.cc
    # Customizes Bison command flags for C++ reentrant parser
    COMPILE_FLAGS   "-d -p yy"
)

# --- Main Executable ---
add_executable(mysqlpcap
    mysqlpcap.cc
    mysql_stream.cc
    mysql_packet.cc
    mysql_stream_manager.cc
    query_pattern.cc
    ip_stream.cc
    table_stats.cc
    pcap_detect.cc
    ${BISON_SQL_PARSER_OUTPUT_SOURCE}
    ${BISON_SQL_PARSER_OUTPUT_HEADER}
)
add_dependencies(mysqlpcap SQL_PARSER)


# Set properties for the main executable
include_directories(
    ${CMAKE_CURRENT_BINARY_DIR}
    ${MYSQL_INCLUDE_DIR}
    ${PCAP_INCLUDE_DIR}
    ${PCRE2_INCLUDE_DIR} 
)

# Link libraries using the older, global approach
target_link_libraries(mysqlpcap
    ${PCAP_LIBRARY}
    ${MYSQL_LIBRARIES}
    ${ZLIB_LIBRARIES} # Dependencies for static libmysqlclient
    ${OPENSSL_LIBRARIES} # Dependencies for static libmysqlclient    ${CMAKE_THREAD_LIBS_INIT} # Use older variable for threads
    ${PCRE2_LIBRARY}
    -ldl -lpthread
)

# --- Test Executables ---
add_executable(test_query_pattern query_pattern.cc)
add_executable(test_table_stats table_stats.cc     ${BISON_SQL_PARSER_OUTPUT_SOURCE} ${BISON_SQL_PARSER_OUTPUT_HEADER})
add_dependencies(test_table_stats SQL_PARSER)
add_executable(test_pcap_detect pcap_detect.cc)

# Set preprocessor definitions
target_compile_definitions(test_query_pattern
    PRIVATE
        TEST_QUERY_PATTERN
)

target_compile_definitions(test_table_stats
    PRIVATE
        TEST_TABLE_STATS
)

target_compile_definitions(test_pcap_detect
    PRIVATE
        TEST_PCAP_DETECT
)

# Link test executables
target_link_libraries(test_query_pattern
    ${PCRE2_LIBRARY}
)

target_link_libraries(test_table_stats
    ${PCRE2_LIBRARY}
)

install(TARGETS mysqlpcap
    DESTINATION bin
)
