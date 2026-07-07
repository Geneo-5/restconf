cmake_minimum_required(VERSION 3.16)
project(restconf-h2c-backend 
	VERSION 1.0.0 
	DESCRIPTION "High-performance single-threaded RESTCONF h2c backend"
	LANGUAGES C
)

# ==============================================================================
# Standards et Options de Compilation
# ==============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Option 1 : Séparation des privilèges (Plugin Externe via UDS)
option(BUILD_EXTERNAL_PLUGIN 
	"Build the sysrepo plugin as a separate privileged daemon (UDS IPC)" OFF)

# Option 2 : Mode Debug JWT (Désactive la vérification crypto)
option(ALLOW_INSECURE_JWT 
	"Disable JWT signature verification (UNSAFE DEBUG MODE)" OFF)

# Option 3 : Niveau de log à la compilation (pour optimiser la taille du code)
# Valeurs: 0=TRACE, 1=DEBUG, 2=INFO, 3=WARN, 4=ERROR, 5=FATAL
set(LOG_LEVEL "2" CACHE STRING "Compile-time maximum log level (0=TRACE to 5=FATAL)")

# Option 4 : Chemin de la socket Unix pour l'IPC serveur ↔ plugin externe
set(PLUGIN_UDS_PATH "/var/run/restconf-plugin.sock" CACHE STRING
	"Unix socket path for IPC between restconf-server and restconf-plugin")

# ==============================================================================
# Dépendances
# ==============================================================================
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
# NOTE: pas de find_package(Threads) : le projet est strictement
# mono-thread (voir CLAUDE.md, règle d'or n°1). Aucun symbole pthread
# n'est utilisé dans le code ; ne pas relier Threads::Threads pour ne
# pas laisser croire qu'un usage de threads serait toléré.

# Récupération des bibliothèques via pkg-config
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET 
	libevent
	libnghttp2
	sysrepo
	libyang
)

# ==============================================================================
# Définition des Sources
# ==============================================================================
set(CORE_SOURCES
	src/main.c
	src/h2c_server.c
	src/router.c
	src/jwt_validator.c
	src/sse_stream.c
	src/codec.c
	src/logger.c
)

set(PLUGIN_SOURCES
	src/plugin/sysrepo_plugin.c
)

set(IPC_COMMON_SOURCES
	src/ipc/uds_common.c
)

# ==============================================================================
# Cible 1 : Le Gateway HTTP (restconf-server)
# ==============================================================================
add_executable(restconf-server ${CORE_SOURCES})

target_include_directories(restconf-server PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(restconf-server PRIVATE
	PkgConfig::DEPS
	OpenSSL::Crypto
)

target_compile_options(restconf-server PRIVATE 
	-Wall -Wextra -Wpedantic)

target_compile_definitions(restconf-server PRIVATE
	RC_COMPILE_TIME_LOG_LEVEL=${LOG_LEVEL}
	PLUGIN_UDS_PATH="${PLUGIN_UDS_PATH}")

# Gestion de l'option JWT Insecure
if(ALLOW_INSECURE_JWT)
	target_compile_definitions(restconf-server PRIVATE 
		ALLOW_INSECURE_JWT)
	message(WARNING 
		"🚨 ALLOW_INSECURE_JWT is ON. JWT signatures are IGNORED!")
endif()

# Gestion du mode Plugin (Interne vs Externe)
if(BUILD_EXTERNAL_PLUGIN)
	message(STATUS 
		"🔨 Mode: EXTERNAL PLUGIN (Séparation des privilèges)")
	
	target_compile_definitions(restconf-server PRIVATE 
		USE_EXTERNAL_PLUGIN)
	
	target_sources(restconf-server PRIVATE 
		src/ipc/uds_gateway.c
		${IPC_COMMON_SOURCES}
	)
else()
	message(STATUS 
		"🔨 Mode: INTERNAL PLUGIN (Monolithique)")
	
	target_sources(restconf-server PRIVATE 
		${PLUGIN_SOURCES}
	)
endif()

# ==============================================================================
# Cible 2 : Le Démon Plugin Externe (restconf-plugin)
# ==============================================================================
if(BUILD_EXTERNAL_PLUGIN)
	add_executable(restconf-plugin 
		src/plugin/plugin_main.c
		${PLUGIN_SOURCES}
		src/ipc/uds_plugin.c
		${IPC_COMMON_SOURCES}
		src/codec.c       # <--- AJOUT INDISPENSABLE
		src/logger.c
	)
	
	target_include_directories(restconf-plugin PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/include
	)
	
	target_link_libraries(restconf-plugin PRIVATE
		PkgConfig::DEPS
		OpenSSL::Crypto
	)
	
	target_compile_options(restconf-plugin PRIVATE 
		-Wall -Wextra -Wpedantic)
	
	target_compile_definitions(restconf-plugin PRIVATE
		RC_COMPILE_TIME_LOG_LEVEL=${LOG_LEVEL}
		PLUGIN_UDS_PATH="${PLUGIN_UDS_PATH}")

	if(ALLOW_INSECURE_JWT)
		target_compile_definitions(restconf-plugin PRIVATE 
			ALLOW_INSECURE_JWT)
	endif()
endif()

# ==============================================================================
# Installation (Optionnel)
# ==============================================================================
install(TARGETS restconf-server DESTINATION bin)

if(BUILD_EXTERNAL_PLUGIN)
	install(TARGETS restconf-plugin DESTINATION bin)
endif()