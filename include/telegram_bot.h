#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <tgbot/tgbot.h>

#include "auth_client.h"
#include "main_client.h"
#include "session_store.h"

class TelegramModuleBot {
public:
    TelegramModuleBot(std::string token,
                      std::shared_ptr<SessionStore> store,
                      AuthClient auth,
                      MainClient main);

    void run();

private:
    TgBot::Bot bot_;
    std::shared_ptr<SessionStore> store_;
    AuthClient auth_;
    MainClient main_;
    std::mutex send_mtx_;

    void safe_send(std::int64_t chatId, const std::string& text, TgBot::InlineKeyboardMarkup::Ptr kb = nullptr);

    bool ensure_auth(std::int64_t chatId, Session& s);
    bool refresh_if_needed(Session& s);

    void setup_handlers();
    TgBot::InlineKeyboardMarkup::Ptr make_kb(
        const std::vector<std::pair<std::string, std::string>>& buttons);

    void show_courses(std::int64_t chatId, Session& s);
    void show_course_tests(std::int64_t chatId, Session& s);
    void start_attempt(std::int64_t chatId, Session& s);
    void show_current_question(std::int64_t chatId, Session& s);
    void handle_answer(std::int64_t chatId, Session& s, const std::string& data);
    void finish_attempt(std::int64_t chatId, Session& s);

    void start_auth_poll_thread();
    void start_notification_thread();
};
