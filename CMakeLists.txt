# ==============================================================================
# RESTCONF Server - Build Configuration (RFC 8040 + RFC 8527)
# ==============================================================================
# Serveur RESTCONF implémentation légère utilisant FastCGI comme transport HTTP
# et sysrepo pour le gestionnaire de données YANG/NMDA via libyang JSON.
#
# Architecture : fcgi2 (transport HTTP via FastCGI) + sysrepo (datastores)
# ==============================================================================

cmake_minimum_required(VERSION 3.16)
project(restconfd VERSION 1.0 LANGUAGES C)

# ==============================================================================
# Configurable Options
# ==============================================================================
option(BUILD_TESTS "Build unit tests" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(ENABLE_FCGI "Enable FastCGI integration" ON)

# ==============================================================================
# External Dependencies (auto-detection with fallback paths)
# ==============================================================================
set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH};/opt/fcgi2;/opt/sysrepo")

# --- Libyang (sysrepo dependencies) ---
find_package(LibYANG REQUIRED CONFIG)
if(NOT LibYANG_FOUND)
    message(FATAL_ERROR "LibYANG must be installed first")
endif()

set(LIBYANG_INCLUDE_DIR ${LibYANG_INCLUDE_DIRS})
set(LIBYANG_LIBRARY "${CMAKE_BINARY_DIR}/libyang.so")
if(NOT LIBYANG_LIBRARY)
    find_library(LIBYANG_LIBRARY libyang HINTS /usr/local/lib /opt/sr/lib)
endif()

# --- Sysrepo ---
find_package(SysRepo REQUIRED CONFIG)
if(NOT SysRepo_FOUND)
    message(FATAL_ERROR "Sysrepo must be installed first")
endif()

set(SYSREPO_INCLUDE_DIR ${SysRepo_INCLUDE_DIRS})
set(SYSREPO_LIBRARY "${CMAKE_BINARY_DIR}/libsysrepo.so")
if(NOT SYSREPO_LIBRARY)
    find_library(SYSREPO_LIBRARY libsysrepo HINTS /usr/local/lib /opt/sr/lib)
endif()

# --- FastCGI ---
find_package(FCGI REQUIRED CONFIG)
if(NOT FCGI_FOUND)
    message(FATAL_ERROR "FCGI must be installed first")
endif()

set(FCGI_INCLUDE_DIR ${FCGI_INCLUDE_DIRS})
set(FCGI_LIBRARY "${CMAKE_BINARY_DIR}/libfcgi.so")
if(NOT FCGI_LIBRARY)
    find_library(FCGI_LIBRARY libfcgi HINTS /usr/local/lib /opt/fcgi2/lib)
endif()

# ==============================================================================
# Include Directories
# ==============================================================================
include_directories(${SYSREPO_INCLUDE_DIRS} ${LIBYANG_INCLUDE_DIRS} ${FCGI_INCLUDE_DIR})

# ==============================================================================
# Executable Target
# ==============================================================================
set(THREADS_PREFER_PTHREAD TRUE)
find_package(Threads REQUIRED)

add_executable(restconfd
    src/main.c
    src/restconf_handler.c
    src/sysrepo_backend.c
    src/http.c
)
target_include_directories(restconfd PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(restconfd PRIVATE
    ${SYSREPO_LIBRARY}
    ${FCGI_LIBRARY}
    ${LIBYANG_LIBRARY}
    pthreads
)

# ==============================================================================
# Installation Rules
# ==============================================================================
install(TARGETS restconfd DESTINATION bin)
install(CODE "COPY ${SYSREPO_LIBRARY} ${CMAKE_INSTALL_PREFIX}/lib"
DESTINATION lib)
install(CODE "COPY ${FCGI_LIBRARY} ${CMAKE_INSTALL_PREFIX}/lib"
DESTINATION lib)
install(CODE "COPY ${LIBYANG_LIBRARY} ${CMAKE_INSTALL_PREFIX}/lib"
DESTINATION lib)

# ==============================================================================
# Documentation Installation
# ==============================================================================
install(FILES README.md DESTINATION doc)
install(FILES LICENSE DESTINATION doc)

# ==============================================================================
# Debug Configuration (remove for production builds)
# ==============================================================================
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR NOT CMAKE_BUILD_TYPE)
    add_compile_options(-g -O0 -Wall -Wextra)
else()
    add_compile_options(-O2)
endif()

# ==============================================================================
# Build Script
# ==============================================================================
execute_process(COMMAND "cmake .." -DCMAKE_BUILD_TYPE=RelWithDebInfo
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

execute_process(COMMAND "make -j" "$(nproc)"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)
