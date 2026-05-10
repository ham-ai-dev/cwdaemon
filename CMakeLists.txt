cmake_minimum_required(VERSION 3.25)
project(cwdaemon VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon CONFIG REQUIRED)
find_package(ftxui CONFIG REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
find_package(tomlplusplus CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(cwdaemon
    src/main.cpp
    src/goertzel.cpp
    src/timing.cpp
    src/audio.cpp
    src/wavfile.cpp
    src/config.cpp
    src/rigctl.cpp
    src/api.cpp
    src/tui.cpp
)

target_include_directories(cwdaemon PRIVATE src/include ${PORTAUDIO_INCLUDE_DIRS})

target_link_libraries(cwdaemon PRIVATE
    Drogon::Drogon
    ftxui::screen
    ftxui::dom
    ftxui::component
    ${PORTAUDIO_LIBRARIES}
    tomlplusplus::tomlplusplus
    nlohmann_json::nlohmann_json
    pthread
)

add_executable(gentone tools/gentone.cpp)
target_include_directories(gentone PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
target_link_libraries(gentone PRIVATE
    ${PORTAUDIO_LIBRARIES}
)

add_executable(cwdiag
    tools/cwdiag.cpp
    src/goertzel.cpp
    src/timing.cpp
)
target_include_directories(cwdiag PRIVATE src/include)
