add_executable (${PROJECT_NAME}
	main.c
	vgmrender.c
	debugping.c
)

target_include_directories (${PROJECT_NAME}
	PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions (${PROJECT_NAME}
	PRIVATE
	PROJECT_NAME="${PROJECT_NAME}"
	DLAR_SCRIPT="${DLAR_SCRIPT}"
)

target_compile_options (${PROJECT_NAME}
	PRIVATE
	${WARNING_FLAGS}
)

target_link_libraries (${PROJECT_NAME}
	PRIVATE
	json-c::json-c
	Threads::Threads
	discord
)

install (TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
