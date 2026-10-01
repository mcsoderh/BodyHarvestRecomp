# Applies patches/deps/<submodule path>/*.patch to the matching submodule.
# Idempotent: a patch that already reverse-applies is skipped. Any patch that
# neither applies nor is already applied stops configuration.
#
# Runs automatically at configure time, and can be run standalone before
# building the recompiler tools: cmake -P cmake/apply_dep_patches.cmake

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
find_package(Git REQUIRED)

file(GLOB_RECURSE DEP_PATCHES RELATIVE "${REPO_ROOT}/patches/deps" "${REPO_ROOT}/patches/deps/*.patch")
list(SORT DEP_PATCHES)

foreach(rel IN LISTS DEP_PATCHES)
    get_filename_component(submodule "${rel}" DIRECTORY)
    set(patch "${REPO_ROOT}/patches/deps/${rel}")
    set(target "${REPO_ROOT}/${submodule}")

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${patch}"
        WORKING_DIRECTORY "${target}"
        RESULT_VARIABLE already_applied
        OUTPUT_QUIET ERROR_QUIET)
    if(already_applied EQUAL 0)
        continue()
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply "${patch}"
        WORKING_DIRECTORY "${target}"
        RESULT_VARIABLE apply_result
        ERROR_VARIABLE apply_error)
    if(NOT apply_result EQUAL 0)
        message(FATAL_ERROR "Failed to apply ${rel} to ${submodule}:\n${apply_error}")
    endif()
    message(STATUS "Applied dependency patch ${rel}")
endforeach()
