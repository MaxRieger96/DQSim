cmake_minimum_required(VERSION 3.30)

project(dqsim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O3 -march=native -g" CACHE STRING "RelWithDebInfo flags" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG" CACHE STRING "Release flags" FORCE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# if(APPLE)
#     if(EXISTS "/opt/homebrew/opt/boost")
#         set(BOOST_ROOT "/opt/homebrew/opt/boost" CACHE PATH "Homebrew Boost root" FORCE)
#         set(Boost_NO_SYSTEM_PATHS ON)
#     endif()
# endif()

find_package(Boost REQUIRED COMPONENTS context thread)
message(STATUS "Boost version: ${Boost_VERSION}")
message(STATUS "Boost include dirs: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost libraries: ${Boost_LIBRARIES}")

find_library(GLPK_LIBRARY NAMES glpk HINTS /opt/homebrew/opt)
find_path(GLPK_INCLUDE_DIR
  NAMES glpk.h
  HINTS /opt/homebrew/opt
  PATH_SUFFIXES include)
if(NOT GLPK_LIBRARY OR NOT GLPK_INCLUDE_DIR)
  message(FATAL_ERROR "Could not find GLPK library or headers")
endif()
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

#---------------------------------------------------------------------------
# Debugging info
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")

# Only new compilers work
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    message(FATAL_ERROR "GCC ≥ 14 required.")
  endif()
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  # libc++ version is not directly exposed by CMake,
  # but std::ranges::to requires libc++ ≥ 16.
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
    message(WARNING "Clang detected — ensure libc++ ≥ 16 or std::ranges::to will not be available.")
  endif()
endif()

#---------------------------------------------------------------------------
# Helper to add includes consistently to all targets
function(dqsim_includes target)
  target_include_directories(${target} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${GLPK_INCLUDE_DIR}
  )
  target_include_directories(${target} SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
  )
endfunction()

#---------------------------------------------------------------------------
# distributed simulation

add_library(dqsim_core
  src/benchmark/BenchmarkDefinition.cpp
  src/benchmark/BenchmarkRunner.cpp
  src/benchmark/ClusterBuilder.cpp
  src/benchmark/DUBuilder.cpp
  src/benchmark/Handcrafted.cpp
  src/distributedplan/DataUnit.cpp
  src/distributedplan/DistributedQueryPlan.cpp
  src/distributedplan/Pipeline.cpp
  src/distributedplan/ProblemWriter.cpp
  src/network/StreamSimulator.cpp
  src/network/NetworkSimulator.cpp
  src/partitioning/Attribute.cpp
  src/partitioning/Partitioner.cpp
  src/partitioning/PartitionLayout.cpp
  src/partitioning/Partition.cpp
  src/partitioning/PartitionManager.cpp
  src/planning/AssignmentPlannerState.cpp
  src/planning/ComponentPlanner.cpp
  src/planning/EnumerationPlanner.cpp
  src/planning/GreedyPlanner.cpp
  src/planning/GreedyInfra.cpp
  src/planning/HEFTPlanner.cpp
  src/planning/HEFTScheduler.cpp
  src/planning/NetHEFTPlanner.cpp
  src/planning/NetHEFTScheduler.cpp
  src/planning/PolarisPlanner.cpp
  src/planning/TaskAssignmentPlanner.cpp
  src/tasks/Task.cpp
  src/MemorySimulator.cpp
  src/Simulator.cpp
  src/SimulatorImpl.cpp
  src/SimulatorRecording.cpp
  src/SimulatorSettings.cpp
  src/Cluster.cpp
  src/CustomTypes.cpp
  src/TempFile.cpp
)
dqsim_includes(dqsim_core)
target_link_libraries(dqsim_core PUBLIC ${GLPK_LIBRARY} Boost::boost)

#---------------------------------------------------------------------------
# create assignment problems from distributed plans and cluster definition

add_executable(ds_task_builder src/taskBuilder.cpp)
target_link_libraries(ds_task_builder PUBLIC dqsim_core ${GLPK_LIBRARY} Boost::boost)
dqsim_includes(ds_task_builder)

#---------------------------------------------------------------------------
# run task assignment on assignment problems

add_executable(ds_task_planner src/planning/TaskPlanner.cpp)
target_link_libraries(ds_task_planner PUBLIC dqsim_core ${GLPK_LIBRARY} Boost::boost)
dqsim_includes(ds_task_planner)

#---------------------------------------------------------------------------
# distributed simulation for given assignment problem and assignment

add_executable(ds_sim src/sim.cpp)
target_link_libraries(ds_sim PUBLIC dqsim_core ${GLPK_LIBRARY} Boost::boost)
dqsim_includes(ds_sim)

#---------------------------------------------------------------------------
# benchmark scheduling and simulated execution times for a list of assignment problems and a benchmark definition

add_executable(ds_bench_s src/benchmarkStandalone.cpp)
target_link_libraries(ds_bench_s PUBLIC dqsim_core ${GLPK_LIBRARY} Boost::boost)
dqsim_includes(ds_bench_s)

#---------------------------------------------------------------------------
# some main

add_executable(ds_main src/main.cpp)
target_link_libraries(ds_main PUBLIC dqsim_core ${GLPK_LIBRARY} Boost::boost)
dqsim_includes(ds_main)

#---------------------------------------------------------------------------
# tests

enable_testing()

add_executable(dqsim_tests
  src/test/main.cpp
  src/test/TestUtil.cpp
  src/test/TestHEFT.cpp
  src/test/TestJson.cpp
  src/test/TestNetworkSimulator.cpp
  src/test/TestPartitionManager.cpp
  src/test/TestProblemWriter.cpp
  src/test/TestSimulation.cpp
  src/test/TestSimulatorStreaming.cpp
  src/test/TestSimulatorTimes.cpp
  src/test/TestStreamSimulator.cpp
)
dqsim_includes(dqsim_tests)
target_link_libraries(dqsim_tests PRIVATE dqsim_core ${GLPK_LIBRARY} Boost::boost)

add_test(NAME dqsim_tests COMMAND dqsim_tests)
