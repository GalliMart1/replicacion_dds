cmake_minimum_required(VERSION 3.10)
project(DecentralizedC2)

set(CMAKE_CXX_STANDARD 17)

# Encuentra paquetes requeridos
find_package(OpenDDS REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

# 1. PRIMERO definimos el ejecutable (target) solo con el código base
add_executable(DecentralizedC2 main.cpp)

# 2. LUEGO procesamos el IDL y adjuntamos las fuentes autogeneradas al target existente
OPENDDS_TARGET_SOURCES(DecentralizedC2 Replication.idl)

# 3. Finalmente, vinculamos todas las librerías necesarias
target_link_libraries(DecentralizedC2
    OpenDDS::Dcps
    OpenDDS::Rtps_Udp
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
    Threads::Threads
)
