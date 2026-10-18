cmake_minimum_required(VERSION 3.15)
project(TrabalhoAPS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Configura o vinculo dinâmico necessário para carregar a janela gráfica no Windows
set(SFML_STATIC_LIBRARIES FALSE)

set(SFML_STATIC_LIBRARIES FALSE)

find_package(imgui CONFIG REQUIRED)
find_package(SFML 3 COMPONENTS Graphics Window System CONFIG REQUIRED)
find_package(ImGui-SFML CONFIG REQUIRED)

set(SOURCES 
    "src/main.cpp"
    "src/Ordenador.cpp"
)

add_executable(TrabalhoAPS ${SOURCES})

target_link_libraries(TrabalhoAPS PRIVATE 
    imgui::imgui
    SFML::Graphics
    SFML::Window
    SFML::System
    ImGui-SFML::ImGui-SFML
    winhttp
)
