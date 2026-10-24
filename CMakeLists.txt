cmake_minimum_required(VERSION 3.21)

# ---- Cache build vars ----

macro(set_from_environment VARIABLE)
	if (NOT DEFINED ${VARIABLE} AND DEFINED ENV{${VARIABLE}})
		set(${VARIABLE} $ENV{${VARIABLE}})
	endif ()
endmacro()

set_from_environment(VCPKG_ROOT)
if (NOT DEFINED VCPKG_ROOT)
	message(FATAL_ERROR "Variable VCPKG_ROOT is not set.")
endif ()

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(FATAL_ERROR "In-source builds not allowed.")
endif()

set(Boost_USE_STATIC_RUNTIME OFF CACHE BOOL "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")

# ---- Project ----

project(
	STBHavokCollisionFix
	VERSION 1.0.0
	LANGUAGES CXX
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/include/Version.h @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/version.rc @ONLY)

if (MSVC)
	add_compile_definitions(_UNICODE)
	if (NOT ${CMAKE_GENERATOR} STREQUAL "Ninja")
		add_compile_options(/MP)
	endif ()
endif ()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set(Boost_USE_STATIC_LIBS ON)

# ---- Dependencies ----

# CommonLibSSE-NG (MIT): external/CommonLibSSE-NG + the STB patch, see external/CommonLibSSE-NG.cmake.
include("${CMAKE_CURRENT_SOURCE_DIR}/external/CommonLibSSE-NG.cmake")

find_package(spdlog REQUIRED CONFIG)

# ---- Sources ----

set(SOURCES
	src/PCH.h
	src/Settings.h
	src/Settings.cpp
	src/MoppValidator.h
	src/MoppValidator.cpp
	src/HitSortFix.h
	src/HitSortFix.cpp
	src/MoppLoadFix.h
	src/MoppLoadFix.cpp
	src/CompressedMeshFix.h
	src/CompressedMeshFix.cpp
	src/main.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

add_library(
	${PROJECT_NAME}
	SHARED
	${SOURCES}
	${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
	${CMAKE_CURRENT_BINARY_DIR}/version.rc
	.clang-format
	.editorconfig
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

target_include_directories(
	${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_BINARY_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
		CommonLibSSE::CommonLibSSE
		spdlog::spdlog
)

set(STB_MSVC_OPTIONS
	/sdl /utf-8 /Zi /permissive-
	/Zc:alignedNew /Zc:auto /Zc:char8_t /Zc:__cplusplus /Zc:externC /Zc:externConstexpr /Zc:forScope
	/Zc:hiddenFriend /Zc:implicitNoexcept /Zc:lambda /Zc:noexceptTypes /Zc:preprocessor
	/Zc:referenceBinding /Zc:rvalueCast /Zc:sizedDealloc /Zc:strictStrings /Zc:ternary
	/Zc:threadSafeInit /Zc:tlsGuards /Zc:trigraphs /Zc:wchar_t
	/external:anglebrackets /external:W0
	/W4 /WX
)

if (MSVC)
	target_compile_options(
		${PROJECT_NAME}
		PRIVATE
			${STB_MSVC_OPTIONS}
			"$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/Ob3>"
	)
	target_link_options(
		${PROJECT_NAME}
		PRIVATE
			/WX
			"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
			"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
	)
endif ()

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

# ---- MOPP validator self-test (no game needed) ----
# moppcheck: built-in synthetic cases, or `moppcheck <dump.bin>` -- a dump from tools/extract_mopp.py.

add_executable(moppcheck tools/moppcheck.cpp src/MoppValidator.cpp)
target_compile_features(moppcheck PRIVATE cxx_std_20)
target_include_directories(moppcheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if (MSVC)
	target_compile_options(moppcheck PRIVATE ${STB_MSVC_OPTIONS})
endif ()

# ---- Output ----
# build/Release/STBHavokCollisionFix.dll (+ .pdb); data/SKSE/Plugins/STBHavokCollisionFix.ini next to it in the mod.
