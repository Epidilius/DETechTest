cmake_minimum_required(VERSION 3.20)
project(PartA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# The SDK and the ingestion service, as one library. It contains no stand-ins.
add_library(tracking STATIC
    sdk/id_parts.cpp
    sdk/ids.cpp
    sdk/dispatch.cpp
    service/validator.cpp
    service/ingestion_service.cpp
)
target_include_directories(tracking PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tracking PUBLIC Threads::Threads)

# A switch over an enum with a missing case is a build error, so adding an enumerator without its name cannot ship.
if(MSVC)
    target_compile_options(tracking PUBLIC /W4 /WX /we4062 /permissive-)
else()
    target_compile_options(tracking PUBLIC -Wall -Wextra -Werror -Werror=switch)
endif()

# Fakes of the systems the brief does not supply: the identity platform, the network, the token check, and MongoDB. Only the demo and the tests link them.
add_library(standins STATIC
    standins/console_store.cpp
)
target_link_libraries(standins PUBLIC tracking)

add_executable(demo main.cpp)
target_link_libraries(demo PRIVATE standins)

add_executable(tracking_tests tests/tests.cpp tests/pipeline_tests.cpp)
target_link_libraries(tracking_tests PRIVATE standins)

enable_testing()
add_test(NAME tracking_tests COMMAND tracking_tests)
