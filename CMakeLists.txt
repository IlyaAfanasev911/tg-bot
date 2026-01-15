cmake_minimum_required(VERSION 3.15)
project(TelegramBot)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


include_directories(
    /opt/homebrew/include
    /usr/local/include
    /opt/homebrew/opt/openssl@3/include
    /opt/homebrew/opt/curl/include
)

add_executable(my_bot main.cpp)


target_link_libraries(my_bot
    /usr/local/lib/libTgBot.a
    /opt/homebrew/lib/libcpr.dylib
    /opt/homebrew/opt/curl/lib/libcurl.dylib
    /opt/homebrew/opt/openssl@3/lib/libssl.dylib
    /opt/homebrew/opt/openssl@3/lib/libcrypto.dylib
    pthread
)