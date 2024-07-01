cmake_minimum_required(VERSION 3.29.3)
project(weather)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_TYPE Debug)

find_package(PkgConfig REQUIRED)
pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson)

add_executable(${PROJECT_NAME} source/main.c)
target_include_directories(${PROJECT_NAME} PRIVATE header)
target_link_directories(${PROJECT_NAME} PRIVATE library)
target_link_libraries(${PROJECT_NAME} PRIVATE PkgConfig::JANSSON curl)
target_compile_options(
	${PROJECT_NAME}
	PRIVATE -O2
			-Werror
			-Wall
			-Wextra
			-pedantic
			-Wfloat-equal
			-Wundef
			-Wshadow
			-Wpointer-arith
			-Wcast-align
			-Wstrict-prototypes
			-Wstrict-overflow=5
			-Wwrite-strings
			-Waggregate-return
			-Wcast-qual
			-Wswitch-default
			-Wswitch-enum
			-Wconversion
			-Wunreachable-code
)
