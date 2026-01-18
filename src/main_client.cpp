#include "main_client.h"

#include <utility>

using json = nlohmann::json;

MainClient::MainClient(std::string base) : base_(std::move(base)) {}

cpr::Response MainClient::get(const std::string& path, const std::string& bearer) {
    return cpr::Get(cpr::Url{base_ + path}, cpr::Header{{"Authorization", "Bearer " + bearer}});
}

cpr::Response MainClient::del(const std::string& path, const std::string& bearer) {
    return cpr::Delete(cpr::Url{base_ + path}, cpr::Header{{"Authorization", "Bearer " + bearer}});
}

cpr::Response MainClient::post(const std::string& path, const std::string& bearer, const json* body) {
    cpr::Header h{{"Authorization", "Bearer " + bearer}};
    if (body) {
        h["Content-Type"] = "application/json";
        return cpr::Post(cpr::Url{base_ + path}, h, cpr::Body{body->dump()});
    }
    return cpr::Post(cpr::Url{base_ + path}, h);
}

cpr::Response MainClient::post_params(const std::string& path,
                                      const std::string& bearer,
                                      const cpr::Parameters& params) {
    return cpr::Post(cpr::Url{base_ + path}, cpr::Header{{"Authorization", "Bearer " + bearer}}, params);
}

cpr::Response MainClient::patch(const std::string& path, const std::string& bearer, const json& body) {
    return cpr::Patch(
        cpr::Url{base_ + path},
        cpr::Header{{"Authorization", "Bearer " + bearer}, {"Content-Type", "application/json"}},
        cpr::Body{body.dump()});
}
