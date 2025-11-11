#include "articles.hpp"
#include "fetcher.hpp"
#include "include/sqlite3.h"
#include "nlohmann/json.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>

using json = nlohmann::json;

namespace Articles
{
    ArticleManager::ArticleManager(sqlite3* db) : db_(db) {}

    bool ArticleManager::fetch_and_store(std::string_view endpoint)
    {
        std::cout << "Fetching articles from " << endpoint << std::endl;

        auto json_content = Fetcher::fetch_json(endpoint);
        if(!json_content)
        {
            std::cerr << "Failed to fetch articles from " << endpoint << std::endl;
            return false;
        }

        int inserted = parse_and_insert(*json_content);
        if(!inserted)
        {
            std::cerr << "Failed to parse and insert articles\n";
            return false;
        }

        std::cout << "Successfully inserted " << inserted << " articles\n";
        return inserted > 0;
    }

    int ArticleManager::parse_and_insert(std::string_view json_content)
    {
        try
        {
            auto insert_start = std::chrono::steady_clock::now();


            auto json_array = json::parse(json_content);
            if(!json_array.is_array())
            {
                std::cerr << "Failed to parse article json content\n";
                return -1;
            }

            int inserted = 0;

            std::cout << "Parsed " << json_array.size() << "artices\n";


            for(const auto& item : json_array)
            {
                Article article;

                article.article_id = item.contains("id") && item["id"].is_number() ? item["id"].get<int>() : 0;
                article.name = item.contains("name") && item["name"].is_string() ? item["name"].get<std::string>() : "";
                article.price = item.contains("price") && item["price"].is_number() ? item["price"].get<int>() : 0;

                if(insert_article(article))
                    inserted++;
            }

            auto insert_end = std::chrono::steady_clock::now();
            auto total = std::chrono::duration_cast<std::chrono::microseconds>(insert_end - insert_start).count();
            std::cout << "Total time to inesert in microseconds: " << total << std::endl;
            std::cout << "Inserted " << inserted << " articles\n";
            return inserted;

        }
        catch(const json::exception e)
        {
            std::cerr << "JSON parse error: " << e.what() << '\n';
            return -1;
        }
    }


    bool ArticleManager::insert_article(const Article& article)
    {
        const char* sql = 
                "INSERT INTO articles (article_id, article_name, article_price) "
                "VALUES (?, ?, ?);";    

        sqlite3_stmt* stmt;

        if(sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
        {
            std::cerr << "Failed to prepare articles: " << sqlite3_errmsg(db_) << std::endl;
            return false;
        }

        sqlite3_bind_int(stmt, 1, article.article_id);
        sqlite3_bind_text(stmt, 2, article.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, article.price);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);

        if(!success)
        {
            std::cerr << "Failed to bind stmt articles\n";
            return false;
        }
        sqlite3_finalize(stmt);
        return success;
    }
}