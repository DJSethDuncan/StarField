cmake_minimum_required(VERSION 3.16)
project(StarField VERSION 1.0.0 LANGUAGES C)

# Shared engine ---------------------------------------------------------------
add_library(starfield_core STATIC core/starfield.c)
target_include_directories(starfield_core PUBLIC core)
set_target_properties(starfield_core PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
if(NOT MSVC)
  target_link_libraries(starfield_core PUBLIC m)
  target_compile_options(starfield_core PRIVATE -Wall -Wextra)
endif()

# Engine tests (ported from The Bridge's starfield tests) --------------------
if(NOT CMAKE_CROSSCOMPILING)
  enable_testing()
  add_executable(starfield_tests tests/test_starfield.c)
  target_link_libraries(starfield_tests PRIVATE starfield_core)
  add_test(NAME starfield_tests COMMAND starfield_tests)
endif()

# Windows screensaver (.scr) ---------------------------------------------------
if(WIN32)
  enable_language(CXX RC)
  add_executable(StarField WIN32
    windows/starfield_scr.cpp
    windows/starfield.rc)
  set_target_properties(StarField PROPERTIES
    SUFFIX ".scr"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON)
  target_link_libraries(StarField PRIVATE
    starfield_core d2d1 dwmapi winmm comctl32 user32 gdi32 advapi32)
  if(MSVC)
    # Static CRT: no Visual C++ redistributable needed on the target machine.
    set_property(TARGET StarField starfield_core PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # The .rc embeds its own manifest (DPI awareness + common controls v6).
    target_link_options(StarField PRIVATE /MANIFEST:NO)
  else()
    target_link_options(StarField PRIVATE -static)
  endif()
endif()
