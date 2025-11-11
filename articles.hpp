#pragma once

#include <optional>
#include <string_view>
#include <string>
#include <memory>
#include "include/sqlite3.h"

namespace Articles
{
    struct Article
    {
        int article_id;
        std::string name;
        double price;
    };

    class ArticleManager
    {
    public:
        explicit ArticleManager(sqlite3* db);

        [[nodiscard]] bool fetch_and_store(std::string_view endpoint);
        [[nodiscard]] int parse_and_insert(std::string_view json_content);
    private:
        sqlite3* db_;
        [[nodiscard]] bool insert_article(const Article& article);
    };

}