#ifndef DATABASE_H
#define DATABASE_H

#include <mutex>
#include <string>

struct sqlite3;

class Database {
public:
    explicit Database(const std::string& database_path);
    ~Database();

    bool initialize(std::string& error_message);
    bool create_user(const std::string& username, const std::string& password, std::string& error_message);
    bool verify_user(const std::string& username, const std::string& password, int& user_id, std::string& error_message);
    std::string create_room(int owner_user_id, std::string& error_message);
    bool join_room(int user_id, const std::string& room_code, std::string& error_message);

private:
    sqlite3* db_;
    std::mutex mutex_;

    bool execute_sql(const std::string& sql, std::string& error_message);
};

#endif