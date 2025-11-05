#include "ticket_client.hpp"

TicketClient::TicketClient(const std::string& server_address, int vehicle_id)
    : server_address_(server_address)
    , vehicle_id_(vehicle_id)
    , running_(true)
{
    std::cout << "========================================\n";
    std::cout << "TicketSync gRPC Client (C++)\n";
    std::cout << "========================================\n";
    std::cout << "Server: " << server_address_ << "\n";
    std::cout << "Vehicle ID: " << vehicle_id_ << "\n";
    std::cout << "========================================\n\n";
}

TicketClient::~TicketClient()
{
    Stop();
}

void TicketClient::Run()
{
    while (running_) {
        ConnectAndStream();
        
        if (running_) {
            std::cout << "→ Reconnecting in 5 seconds...\n\n";
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
}

void TicketClient::Stop()
{
    running_ = false;
    std::cout << "\n✓ Client stopped\n";
}

void TicketClient::ConnectAndStream()
{
    try {
        std::cout << "Connecting to server...\n";
        
        // Create channel
        channel_ = grpc::CreateChannel(
            server_address_,
            grpc::InsecureChannelCredentials()
        );
        
        stub_ = vehicle::TicketSync::NewStub(channel_);
        
        // Create request
        vehicle::SubscribeForNewTicketsRequest request;
        request.set_vehicle_id(vehicle_id_);
        
        // Start streaming
        grpc::ClientContext context;
        std::unique_ptr<grpc::ClientReader<vehicle::SubscribeForNewTicketsResponse>> reader(
            stub_->SubscribeForNewTickets(&context, request)
        );
        
        std::cout << "✓ Connected! Listening for tickets...\n\n";
        
        // Read stream
        vehicle::SubscribeForNewTicketsResponse response;
        while (running_ && reader->Read(&response)) {
            if (response.has_new_ticket_created()) {
                PrintTicket(response.new_ticket_created());
            }
        }
        
        // Check status
        grpc::Status status = reader->Finish();
        if (!status.ok()) {
            std::cerr << "✗ RPC failed: " << status.error_code() 
                     << " - " << status.error_message() << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "✗ Error: " << e.what() << "\n";
    }
}

void TicketClient::PrintTicket(const vehicle::Ticket& ticket)
{
    std::cout << "========================================\n";
    std::cout << "✓ NEW TICKET RECEIVED!\n";
    std::cout << "========================================\n";
    
    std::cout << "  ID: " << ticket.id() << "\n";
    std::cout << "  Active: " << (ticket.active() ? "Yes" : "No") << "\n";
    std::cout << "  Caption: " << ticket.caption() << "\n";
    
    if (ticket.has_date_created()) {
        std::cout << "  Created: " << TimestampToString(ticket.date_created()) << "\n";
    }
    
    if (ticket.has_valid_from()) {
        std::cout << "  Valid From: " << TimestampToString(ticket.valid_from()) << "\n";
    }
    
    if (ticket.has_valid_to()) {
        std::cout << "  Valid To: " << TimestampToString(ticket.valid_to()) << "\n";
    }
    
    if (ticket.has_account_id()) {
        std::cout << "  Account ID: " << ticket.account_id().value() << "\n";
    }
    
    if (ticket.has_traffic_zone()) {
        std::cout << "  Traffic Zone: " << ticket.traffic_zone().value() << "\n";
    }
    
    if (ticket.has_article_id()) {
        std::cout << "  Article ID: " << ticket.article_id().value() << "\n";
    }
    
    if (ticket.has_invoice_item_id()) {
        std::cout << "  Invoice Item ID: " << ticket.invoice_item_id().value() << "\n";
    }
    
    if (ticket.has_token()) {
        std::cout << "  Token: " << ticket.token().value() << "\n";
    }
    
    std::cout << "========================================\n\n";
}

std::string TicketClient::TimestampToString(const google::protobuf::Timestamp& ts)
{
    time_t seconds = ts.seconds();
    struct tm* tm_info = localtime(&seconds);
    char buffer[26];
    strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    return std::string(buffer);
}

// Main function
int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <server:port> <vehicle_id>\n";
        std::cout << "Example: " << argv[0] << " localhost:50051 123\n";
        return 1;
    }
    
    std::string server_address(argv[1]);
    int vehicle_id = std::stoi(argv[2]);
    
    // Create client
    TicketClient client(server_address, vehicle_id);
    
    // Handle Ctrl+C
    std::signal(SIGINT, [](int) {
        std::cout << "\n→ Shutting down...\n";
        std::exit(0);
    });
    
    // Run
    client.Run();
    
    return 0;
}