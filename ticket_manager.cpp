#include "ticket_manager.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace Tickets 
{
    TicketManager::TicketManager(Database& db, const std::string& grpc_server_address)
        : db_(db)
        , server_address_(grpc_server_address)
        , running_(false)
    {
        std::cout << "=== Ticket Manager (gRPC Client) ===\n";
        std::cout << "Server: " << server_address_ << "\n";
        std::cout << "=====================================\n\n";
        
        // Enable WAL checkpoint on every transaction for immediate persistence
        char* err_msg = nullptr;
        if (sqlite3_exec(db_.get(), "PRAGMA synchronous = NORMAL;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
            std::cerr << "[TicketManager] Warning: Could not set synchronous mode: " << err_msg << '\n';
            sqlite3_free(err_msg);
        }
    }

    TicketManager::~TicketManager()
    {
        Stop();
    }

    void TicketManager::Start()
    {
        if (running_.exchange(true)) {
            return;
        }
        
        streaming_thread_ = std::thread(&TicketManager::StreamingThread, this);
        std::cout << "[TicketManager] Started streaming thread\n";
    }

    void TicketManager::Stop()
    {
        if (!running_.exchange(false)) {
            return;
        }
        
        std::cout << "[TicketManager] Stopping...\n";
        
        {
            std::lock_guard<std::mutex> lock(context_mutex_);
            if (current_context_) {
                std::cout << "[TicketManager] Cancelling gRPC stream...\n";
                current_context_->TryCancel();
            }
        }
        
        if (streaming_thread_.joinable()) {
            streaming_thread_.join();
        }
        
        std::cout << "[TicketManager] Stopped\n";
    }

    void TicketManager::StreamingThread()
    {
        while (running_) {
            try {
                std::cout << "[TicketManager] Connecting to gRPC server at " << server_address_ << "...\n";
                
                
                channel_ = grpc::CreateChannel(
                    server_address_,
                    grpc::InsecureChannelCredentials()
                );
                
                auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
                if (!channel_->WaitForConnected(deadline)) {
                    std::cerr << "[TicketManager] Failed to connect to server. Retrying in 5 seconds...\n";
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    continue;
                }
                
                stub_ = vehicle::TicketSync::NewStub(channel_);
                
                vehicle::SubscribeForNewTicketsRequest request;
                
                std::cout << "[TicketManager] Connected. Waiting for new tickets...\n";
                
                grpc::ClientContext context;
                {
                    std::lock_guard<std::mutex> lock(context_mutex_);
                    current_context_ = &context;
                }
                
                std::unique_ptr<grpc::ClientReader<vehicle::SubscribeForNewTicketsResponse>> reader(
                    stub_->SubscribeForNewTickets(&context, request)
                );
                
                // Read stream
                vehicle::SubscribeForNewTicketsResponse response;
                while (running_ && reader->Read(&response)) {
                    if (response.has_new_ticket_created()) {
                        std::cout << "[TicketManager] New ticket received\n";
                        
                        // Convert and insert ticket
                        Ticket ticket = ConvertFromProto(response.new_ticket_created());
                        
                        if (InsertTicket(ticket)) {
                            std::cout << "[TicketManager] Successfully stored ticket ID: " 
                                     << ticket.ticket_id << "\n";
                        } else {
                            std::cerr << "[TicketManager] Failed to store ticket ID: " 
                                     << ticket.ticket_id << "\n";
                        }
                    }
                }
                
                // Clear context pointer
                {
                    std::lock_guard<std::mutex> lock(context_mutex_);
                    current_context_ = nullptr;
                }

                // Check status
                grpc::Status status = reader->Finish();
                if (!status.ok() && status.error_code() != grpc::StatusCode::CANCELLED) {
                    std::cerr << "[TicketManager] Stream ended: " << status.error_code() 
                            << " - " << status.error_message() << "\n";
                    
                    if (running_) {
                        std::cout << "[TicketManager] Reconnecting in 5 seconds...\n";
                        std::this_thread::sleep_for(std::chrono::seconds(5));
                    }
                } else if (status.error_code() == grpc::StatusCode::CANCELLED) {
                    std::cout << "[TicketManager] Stream cancelled (shutdown requested)\n";
                }
                
            } catch (const std::exception& e) {
                std::cerr << "[TicketManager] Error: " << e.what() << "\n";
                if (running_) {
                    std::cout << "[TicketManager] Retrying in 5 seconds...\n";
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                }
            }
        }
    }

    Ticket TicketManager::ConvertFromProto(const vehicle::Ticket& proto_ticket)
    {
        Ticket ticket;
        
        ticket.ticket_id = proto_ticket.id();
        ticket.active = proto_ticket.active();
        
        if (proto_ticket.has_date_created()) {
            ticket.date_created = TimestampToString(proto_ticket.date_created());
        }
        
        if (proto_ticket.has_account_id()) {
            ticket.account_id = proto_ticket.account_id().value();
        }
        
        ticket.caption = proto_ticket.caption();
        
        if (proto_ticket.has_valid_from()) {
            ticket.valid_from = TimestampToString(proto_ticket.valid_from());
        }
        
        if (proto_ticket.has_valid_to()) {
            ticket.valid_to = TimestampToString(proto_ticket.valid_to());
        }
        
        if (proto_ticket.has_traffic_zone()) {
            ticket.traffic_zone = proto_ticket.traffic_zone().value();
        }
        
        if (proto_ticket.has_article_id()) {
            ticket.article_id = proto_ticket.article_id().value();
        }
        
        if (proto_ticket.has_invoice_item_id()) {
            ticket.invoice_item_id = proto_ticket.invoice_item_id().value();
        }
        
        if (proto_ticket.has_token()) {
            ticket.token = proto_ticket.token().value();
        }
        
        return ticket;
    }


    bool TicketManager::InsertTicket(const Ticket& ticket)
    {
        char* err_msg = nullptr;
        if (sqlite3_exec(db_.get(), "BEGIN IMMEDIATE;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
            std::cerr << "[TicketManager] Failed to begin transaction: " << err_msg << '\n';
            sqlite3_free(err_msg);
            return false;
        }

        const char* sql = 
            "INSERT OR REPLACE INTO tickets (ticket_id, active, date_created, account_id, "
            "caption, valid_from, valid_to, traffic_area, traffic_zone, "
            "article_id, invoice_item_id, token) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_.get(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cerr << "[TicketManager] Failed to prepare statement: " 
                     << sqlite3_errmsg(db_.get()) << '\n';
            sqlite3_exec(db_.get(), "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        // Bind parameters
        sqlite3_bind_int64(stmt, 1, ticket.ticket_id);
        sqlite3_bind_int(stmt, 2, ticket.active ? 1 : 0);
        sqlite3_bind_text(stmt, 3, ticket.date_created.c_str(), -1, SQLITE_TRANSIENT);

        if (ticket.account_id) {
            sqlite3_bind_int(stmt, 4, *ticket.account_id);
        } else {
            sqlite3_bind_null(stmt, 4);
        }

        sqlite3_bind_text(stmt, 5, ticket.caption.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, ticket.valid_from.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, ticket.valid_to.c_str(), -1, SQLITE_TRANSIENT);
        if (ticket.valid_from.empty()) {
            sqlite3_bind_null(stmt, 6);
        } else {
            sqlite3_bind_text(stmt, 6, ticket.valid_from.c_str(), -1, SQLITE_TRANSIENT);
        }

        if (ticket.valid_to.empty()) {
            sqlite3_bind_null(stmt, 7);
        } else {
            sqlite3_bind_text(stmt, 7, ticket.valid_to.c_str(), -1, SQLITE_TRANSIENT);
        }
        sqlite3_bind_text(stmt, 8, ticket.traffic_area.c_str(), -1, SQLITE_TRANSIENT);

        if (ticket.traffic_zone) {
            sqlite3_bind_int(stmt, 9, *ticket.traffic_zone);
        } else {
            sqlite3_bind_null(stmt, 9);
        }

        if (ticket.article_id) {
            sqlite3_bind_int(stmt, 10, *ticket.article_id);
        } else {
            sqlite3_bind_null(stmt, 10);
        }

        if (ticket.invoice_item_id) {
            sqlite3_bind_int(stmt, 11, *ticket.invoice_item_id);
        } else {
            sqlite3_bind_null(stmt, 11);
        }

        sqlite3_bind_text(stmt, 12, ticket.token.c_str(), -1, SQLITE_TRANSIENT);

        bool success = (sqlite3_step(stmt) == SQLITE_DONE);

        sqlite3_finalize(stmt);

        if (!success) {
            std::cerr << "[TicketManager] Failed to insert ticket: " 
                     << sqlite3_errmsg(db_.get()) << '\n';
            sqlite3_exec(db_.get(), "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        if (sqlite3_exec(db_.get(), "COMMIT;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
            std::cerr << "[TicketManager] Failed to commit transaction: " << err_msg << '\n';
            sqlite3_free(err_msg);
            return false;
        }


        int log_size, checkpointed;
        int rc = sqlite3_wal_checkpoint_v2(
            db_.get(),
            nullptr,  // All databases
            SQLITE_CHECKPOINT_FULL,  
            &log_size,
            &checkpointed
        );

        if (rc != SQLITE_OK) {
            std::cerr << "[TicketManager] Warning: WAL checkpoint failed: " 
                     << sqlite3_errmsg(db_.get()) << '\n';
        } else {
            std::cout << "[TicketManager] WAL checkpoint: " << checkpointed 
                     << "/" << log_size << " frames checkpointed\n";
        }

        return true;
    }


    std::string TicketManager::TimestampToString(const google::protobuf::Timestamp& ts)
    {
        time_t seconds = ts.seconds();
        struct tm* tm_info = localtime(&seconds);
        char buffer[26];
        strftime(buffer, 26, "%Y-%m-%dT%H:%M:%S", tm_info);
        return std::string(buffer);
    }
}