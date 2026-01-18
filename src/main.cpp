#include <iostream>
#include <memory>

#include "auth_client.h"
#include "main_client.h"
#include "redis_client.h"
#include "session_store.h"
#include "telegram_bot.h"
#include "util.h"

int main() {
    const std::string tg_token = getenv_or("TG_BOT_TOKEN", "");
    if (tg_token.empty()) {
        std::cerr << "TG_BOT_TOKEN env var is required" << std::endl;
        return 1;
    }

    const std::string redis_host = getenv_or("REDIS_HOST", "127.0.0.1");
    const int redis_port = std::stoi(getenv_or("REDIS_PORT", "6379"));

    const std::string auth_base = getenv_or("AUTH_BASE_URL", "http://127.0.0.1:8080");
    const std::string main_base = getenv_or("MAIN_BASE_URL", "http://127.0.0.1:8000");

    auto redis = std::make_shared<RedisClient>(redis_host, redis_port);
    auto store = std::make_shared<SessionStore>(redis);

    if (!store->ping()) {
        std::cerr << "Failed to connect to Redis at " << redis_host << ":" << redis_port << std::endl;
        return 1;
    }

    TelegramModuleBot bot(tg_token, store, AuthClient(auth_base), MainClient(main_base));
    bot.run();
    return 0;
}
