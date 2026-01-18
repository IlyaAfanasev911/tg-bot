#pragma once

#include <string>

#include <nlohmann/json.hpp>

enum class SessionStatus { UNKNOWN, ANON, AUTH };

struct Session {
    SessionStatus status{SessionStatus::UNKNOWN};
    std::string token_in;
    std::string login_type;
    std::string access_token;
    std::string refresh_token;

    int current_course_id{-1};
    int current_test_id{-1};
    int current_attempt_id{-1};
    int current_answer_index{0};
};

std::string status_to_string(SessionStatus s);
SessionStatus status_from_string(const std::string& s);

nlohmann::json session_to_json(const Session& s);
Session session_from_json(const nlohmann::json& j);
