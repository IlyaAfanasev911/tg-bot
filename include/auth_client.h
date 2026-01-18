#pragma once

#include <optional>
#include <string>

class AuthClient {
public:
    explicit AuthClient(std::string base);

    struct LoginStartResult {
        enum class Kind { URL, CODE, ERROR };
        Kind kind{Kind::ERROR};
        std::string value;
        std::string error;
    };

    LoginStartResult start_login(const std::string& type, const std::string& token_in);

    struct CheckResult {
        int http{0};
        std::string status;
        std::string access;
        std::string refresh;
    };

    CheckResult check(const std::string& token_in);
    std::optional<std::pair<std::string, std::string>> refresh(const std::string& refresh_token);
    bool logout(const std::string& refresh_token, bool all);

private:
    std::string base_;
};
