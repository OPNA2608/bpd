add_executable (${PROJECT_NAME}
	main.c
)

target_link_libraries (${PROJECT_NAME}
	PRIVATE
	discord
	json-c::json-c
)

install (TARGETS ${PROJECT_NAME}
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
