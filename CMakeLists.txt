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

# Option 3 : Plugin de test RESTCONF
option(BUILD_TEST_PLUGIN 
	"Build the test plugin for restconf-test.yang (for qualification tests)" OFF)

# Inclure le sous-dossier test si l'option est active
if(BUILD_TEST_PLUGIN)
    add_subdirectory(test)
endif()

# Option 3 : Niveau de log à la compilation (pour optimiser la taille du code)
# Valeurs: 0=TRACE, 1=DEBUG, 2=INFO, 3=WARN, 4=ERROR, 5=FATAL
set(LOG_LEVEL "2" CACHE STRING "Compile-time maximum log level (0=TRACE to 5=FATAL)")

# Option 4 : Chemin de la socket Unix pour l'IPC serveur ↔ plugin externe
set(PLUGIN_UDS_PATH "/var/run/restconf-plugin.sock" CACHE STRING
	"Unix socket path for IPC between restconf-server and restconf-plugin")

# Option 5 : Base du dépôt YANG de sysrepo (ROADMAP.md item 3.10)
# Utilisé uniquement en mode Externe (BUILD_EXTERNAL_PLUGIN=ON) pour
# construire le contexte libyang local du gateway, depuis
# <path>/yang. Doit correspondre au REPO_PATH de sysrepo (défaut
# amont: /etc/sysrepo). Surchargeable aussi à l'exécution via la
# variable d'environnement SYSREPO_REPOSITORY_PATH.
set(SR_YANG_REPO_PATH "/etc/sysrepo" CACHE STRING
	"Base path of the sysrepo YANG repository (modules under <path>/yang), used to build restconf-server's local libyang context in External Plugin mode")

# ==============================================================================
# Dépendances
# ==============================================================================
find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
# ROADMAP.md item 3.12 : the sysrepo worker thread is a documented
# exception to the single-thread rule (AGENTS.md rule #1).  It is
# the sole owner of sr_conn_ctx_t and confined behind a strict
# message-passing boundary.  No other pthread usage is permitted.

# Récupération des bibliothèques via pkg-config
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET 
	libevent
	libnghttp2
	sysrepo
	libyang
	json-c
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
	src/plugin/sysrepo_worker.c
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
	pthread
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
		USE_EXTERNAL_PLUGIN
		# ROADMAP.md item 3.10 : chemin du dépôt YANG utilisé par
		# uds_gateway.c pour construire son contexte libyang local.
		SR_REPO_PATH_DEFAULT="${SR_YANG_REPO_PATH}")
	
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
		pthread
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

# ==============================================================================
# Intégration CTest (ROADMAP.md item 8.9)
# ==============================================================================
# La suite de conformité RFC 8040/8527/8650 vit dans test/*.py (pytest +
# client h2c maison, cf. test/conftest.py). Chaque test y démarre lui-même
# le binaire restconf-server (et sysrepo-plugind le cas échéant) via
# subprocess : aucune orchestration Docker n'est requise pour lancer
# pytest, seulement un environnement sysrepo avec le module
# doc/test/modules/restconf-test.yang déjà installé (cf. test-install,
# target définie dans test/CMakeLists.txt, et docker/entrypoint.sh qui
# invoque exactement la même commande pytest en conteneur).
#
# BUILD_CTEST_INTEGRATION est désactivé par défaut : l'activer suppose que
# l'environnement d'exécution (hôte ou conteneur) a déjà sysrepo/le module
# de test installés ; sinon la commande pytest échouera au démarrage du
# premier test (fixture server_process).
option(BUILD_CTEST_INTEGRATION
	"Register the pytest RESTCONF conformance suite as a CTest test (requires sysrepo + restconf-test.yang already installed, see test-install target)" OFF)

if(BUILD_CTEST_INTEGRATION)
	enable_testing()

	find_program(PYTEST_EXECUTABLE NAMES pytest pytest3)
	if(NOT PYTEST_EXECUTABLE)
		message(WARNING "BUILD_CTEST_INTEGRATION=ON mais 'pytest' est introuvable dans PATH ; ctest échouera.")
		set(PYTEST_EXECUTABLE pytest)
	endif()

	add_test(
		NAME restconf_pytest_suite
		COMMAND ${PYTEST_EXECUTABLE} test/ --show-capture no
		WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	)

	# La suite complète (145 tests, incluant démarrage/arrêt répétés du
	# serveur h2c et abonnements SSE) prend largement plus que le budget
	# ctest par défaut (1500s) ; borne large mais finie pour détecter un
	# vrai blocage (ex. régression de type 3.12 sur sr_get_data()).
	set_tests_properties(restconf_pytest_suite PROPERTIES
		TIMEOUT 900
		ENVIRONMENT "RESTCONF_SERVER_BIN=$<TARGET_FILE:restconf-server>"
	)

	message(STATUS "🧪 CTest: restconf_pytest_suite enregistré (ctest --output-on-failure)")
endif()