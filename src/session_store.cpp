#include "session_store.h"

#include <utility>

#include <nlohmann/json.hpp>

#include "util.h"

SessionStore::SessionStore(std::shared_ptr<RedisClient> redis)
    : redis_(std::move(redis)), prefix_(getenv_or("TG_REDIS_PREFIX", "tg")) {}

std::string SessionStore::key_for_chat(std::int64_t chatId) const {
    return prefix_ + ":session:" + std::to_string(chatId);
}

Session SessionStore::load(std::int64_t chatId) {
    auto raw = redis_->get(key_for_chat(chatId));
    if (!raw) return Session{};
    try {
        return session_from_json(nlohmann::json::parse(*raw));
    } catch (...) {
        return Session{};
    }
}

void SessionStore::save(std::int64_t chatId, const Session& s, int ttlSeconds) {
    redis_->set(key_for_chat(chatId), session_to_json(s).dump(), ttlSeconds);
}

void SessionStore::clear(std::int64_t chatId) {
    redis_->del(key_for_chat(chatId));
    redis_->srem(prefix_ + ":auth", std::to_string(chatId));
    redis_->srem(prefix_ + ":anon", std::to_string(chatId));
}

void SessionStore::mark_anon(std::int64_t chatId) {
    redis_->sadd(prefix_ + ":anon", std::to_string(chatId));
    redis_->srem(prefix_ + ":auth", std::to_string(chatId));
}

void SessionStore::mark_auth(std::int64_t chatId) {
    redis_->sadd(prefix_ + ":auth", std::to_string(chatId));
    redis_->srem(prefix_ + ":anon", std::to_string(chatId));
}

std::vector<std::int64_t> SessionStore::anon_chats() {
    std::vector<std::int64_t> out;
    for (auto& s : redis_->smembers(prefix_ + ":anon")) {
        try {
            out.push_back(std::stoll(s));
        } catch (...) {
        }
    }
    return out;
}

std::vector<std::int64_t> SessionStore::auth_chats() {
    std::vector<std::int64_t> out;
    for (auto& s : redis_->smembers(prefix_ + ":auth")) {
        try {
            out.push_back(std::stoll(s));
        } catch (...) {
        }
    }
    return out;
}

bool SessionStore::ping() { return redis_->ping(); }
