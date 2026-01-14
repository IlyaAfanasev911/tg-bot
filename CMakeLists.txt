cmake_minimum_required(VERSION 3.10)
project(QuizBot)

set(CMAKE_CXX_STANDARD 17)


include_directories(/opt/homebrew/include /usr/local/include)
link_directories(/opt/homebrew/lib /usr/local/lib)


find_package(nlohmann_json REQUIRED)
find_package(cpr REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(my_bot main.cpp)


target_link_libraries(my_bot
    TgBot
    cpr::cpr
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread
    z
    curl
)