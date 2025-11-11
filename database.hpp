#pragma once

#include "include/sqlite3.h"

#include <memory>
#include <string_view>

class Database
{
public:

    explicit Database(std::string_view path);
    ~Database() = default;

    // no copy constructor or operator
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    //allowed move for const and op
    Database(Database&&) = default;
    Database& operator=(Database&&) noexcept = default;
    
    [[nodiscard]]sqlite3* get() const noexcept {return db_.get();}


private:

    struct SQLiteDeleter
    {
        void operator()(sqlite3* db) const noexcept
        {
            sqlite3_close(db);
        }
    };

    std::unique_ptr<sqlite3, SQLiteDeleter> db_;
    void execute_sql(std::string_view sql);

    void init_tables();
};