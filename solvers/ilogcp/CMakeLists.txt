# CMake build script for the IBM ILOG Concert/CP driver for AMPL.

if (NOT CMAKE_BUILD_TYPE)
  message(STATUS "No build type selected, default to Release")
  set(CMAKE_BUILD_TYPE Release)
endif ()

find_package(CPLEX)
if (CPLEX_FOUND AND CPLEX_CONCERT_FOUND AND CPLEX_CP_FOUND)
  add_definitions(${CPLEX_CONCERT_DEFINITIONS} ${CPLEX_ILOCPLEX_DEFINITIONS})
  include_directories(.. ${CPLEX_ILOCPLEX_INCLUDE_DIRS} ${CPLEX_CP_INCLUDE_DIRS})

  add_library(amplilogcp
    build_expr.cpp
    ilogcp.cpp
    ilogcp.h
    ilogcp_date.h
    util.cpp
    util.h)

  add_executable(ilogcp main.cpp)

  target_link_libraries(ilogcp amplilogcp ${SOLVER_LIBS}
    ${CPLEX_ILOCPLEX_LIBRARIES} ${CPLEX_CP_LIBRARIES} ${CPLEX_LIBRARIES})
endif ()