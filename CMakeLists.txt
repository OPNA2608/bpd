cmake_minimum_required (VERSION 3.10)
project (bpd VERSION 0.0.1 LANGUAGES C)

include (GNUInstallDirs)

set (DLAR_SCRIPT_DIR ${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME})
set (DLAR_SCRIPT ${CMAKE_INSTALL_FULL_DATADIR}/${PROJECT_NAME}/download_and_render.sh)

set (CMAKE_C_STANDARD 99)
set (CMAKE_C_EXTENSIONS ON)
set (CMAKE_C_STANDARD_REQUIRED ON)

option (WARNINGS_ARE_ERRORS "Treat any warnings as errors" ON)
if (NOT MSVC)
	list (APPEND WARNING_FLAGS
		-Wall
		-Wextra
		-pedantic
	)
	if (WARNINGS_ARE_ERRORS)
		list (APPEND WARNING_FLAGS
			-Werror

			# Need to follow APIs
			-Wno-error=unused-parameter
		)
	endif()
endif()

# TODO detect concord
find_package(json-c CONFIG REQUIRED)
find_package(Threads REQUIRED)

add_subdirectory (src)

install (PROGRAMS download_and_render.sh
	DESTINATION ${DLAR_SCRIPT_DIR})
