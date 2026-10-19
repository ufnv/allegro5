# ICBM_STAGE: builds the Allegro libraries ICBM links and copies them, with the
# merged headers and the package's deps, into the game's own folder
# (<ICBM_DIR>/allegro5, stage_for_icbm.py). Injected by configure_release.cmd
# with -DCMAKE_PROJECT_INCLUDE=<this file>; Allegro's own CMake files stay
# untouched. Stages only from the branch ICBM_BRANCH (default icbm-hdr).

include_guard(GLOBAL)

get_filename_component(_icbm_default_dir "${CMAKE_SOURCE_DIR}/../../ICBM" ABSOLUTE)
set(ICBM_DIR "${_icbm_default_dir}" CACHE PATH "ICBM checkout the libraries are staged into")
set(ICBM_BRANCH "icbm-hdr" CACHE STRING "Allegro branch ICBM is built from (staging refuses any other)")
set(ICBM_STAGE_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/stage_for_icbm.py")

function(icbm_add_stage_target)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    set(py "${Python3_EXECUTABLE}")
  else()
    find_program(ICBM_PY_LAUNCHER py REQUIRED)
    set(py "${ICBM_PY_LAUNCHER}" -3)
  endif()

  # the libraries in ICBM\allegro5\lib
  set(libs allegro allegro_main allegro_image allegro_primitives allegro_font
      allegro_ttf allegro_audio allegro_acodec allegro_color allegro_dialog
      allegro_memfile allegro_physfs allegro_video)
  set(deps)
  foreach(t IN LISTS libs)
    if(TARGET ${t})
      list(APPEND deps ${t})
    else()
      message(WARNING "ICBM_STAGE: no target ${t} (addon not configured?)")
    endif()
  endforeach()

  add_custom_target(ICBM_STAGE
    COMMAND ${py} -I "${ICBM_STAGE_SCRIPT}" "${CMAKE_SOURCE_DIR}" "${ICBM_DIR}"
            --config $<CONFIG> --require-branch "${ICBM_BRANCH}"
    COMMENT "Staging Allegro into ${ICBM_DIR}/allegro5"
    VERBATIM)
  add_dependencies(ICBM_STAGE ${deps})
  message(STATUS "ICBM_STAGE: stages into ${ICBM_DIR}/allegro5 (branch ${ICBM_BRANCH})")
endfunction()

# after the whole tree is configured, when every library target exists
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL icbm_add_stage_target)
