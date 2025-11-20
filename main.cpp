#include "database.hpp"
#include "fetcher.hpp"
#include "coupons.hpp"
#include "articles.hpp"
#include "sender.hpp"
#include "ticket_manager.hpp"
#include "config.hpp"
#include <iostream>
#include <exception>
#include <string>
#include <csignal>
#include <thread>
#include <atomic>

Sender* g_sender = nullptr;
Tickets::TicketManager* g_ticket_manager = nullptr;
std::atomic<bool> g_running(true);

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down...\n";
    g_running = false;
    
    // Stop ticket manager first (gRPC client)
    if (g_ticket_manager) {
        std::cout << "[MAIN] Stopping ticket manager...\n";
        g_ticket_manager->Stop();
    }
    
    // Then stop sender (TCP server)
    if (g_sender) {
        std::cout << "[MAIN] Stopping TCP server...\n";
        g_sender->stop();
    }
}

void print_usage(const char* program_name) {
    std::cout << "Usage:\n";
    std::cout << "  " << program_name << " server [port] [grpc_addr]  - Start server (TCP + gRPC client)\n";
    std::cout << "      port: TCP port for validators (default: 8888)\n";
    std::cout << "      grpc_addr: gRPC ticket server (default: localhost:5109)\n\n";
    std::cout << "  " << program_name << " fetch coupon              - Fetch coupons from REST API\n";
    std::cout << "  " << program_name << " fetch articles            - Fetch articles from REST API\n";
    std::cout << "  " << program_name << " validate <card_id>        - Validate coupon by card_id\n";
}

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            print_usage(argv[0]);
            return 1;
        }

        std::string command = argv[1];

        std::cout << "Opening database...\n";
        Database db(config::DB_PATH);
        std::cout << "Database opened\n\n";

        if (command == "server") {

            int tcp_port = (argc >= 3) ? std::stoi(argv[2]) : config::DEFAULT_TCP_PORT;
            std::string grpc_server = (argc >= 4) ? argv[3] : std::string(config::GRPC_TICKET_SERVER);
            
            std::cout << "=== Starting OCU Service ===\n";
            std::cout << "TCP Port (for validators): " << tcp_port << "\n";
            std::cout << "gRPC Server (for tickets): " << grpc_server << "\n";
            std::cout << "============================\n\n";
            
            std::cout << "[MAIN] Starting Ticket Manager (gRPC client)...\n";
            Tickets::TicketManager ticket_manager(db, grpc_server);
            g_ticket_manager = &ticket_manager;
            ticket_manager.Start();
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            std::cout << "[MAIN] Starting TCP Server for validators...\n";
            Sender sender(db, tcp_port);
            g_sender = &sender;
            
            std::signal(SIGINT, signal_handler);
            std::signal(SIGTERM, signal_handler);
            
            g_running = true;
            
            std::thread sender_thread([&sender]() {
                sender.run();
            });
            
            std::cout << "[MAIN] All services started. Press Ctrl+C to stop.\n";
            
            // Wait for shutdown signal
            while (g_running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            // Clean shutdown
            std::cout << "[MAIN] Initiating shutdown...\n";
            sender.stop();
            ticket_manager.Stop();
            
            if (sender_thread.joinable()) {
                sender_thread.join();
            }
            
            std::cout << "[MAIN] All services stopped\n";
        }
        else if (command == "fetch" && argc >= 3) {
            std::string fetch_type = argv[2];
            
            if (fetch_type == "coupon") {
                std::cout << "=== Fetching Coupons from REST API ===\n";
                Coupons::CouponManager manager(db.get());
                
                if (manager.fetch_and_store(config::COUPON_ENDPOINT)) {
                    std::cout << "Success\n";
                } else {
                    std::cerr << "Failed\n";
                    return 1;
                }
            } 
            else if (fetch_type == "article" || fetch_type == "articles") {
                std::cout << "=== Fetching Articles from REST API ===\n";
                Articles::ArticleManager manager(db.get());
                
                if (manager.fetch_and_store(config::ARTICLES_ENDPOINT)) {
                    std::cout << "Success\n";
                } else {
                    std::cerr << "Failed\n";
                    return 1;
                }
            }
            else {
                std::cerr << "Unknown fetch type: " << fetch_type << "\n";
                print_usage(argv[0]);
                return 1;
            }
        }
        else if (command == "validate" && argc >= 3) {
            std::string card_number = argv[2];
            
            std::cout << "=== Validating: " << card_number << " ===\n";
            Coupons::CouponManager manager(db.get());
            
            if (manager.is_valid_card(card_number)) {
                auto coupons = manager.get_coupons_by_card(card_number);
                if (!coupons.empty()) {
                    std::cout << "VALID - Coupon ID: " << coupons[0].coupon_id << "\n";
                    std::cout << "  Customer ID: " << coupons[0].customer_id << "\n";
                    std::cout << "  Valid From: " << coupons[0].valid_from << "\n";
                    std::cout << "  Valid To: " << coupons[0].valid_to << "\n";
                } else {
                    std::cout << "VALID (but no coupon details found)\n";
                }
            } else {
                std::cout << "INVALID or EXPIRED\n";
                return 1;
            }
        }
        else {
            std::cerr << "Unknown command: " << command << "\n";
            print_usage(argv[0]);
            return 1;
        }

        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}