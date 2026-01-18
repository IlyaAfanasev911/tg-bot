cmake_minimum_required(VERSION 3.20)
project(tg_module LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- Dependencies ---
# TgBot (Telegram Bot API C++)
FetchContent_Declare(
  tgbot
  GIT_REPOSITORY https://github.com/reo7sp/tgbot-cpp.git
  # commit "support boost-1.87.0"
  GIT_TAG 0a89870157a1f8ad546a5336bb4717f8700911d5
)


# CPR (HTTP)
FetchContent_Declare(
  cpr
  GIT_REPOSITORY https://github.com/libcpr/cpr.git
  GIT_TAG 1.10.5
)

# nlohmann/json
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(tgbot cpr nlohmann_json)

# --- Fix TgBot target naming differences ---
# Some builds export target "TgBot", some "TgBot::TgBot".
# Create an alias if only "TgBot" exists.
if(TARGET TgBot AND NOT TARGET TgBot::TgBot)
  add_library(TgBot::TgBot ALIAS TgBot)
endif()

add_executable(
  tg_module
  src/main.cpp
  src/auth_client.cpp
  src/main_client.cpp
  src/redis_client.cpp
  src/session.cpp
  src/session_store.cpp
  src/telegram_bot.cpp
  src/util.cpp
)

target_link_libraries(tg_module PRIVATE TgBot::TgBot cpr::cpr nlohmann_json::nlohmann_json)

target_include_directories(tg_module PRIVATE include)

if(UNIX AND NOT APPLE)
  target_link_libraries(tg_module PRIVATE pthread)
endif()

if(APPLE)
  target_link_libraries(tg_module PRIVATE "-framework CoreFoundation")
endif()
