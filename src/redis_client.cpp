#include "redis_client.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <sstream>
#include <utility>

RedisClient::RedisClient(std::string host, int port) : host_(std::move(host)), port_(port) {}

bool RedisClient::ping() {
    auto r = cmd({"PING"});
    return r && r->type == Resp::Type::SimpleString && r->str == "PONG";
}

std::optional<std::string> RedisClient::get(const std::string& key) {
    auto r = cmd({"GET", key});
    if (!r) return std::nullopt;
    if (r->type == Resp::Type::Null) return std::nullopt;
    if (r->type == Resp::Type::BulkString) return r->str;
    return std::nullopt;
}

bool RedisClient::set(const std::string& key, const std::string& val, int ttlSeconds) {
    std::vector<std::string> args = {"SET", key, val};
    if (ttlSeconds > 0) {
        args.push_back("EX");
        args.push_back(std::to_string(ttlSeconds));
    }
    auto r = cmd(args);
    return r && r->type == Resp::Type::SimpleString && r->str == "OK";
}

long long RedisClient::del(const std::string& key) {
    auto r = cmd({"DEL", key});
    if (!r || r->type != Resp::Type::Integer) return 0;
    return r->i;
}

long long RedisClient::sadd(const std::string& setKey, const std::string& member) {
    auto r = cmd({"SADD", setKey, member});
    if (!r || r->type != Resp::Type::Integer) return 0;
    return r->i;
}

long long RedisClient::srem(const std::string& setKey, const std::string& member) {
    auto r = cmd({"SREM", setKey, member});
    if (!r || r->type != Resp::Type::Integer) return 0;
    return r->i;
}

std::vector<std::string> RedisClient::smembers(const std::string& setKey) {
    std::vector<std::string> out;
    auto r = cmd({"SMEMBERS", setKey});
    if (!r || r->type != Resp::Type::Array) return out;
    for (auto& it : r->arr) {
        if (it.type == Resp::Type::BulkString) out.push_back(it.str);
    }
    return out;
}

std::string RedisClient::encode(const std::vector<std::string>& args) {
    std::ostringstream ss;
    ss << "*" << args.size() << "\r\n";
    for (const auto& a : args) {
        ss << "$" << a.size() << "\r\n" << a << "\r\n";
    }
    return ss.str();
}

bool RedisClient::read_line(int fd, std::string& line) {
    line.clear();
    char c;
    while (true) {
        ssize_t n = ::recv(fd, &c, 1, 0);
        if (n <= 0) return false;
        if (c == '\r') {
            n = ::recv(fd, &c, 1, 0);
            if (n <= 0) return false;
            if (c != '\n') return false;
            return true;
        }
        line.push_back(c);
    }
}

bool RedisClient::read_n(int fd, std::string& out, std::size_t n) {
    out.clear();
    out.resize(n);
    std::size_t got = 0;
    while (got < n) {
        ssize_t r = ::recv(fd, out.data() + got, n - got, 0);
        if (r <= 0) return false;
        got += static_cast<std::size_t>(r);
    }
    return true;
}

std::optional<RedisClient::Resp> RedisClient::parse_resp(int fd) {
    char prefix;
    ssize_t n = ::recv(fd, &prefix, 1, 0);
    if (n <= 0) return std::nullopt;

    Resp r;
    std::string line;

    if (prefix == '+') {
        if (!read_line(fd, line)) return std::nullopt;
        r.type = Resp::Type::SimpleString;
        r.str = line;
        return r;
    }
    if (prefix == '-') {
        if (!read_line(fd, line)) return std::nullopt;
        r.type = Resp::Type::Error;
        r.str = line;
        return r;
    }
    if (prefix == ':') {
        if (!read_line(fd, line)) return std::nullopt;
        r.type = Resp::Type::Integer;
        r.i = std::stoll(line);
        return r;
    }
    if (prefix == '$') {
        if (!read_line(fd, line)) return std::nullopt;
        long long len = std::stoll(line);
        if (len < 0) {
            r.type = Resp::Type::Null;
            return r;
        }
        std::string payload;
        if (!read_n(fd, payload, static_cast<std::size_t>(len))) return std::nullopt;
        std::string crlf;
        if (!read_n(fd, crlf, 2)) return std::nullopt;
        if (crlf != "\r\n") return std::nullopt;
        r.type = Resp::Type::BulkString;
        r.str = std::move(payload);
        return r;
    }
    if (prefix == '*') {
        if (!read_line(fd, line)) return std::nullopt;
        long long count = std::stoll(line);
        r.type = Resp::Type::Array;
        if (count < 0) return r;
        r.arr.reserve(static_cast<std::size_t>(count));
        for (long long i = 0; i < count; ++i) {
            auto item = parse_resp(fd);
            if (!item) return std::nullopt;
            r.arr.push_back(std::move(*item));
        }
        return r;
    }
    return std::nullopt;
}

int RedisClient::connect_tcp(const std::string& host, int port) {
    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    int rc = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
    if (rc != 0) return -1;

    int fd = -1;
    for (auto p = res; p != nullptr; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(res);
    return fd;
}

std::optional<RedisClient::Resp> RedisClient::cmd(const std::vector<std::string>& args) {
    std::lock_guard<std::mutex> lk(mtx_);
    int fd = connect_tcp(host_, port_);
    if (fd < 0) return std::nullopt;

    std::string payload = encode(args);
    std::size_t sent = 0;
    while (sent < payload.size()) {
        ssize_t w = ::send(fd, payload.data() + sent, payload.size() - sent, 0);
        if (w <= 0) {
            ::close(fd);
            return std::nullopt;
        }
        sent += static_cast<std::size_t>(w);
    }

    auto resp = parse_resp(fd);
    ::close(fd);
    return resp;
}
