cmake_minimum_required(VERSION 3.24)
project(RegiScope LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Runtime C/C++ di Visual Studio incluso nell'exe (/MT): RegiScope parte su qualsiasi PC
# senza installare il "Visual C++ Redistributable". Vale anche per Zydis e libtcc.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "RegiScope va compilato a 64 bit (x64).")
endif()

include(FetchContent)

# TinyCC: il compilatore C incorporato che compila gli snippet in memoria.
# SOURCE_SUBDIR inesistente = scarica i sorgenti senza usare il loro build system.
FetchContent_Declare(tinycc
  GIT_REPOSITORY https://github.com/TinyCC/tinycc.git
  GIT_TAG        c638ddfb593214813aa33536fb993b9a45d23793
  SOURCE_SUBDIR  nessun-cmake)

# Zydis: disassemblatore x86-64 per mostrare l'istruzione corrente.
set(ZYDIS_BUILD_TOOLS    OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN  OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zydis
  GIT_REPOSITORY https://github.com/zyantific/zydis.git
  GIT_TAG        v4.1.0
  GIT_SHALLOW    TRUE)

FetchContent_MakeAvailable(tinycc zydis)
set(TCC_DIR ${tinycc_SOURCE_DIR})

# ---- libtcc (libreria statica) ----
file(WRITE ${CMAKE_BINARY_DIR}/tcc_config/config.h "#define TCC_VERSION \"0.9.28rc\"\n")
# tcc_lines.c include libtcc.c e aggiunge la ricerca "indirizzo -> riga di C".
add_library(libtcc STATIC src/tcc_lines.c)
target_include_directories(libtcc PUBLIC ${TCC_DIR} PRIVATE ${CMAKE_BINARY_DIR}/tcc_config)
target_compile_definitions(libtcc PRIVATE TCC_TARGET_PE TCC_TARGET_X86_64 ONE_SOURCE=1 _CRT_SECURE_NO_WARNINGS)
if(MSVC)
  target_compile_options(libtcc PRIVATE /W0)
else()
  target_compile_options(libtcc PRIVATE -w)
endif()

# ---- applicazione ----
add_executable(RegiScope WIN32
  src/main.cpp
  src/runner.cpp
  src/snippet_runtime.cpp
  src/sysinfo.cpp
  src/panels.cpp
  src/devices.cpp
  src/devices_window.cpp
  res/regiscope.rc)
target_compile_definitions(RegiScope PRIVATE UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(RegiScope PRIVATE libtcc Zydis wbemuuid ole32 oleaut32 psapi comdlg32 comctl32
  mfplat mf mfreadwrite mfuuid winmm)
if(MSVC)
  target_compile_options(RegiScope PRIVATE /utf-8 /W3)
endif()

# File che servono a TinyCC durante l'esecuzione, copiati accanto all'eseguibile.
add_custom_command(TARGET RegiScope POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:RegiScope>/snippet_include
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${TCC_DIR}/include $<TARGET_FILE_DIR:RegiScope>/snippet_include
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/snippet_include $<TARGET_FILE_DIR:RegiScope>/snippet_include
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:RegiScope>/snippet_runtime
  COMMAND ${CMAKE_COMMAND} -E copy ${TCC_DIR}/lib/libtcc1.c $<TARGET_FILE_DIR:RegiScope>/snippet_runtime/
  COMMAND ${CMAKE_COMMAND} -E copy ${TCC_DIR}/win32/lib/chkstk.S $<TARGET_FILE_DIR:RegiScope>/snippet_runtime/
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/esempi $<TARGET_FILE_DIR:RegiScope>/esempi)

set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT RegiScope)
