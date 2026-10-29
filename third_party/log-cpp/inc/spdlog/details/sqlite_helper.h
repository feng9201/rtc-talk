#ifndef SPDLOG_DETAILS_SQLITE_HELPER_H
#define SPDLOG_DETAILS_SQLITE_HELPER_H
#include "w_log_cpp.h"
#include <spdlog/common.h>

namespace SQLite {
class Database;
class Transaction;
class Statement;
}  // namespace SQLite

namespace w_log_cpp {
namespace details {

class LOGCPP_API sqlite_helper {
   public:
    sqlite_helper() = default;

    sqlite_helper(const sqlite_helper &) = delete;
    sqlite_helper &operator=(const sqlite_helper &) = delete;
    ~sqlite_helper();

    void open(const spdlog::filename_t &fname);
    void flush();
    void sync();
    void close();
    void write(const std::string &time, const int &level, const std::string &info, const std::string &message);

    const spdlog::filename_t &filename() const;

   private:
    const int open_tries_ = 5;
    const unsigned int open_interval_ = 10;
    spdlog::filename_t filename_;
    std::shared_ptr<SQLite::Database> db_;
    std::shared_ptr<SQLite::Transaction> transaction_;
    std::shared_ptr<SQLite::Statement> insert_query_;
};

}  // namespace details
}  // namespace w_log_cpp

#endif  // SPDLOG_DETAILS_SQLITE_HELPER_H