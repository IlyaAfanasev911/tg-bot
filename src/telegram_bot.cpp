#include "telegram_bot.h"

#include <chrono>
#include <iostream>
#include <set>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

#include "session.h"
#include "util.h"

using json = nlohmann::json;

namespace {

std::string command_payload(const std::string& text) {
    auto pos = text.find(' ');
    if (pos == std::string::npos) return {};
    return trim(text.substr(pos + 1));
}

bool parse_bool_flag(const std::string& s, bool* out) {
    if (s == "1" || s == "true" || s == "yes") {
        *out = true;
        return true;
    }
    if (s == "0" || s == "false" || s == "no") {
        *out = false;
        return true;
    }
    return false;
}

bool parse_int(const std::string& s, int* out) {
    try {
        std::size_t idx = 0;
        int v = std::stoi(s, &idx);
        if (idx != s.size()) return false;
        *out = v;
        return true;
    } catch (...) {
        return false;
    }
}

std::string help_text() {
    return "---- Аккаунт ----\n"
           "/login github|yandex|code - вход\n"
           "/logout - выход\n"
           "/me - мой профиль\n"
           "/set_full_name <full_name> - изменить свое ФИО\n"
           "\n"
           "---- Курсы ----\n"
           "/courses - список курсов\n"
           "/course_create <title> | <description>\n"
           "/course_delete <course_id>\n"
           "\n"
           "---- Тесты ----\n"
           "/test_create <course_id> | <title> | <is_active 0|1>\n"
           "/test_delete <course_id> <test_id>\n"
           "\n"
           "---- Вопросы ----\n"
           "/question_create <test_id|0> | <title> | <text> | <opt1;opt2;opt3> | <correct_index>\n"
           "\n"
           "---- Админ ----\n"
           "/users - список пользователей\n"
           "/ban <user_id> - заблокировать пользователя\n"
           "/unban <user_id> - разблокировать пользователя\n"
           "\n"
           "---- Другое ----\n"
           "/help - помощь\n";
}

} // namespace

TelegramModuleBot::TelegramModuleBot(std::string token,
                                     std::shared_ptr<SessionStore> store,
                                     AuthClient auth,
                                     MainClient main)
    : bot_(std::move(token)), store_(std::move(store)), auth_(std::move(auth)), main_(std::move(main)) {
    setup_handlers();
}

void TelegramModuleBot::run() {
    std::cout << "TG bot started" << std::endl;
    start_auth_poll_thread();
    start_notification_thread();
    TgBot::TgLongPoll poll(bot_);
    while (true) {
        poll.start();
    }
}

void TelegramModuleBot::safe_send(std::int64_t chatId,
                                  const std::string& text,
                                  TgBot::InlineKeyboardMarkup::Ptr kb) {
    try {
        std::lock_guard<std::mutex> lk(send_mtx_);

        bot_.getApi().sendMessage(chatId,
                                  text,
                                  nullptr,
                                  nullptr,
                                  kb,
                                  std::string{},
                                  false,
                                  std::vector<TgBot::MessageEntity::Ptr>{},
                                  0,
                                  false);
    } catch (...) {
    }
}

bool TelegramModuleBot::ensure_auth(std::int64_t chatId, Session& s) {
    if (s.status == SessionStatus::AUTH && !s.access_token.empty() && !s.refresh_token.empty()) return true;

    if (s.status == SessionStatus::ANON && !s.token_in.empty()) {
        auto cr = auth_.check(s.token_in);

        if (cr.http == 200 && cr.status == "доступ предоставлен" && !cr.access.empty() && !cr.refresh.empty()) {
            s.status = SessionStatus::AUTH;
            s.access_token = cr.access;
            s.refresh_token = cr.refresh;
            s.token_in.clear();

            store_->save(chatId, s);
            store_->mark_auth(chatId);
            safe_send(chatId, "✅ Авторизация завершена. Можно пользоваться ботом. /courses");
            return true;
        }

        if (cr.http == 401 || cr.http == 404) {
            store_->clear(chatId);
            safe_send(chatId, "⏳ Авторизация не завершена или истекла. Запусти снова: /login github|yandex|code");
            return false;
        }

        safe_send(chatId, "⏳ Авторизация ещё не завершена. Заверши вход и попробуй снова.");
        return false;
    }

    safe_send(chatId, "Ты не авторизован. Используй: /login github|yandex|code");
    return false;
}

bool TelegramModuleBot::refresh_if_needed(Session& s) {
    if (s.refresh_token.empty()) return false;
    auto t = auth_.refresh(s.refresh_token);
    if (!t) return false;
    s.access_token = t->first;
    s.refresh_token = t->second;
    return true;
}

void TelegramModuleBot::setup_handlers() {
    bot_.getEvents().onCommand("start", [this](TgBot::Message::Ptr m) {
        Session s = store_->load(m->chat->id);
        if (s.status == SessionStatus::AUTH && !s.access_token.empty() && !s.refresh_token.empty()) {
            safe_send(m->chat->id, "Привет! Ты уже авторизован. /help");
            return;
        }
        if (s.status == SessionStatus::ANON && !s.token_in.empty()) {
            safe_send(m->chat->id, "Привет! Авторизация в процессе. /help");
            return;
        }
        safe_send(m->chat->id, "Привет! Ты не авторизован. Используй: /login github|yandex|code\n\n/help");
    });

    bot_.getEvents().onCommand("help", [this](TgBot::Message::Ptr m) {
        safe_send(m->chat->id, help_text());
    });

    bot_.getEvents().onCommand("login", [this](TgBot::Message::Ptr m) {
        auto parts = split_ws(m->text);
        if (parts.size() < 2) {
            safe_send(m->chat->id, "Использование: /login github|yandex|code");
            return;
        }
        const std::string type = parts[1];
        if (type != "github" && type != "yandex" && type != "code") {
            safe_send(m->chat->id, "Неизвестный type. Используй: github | yandex | code");
            return;
        }

        Session s;
        s.status = SessionStatus::ANON;
        s.login_type = type;
        s.token_in = random_token(32);
        s.access_token.clear();
        s.refresh_token.clear();
        s.current_attempt_id = -1;
        s.current_answer_index = 0;

        store_->save(m->chat->id, s);
        store_->mark_anon(m->chat->id);

        auto res = auth_.start_login(type, s.token_in);
        if (res.kind == AuthClient::LoginStartResult::Kind::URL) {
            safe_send(m->chat->id, "Открой ссылку для входа:\n" + res.value);
            safe_send(m->chat->id,
                      "После входа бот сам подхватит сессию (или напиши /courses).\n"
                      "Если не подхватилось: повтори /courses через пару секунд.");
            return;
        }
        if (res.kind == AuthClient::LoginStartResult::Kind::CODE) {
            safe_send(m->chat->id, "Код для входа: " + res.value);
            safe_send(m->chat->id, "Дальше заверши авторизацию, бот сам подхватит сессию.");
            return;
        }

        store_->clear(m->chat->id);
        safe_send(m->chat->id, "Не удалось начать авторизацию: " + res.error);
    });

    bot_.getEvents().onCommand("logout", [this](TgBot::Message::Ptr m) {
        bool all = (m->text.find("all=true") != std::string::npos);
        Session s = store_->load(m->chat->id);
        if (!s.refresh_token.empty()) {
            auth_.logout(s.refresh_token, all);
        }
        store_->clear(m->chat->id);
        safe_send(m->chat->id, "✅ Выход выполнен");
    });

    bot_.getEvents().onCommand("courses", [this](TgBot::Message::Ptr m) {
        Session s = store_->load(m->chat->id);
        if (!ensure_auth(m->chat->id, s)) return;
        store_->save(m->chat->id, s);
        show_courses(m->chat->id, s);
    });

    bot_.getEvents().onCommand("users", [this](TgBot::Message::Ptr m) {
        Session s = store_->load(m->chat->id);
        if (!ensure_auth(m->chat->id, s)) return;
        store_->save(m->chat->id, s);

        auto r = main_.get("/api/users", s.access_token);
        if (r.status_code == 401 && refresh_if_needed(s)) {
            store_->save(m->chat->id, s);
            r = main_.get("/api/users", s.access_token);
        }
        if (r.status_code == 403) {
            safe_send(m->chat->id, "У вас нет разрешения на это действие.");
            return;
        }
        if (r.status_code == 404) {
            safe_send(m->chat->id, "Пользователи не найдены.");
            return;
        }
        if (r.status_code != 200) {
            safe_send(m->chat->id, "Не удалось получить пользователей (HTTP " + std::to_string(r.status_code) + ")");
            return;
        }
        try {
            auto j = json::parse(r.text);
            if (!j.is_array() || j.empty()) {
                safe_send(m->chat->id, "Список пользователей пуст.");
                return;
            }
            std::string msg = "Пользователи:\n";
            for (auto& u : j) {
                msg += "#" + std::to_string(u.value("id", 0)) + " ";
                msg += u.value("username", "user");
                auto fn = u.value("full_name", "");
                if (!fn.empty()) msg += " (" + fn + ")";
                msg += (u.value("is_blocked", false) ? " [blocked]" : "");
                msg += "\n";
            }
            const std::size_t kMax = 3500;
            if (msg.size() <= kMax) {
                safe_send(m->chat->id, msg);
                return;
            }
            std::string chunk;
            chunk.reserve(kMax);
            std::istringstream iss(msg);
            std::string line;
            while (std::getline(iss, line)) {
                if (chunk.size() + line.size() + 1 > kMax) {
                    safe_send(m->chat->id, chunk);
                    chunk.clear();
                }
                chunk += line + "\n";
            }
            if (!chunk.empty()) safe_send(m->chat->id, chunk);
        } catch (...) {
            safe_send(m->chat->id, "Ошибка разбора ответа /api/users");
        }
    });

    bot_.getEvents().onCommand("ban", [this](TgBot::Message::Ptr m) {
        Session s = store_->load(m->chat->id);
        if (!ensure_auth(m->chat->id, s)) return;

        auto payload = command_payload(m->text);
        auto parts = split_ws(payload);
        if (parts.size() < 1) {
            safe_send(m->chat->id, "Использование: /ban <user_id>");
            return;
        }
        int user_id = 0;
        if (!parse_int(parts[0], &user_id)) {
            safe_send(m->chat->id, "user_id должен быть числом.");
            return;
        }

        json body{{"is_blocked", true}};
        auto r = main_.post("/api/users/" + std::to_string(user_id) + "/block", s.access_token, &body);
        if (r.status_code == 401 && refresh_if_needed(s)) {
            store_->save(m->chat->id, s);
            r = main_.post("/api/users/" + std::to_string(user_id) + "/block", s.access_token, &body);
        }
        if (r.status_code == 403) {
            safe_send(m->chat->id, "У вас нет разрешения на это действие.");
            return;
        }
        if (r.status_code != 200) {
            safe_send(m->chat->id, "Не удалось заблокировать пользователя (HTTP " + std::to_string(r.status_code) + ")");
            return;
        }
        safe_send(m->chat->id, "✅ Пользователь заблокирован.");
    });

    bot_.getEvents().onCommand("unban", [this](TgBot::Message::Ptr m) {
        Session s = store_->load(m->chat->id);
        if (!ensure_auth(m->chat->id, s)) return;

        auto payload = command_payload(m->text);
        auto parts = split_ws(payload);
        if (parts.size() < 1) {
            safe_send(m->chat->id, "Использование: /unban <user_id>");
            return;
        }
        int user_id = 0;
        if (!parse_int(parts[0], &user_id)) {
            safe_send(m->chat->id, "user_id должен быть числом.");
            return;
        }

        json body{{"is_blocked", false}};
        auto r = main_.post("/api/users/" + std::to_string(user_id) + "/block", s.access_token, &body);
        if (r.status_code == 401 && refresh_if_needed(s)) {
            store_->save(m->chat->id, s);
            r = main_.post("/api/users/" + std::to_string(user_id) + "/block", s.access_token, &body);
        }
        if (r.status_code == 403) {
            safe_send(m->chat->id, "У вас нет разрешения на это действие.");
            return;
        }
        if (r.status_code != 200) {
            safe_send(m->chat->id, "Не удалось разблокировать пользователя (HTTP " + std::to_string(r.status_code) + ")");
            return;
        }
        safe_send(m->chat->id, "✅ Пользователь разблокирован.");
    });

    bot_.getEvents().onCommand("set_full_name", [this](TgBot::Message::Ptr m) {
        Session s = store_->load(m->chat->id);
        if (!ensure_auth(m->chat->id, s)) return;

        auto payload = command_payload(m->text);
        auto full_name = trim(payload);
        if (full_name.empty()) {
            safe_send(m->chat->id, "Использование: /set_full_name <full_name>");
            return;
        }

        auto me = main_.get("/api/users/me", s.access_token);
        if (me.status_code == 401 && refresh_if_needed(s)) {
            store_->save(m->chat->id, s);
            me = main_.get("/api/users/me", s.access_token);
        }
        if (me.status_code == 403) {
            safe_send(m->chat->id, "У вас нет разрешения на это действие.");
            return;
        }
        if (me.status_code != 200) {
            safe_send(m->chat->id, "Не удалось получить пользователя (HTTP " + std::to_string(me.status_code) + ")");
            return;
        }

        int user_id = -1;
        try {
            auto j = json::parse(me.text);
            user_id = j.value("id", -1);
        } catch (...) {
            user_id = -1;
        }
        if (user_id < 0) {
            safe_send(m->chat->id, "Не удалось определить user_id.");
            return;
        }

        json body{{"full_name", full_name}};
        auto r = main_.patch("/api/users/" + std::to_string(user_id) + "/full-name", s.access_token, body);
        if (r.status_code == 401 && refresh_if_needed(s)) {
            store_->save(m->chat->id, s);
            r = main_.patch("/api/users/" + std::to_string(user_id) + "/full-name", s.access_token, body);
        }
        if (r.status_code == 403) {
            safe_send(m->chat->id, "У вас нет разрешения на это действие.");
            return;
        }
        if (r.status_code != 200) {
            safe_send(m->chat->id, "Не удалось обновить ФИО (HTTP " + std::to_string(r.status_code) + ")");
            return;
        }
        safe_send(m->chat->id, "✅ ФИО обновлено.");
    });

    bot_.getEvents().onCommand("me", [this](TgBot::Message::Ptr m) {
        Session s = store_->load(m->chat->id);
        if (!ensure_auth(m->chat->id, s)) return;
        store_->save(m->chat->id, s);

        auto r = main_.get("/api/users/me", s.access_token);
        if (r.status_code == 401 && refresh_if_needed(s)) {
            store_->save(m->chat->id, s);
            r = main_.get("/api/users/me", s.access_token);
        }
        if (r.status_code == 403) {
            safe_send(m->chat->id, "У вас нет разрешения на это действие.");
            return;
        }
        if (r.status_code != 200) {
            safe_send(m->chat->id, "Не удалось получить пользователя (HTTP " + std::to_string(r.status_code) + ")");
            return;
        }

        int user_id = -1;
        try {
            auto j = json::parse(r.text);
            user_id = j.value("id", -1);
        } catch (...) {
            user_id = -1;
        }
        if (user_id < 0) {
            safe_send(m->chat->id, "Не удалось определить user_id.");
            return;
        }

        auto d = main_.get("/api/users/" + std::to_string(user_id) + "/data", s.access_token);
        if (d.status_code == 401 && refresh_if_needed(s)) {
            store_->save(m->chat->id, s);
            d = main_.get("/api/users/" + std::to_string(user_id) + "/data", s.access_token);
        }
        if (d.status_code == 403) {
            safe_send(m->chat->id, "У вас нет разрешения на это действие.");
            return;
        }
        if (d.status_code != 200) {
            safe_send(m->chat->id, "Не удалось получить данные пользователя (HTTP " + std::to_string(d.status_code) + ")");
            return;
        }
        try {
            auto j = json::parse(d.text);
            std::ostringstream msg;
            msg << "Пользователь #" << j.value("id", 0) << "\n";
            msg << "Username: " << j.value("username", "") << "\n";
            msg << "Full name: " << j.value("full_name", "") << "\n";
            msg << "Email: " << j.value("email", "") << "\n";
            msg << "Blocked: " << (j.value("is_blocked", false) ? "yes" : "no") << "\n";
            msg << "Courses: " << j.value("courses_count", 0) << "\n";
            msg << "Attempts: " << j.value("attempts_count", 0);
            safe_send(m->chat->id, msg.str());
        } catch (...) {
            safe_send(m->chat->id, "Ошибка разбора ответа /api/users/{id}/data");
        }
    });

    bot_.getEvents().onCommand("course_create", [this](TgBot::Message::Ptr m) {
        Session s = store_->load(m->chat->id);
        if (!ensure_auth(m->chat->id, s)) return;

        auto payload = command_payload(m->text);
        auto parts = split_by(payload, '|');
        if (parts.empty() || trim(parts[0]).empty()) {
            safe_send(m->chat->id, "Использование: /course_create <title> | <description>");
            return;
        }
        std::string title = trim(parts[0]);
        std::string desc = parts.size() > 1 ? trim(parts[1]) : "";

        auto r = main_.post_params("/api/courses",
                                   s.access_token,
                                   cpr::Parameters{{"title", title}, {"description", desc}});
        if (r.status_code == 401 && refresh_if_needed(s)) {
            store_->save(m->chat->id, s);
            r = main_.post_params("/api/courses",
                                  s.access_token,
                                  cpr::Parameters{{"title", title}, {"description", desc}});
        }
        if (r.status_code == 403) {
            safe_send(m->chat->id, "У вас нет разрешения на это действие.");
            return;
        }
        if (r.status_code != 201 && r.status_code != 200) {
            safe_send(m->chat->id, "Не удалось создать курс (HTTP " + std::to_string(r.status_code) + ")");
            return;
        }
        try {
            auto j = json::parse(r.text);
            safe_send(m->chat->id,
                      "✅ Курс создан: #" + std::to_string(j.value("id", 0)) + " " + j.value("title", title));
        } catch (...) {
            safe_send(m->chat->id, "Курс создан.");
        }
    });

    bot_.getEvents().onCommand("course_delete", [this](TgBot::Message::Ptr m) {
        Session s = store_->load(m->chat->id);
        if (!ensure_auth(m->chat->id, s)) return;

        auto payload = command_payload(m->text);
        auto parts = split_ws(payload);
        if (parts.size() < 1) {
            safe_send(m->chat->id, "Использование: /course_delete <course_id>");
            return;
        }
        int course_id = 0;
        if (!parse_int(parts[0], &course_id)) {
            safe_send(m->chat->id, "course_id должен быть числом.");
            return;
        }
        auto r = main_.del("/api/courses/" + std::to_string(course_id), s.access_token);
        if (r.status_code == 401 && refresh_if_needed(s)) {
            store_->save(m->chat->id, s);
            r = main_.del("/api/courses/" + std::to_string(course_id), s.access_token);
        }
        if (r.status_code == 403) {
            safe_send(m->chat->id, "У вас нет разрешения на это действие.");
            return;
        }
        if (r.status_code == 404) {
            safe_send(m->chat->id, "Курс не найден.");
            return;
        }
        if (r.status_code != 200) {
            safe_send(m->chat->id, "Не удалось удалить курс (HTTP " + std::to_string(r.status_code) + ")");
            return;
        }
        safe_send(m->chat->id, "✅ Курс удален (логически).");
    });

    bot_.getEvents().onCommand("test_create", [this](TgBot::Message::Ptr m) {
        Session s = store_->load(m->chat->id);
        if (!ensure_auth(m->chat->id, s)) return;

        auto payload = command_payload(m->text);
        auto parts = split_by(payload, '|');
        if (parts.size() < 3) {
            safe_send(m->chat->id, "Использование: /test_create <course_id> | <title> | <is_active 0|1>");
            return;
        }
        int course_id = 0;
        if (!parse_int(trim(parts[0]), &course_id)) {
            safe_send(m->chat->id, "course_id должен быть числом.");
            return;
        }
        std::string title = trim(parts[1]);
        bool is_active = false;
        if (!parse_bool_flag(trim(parts[2]), &is_active)) {
            safe_send(m->chat->id, "is_active должен быть 0/1 или true/false.");
            return;
        }

        json body{{"title", title}, {"is_active", is_active}};
        auto r = main_.post("/api/courses/" + std::to_string(course_id) + "/tests", s.access_token, &body);
        if (r.status_code == 401 && refresh_if_needed(s)) {
            store_->save(m->chat->id, s);
            r = main_.post("/api/courses/" + std::to_string(course_id) + "/tests", s.access_token, &body);
        }
        if (r.status_code == 403) {
            safe_send(m->chat->id, "У вас нет разрешения на это действие.");
            return;
        }
        if (r.status_code != 201 && r.status_code != 200) {
            safe_send(m->chat->id, "Не удалось создать тест (HTTP " + std::to_string(r.status_code) + ")");
            return;
        }
        try {
            auto j = json::parse(r.text);
            safe_send(m->chat->id,
                      "✅ Тест создан: #" + std::to_string(j.value("id", 0)) + " " + j.value("title", title));
        } catch (...) {
            safe_send(m->chat->id, "Тест создан.");
        }
    });

    bot_.getEvents().onCommand("test_delete", [this](TgBot::Message::Ptr m) {
        Session s = store_->load(m->chat->id);
        if (!ensure_auth(m->chat->id, s)) return;

        auto payload = command_payload(m->text);
        auto parts = split_ws(payload);
        if (parts.size() < 2) {
            safe_send(m->chat->id, "Использование: /test_delete <course_id> <test_id>");
            return;
        }
        int course_id = 0;
        int test_id = 0;
        if (!parse_int(parts[0], &course_id) || !parse_int(parts[1], &test_id)) {
            safe_send(m->chat->id, "course_id и test_id должны быть числами.");
            return;
        }

        auto r =
            main_.del("/api/courses/" + std::to_string(course_id) + "/tests/" + std::to_string(test_id),
                      s.access_token);
        if (r.status_code == 401 && refresh_if_needed(s)) {
            store_->save(m->chat->id, s);
            r = main_.del("/api/courses/" + std::to_string(course_id) + "/tests/" + std::to_string(test_id),
                          s.access_token);
        }
        if (r.status_code == 403) {
            safe_send(m->chat->id, "У вас нет разрешения на это действие.");
            return;
        }
        if (r.status_code == 404) {
            safe_send(m->chat->id, "Тест не найден.");
            return;
        }
        if (r.status_code != 200) {
            safe_send(m->chat->id, "Не удалось удалить тест (HTTP " + std::to_string(r.status_code) + ")");
            return;
        }
        safe_send(m->chat->id, "✅ Тест удален (логически).");
    });

    bot_.getEvents().onCommand("question_create", [this](TgBot::Message::Ptr m) {
        Session s = store_->load(m->chat->id);
        if (!ensure_auth(m->chat->id, s)) return;

        auto payload = command_payload(m->text);
        auto parts = split_by(payload, '|');
        if (parts.size() < 5) {
            safe_send(m->chat->id,
                      "Использование: /question_create <test_id|0> | <title> | <text> | <opt1;opt2> | <correct_index>");
            return;
        }

        int test_id = 0;
        if (!parse_int(trim(parts[0]), &test_id)) {
            safe_send(m->chat->id, "test_id должен быть числом (0 если без привязки).");
            return;
        }
        std::string title = trim(parts[1]);
        std::string text = trim(parts[2]);
        auto opt_parts = split_by(trim(parts[3]), ';');
        int correct_index = 0;
        if (!parse_int(trim(parts[4]), &correct_index)) {
            safe_send(m->chat->id, "correct_index должен быть числом.");
            return;
        }

        std::vector<std::string> options;
        for (auto& o : opt_parts) {
            auto t = trim(o);
            if (!t.empty()) options.push_back(t);
        }
        if (options.empty()) {
            safe_send(m->chat->id, "Нужно указать хотя бы один вариант ответа.");
            return;
        }
        if (correct_index < 0 || correct_index >= static_cast<int>(options.size())) {
            safe_send(m->chat->id, "correct_index вне диапазона вариантов.");
            return;
        }

        json body{{"title", title}, {"text", text}, {"options", options}, {"correct_index", correct_index}};
        if (test_id > 0) {
            body["test_id"] = test_id;
        } else {
            body["test_id"] = nullptr;
        }

        auto r = main_.post("/api/questions", s.access_token, &body);
        if (r.status_code == 401 && refresh_if_needed(s)) {
            store_->save(m->chat->id, s);
            r = main_.post("/api/questions", s.access_token, &body);
        }
        if (r.status_code == 403) {
            safe_send(m->chat->id, "У вас нет разрешения на это действие.");
            return;
        }
        if (r.status_code == 404 && test_id > 0) {
            safe_send(m->chat->id, "Тест не найден.");
            return;
        }
        if (r.status_code != 201 && r.status_code != 200) {
            safe_send(m->chat->id, "Не удалось создать вопрос (HTTP " + std::to_string(r.status_code) + ")");
            return;
        }
        safe_send(m->chat->id, "✅ Вопрос создан.");
    });

    bot_.getEvents().onCallbackQuery([this](TgBot::CallbackQuery::Ptr q) {
        const auto chatId = q->message->chat->id;
        Session s = store_->load(chatId);
        if (!ensure_auth(chatId, s)) {
            bot_.getApi().answerCallbackQuery(q->id);
            return;
        }

        const std::string data = q->data;
        if (starts_with(data, "course:")) {
            s.current_course_id = std::stoi(data.substr(std::string("course:").size()));
            store_->save(chatId, s);
            show_course_tests(chatId, s);
        } else if (starts_with(data, "test:")) {
            s.current_test_id = std::stoi(data.substr(std::string("test:").size()));
            store_->save(chatId, s);
            start_attempt(chatId, s);
        } else if (starts_with(data, "ans:")) {
            handle_answer(chatId, s, data);
        } else if (starts_with(data, "finish:")) {
            finish_attempt(chatId, s);
        } else if (data == "back:courses") {
            show_courses(chatId, s);
        }

        bot_.getApi().answerCallbackQuery(q->id);
    });

    bot_.getEvents().onAnyMessage([this](TgBot::Message::Ptr m) {
        if (!m || m->text.empty()) return;
        if (!m->text.empty() && m->text[0] == '/') {
            static const std::set<std::string> known = {"/start",
                                                        "/help",
                                                        "/login",
                                                        "/logout",
                                                        "/courses",
                                                        "/users",
                                                        "/ban",
                                                        "/unban",
                                                        "/set_full_name",
                                                        "/course_create",
                                                        "/course_delete",
                                                        "/test_create",
                                                        "/test_delete",
                                                        "/question_create",
                                                        "/me"};
            auto cmd = m->text;
            auto pos = cmd.find(' ');
            if (pos != std::string::npos) cmd = cmd.substr(0, pos);
            if (known.count(cmd) == 0) {
                safe_send(m->chat->id, "Нет такой команды. /start");
            }
        }
    });
}

TgBot::InlineKeyboardMarkup::Ptr TelegramModuleBot::make_kb(
    const std::vector<std::pair<std::string, std::string>>& buttons) {
    auto kb = TgBot::InlineKeyboardMarkup::Ptr(new TgBot::InlineKeyboardMarkup);
    for (const auto& [text, data] : buttons) {
        std::vector<TgBot::InlineKeyboardButton::Ptr> row;
        auto b = TgBot::InlineKeyboardButton::Ptr(new TgBot::InlineKeyboardButton);
        b->text = text;
        b->callbackData = data;
        row.push_back(b);
        kb->inlineKeyboard.push_back(row);
    }
    return kb;
}

void TelegramModuleBot::show_courses(std::int64_t chatId, Session& s) {
    auto r = main_.get("/api/courses", s.access_token);
    if (r.status_code == 401 && refresh_if_needed(s)) {
        store_->save(chatId, s);
        r = main_.get("/api/courses", s.access_token);
    }
    if (r.status_code != 200) {
        safe_send(chatId, "Не удалось получить курсы (HTTP " + std::to_string(r.status_code) + ")");
        return;
    }
    try {
        auto j = json::parse(r.text);
        std::vector<std::pair<std::string, std::string>> btns;
        for (auto& c : j) {
            btns.push_back({c.value("title", "курс") + " (#" + std::to_string(c.value("id", 0)) + ")",
                            "course:" + std::to_string(c.value("id", 0))});
        }
        if (btns.empty()) {
            safe_send(chatId, "Курсов пока нет.");
            return;
        }
        safe_send(chatId, "Выбери курс:", make_kb(btns));
    } catch (...) {
        safe_send(chatId, "Ошибка разбора ответа /api/courses");
    }
}

void TelegramModuleBot::show_course_tests(std::int64_t chatId, Session& s) {
    if (s.current_course_id < 0) {
        safe_send(chatId, "Сначала выбери курс: /courses");
        return;
    }
    auto r = main_.get("/api/courses/" + std::to_string(s.current_course_id) + "/tests", s.access_token);
    if (r.status_code == 401 && refresh_if_needed(s)) {
        store_->save(chatId, s);
        r = main_.get("/api/courses/" + std::to_string(s.current_course_id) + "/tests", s.access_token);
    }
    if (r.status_code != 200) {
        safe_send(chatId, "Не удалось получить тесты (HTTP " + std::to_string(r.status_code) + ")");
        return;
    }
    try {
        auto j = json::parse(r.text);
        std::vector<std::pair<std::string, std::string>> btns;
        for (auto& t : j) {
            const bool active = t.value("is_active", false);
            std::string title = t.value("title", "test") + (active ? " ✅" : " ⛔");
            if (active) {
                btns.push_back({title, "test:" + std::to_string(t.value("id", 0))});
            }
        }
        btns.push_back({"⬅️ Назад", "back:courses"});
        safe_send(chatId, "Тесты курса (только активные):", make_kb(btns));
    } catch (...) {
        safe_send(chatId, "Ошибка разбора ответа tests");
    }
}

void TelegramModuleBot::start_attempt(std::int64_t chatId, Session& s) {
    if (s.current_test_id < 0) return;

    auto r = main_.post("/api/attempts/tests/" + std::to_string(s.current_test_id), s.access_token);
    if (r.status_code == 401 && refresh_if_needed(s)) {
        store_->save(chatId, s);
        r = main_.post("/api/attempts/tests/" + std::to_string(s.current_test_id), s.access_token);
    }
    if (r.status_code != 201 && r.status_code != 200) {
        safe_send(chatId, "Не удалось начать попытку (HTTP " + std::to_string(r.status_code) + ")");
        return;
    }
    try {
        auto j = json::parse(r.text);
        s.current_attempt_id = j.value("id", -1);
        s.current_answer_index = 0;
        store_->save(chatId, s);
        safe_send(chatId, "📝 Попытка начата. Загружаю вопрос...");
        show_current_question(chatId, s);
    } catch (...) {
        safe_send(chatId, "Ошибка разбора ответа attempts");
    }
}

void TelegramModuleBot::show_current_question(std::int64_t chatId, Session& s) {
    if (s.current_attempt_id < 0) return;

    auto rAns = main_.get("/api/answers/attempts/" + std::to_string(s.current_attempt_id), s.access_token);
    if (rAns.status_code == 401 && refresh_if_needed(s)) {
        store_->save(chatId, s);
        rAns = main_.get("/api/answers/attempts/" + std::to_string(s.current_attempt_id), s.access_token);
    }
    if (rAns.status_code != 200) {
        safe_send(chatId, "Не удалось получить ответы попытки (HTTP " + std::to_string(rAns.status_code) + ")");
        return;
    }

    try {
        auto answers = json::parse(rAns.text);
        if (!answers.is_array() || answers.empty()) {
            safe_send(chatId, "В этой попытке нет вопросов.");
            return;
        }

        if (s.current_answer_index >= static_cast<int>(answers.size())) {
            auto kb = make_kb({{"🏁 Завершить попытку", "finish:" + std::to_string(s.current_attempt_id)}});
            safe_send(chatId, "Вопросы закончились.", kb);
            return;
        }

        auto a = answers.at(s.current_answer_index);
        int answer_id = a.value("id", -1);
        int question_id = a.value("question_id", -1);
        if (answer_id < 0 || question_id < 0) {
            safe_send(chatId, "Некорректные данные вопроса.");
            return;
        }

        auto rQ = main_.get("/api/questions/" + std::to_string(question_id), s.access_token);
        if (rQ.status_code == 401 && refresh_if_needed(s)) {
            store_->save(chatId, s);
            rQ = main_.get("/api/questions/" + std::to_string(question_id), s.access_token);
        }
        if (rQ.status_code != 200) {
            safe_send(chatId, "Не удалось получить вопрос (HTTP " + std::to_string(rQ.status_code) + ")");
            return;
        }

        auto q = json::parse(rQ.text);
        std::string title = q.value("title", "Вопрос");
        std::string text = q.value("text", "");
        auto opts = q.value("options", json::array());

        std::vector<std::pair<std::string, std::string>> btns;
        int idx = 0;
        for (auto& opt : opts) {
            btns.push_back({opt.get<std::string>(),
                            "ans:" + std::to_string(answer_id) + ":" + std::to_string(idx)});
            idx++;
        }
        if (btns.empty()) {
            safe_send(chatId, "У вопроса нет вариантов.");
            return;
        }

        std::ostringstream msg;
        msg << "(" << (s.current_answer_index + 1) << "/" << answers.size() << ") " << title << "\n\n"
            << text;
        safe_send(chatId, msg.str(), make_kb(btns));
    } catch (...) {
        safe_send(chatId, "Ошибка разбора данных вопроса");
    }
}

void TelegramModuleBot::handle_answer(std::int64_t chatId, Session& s, const std::string& data) {
    auto parts = split_by(data, ':');
    if (parts.size() != 3) return;

    int answer_id = std::stoi(parts[1]);
    int value = std::stoi(parts[2]);

    auto r = main_.patch("/api/answers/" + std::to_string(answer_id), s.access_token, json{{"value", value}});
    if (r.status_code == 401 && refresh_if_needed(s)) {
        store_->save(chatId, s);
        r = main_.patch("/api/answers/" + std::to_string(answer_id), s.access_token, json{{"value", value}});
    }
    if (r.status_code != 200) {
        safe_send(chatId, "Не удалось сохранить ответ (HTTP " + std::to_string(r.status_code) + ")");
        return;
    }

    s.current_answer_index += 1;
    store_->save(chatId, s);
    show_current_question(chatId, s);
}

void TelegramModuleBot::finish_attempt(std::int64_t chatId, Session& s) {
    if (s.current_attempt_id < 0) return;

    auto r = main_.post("/api/attempts/" + std::to_string(s.current_attempt_id) + "/finish", s.access_token);
    if (r.status_code == 401 && refresh_if_needed(s)) {
        store_->save(chatId, s);
        r = main_.post("/api/attempts/" + std::to_string(s.current_attempt_id) + "/finish", s.access_token);
    }
    if (r.status_code != 200) {
        safe_send(chatId, "Не удалось завершить попытку (HTTP " + std::to_string(r.status_code) + ")");
        return;
    }

    try {
        auto j = json::parse(r.text);
        auto score = j.value("score", 0.0);
        safe_send(chatId, "🏁 Попытка завершена. Score: " + std::to_string(score));
    } catch (...) {
        safe_send(chatId, "Попытка завершена.");
    }

    s.current_attempt_id = -1;
    s.current_answer_index = 0;
    store_->save(chatId, s);
}

void TelegramModuleBot::start_auth_poll_thread() {
    std::thread([this]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            for (auto chatId : store_->anon_chats()) {
                Session s = store_->load(chatId);
                if (s.status != SessionStatus::ANON || s.token_in.empty()) {
                    store_->mark_anon(chatId);
                    continue;
                }
                auto cr = auth_.check(s.token_in);
                if (cr.http == 200 && cr.status == "доступ предоставлен" && !cr.access.empty() && !cr.refresh.empty()) {
                    s.status = SessionStatus::AUTH;
                    s.access_token = cr.access;
                    s.refresh_token = cr.refresh;
                    s.token_in.clear();
                    store_->save(chatId, s);
                    store_->mark_auth(chatId);
                    safe_send(chatId, "✅ Авторизация завершена. /courses");
                } else if (cr.http == 401 || cr.http == 404) {
                    store_->clear(chatId);
                    safe_send(chatId, "⏳ Авторизация истекла. Запусти снова: /login github|yandex|code");
                }
            }
        }
    }).detach();
}

void TelegramModuleBot::start_notification_thread() {
    int interval = std::stoi(getenv_or("TG_NOTIFICATION_INTERVAL_SEC", "30"));
    if (interval < 5) interval = 5;

    std::thread([this, interval]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(interval));
            for (auto chatId : store_->auth_chats()) {
                Session s = store_->load(chatId);
                if (s.status != SessionStatus::AUTH || s.access_token.empty()) continue;

                auto r = main_.get("/notification", s.access_token);
                if (r.status_code == 401 && refresh_if_needed(s)) {
                    store_->save(chatId, s);
                    r = main_.get("/notification", s.access_token);
                }
                if (r.status_code != 200) continue;

                try {
                    auto notes = json::parse(r.text);
                    if (!notes.is_array() || notes.empty()) continue;

                    int sent = 0;
                    for (auto& n : notes) {
                        std::string msg = n.value("message", "");
                        if (msg.empty()) continue;
                        safe_send(chatId, "🔔 " + msg);
                        sent++;
                    }

                    if (sent > 0) {
                        auto d = main_.del("/notification", s.access_token);
                        if (d.status_code == 401 && refresh_if_needed(s)) {
                            store_->save(chatId, s);
                            (void)main_.del("/notification", s.access_token);
                        }
                    }
                } catch (...) {
                }
            }
        }
    }).detach();
}
