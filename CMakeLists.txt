# ==============================================================================
# RESTCONF Server - Build Configuration (RFC 8040 + RFC 8527)
# ==============================================================================
# Implementation legere d'un serveur RESTCONF utilisant fcgi2 (FCGX_*) comme
# transport HTTP et sysrepo (+ libyang pour le JSON RFC 7951) comme moteur de
# datastores YANG/NMDA.
#
# NB (corrections par rapport au brouillon precedent) :
#  - ni sysrepo, ni libyang, ni fcgi2 ne fournissent de fichier CMake
#    "Config.cmake" standard sur la plupart des distributions / installations
#    depuis les sources : find_package(... CONFIG) echouait systematiquement.
#    On utilise donc pkg-config quand il est disponible, puis on retombe sur
#    find_path()/find_library() avec les emplacements usuels.
#  - les sources errors.c et restconf_path.c manquaient de la liste de
#    add_executable() (le lien echouait avec des symboles manquants).
#  - la cible de lien "pthreads" n'existe pas ; c'est Threads::Threads
#    (fourni par find_package(Threads)) qu'il faut utiliser.
#  - install(CODE "COPY ...") n'est pas une commande CMake valide ; remplace
#    par install(FILES ... DESTINATION lib).
#  - les deux execute_process(COMMAND "cmake .." / "make -j" ...) en fin de
#    fichier relancaient cmake/make de maniere recursive pendant la phase de
#    configuration de ce meme projet : supprimes (c'est a l'utilisateur de
#    lancer cmake/make depuis son repertoire de build, cf. README.md).
# ==============================================================================

cmake_minimum_required(VERSION 3.16)
project(restconfd VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -Wall -Wextra -Wno-unused-parameter)
else()
    add_compile_options(-O2 -Wall -Wextra -Wno-unused-parameter)
endif()

# ==============================================================================
# Options
# ==============================================================================
option(BUILD_TESTS "Build unit tests" OFF)               # reserve pour plus tard
option(INSTALL_RUNTIME_LIBS "Copier les .so de sysrepo/libyang/fcgi2 a l'install" OFF)

# ==============================================================================
# Dependances externes
# ==============================================================================
find_package(PkgConfig)
find_package(Threads REQUIRED)

if(PkgConfig_FOUND)
    pkg_check_modules(PC_SYSREPO QUIET libsysrepo)
    pkg_check_modules(PC_LIBYANG QUIET libyang)
endif()

# --- sysrepo ---
find_path(SYSREPO_INCLUDE_DIR sysrepo.h
    HINTS ${PC_SYSREPO_INCLUDE_DIRS} /usr/local/include /usr/include /opt/sysrepo/include)
find_library(SYSREPO_LIBRARY NAMES sysrepo
    HINTS ${PC_SYSREPO_LIBRARY_DIRS} /usr/local/lib /usr/lib /opt/sysrepo/lib)

# --- libyang (dependance de sysrepo) ---
find_path(LIBYANG_INCLUDE_DIR libyang/libyang.h
    HINTS ${PC_LIBYANG_INCLUDE_DIRS} /usr/local/include /usr/include /opt/sysrepo/include)
find_library(LIBYANG_LIBRARY NAMES yang
    HINTS ${PC_LIBYANG_LIBRARY_DIRS} /usr/local/lib /usr/lib /opt/sysrepo/lib)

# --- fcgi2 (FastCGI-Archives/fcgi2, API FCGX_*) ---
find_path(FCGI_INCLUDE_DIR fcgiapp.h
    HINTS /usr/local/include /usr/include /opt/fcgi2/include)
find_library(FCGI_LIBRARY NAMES fcgi
    HINTS /usr/local/lib /usr/lib /opt/fcgi2/lib)

if(NOT SYSREPO_LIBRARY OR NOT SYSREPO_INCLUDE_DIR)
    message(FATAL_ERROR "sysrepo introuvable. Compilez/installez-le (depot indique "
        "dans le fichier projet 'sysrepo') ou passez -DSYSREPO_INCLUDE_DIR=... "
        "-DSYSREPO_LIBRARY=... explicitement.")
endif()
if(NOT LIBYANG_LIBRARY OR NOT LIBYANG_INCLUDE_DIR)
    message(FATAL_ERROR "libyang introuvable (dependance de sysrepo).")
endif()
if(NOT FCGI_LIBRARY OR NOT FCGI_INCLUDE_DIR)
    message(FATAL_ERROR "libfcgi (fcgi2) introuvable. Compilez-le depuis le depot "
        "indique dans le fichier projet 'fastcgi2', ou passez -DFCGI_INCLUDE_DIR=... "
        "-DFCGI_LIBRARY=... explicitement.")
endif()

# ==============================================================================
# Cible executable
# ==============================================================================
add_executable(restconfd
    src/main.c
    src/http.c
    src/errors.c
    src/restconf_path.c
    src/sysrepo_backend.c
    src/restconf_handler.c
)

target_include_directories(restconfd PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SYSREPO_INCLUDE_DIR}
    ${LIBYANG_INCLUDE_DIR}
    ${FCGI_INCLUDE_DIR}
)

target_link_libraries(restconfd PRIVATE
    ${SYSREPO_LIBRARY}
    ${LIBYANG_LIBRARY}
    ${FCGI_LIBRARY}
    Threads::Threads
)

# ==============================================================================
# Installation
# ==============================================================================
# ==============================================================================
# Plugin sysrepo : ietf-restconf-monitoring:restconf-state (donnee
# operationnelle, sr_oper_get_subscribe(), cf. "Plugin sysrepo" dans
# README.md). Bibliotheque partagee independante de restconfd : destinee a
# etre chargee par sysrepo-plugind, pas liee a l'executable restconfd.
# ==============================================================================
add_library(restconf_monitoring_plugin MODULE
    plugins/restconf_monitoring/restconf_monitoring.c
)
set_target_properties(restconf_monitoring_plugin PROPERTIES
    OUTPUT_NAME restconf_monitoring
    PREFIX ""
)
target_include_directories(restconf_monitoring_plugin PRIVATE
    ${SYSREPO_INCLUDE_DIR}
    ${LIBYANG_INCLUDE_DIR}
)
target_link_libraries(restconf_monitoring_plugin PRIVATE
    ${SYSREPO_LIBRARY}
    ${LIBYANG_LIBRARY}
)

# ==============================================================================
# Installation
# ==============================================================================
install(TARGETS restconfd RUNTIME DESTINATION bin)
install(FILES README.md LICENSE DESTINATION doc)

# NB : restconf_monitoring_plugin n'est PAS installe automatiquement dans le
# repertoire de plugins de sysrepo ("SRPD plugins path", affiche par cmake
# lors de la configuration de sysrepo lui-meme) : ce chemin est externe a ce
# projet et varie selon votre installation. Copiez manuellement
# restconf_monitoring.so a cet emplacement puis redemarrez sysrepo-plugind,
# cf. README.md.

if(INSTALL_RUNTIME_LIBS)
    install(FILES ${SYSREPO_LIBRARY} ${LIBYANG_LIBRARY} ${FCGI_LIBRARY}
        DESTINATION lib)
endif()

if(BUILD_TESTS)
    message(STATUS "BUILD_TESTS=ON mais aucune suite de tests n'est encore definie.")
endif()
