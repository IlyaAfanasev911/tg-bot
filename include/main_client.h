#pragma once

#include <string>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>

class MainClient {
public:
    explicit MainClient(std::string base);

    cpr::Response get(const std::string& path, const std::string& bearer);
    cpr::Response del(const std::string& path, const std::string& bearer);
    cpr::Response post(const std::string& path, const std::string& bearer, const nlohmann::json* body = nullptr);
    cpr::Response post_params(const std::string& path,
                              const std::string& bearer,
                              const cpr::Parameters& params);
    cpr::Response patch(const std::string& path, const std::string& bearer, const nlohmann::json& body);

private:
    std::string base_;
};
