#include "database.hpp"
#include "config.hpp"

#include <stdexcept>
#include <array>
#include <string_view>  
#include <sstream>


template<typename... Args>
std::string format_string(Args&&... args)
{
    std::ostringstream oss;
    (oss << ... << args);
    return oss.str();
}


Database::Database(std::string_view path)
{
    sqlite3* raw_db = nullptr;
    if(sqlite3_open(path.data(), &raw_db) != SQLITE_OK)
        throw std::runtime_error
        (
           format_string("Cannot open database: ", path)
        );
        
    db_.reset(raw_db);

    execute_sql("PRAGMA journal_mode=WAL;");
    execute_sql("PRAGMA busy_timeout=500;");

    
    init_tables();

}

void Database::init_tables()
{
    constexpr std::array table_schemas = 
    {
        std::string_view
        {
            "CREATE TABLE IF NOT EXISTS coupons("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "coupon_id INTEGER,"
            "customer_id INTEGER,"
            "card_id INTEGER,"
            "card_number TEXT,"
            "valid_from TEXT,"
            "valid_to TEXT,"
            "traffic_area_group TEXT);"
        },
        std::string_view
        {
            "CREATE TABLE IF NOT EXISTS article_tickets("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "active INTEGER,"
            "caption TEXT,"
            "price REAL,"
            "article_id INTEGER,"
            "period_type_id INTEGER,"
            "fixed_valid_to TEXT,"
            "relative_valid_to INTEGER,"
            "valid_in_zones TEXT,"
            "available_in_zones TEXT,"
            "valid_only_in_zone_of_acquisition INTEGER"
            ");"
        },
        std::string_view
        {
            "CREATE TABLE IF NOT EXISTS qr_validated("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "datetime TEXT DEFAULT(datetime('now','localtime')),"
            "qr_code TEXT,"
            "validator_id,"
            "valid INTEGER);"
        },
        std::string_view
        {
            "CREATE TABLE IF NOT EXISTS card_validated("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "datetime TEXT DEFAULT(datetime('now','localtime')),"
            "card_id INTEGER,"
            "valid INTEGER);"  
        },
        std::string_view
        {
            "CREATE TABLE IF NOT EXISTS purchases("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "article_id INTEGER,"
            "card_number TEXT,"
            "quantity INTEGER,"
            "success INTEGER,"
            "timestamp TEXT)"
        },
        std::string_view
        {
            "CREATE TABLE IF NOT EXISTS articles("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "article_id INTEGER,"
            "article_name TEXT,"
            "article_price REAL);"
        },
        std::string_view
        {

            "CREATE TABLE IF NOT EXISTS tickets("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "ticket_id INTEGER,"
            "active INT,"
            "date_created TEXT,"
            "account_id INTEGER,"
            "caption TEXT,"
            "valid_from TEXT,"
            "valid_to TEXT,"
            "traffic_area TEXT,"
            "traffic_zone INTEGER,"
            "article_id INTEGER,"
            "invoice_item_id INTEGER,"
            "token TEXT)"
            /*public class Ticket
            {
                public long Id { get; set; }
                public bool Active { get; set; }
                public DateTime DateCreated { get; set; }
                public int? AccountId { get; set; }
                public string Caption { get; set; }
                public DateTime? ValidFrom { get; set; }
                public DateTime? ValidTo { get; set; }
                public string TrafficArea { get; set; }
                public int? TrafficZone { get; set; }
                public int? ArticleId { get; set; }
                public int? InvoiceItemId { get; set; }
                public Guid? Token { get; set; }
            }*/
        }
    };
    
    for (const auto& schema : table_schemas) 
        execute_sql(schema);
    
}

void Database::execute_sql(std::string_view sql)
{
    char* error_msg = nullptr;

    if(sqlite3_exec(db_.get(), sql.data(), nullptr, nullptr, &error_msg) != SQLITE_OK)
    {
        std::string error = error_msg ? error_msg : "Unknown error";
        sqlite3_free(error_msg);

        throw std::runtime_error
        (
            format_string("SQL execution failed: ", error, "\nQuery: ", sql)
        );
    }
}