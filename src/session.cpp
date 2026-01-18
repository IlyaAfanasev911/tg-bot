#include "session.h"

using json = nlohmann::json;

std::string status_to_string(SessionStatus s) {
    switch (s) {
        case SessionStatus::UNKNOWN: return "UNKNOWN";
        case SessionStatus::ANON: return "ANON";
        case SessionStatus::AUTH: return "AUTH";
    }
    return "UNKNOWN";
}

SessionStatus status_from_string(const std::string& s) {
    if (s == "ANON") return SessionStatus::ANON;
    if (s == "AUTH") return SessionStatus::AUTH;
    return SessionStatus::UNKNOWN;
}

json session_to_json(const Session& s) {
    return json{{"status", status_to_string(s.status)},
                {"token_in", s.token_in},
                {"login_type", s.login_type},
                {"access_token", s.access_token},
                {"refresh_token", s.refresh_token},
                {"current_course_id", s.current_course_id},
                {"current_test_id", s.current_test_id},
                {"current_attempt_id", s.current_attempt_id},
                {"current_answer_index", s.current_answer_index}};
}

Session session_from_json(const json& j) {
    Session s;
    s.status = status_from_string(j.value("status", "UNKNOWN"));
    s.token_in = j.value("token_in", "");
    s.login_type = j.value("login_type", "");
    s.access_token = j.value("access_token", "");
    s.refresh_token = j.value("refresh_token", "");
    s.current_course_id = j.value("current_course_id", -1);
    s.current_test_id = j.value("current_test_id", -1);
    s.current_attempt_id = j.value("current_attempt_id", -1);
    s.current_answer_index = j.value("current_answer_index", 0);
    return s;
}
