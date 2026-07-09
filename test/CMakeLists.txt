# CMakeLists.txt pour le plugin de test restconf-test
# 
# Ce fichier configure la compilation du plugin sysrepo pour les tests.
# Le plugin est active via l'option -DBUILD_TEST_PLUGIN=ON
#
# Usage:
#   cmake -DBUILD_TEST_PLUGIN=ON ..
#   make
#   make test-install  # Installe le plugin et le module YANG

cmake_minimum_required(VERSION 3.16)

# Nom du plugin
set(PLUGIN_NAME "resconf-test-plugin")
set(MODULE_NAME "restconf-test")

# ---------------------------------------------------------------------------
# Sources du plugin
# ---------------------------------------------------------------------------
set(TEST_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/resconf-test.c
)

# ---------------------------------------------------------------------------
# Inclure les headers du projet parent
# ---------------------------------------------------------------------------
include_directories(
    ${CMAKE_SOURCE_DIR}/include
)

# ---------------------------------------------------------------------------
# Trouver les dependances
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET
    sysrepo
    libyang
    libevent
    json-c
)

# ---------------------------------------------------------------------------
# Creer le plugin
# ---------------------------------------------------------------------------
if(BUILD_TEST_PLUGIN)
    message(STATUS "🔨 Building test plugin: ${PLUGIN_NAME}")
    
    # Creer le plugin partage
    add_library(${PLUGIN_NAME} SHARED ${TEST_PLUGIN_SOURCES})
    
    # Configuration du plugin
    target_include_directories(${PLUGIN_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
    )
    
    target_link_libraries(${PLUGIN_NAME} PRIVATE
        PkgConfig::DEPS
    )
    
    target_compile_options(${PLUGIN_NAME} PRIVATE
        -Wall -Wextra -Wpedantic
        -fPIC
    )
    
    target_compile_definitions(${PLUGIN_NAME} PRIVATE
        RC_COMPILE_TIME_LOG_LEVEL=${LOG_LEVEL}
    )
    
    # Nom de la librairie de sortie
    set_target_properties(${PLUGIN_NAME} PROPERTIES
        OUTPUT_NAME "sr_plugin_${MODULE_NAME}"
        PREFIX ""
        SUFFIX ".so"
    )
    
    # -----------------------------------------------------------------------
    # Target custom pour installer le plugin et le module YANG
    # -----------------------------------------------------------------------
    
    # Chemin d'installation par defaut
    set(PLUGIN_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/lib/sysrepo/plugins" 
        CACHE PATH "Directory to install the test plugin")
    set(YANG_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/share/sysrepo/yang" 
        CACHE PATH "Directory to install the YANG modules")
    
    # Commande pour installer le plugin
    add_custom_command(
        TARGET ${PLUGIN_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Plugin built: ${CMAKE_BINARY_DIR}/${PLUGIN_NAME}.so"
        COMMENT "Plugin de test compile avec succes"
    )
    
    # Target pour installer le plugin
    add_custom_target(test-install
        # Creer les dossiers avec les bonnes permissions
        COMMAND mkdir -p "${PLUGIN_INSTALL_DIR}"
        COMMAND chmod 755 "${PLUGIN_INSTALL_DIR}" || true
        COMMAND mkdir -p "${YANG_INSTALL_DIR}"
        COMMAND chmod 755 "${YANG_INSTALL_DIR}" || true
        
        # Copier le plugin
        COMMAND cp -f "${CMAKE_BINARY_DIR}/${PLUGIN_NAME}.so" "${PLUGIN_INSTALL_DIR}/sr_plugin_${MODULE_NAME}.so"
        COMMAND chmod 755 "${PLUGIN_INSTALL_DIR}/sr_plugin_${MODULE_NAME}.so" || true
        
        # Copier le module YANG
        COMMAND cp -f "${CMAKE_SOURCE_DIR}/doc/test/modules/restconf-test.yang" "${YANG_INSTALL_DIR}/restconf-test.yang"
        COMMAND chmod 644 "${YANG_INSTALL_DIR}/restconf-test.yang" || true
        
        # Installer le module YANG dans sysrepo
        COMMAND sysrepoctl -i "${YANG_INSTALL_DIR}/restconf-test.yang" || true
        
        # Charger le plugin dans sysrepo
        COMMAND sysrepoctl -P "${PLUGIN_INSTALL_DIR}/sr_plugin_${MODULE_NAME}.so" || true
        
        DEPENDS ${PLUGIN_NAME}
        COMMENT "Installation du plugin de test et du module YANG"
    )
    
    # Target pour desinstaller
    add_custom_target(test-uninstall
        COMMAND sysrepoctl -p "${PLUGIN_INSTALL_DIR}/sr_plugin_${MODULE_NAME}.so" || true
        COMMAND sysrepoctl -r restconf-test || true
        COMMAND ${CMAKE_COMMAND} -E remove
            "${PLUGIN_INSTALL_DIR}/sr_plugin_${MODULE_NAME}.so"
        COMMAND ${CMAKE_COMMAND} -E remove
            "${YANG_INSTALL_DIR}/restconf-test.yang"
        COMMENT "Desinstallation du plugin de test et du module YANG"
    )
    
    # Target pour recharger
    add_custom_target(test-reload
        COMMAND sysrepoctl -p "${PLUGIN_INSTALL_DIR}/sr_plugin_${MODULE_NAME}.so" || true
        COMMAND sysrepoctl -P "${PLUGIN_INSTALL_DIR}/sr_plugin_${MODULE_NAME}.so" || true
        DEPENDS test-install
        COMMENT "Rechargement du plugin de test"
    )
    
    # Installation standard
    install(TARGETS ${PLUGIN_NAME}
        LIBRARY DESTINATION "${PLUGIN_INSTALL_DIR}"
        PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
    )
    
    install(FILES "${CMAKE_SOURCE_DIR}/doc/test/modules/restconf-test.yang"
        DESTINATION "${YANG_INSTALL_DIR}"
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
    )
    
    message(STATUS "✅ Test plugin configured successfully")
    message(STATUS "    Plugin: ${PLUGIN_NAME}")
    message(STATUS "    Module: ${MODULE_NAME}")
    message(STATUS "    Install dir: ${PLUGIN_INSTALL_DIR}")
    message(STATUS "")
    message(STATUS "    To build and install:")
    message(STATUS "      cmake -DBUILD_TEST_PLUGIN=ON ..")
    message(STATUS "      make")
    message(STATUS "      make test-install")
    
else()
    message(STATUS "⚪ Test plugin NOT built (set -DBUILD_TEST_PLUGIN=ON to enable)")
endif()
