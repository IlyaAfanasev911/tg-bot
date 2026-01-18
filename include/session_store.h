#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "redis_client.h"
#include "session.h"

class SessionStore {
public:
    explicit SessionStore(std::shared_ptr<RedisClient> redis);

    std::string key_for_chat(std::int64_t chatId) const;
    Session load(std::int64_t chatId);
    void save(std::int64_t chatId, const Session& s, int ttlSeconds = 60 * 60 * 24 * 7);
    void clear(std::int64_t chatId);

    void mark_anon(std::int64_t chatId);
    void mark_auth(std::int64_t chatId);

    std::vector<std::int64_t> anon_chats();
    std::vector<std::int64_t> auth_chats();

    bool ping();

private:
    std::shared_ptr<RedisClient> redis_;
    std::string prefix_;
};
