cmake_minimum_required(VERSION 3.16)
project(restconf-h2c-backend 
    VERSION 1.0.0 
    DESCRIPTION "High-performance single-threaded RESTCONF h2c backend"
    LANGUAGES C
)

# ==============================================================================
# Standards et Options
# ==============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Option pour activer la séparation des privilèges (Plugin Externe via UDS)
option(BUILD_EXTERNAL_PLUGIN "Build the sysrepo plugin as a separate privileged daemon (UDS IPC)" OFF)

# ==============================================================================
# Dépendances
# ==============================================================================
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED) # Nécessaire pour le linkage de certaines libs sous-jacentes

# Récupération des bibliothèques via pkg-config (Cible moderne IMPORTED_TARGET)
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET 
    libevent
    libnghttp2
    sysrepo
    libyang
)

# ==============================================================================
# Structure des Sources
# ==============================================================================
set(CORE_SOURCES
    src/main.c
    src/h2c_server.c
    src/router.c
    src/jwt_validator.c
    src/sse_stream.c
)

set(PLUGIN_SOURCES
    src/plugin/sysrepo_plugin.c
)

set(IPC_SOURCES
    src/ipc/uds_common.c
)

# ==============================================================================
# Cible Principale : Le Gateway HTTP (restconf-server)
# ==============================================================================
add_executable(restconf-server ${CORE_SOURCES})

target_include_directories(restconf-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${KEYUTILS_INCLUDE_DIR}
)

target_link_libraries(restconf-server PRIVATE
    PkgConfig::DEPS
    OpenSSL::Crypto
    ${KEYUTILS_LIBRARY}
    Threads::Threads
)

target_compile_options(restconf-server PRIVATE -Wall -Wextra -Wpedantic)

# ==============================================================================
# Gestion du Mode : Interne (Monolithique) vs Externe (Séparé)
# ==============================================================================
if(BUILD_EXTERNAL_PLUGIN)
    message(STATUS "🔨 Mode: EXTERNAL PLUGIN (Séparation des privilèges via UDS)")
    
    # 1. Configuration du Gateway (Client UDS)
    target_compile_definitions(restconf-server PRIVATE USE_EXTERNAL_PLUGIN)
    target_sources(restconf-server PRIVATE 
        src/ipc/uds_gateway.c 
        ${IPC_SOURCES}
    )

    # 2. Création de l'exécutable du Plugin Externe (Démon Privilégié)
    add_executable(restconf-plugin 
        src/plugin/plugin_main.c 
        ${PLUGIN_SOURCES} 
        src/ipc/uds_plugin.c 
        ${IPC_SOURCES}
    )
    
    target_include_directories(restconf-plugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${KEYUTILS_INCLUDE_DIR}
    )
    
    target_link_libraries(restconf-plugin PRIVATE
        PkgConfig::DEPS
        OpenSSL::Crypto
        ${KEYUTILS_LIBRARY}
        Threads::Threads
    )
    
    target_compile_options(restconf-plugin PRIVATE -Wall -Wextra -Wpedantic)

else()
    message(STATUS "🔨 Mode: INTERNAL PLUGIN (Monolithique, Processus unique)")
    
    # Intégration directe des sources du plugin dans le serveur principal
    target_sources(restconf-server PRIVATE ${PLUGIN_SOURCES})
    
    # Aucun code IPC nécessaire, appels de fonctions C directs
endif()

# ==============================================================================
# Installation (Optionnel)
# ==============================================================================
install(TARGETS restconf-server DESTINATION bin)
if(BUILD_EXTERNAL_PLUGIN)
    install(TARGETS restconf-plugin DESTINATION bin)
endif()