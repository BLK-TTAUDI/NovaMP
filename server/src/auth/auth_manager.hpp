// server/src/auth/auth_manager.hpp
#pragma once

#include <string>
#include <optional>
#include "../db/database.hpp"

namespace novaMP {

struct AuthConfig {
    std::string jwt_secret;
    int         jwt_expiry_hours;
    int         bcrypt_rounds;
    std::string discord_id;
    std::string discord_secret;
    std::string discord_redirect;
};

struct UserRecord {
    int64_t     id;
    std::string username;
    std::string email;
    std::string role;
    bool        banned;
    std::string ban_reason;
};

struct AuthResult {
    bool        success;
    std::string token;
    std::string refresh_token;
    UserRecord  user;
    std::string error;
};

class AuthManager {
public:
    AuthManager(Database& db, const AuthConfig& cfg);

    AuthResult registerUser(const std::string& username,
                            const std::string& email,
                            const std::string& password);

    AuthResult loginUser(const std::string& username_or_email,
                         const std::string& password);

    AuthResult refreshToken(const std::string& refresh_token);

    std::optional<UserRecord> verifyToken(const std::string& token);

    std::string generateServerToken();
    bool        validateServerToken(const std::string& token);

    void logout(const std::string& refresh_token);
    bool banUser(int64_t user_id, const std::string& reason, int64_t banned_by_id);

private:
    Database&   m_db;
    AuthConfig  m_cfg;

    std::string hashPassword(const std::string& password);
    bool        verifyPassword(const std::string& password, const std::string& hash);
    std::string makeJWT(const UserRecord& user);
    std::optional<UserRecord> parseJWT(const std::string& token);
    std::string makeRefreshToken();
    UserRecord  rowToUser(sqlite3_stmt* s);
};

} // namespace novaMP
