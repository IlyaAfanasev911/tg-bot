#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

class RedisClient {
public:
    explicit RedisClient(std::string host, int port);

    bool ping();
    std::optional<std::string> get(const std::string& key);
    bool set(const std::string& key, const std::string& val, int ttlSeconds = -1);
    long long del(const std::string& key);
    long long sadd(const std::string& setKey, const std::string& member);
    long long srem(const std::string& setKey, const std::string& member);
    std::vector<std::string> smembers(const std::string& setKey);

private:
    struct Resp {
        enum class Type { SimpleString, Error, Integer, BulkString, Array, Null };
        Type type{Type::Null};
        std::string str;
        long long i{0};
        std::vector<Resp> arr;
    };

    std::string host_;
    int port_{6379};
    std::mutex mtx_;

    static std::string encode(const std::vector<std::string>& args);
    static bool read_line(int fd, std::string& line);
    static bool read_n(int fd, std::string& out, std::size_t n);
    static std::optional<Resp> parse_resp(int fd);
    static int connect_tcp(const std::string& host, int port);
    std::optional<Resp> cmd(const std::vector<std::string>& args);
};
