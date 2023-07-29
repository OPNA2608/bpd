add_executable (${PROJECT_NAME}
	main.c
)

target_compile_options (${PROJECT_NAME}
	PRIVATE
	${WARNING_FLAGS}
)

target_link_libraries (${PROJECT_NAME}
	PRIVATE
	discord
	json-c::json-c
	Threads::Threads
)

install (TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
