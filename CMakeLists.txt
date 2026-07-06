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

# ==============================================================================
# Dépendances
# ==============================================================================
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

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
	Threads::Threads
)

target_compile_options(restconf-server PRIVATE 
	-Wall -Wextra -Wpedantic)

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
	)
	
	target_include_directories(restconf-plugin PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/include
	)
	
	target_link_libraries(restconf-plugin PRIVATE
		PkgConfig::DEPS
		OpenSSL::Crypto
		Threads::Threads
	)
	
	target_compile_options(restconf-plugin PRIVATE 
		-Wall -Wextra -Wpedantic)

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