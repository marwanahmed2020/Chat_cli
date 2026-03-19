#include "database.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sqlite3.h>

#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr int kSaltBytes = 16;
constexpr int kHashBytes = 32;
constexpr int kPbkdf2Iterations = 120000;

std::string bytes_to_hex(const unsigned char* data, size_t length) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (size_t i = 0; i < length; ++i) {
        output << std::setw(2) << static_cast<int>(data[i]);
    }
    return output.str();
}

std::vector<unsigned char> hex_to_bytes(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        return {};
    }

    std::vector<unsigned char> bytes;
    bytes.reserve(hex.size() / 2);

    for (size_t i = 0; i < hex.size(); i += 2) {
        unsigned int value = 0;
        std::stringstream parser;
        parser << std::hex << hex.substr(i, 2);
        parser >> value;
        bytes.push_back(static_cast<unsigned char>(value));
    }

    return bytes;
}

std::string generate_salt() {
    unsigned char salt[kSaltBytes];
    if (RAND_bytes(salt, sizeof(salt)) != 1) {
        throw std::runtime_error("Failed to generate cryptographic salt");
    }
    return bytes_to_hex(salt, sizeof(salt));
}

std::string hash_password_pbkdf2(const std::string& password, const std::string& salt_hex) {
    std::vector<unsigned char> salt = hex_to_bytes(salt_hex);
    if (salt.empty()) {
        throw std::runtime_error("Invalid salt");
    }

    unsigned char hash[kHashBytes];
    if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()), salt.data(), static_cast<int>(salt.size()),
                          kPbkdf2Iterations, EVP_sha256(), sizeof(hash), hash) != 1) {
        throw std::runtime_error("PBKDF2 failed");
    }

    return bytes_to_hex(hash, sizeof(hash));
}

bool secure_hash_equals(const std::string& left_hex, const std::string& right_hex) {
    std::vector<unsigned char> left = hex_to_bytes(left_hex);
    std::vector<unsigned char> right = hex_to_bytes(right_hex);

    if (left.size() != right.size() || left.empty()) {
        return false;
    }

    return CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

std::string generate_room_code() {
    unsigned int random_value = 0;
    if (RAND_bytes(reinterpret_cast<unsigned char*>(&random_value), sizeof(random_value)) != 1) {
        throw std::runtime_error("Failed to generate room code");
    }

    int room_number = static_cast<int>(random_value % 900000) + 100000;
    return std::to_string(room_number);
}
}  // namespace

Database::Database(const std::string& database_path) : db_(nullptr) {
    if (sqlite3_open(database_path.c_str(), &db_) != SQLITE_OK) {
        std::string error = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("Failed to open SQLite database: " + error);
    }

    sqlite3_busy_timeout(db_, 3000);
}

Database::~Database() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::execute_sql(const std::string& sql, std::string& error_message) {
    char* error = nullptr;
    int result = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error);
    if (result != SQLITE_OK) {
        error_message = error != nullptr ? error : "Unknown SQL error";
        if (error != nullptr) {
            sqlite3_free(error);
        }
        return false;
    }
    return true;
}

bool Database::initialize(std::string& error_message) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!execute_sql("PRAGMA foreign_keys = ON;", error_message)) {
        return false;
    }

    const std::string users_table =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT UNIQUE NOT NULL,"
        "password_hash TEXT NOT NULL,"
        "salt TEXT NOT NULL,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    const std::string rooms_table =
        "CREATE TABLE IF NOT EXISTS rooms ("
        "room_code TEXT PRIMARY KEY,"
        "owner_user_id INTEGER NOT NULL,"
        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "FOREIGN KEY(owner_user_id) REFERENCES users(id) ON DELETE CASCADE"
        ");";

    const std::string members_table =
        "CREATE TABLE IF NOT EXISTS room_members ("
        "room_code TEXT NOT NULL,"
        "user_id INTEGER NOT NULL,"
        "joined_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "PRIMARY KEY(room_code, user_id),"
        "FOREIGN KEY(room_code) REFERENCES rooms(room_code) ON DELETE CASCADE,"
        "FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE"
        ");";

    return execute_sql(users_table, error_message) && execute_sql(rooms_table, error_message) &&
           execute_sql(members_table, error_message);
}

bool Database::create_user(const std::string& username, const std::string& password, std::string& error_message) {
    if (username.size() < 3 || username.size() > 32) {
        error_message = "Username must be between 3 and 32 characters.";
        return false;
    }

    if (password.size() < 8) {
        error_message = "Password must be at least 8 characters.";
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    std::string salt;
    std::string password_hash;
    try {
        salt = generate_salt();
        password_hash = hash_password_pbkdf2(password, salt);
    } catch (const std::exception& exception) {
        error_message = exception.what();
        return false;
    }

    const char* sql = "INSERT INTO users(username, password_hash, salt) VALUES(?, ?, ?);";
    sqlite3_stmt* statement = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error_message = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(statement, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, password_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 3, salt.c_str(), -1, SQLITE_TRANSIENT);

    int result = sqlite3_step(statement);
    sqlite3_finalize(statement);

    if (result != SQLITE_DONE) {
        error_message = sqlite3_errmsg(db_);
        return false;
    }

    return true;
}

bool Database::verify_user(const std::string& username, const std::string& password, int& user_id,
                           std::string& error_message) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "SELECT id, password_hash, salt FROM users WHERE username = ?;";
    sqlite3_stmt* statement = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        error_message = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(statement, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    int result = sqlite3_step(statement);
    if (result != SQLITE_ROW) {
        sqlite3_finalize(statement);
        error_message = "Invalid username or password.";
        return false;
    }

    int fetched_id = sqlite3_column_int(statement, 0);
    const unsigned char* stored_hash = sqlite3_column_text(statement, 1);
    const unsigned char* stored_salt = sqlite3_column_text(statement, 2);

    if (stored_hash == nullptr || stored_salt == nullptr) {
        sqlite3_finalize(statement);
        error_message = "Corrupted credentials.";
        return false;
    }

    std::string computed_hash;
    try {
        computed_hash = hash_password_pbkdf2(password, reinterpret_cast<const char*>(stored_salt));
    } catch (const std::exception& exception) {
        sqlite3_finalize(statement);
        error_message = exception.what();
        return false;
    }

    bool match = secure_hash_equals(computed_hash, reinterpret_cast<const char*>(stored_hash));
    sqlite3_finalize(statement);

    if (!match) {
        error_message = "Invalid username or password.";
        return false;
    }

    user_id = fetched_id;   // only assigned after hash is confirmed correct
    return true;
}

std::string Database::create_room(int owner_user_id, std::string& error_message) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* insert_room_sql = "INSERT INTO rooms(room_code, owner_user_id) VALUES(?, ?);";
    const char* insert_member_sql = "INSERT OR IGNORE INTO room_members(room_code, user_id) VALUES(?, ?);";

    for (int attempt = 0; attempt < 20; ++attempt) {
        std::string room_code;
        try {
            room_code = generate_room_code();
        } catch (const std::exception& exception) {
            error_message = exception.what();
            return "";
        }

        sqlite3_stmt* room_statement = nullptr;
        if (sqlite3_prepare_v2(db_, insert_room_sql, -1, &room_statement, nullptr) != SQLITE_OK) {
            error_message = sqlite3_errmsg(db_);
            return "";
        }

        sqlite3_bind_text(room_statement, 1, room_code.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(room_statement, 2, owner_user_id);

        int room_result = sqlite3_step(room_statement);
        sqlite3_finalize(room_statement);

        if (room_result != SQLITE_DONE) {
            continue;
        }

        sqlite3_stmt* member_statement = nullptr;
        if (sqlite3_prepare_v2(db_, insert_member_sql, -1, &member_statement, nullptr) != SQLITE_OK) {
            error_message = sqlite3_errmsg(db_);
            return "";
        }

        sqlite3_bind_text(member_statement, 1, room_code.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(member_statement, 2, owner_user_id);

        int member_result = sqlite3_step(member_statement);
        sqlite3_finalize(member_statement);

        if (member_result != SQLITE_DONE) {
            error_message = sqlite3_errmsg(db_);
            return "";
        }

        return room_code;
    }

    error_message = "Failed to allocate a unique room code.";
    return "";
}

bool Database::join_room(int user_id, const std::string& room_code, std::string& error_message) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* exists_sql = "SELECT 1 FROM rooms WHERE room_code = ?;";
    sqlite3_stmt* exists_statement = nullptr;

    if (sqlite3_prepare_v2(db_, exists_sql, -1, &exists_statement, nullptr) != SQLITE_OK) {
        error_message = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(exists_statement, 1, room_code.c_str(), -1, SQLITE_TRANSIENT);
    int exists_result = sqlite3_step(exists_statement);
    sqlite3_finalize(exists_statement);

    if (exists_result != SQLITE_ROW) {
        error_message = "Room not found.";
        return false;
    }

    const char* join_sql = "INSERT OR IGNORE INTO room_members(room_code, user_id) VALUES(?, ?);";
    sqlite3_stmt* join_statement = nullptr;

    if (sqlite3_prepare_v2(db_, join_sql, -1, &join_statement, nullptr) != SQLITE_OK) {
        error_message = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(join_statement, 1, room_code.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(join_statement, 2, user_id);

    int join_result = sqlite3_step(join_statement);
    sqlite3_finalize(join_statement);

    if (join_result != SQLITE_DONE) {
        error_message = sqlite3_errmsg(db_);
        return false;
    }

    return true;
}

void Database::admin_print_users(std::ostream& out) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "SELECT id, username FROM users ORDER BY id;";
    sqlite3_stmt* statement = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        out << "DB error: " << sqlite3_errmsg(db_) << "\n";
        return;
    }

    out << "\n--- users ---\n";
    while (sqlite3_step(statement) == SQLITE_ROW) {
        int id = sqlite3_column_int(statement, 0);
        const unsigned char* username = sqlite3_column_text(statement, 1);
        out << id << " | " << (username ? reinterpret_cast<const char*>(username) : "") << "\n";
    }
    out << "------------\n";
    sqlite3_finalize(statement);
}

void Database::admin_print_rooms(std::ostream& out) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "SELECT room_code, owner_user_id FROM rooms ORDER BY room_code;";
    sqlite3_stmt* statement = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        out << "DB error: " << sqlite3_errmsg(db_) << "\n";
        return;
    }

    out << "\n--- rooms ---\n";
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char* code = sqlite3_column_text(statement, 0);
        int owner = sqlite3_column_int(statement, 1);
        out << (code ? reinterpret_cast<const char*>(code) : "") << " | owner=" << owner << "\n";
    }
    out << "------------\n";
    sqlite3_finalize(statement);
}

void Database::admin_print_members(std::ostream& out) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql =
        "SELECT rm.room_code, rm.user_id, u.username "
        "FROM room_members rm "
        "LEFT JOIN users u ON u.id = rm.user_id "
        "ORDER BY rm.room_code, rm.user_id;";
    sqlite3_stmt* statement = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        out << "DB error: " << sqlite3_errmsg(db_) << "\n";
        return;
    }

    out << "\n--- room_members ---\n";
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const unsigned char* code = sqlite3_column_text(statement, 0);
        int user_id = sqlite3_column_int(statement, 1);
        const unsigned char* username = sqlite3_column_text(statement, 2);
        out << (code ? reinterpret_cast<const char*>(code) : "") << " | user=" << user_id << " ("
            << (username ? reinterpret_cast<const char*>(username) : "") << ")\n";
    }
    out << "--------------------\n";
    sqlite3_finalize(statement);
}