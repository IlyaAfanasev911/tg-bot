#include "auth_client.h"

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

#include <utility>

using json = nlohmann::json;

AuthClient::AuthClient(std::string base) : base_(std::move(base)) {}

AuthClient::LoginStartResult AuthClient::start_login(const std::string& type, const std::string& token_in) {
    auto r = cpr::Get(cpr::Url{base_ + "/auth/login"}, cpr::Parameters{{"type", type}, {"token_in", token_in}});
    if (r.status_code != 200) {
        return {.kind = LoginStartResult::Kind::ERROR,
                .error = "auth/login failed: HTTP " + std::to_string(r.status_code)};
    }
    try {
        auto j = json::parse(r.text);
        if (j.contains("url")) return {.kind = LoginStartResult::Kind::URL, .value = j["url"].get<std::string>()};
        if (j.contains("code")) return {.kind = LoginStartResult::Kind::CODE, .value = j["code"].get<std::string>()};
        return {.kind = LoginStartResult::Kind::ERROR, .error = "unexpected auth/login response"};
    } catch (...) {
        return {.kind = LoginStartResult::Kind::ERROR, .error = "bad json from auth/login"};
    }
}

AuthClient::CheckResult AuthClient::check(const std::string& token_in) {
    auto r = cpr::Get(cpr::Url{base_ + "/auth/check"}, cpr::Parameters{{"token_in", token_in}});
    CheckResult out;
    out.http = r.status_code;
    try {
        auto j = json::parse(r.text);
        out.status = j.value("status", "");
        out.access = j.value("access_token", "");
        out.refresh = j.value("refresh_token", "");
    } catch (...) {
        out.status = "";
    }
    return out;
}

std::optional<std::pair<std::string, std::string>> AuthClient::refresh(const std::string& refresh_token) {
    auto r = cpr::Post(
        cpr::Url{base_ + "/auth/refresh"},
        cpr::Header{{"Content-Type", "application/json"}},
        cpr::Body{json{{"refresh_token", refresh_token}}.dump()});

    if (r.status_code != 200) return std::nullopt;

    try {
        auto j = json::parse(r.text);
        return std::make_pair(j.at("access_token").get<std::string>(), j.at("refresh_token").get<std::string>());
    } catch (...) {
        return std::nullopt;
    }
}

bool AuthClient::logout(const std::string& refresh_token, bool all) {
    auto r = cpr::Post(
        cpr::Url{base_ + "/auth/logout"},
        cpr::Parameters{{"refresh_token", refresh_token}, {"all", all ? "true" : "false"}});
    return r.status_code == 200;
}
