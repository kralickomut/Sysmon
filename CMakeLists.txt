cmake_minimum_required(VERSION 3.16)
project(statsTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(sysmon_core
  # core headers (shown in IDE)
  core/ISystemMonitor.h
  core/CpuStats.h
  core/MemStats.h
  core/ProcessStats.h

  # platform headers (shown in IDE)
  platform/linux/LinuxSystemMonitor.h
  platform/mac/MacSystemMonitor.h
  platform/win/WinSystemMonitor.h

  # all platform sources (shown in IDE)
  platform/linux/LinuxSystemMonitor.cpp
  platform/mac/MacSystemMonitor.cpp
  platform/win/WinSystemMonitor.cpp
)

# Build only the current platform .cpp; keep others visible
if(APPLE)
  set_source_files_properties(
    platform/linux/LinuxSystemMonitor.cpp
    platform/win/WinSystemMonitor.cpp
    PROPERTIES HEADER_FILE_ONLY TRUE)
elseif(WIN32)
  set_source_files_properties(
    platform/linux/LinuxSystemMonitor.cpp
    platform/mac/MacSystemMonitor.cpp
    PROPERTIES HEADER_FILE_ONLY TRUE)
else() # Linux
  set_source_files_properties(
    platform/mac/MacSystemMonitor.cpp
    platform/win/WinSystemMonitor.cpp
    PROPERTIES HEADER_FILE_ONLY TRUE)
endif()

target_link_libraries(sysmon_core PUBLIC Qt6::Core)

# ✅ Make <core/...> and <platform/...> includes work everywhere
target_include_directories(sysmon_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}   # project root as include root
)

# macOS frameworks if you need them
if(APPLE)
  target_link_libraries(sysmon_core PUBLIC "-framework IOKit" "-framework CoreFoundation")
endif()

add_executable(statsTest main.cpp)
target_link_libraries(statsTest PRIVATE sysmon_core)

install(TARGETS statsTest RUNTIME DESTINATION bin)
