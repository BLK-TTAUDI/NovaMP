// server/src/auth/auth_manager.cpp
#include "auth_manager.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <chrono>

namespace novaMP {
namespace {

std::string b64url_encode(const std::string& in) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    unsigned val = 0, bits = 0;
    for (unsigned char c : in) {
        val = (val << 8) | c;
        bits += 8;
        while (bits >= 6) { out += table[(val >> (bits - 6)) & 0x3F]; bits -= 6; }
    }
    if (bits > 0) out += table[(val << (6 - bits)) & 0x3F];
    return out;
}

std::string hmac_sha256(const std::string& key, const std::string& data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int  len = 0;
    HMAC(EVP_sha256(),
         key.data(), (int)key.size(),
         reinterpret_cast<const unsigned char*>(data.data()), data.size(),
         digest, &len);
    return b64url_encode(std::string(reinterpret_cast<char*>(digest), len));
}

std::string sha256_hex(const std::string& in) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(in.data()), in.size(), hash);
    std::ostringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    return ss.str();
}

std::string random_hex(size_t bytes) {
    std::vector<unsigned char> buf(bytes);
    RAND_bytes(buf.data(), (int)bytes);
    std::ostringstream ss;
    for (auto b : buf)
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    return ss.str();
}

std::string pbkdf2_hash(const std::string& password, const std::string& salt, int rounds) {
    unsigned char out[32];
    PKCS5_PBKDF2_HMAC(password.c_str(), (int)password.size(),
                      reinterpret_cast<const unsigned char*>(salt.c_str()), (int)salt.size(),
                      rounds, EVP_sha256(), sizeof(out), out);
    return "$pbkdf2$" + salt + "$" + b64url_encode(std::string(reinterpret_cast<char*>(out), 32));
}

bool pbkdf2_verify(const std::string& password, const std::string& stored) {
    auto p2 = stored.find('$', 1);
    auto p3 = stored.find('$', p2 + 1);
    auto p4 = stored.find('$', p3 + 1);
    if (p2 == std::string::npos || p3 == std::string::npos || p4 == std::string::npos)
        return false;
    std::string salt = stored.substr(p3 + 1, p4 - p3 - 1);
    std::string expected = pbkdf2_hash(password, salt, 100000);
    if (expected.size() != stored.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < expected.size(); ++i)
        diff |= (unsigned char)(expected[i] ^ stored[i]);
    return diff == 0;
}

} // anonymous namespace

AuthManager::AuthManager(Database& db, const AuthConfig& cfg)
    : m_db(db), m_cfg(cfg) {}

std::string AuthManager::hashPassword(const std::string& password) {
    return pbkdf2_hash(password, random_hex(16), 100000);
}

bool AuthManager::verifyPassword(const std::string& password, const std::string& hash) {
    return pbkdf2_verify(password, hash);
}

std::string AuthManager::makeJWT(const UserRecord& user) {
    using namespace std::chrono;
    nlohmann::json header  = {{"alg","HS256"},{"typ","JWT"}};
    nlohmann::json payload = {
        {"sub", std::to_string(user.id)},
        {"usr", user.username},
        {"rol", user.role},
        {"iat", duration_cast<seconds>(system_clock::now().time_since_epoch()).count()},
        {"exp", duration_cast<seconds>(
            (system_clock::now() + hours(m_cfg.jwt_expiry_hours)).time_since_epoch()).count()}
    };
    std::string h = b64url_encode(header.dump());
    std::string p = b64url_encode(payload.dump());
    return h + "." + p + "." + hmac_sha256(m_cfg.jwt_secret, h + "." + p);
}

std::optional<UserRecord> AuthManager::parseJWT(const std::string& token) {
    auto d1 = token.find('.');
    auto d2 = token.find('.', d1 + 1);
    if (d1 == std::string::npos || d2 == std::string::npos) return {};
    std::string h = token.substr(0, d1);
    std::string p = token.substr(d1 + 1, d2 - d1 - 1);
    std::string sig = token.substr(d2 + 1);
    if (hmac_sha256(m_cfg.jwt_secret, h + "." + p) != sig) return {};
    try {
        auto pl = nlohmann::json::parse(b64url_encode(p));
        using namespace std::chrono;
        int64_t exp = pl["exp"].get<int64_t>();
        int64_t now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
        if (now > exp) return {};
        UserRecord u;
        u.id       = std::stoll(pl["sub"].get<std::string>());
        u.username = pl["usr"].get<std::string>();
        u.role     = pl["rol"].get<std::string>();
        u.banned   = false;
        return u;
    } catch (...) { return {}; }
}

std::string AuthManager::makeRefreshToken() { return random_hex(32); }

UserRecord AuthManager::rowToUser(sqlite3_stmt* s) {
    return { Database::col_int64(s,0), Database::col_text(s,1),
             Database::col_text(s,2),  Database::col_text(s,3),
             Database::col_int(s,4) != 0, Database::col_text(s,5) };
}

AuthResult AuthManager::registerUser(const std::string& username,
                                     const std::string& email,
                                     const std::string& password)
{
    if (username.size() < 3 || username.size() > 32)
        return {false,"","",{},"Username must be 3-32 characters"};
    if (password.size() < 8)
        return {false,"","",{},"Password must be at least 8 characters"};

    bool exists = false;
    m_db.query("SELECT 1 FROM users WHERE username=? OR email=?",
               [&](sqlite3_stmt*){ exists = true; }, username, email);
    if (exists) return {false,"","",{},"Username or email already taken"};

    int64_t id = m_db.insert(
        "INSERT INTO users(username,email,password_hash) VALUES(?,?,?)",
        username, email, hashPassword(password));

    UserRecord u{id, username, email, "player", false, ""};
    std::string token   = makeJWT(u);
    std::string refresh = makeRefreshToken();

    using namespace std::chrono;
    auto exp_str = std::to_string(duration_cast<seconds>(
        (system_clock::now() + days(30)).time_since_epoch()).count());
    m_db.exec("INSERT INTO auth_tokens(user_id,token_hash,expires_at) VALUES(?,?,?)",
              id, sha256_hex(refresh), exp_str);

    spdlog::info("New user registered: {} (id={})", username, id);
    return {true, token, refresh, u, ""};
}

AuthResult AuthManager::loginUser(const std::string& username_or_email,
                                   const std::string& password)
{
    std::optional<UserRecord> found;
    std::string stored_hash;
    m_db.query(
        "SELECT id,username,email,role,banned,ban_reason,password_hash "
        "FROM users WHERE (username=? OR email=?) LIMIT 1",
        [&](sqlite3_stmt* s) {
            found       = rowToUser(s);
            stored_hash = Database::col_text(s, 6);
        }, username_or_email, username_or_email);

    if (!found) return {false,"","",{},"Invalid credentials"};
    if (!verifyPassword(password, stored_hash)) return {false,"","",{},"Invalid credentials"};
    if (found->banned) return {false,"","",*found,"Account banned: " + found->ban_reason};

    m_db.exec("UPDATE users SET last_login=datetime('now') WHERE id=?", found->id);

    std::string token   = makeJWT(*found);
    std::string refresh = makeRefreshToken();
    using namespace std::chrono;
    auto exp_str = std::to_string(duration_cast<seconds>(
        (system_clock::now() + days(30)).time_since_epoch()).count());
    m_db.exec("INSERT INTO auth_tokens(user_id,token_hash,expires_at) VALUES(?,?,?)",
              found->id, sha256_hex(refresh), exp_str);

    return {true, token, refresh, *found, ""};
}

std::optional<UserRecord> AuthManager::verifyToken(const std::string& token) {
    return parseJWT(token);
}

std::string AuthManager::generateServerToken() { return random_hex(32); }

bool AuthManager::validateServerToken(const std::string& token) {
    bool ok = false;
    m_db.query("SELECT 1 FROM game_servers WHERE auth_token=? LIMIT 1",
               [&](sqlite3_stmt*){ ok = true; }, token);
    return ok;
}

void AuthManager::logout(const std::string& refresh_token) {
    m_db.exec("DELETE FROM auth_tokens WHERE token_hash=?", sha256_hex(refresh_token));
}

bool AuthManager::banUser(int64_t user_id, const std::string& reason, int64_t banned_by_id) {
    m_db.exec("UPDATE users SET banned=1, ban_reason=? WHERE id=?", reason, user_id);
    m_db.exec("INSERT INTO bans(user_id,reason,banned_by) VALUES(?,?,?)",
              user_id, reason, banned_by_id);
    return true;
}

} // namespace novaMP
