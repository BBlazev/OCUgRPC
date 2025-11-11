#include "ticket_client.hpp"

TicketClient::TicketClient(const std::string& server_address)
    : server_address_(server_address)
    , running_(true)
{
    std::cout << "========================================\n";
    std::cout << "TicketSync gRPC Client (C++)\n";
    std::cout << "========================================\n";
    std::cout << "Server: " << server_address_ << "\n";
    std::cout << "========================================\n\n";
}

TicketClient::~TicketClient()
{
    Stop();
}

void TicketClient::Run()
{
    ConnectAndStream();
}

void TicketClient::Stop()
{
    running_ = false;
    std::cout << "\nChannel closed. Exiting.\n";
}

void TicketClient::ConnectAndStream()
{
    try {
        std::cout << "[INFO] Connecting to gRPC server at " << server_address_ << "...\n";
        
        // Create channel
        channel_ = grpc::CreateChannel(
            server_address_,
            grpc::InsecureChannelCredentials()
        );
        
        stub_ = vehicle::TicketSync::NewStub(channel_);
        
        // Create request (empty, no vehicle_id like Python version)
        vehicle::SubscribeForNewTicketsRequest request;
        
        std::cout << "[INFO] Subscription request sent. Waiting for new tickets...\n";
        
        // Start streaming
        grpc::ClientContext context;
        std::unique_ptr<grpc::ClientReader<vehicle::SubscribeForNewTicketsResponse>> reader(
            stub_->SubscribeForNewTickets(&context, request)
        );
        
        // Read stream
        vehicle::SubscribeForNewTicketsResponse response;
        while (running_ && reader->Read(&response)) {
            if (response.has_new_ticket_created()) {
                std::cout << "[INFO] New ticket received\n";
                PrintTicket(response.new_ticket_created());
            }
        }
        
        // Check status
        grpc::Status status = reader->Finish();
        if (!status.ok()) {
            std::cerr << "[ERROR] gRPC error: " << status.error_code() 
                     << " - " << status.error_message() << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Error: " << e.what() << "\n";
    }
}

void TicketClient::PrintTicket(const vehicle::Ticket& ticket)
{
    std::cout << "\n--- New Ticket ---\n";
    std::cout << "  ID: " << ticket.id() << "\n";
    std::cout << "  Active: " << (ticket.active() ? "true" : "false") << "\n";
    
    if (ticket.has_date_created()) {
        std::cout << "  Date Created: " << TimestampToString(ticket.date_created()) << "\n";
    }
    
    if (ticket.has_account_id()) {
        std::cout << "  Account ID: " << ticket.account_id().value() << "\n";
    }
    
    std::cout << "  Caption: " << ticket.caption() << "\n";
    
    if (ticket.has_valid_from()) {
        std::cout << "  Valid From: " << TimestampToString(ticket.valid_from()) << "\n";
    }
    
    if (ticket.has_valid_to()) {
        std::cout << "  Valid To: " << TimestampToString(ticket.valid_to()) << "\n";
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
    
    std::cout << std::string(50, '-') << "\n";
}

std::string TicketClient::TimestampToString(const google::protobuf::Timestamp& ts)
{
    time_t seconds = ts.seconds();
    struct tm* tm_info = localtime(&seconds);
    char buffer[26];
    strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);
    return std::string(buffer);
}

// Signal handler
static TicketClient* global_client = nullptr;

void signal_handler(int signal)
{
    if (global_client) {
        std::cout << "\n[WARNING] Interrupted by user. Closing connection...\n";
        global_client->Stop();
    }
    std::exit(0);
}

// Main function
int main(int argc, char** argv)
{
    std::string server_address = "localhost:5109";
    
    if (argc >= 2) {
        server_address = argv[1];
    }
    
    // Create client
    TicketClient client(server_address);
    global_client = &client;
    
    // Handle Ctrl+C
    std::signal(SIGINT, signal_handler);
    
    // Run
    client.Run();
    
    return 0;
}