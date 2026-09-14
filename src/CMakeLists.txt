#
# asterisk-chan-quectel
#

INCLUDE(${CMAKE_CURRENT_SOURCE_DIR}/source-files.cmake)

ADD_LIBRARY(asterisk-chan-quectel SHARED ${SOURCES} ${HEADERS})
TARGET_COMPILE_FEATURES(asterisk-chan-quectel PUBLIC c_std_99)
IF(CHECK_SOURCE_DATE_EPOCH)
    ADD_DEPENDENCIES(asterisk-chan-quectel check-source-date-epoch)
ENDIF()

TARGET_INCLUDE_DIRECTORIES(asterisk-chan-quectel BEFORE PRIVATE ${CMAKE_BINARY_DIR}/include)

TARGET_COMPILE_DEFINITIONS(asterisk-chan-quectel PRIVATE 
    _GNU_SOURCE
    HAVE_CONFIG_H
)

TARGET_LINK_LIBRARIES(asterisk-chan-quectel PRIVATE
    AsteriskModule
    Threads::Threads
    ALSA::ALSA
    SQLite::SQLite3
    Iconv::Iconv
)

SET_TARGET_PROPERTIES(asterisk-chan-quectel PROPERTIES
    LIBRARY_OUTPUT_NAME ${AST_MODULE}
    PREFIX ""
    POSITION_INDEPENDENT_CODE ON
)

TARGET_COMPILE_OPTIONS(asterisk-chan-quectel PRIVATE
    $<$<AND:$<C_COMPILER_ID:GNU>,$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,8>>:-ffile-prefix-map=${CMAKE_CURRENT_BINARY_DIR}=bin -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=src>
    $<$<AND:$<C_COMPILER_ID:GNU>,$<VERSION_GREATER_EQUAL:$<C_COMPILER_VERSION>,4>>:-Wall>
)

TARGET_LINK_OPTIONS(asterisk-chan-quectel PRIVATE
    $<$<BOOL:${LINKER_VERBOSE}>:-Wl,--verbose>
    $<$<BOOL:${LINKER_TRACE}>:-Wl,--trace>
)

IF(CMAKE_LIBRARY_ARCHITECTURE)
    MESSAGE(STATUS "Installing module on architecture-specific directory - lib/${CMAKE_LIBRARY_ARCHITECTURE}/asterisk/modules")
    INSTALL(
        TARGETS asterisk-chan-quectel
        DESTINATION lib/${CMAKE_LIBRARY_ARCHITECTURE}/asterisk/modules
        COMPONENT chan-quectel
    )
ELSE()
    INSTALL(
        TARGETS asterisk-chan-quectel
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/asterisk/modules
        COMPONENT chan-quectel
    )
ENDIF()

# install configuration file
INSTALL(
    FILES ${CMAKE_SOURCE_DIR}/quectel.conf
    DESTINATION ${CMAKE_INSTALL_FULL_SYSCONFDIR}/asterisk
    COMPONENT chan-quectel
)

IF(IS_GIT_REPO AND BUILD_TESTING)
    IF(NOT CMAKE_CROSSCOMPILING)
        ADD_EXECUTABLE(quectel-enum-test ${CMAKE_SOURCE_DIR}/test/enum_bounds.c)
        TARGET_INCLUDE_DIRECTORIES(quectel-enum-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        TARGET_COMPILE_DEFINITIONS(quectel-enum-test PRIVATE _GNU_SOURCE)
        TARGET_COMPILE_FEATURES(quectel-enum-test PRIVATE c_std_99)
        TARGET_COMPILE_OPTIONS(quectel-enum-test PRIVATE -Wall -Wextra -Werror)
        TARGET_LINK_LIBRARIES(quectel-enum-test PRIVATE AsteriskModule)
        ADD_TEST(NAME "Enum bounds regression" COMMAND quectel-enum-test)
        SET_TESTS_PROPERTIES("Enum bounds regression" PROPERTIES LABELS "REGRESSION" TIMEOUT 20)
        FIND_PACKAGE(Python3 COMPONENTS Interpreter REQUIRED)
        ADD_TEST(NAME "PCM regression" COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/test_pcm.py)
        ADD_TEST(NAME "UAC lifecycle regression" COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/test_audio_lifecycle.py)
        ADD_TEST(NAME "Registration initialization regression" COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/test_registration_init.py)
        SET_TESTS_PROPERTIES("PCM regression" "UAC lifecycle regression" "Registration initialization regression" PROPERTIES
            LABELS "AUDIO;REGRESSION"
            TIMEOUT 60
            ENVIRONMENT "CC=${CMAKE_C_COMPILER}"
        )
    ENDIF()
    IF(DEFINED CMAKE_READELF)
        ADD_TEST(NAME "Load library" COMMAND ${CMAKE_COMMAND}
            -DCMAKE_READELF=${CMAKE_READELF}
            -DMODULE_PATH=$<TARGET_FILE:asterisk-chan-quectel>
            -P ${CMAKE_SOURCE_DIR}/cmake/test/dump-header.cmake
        )        
        ADD_TEST(NAME "Check library dependencies" COMMAND ${CMAKE_COMMAND}
            -DCMAKE_READELF=${CMAKE_READELF}
            -DMODULE_PATH=$<TARGET_FILE:asterisk-chan-quectel>
            -P ${CMAKE_SOURCE_DIR}/cmake/test/needed-libs.cmake
        )
        ADD_TEST(NAME "Check comment section" COMMAND ${CMAKE_COMMAND}
            -DCMAKE_READELF=${CMAKE_READELF}
            -DMODULE_PATH=$<TARGET_FILE:asterisk-chan-quectel>
            -DCOMPILER_VERSION=${CMAKE_C_COMPILER_VERSION}
            -P ${CMAKE_SOURCE_DIR}/cmake/test/comment-section.cmake
        )
        SET_TESTS_PROPERTIES(
            "Load library"
            "Check library dependencies"
            "Check comment section"
            PROPERTIES
                LABELS "DLL;READELF"
                TIMEOUT 20
                WORKING_DIRECTORY $<TARGET_FILE_DIR:asterisk-chan-quectel>
        )
        IF(CMAKE_CROSSCOMPILING)
            ADD_TEST(NAME "Check architecture-specific metadata" COMMAND ${CMAKE_COMMAND}
                -DCMAKE_READELF=${CMAKE_READELF}
                -DMODULE_PATH=$<TARGET_FILE:asterisk-chan-quectel>
                -P ${CMAKE_SOURCE_DIR}/cmake/test/arch-specific.cmake
            )
            SET_TESTS_PROPERTIES(
                "Check architecture-specific metadata"
                PROPERTIES
                    LABELS "DLL;READELF"
                    TIMEOUT 20
                    WORKING_DIRECTORY $<TARGET_FILE_DIR:asterisk-chan-quectel>
            )
        ENDIF()
    ELSE()
        MESSAGE(WARNING Cannot find readelf utility)
    ENDIF()
    ADD_TEST(NAME "Check AST_BUILDOPT_SUM" COMMAND ${CMAKE_COMMAND}
        -DMODULE_PATH=$<TARGET_FILE:asterisk-chan-quectel>
        -DAST_BUILDOPT_SUM=${AST_BUILDOPT_SUM}
        -P ${CMAKE_SOURCE_DIR}/cmake/test/AST_BUILDOPT_SUM.cmake
    )
    SET_TESTS_PROPERTIES(
        "Check AST_BUILDOPT_SUM"
        PROPERTIES
            LABELS DLL
            TIMEOUT 20
            WORKING_DIRECTORY $<TARGET_FILE_DIR:asterisk-chan-quectel>
    )
ENDIF()

# formatting targets
IF(NOT CLANG_FORMAT)
    MESSAGE(WARNING "Cannot create formatting targets - clang-format not found")
ELSE()
    ADD_CUSTOM_TARGET(asterisk-chan-quectel-code-formatter
        COMMAND ${CLANG_FORMAT}
            -i
            --style=file
            $<TARGET_PROPERTY:asterisk-chan-quectel,SOURCES>
        WORKING_DIRECTORY
            $<TARGET_PROPERTY:asterisk-chan-quectel,SOURCE_DIR>
        COMMENT
            "Formatting code"
        COMMAND_EXPAND_LISTS
    )

    ADD_CUSTOM_TARGET(asterisk-chan-quectel-code-formatting-check
        COMMAND ${CLANG_FORMAT}
            --dry-run
            --verbose
            -i
            --style=file
            $<TARGET_PROPERTY:asterisk-chan-quectel,SOURCES>
        WORKING_DIRECTORY
            $<TARGET_PROPERTY:asterisk-chan-quectel,SOURCE_DIR>
        COMMENT
            "Checking code formatting"
        COMMAND_EXPAND_LISTS
    )

    # ADD_DEPENDENCIES(asterisk-chan-quectel asterisk-chan-quectel-code-formatter)
    # ADD_DEPENDENCIES(asterisk-chan-quectel asterisk-chan-quectel-code-formatting-check)
ENDIF()
