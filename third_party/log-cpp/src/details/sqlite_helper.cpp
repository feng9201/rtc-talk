#include "spdlog/details/sqlite_helper.h"

#include <SQLiteCpp/SQLiteCpp.h>
#include <SQLiteCpp/VariadicBind.h>
#include <spdlog/common.h>
#include <spdlog/details/os.h>
#include <sqlite3.h>

namespace w_log_cpp {
namespace details {
namespace {
const std::string sql_table_name = "w_log_cpp";
const std::string create_table_sql =
    "CREATE TABLE " + sql_table_name +
    " (id INTEGER PRIMARY KEY AUTOINCREMENT,time TEXT, level INTEGER,info TEXT, message TEXT );";
const std::string insert_sql = "INSERT INTO " + sql_table_name + " (time, level, info,message) VALUES (?, ?, ?,?);";

}  // namespace
sqlite_helper::~sqlite_helper() { close(); }

void sqlite_helper::open(const spdlog::filename_t &fname) {
    close();
    filename_ = fname;
    for (int tries = 0; tries < open_tries_; ++tries) {
        spdlog::details::os::create_dir(spdlog::details::os::dir_name(fname));
        try {
            db_ = std::make_shared<SQLite::Database>(filename_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        } catch (std::exception &e) {
            spdlog::throw_spdlog_ex(std::string("Failed to open sqlite file for writing,err:") + e.what(), errno);
        }
        if (db_) break;
        spdlog::details::os::sleep_for_millis(open_interval_);
    }

    if (!db_) return;

    auto has = db_->tableExists(sql_table_name);
    if (!has) {
        db_->exec(create_table_sql.c_str());
        if (db_->getErrorCode() != SQLITE_OK) {
            spdlog::throw_spdlog_ex(
                std::string("Failed to create table w_log_cpp for writing,err:") + db_->getErrorMsg(), errno);
        }
    }
    //关闭写同步
    db_->exec("PRAGMA synchronous = OFF;");
    transaction_ = std::make_shared<SQLite::Transaction>(*db_);
    insert_query_ = std::make_shared<SQLite::Statement>(*db_, insert_sql);
}

void sqlite_helper::flush() {
    try {
        if (transaction_) {
            transaction_->commit();
            transaction_ = std::make_shared<SQLite::Transaction>(*db_);
            insert_query_ = std::make_shared<SQLite::Statement>(*db_, insert_sql);
        }
    } catch (std::exception &e) {
        spdlog::throw_spdlog_ex(std::string("Failed to commit transaction,err:") + e.what(), errno);
    }
}

void sqlite_helper::sync() {
    // sqlite3_db_cacheflush(db_->getHandle());
}

void sqlite_helper::close() {
    if (db_) {
        insert_query_.reset();
        transaction_.reset();
        db_.reset();
    }
}

void sqlite_helper::write(const std::string &time, const int &level, const std::string &info,
                          const std::string &message) {
    if (db_) {
        try {
            insert_query_->bind(1, time);
            insert_query_->bind(2, level);
            insert_query_->bind(3, info);
            insert_query_->bind(4, message);
            insert_query_->exec();
            insert_query_->reset();
        } catch (std::exception &e) {
            spdlog::throw_spdlog_ex(std::string("Failed to write to sqlite,err:") + e.what(), errno);
        }
    } else {
        spdlog::throw_spdlog_ex("Failed to write to sqlite,db is null", errno);
    }
}

const spdlog::filename_t &sqlite_helper::filename() const { return filename_; }

}  // namespace details
}  // namespace w_log_cpp
