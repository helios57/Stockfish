#include "provisioner_agent.h"
#include <iostream>
#include <thread>
#include <sstream>
#include <cstdlib>
#include <unistd.h>

namespace Stockfish {

ProvisionerAgent::ProvisionerAgent(const AgentConfig& cfg) : config(cfg) {
    std::string target = config.server + ":" + std::to_string(config.server_port);
    std::shared_ptr<grpc::ChannelCredentials> creds;
    
    if (config.use_tls) {
        grpc::SslCredentialsOptions ssl_opts;
        creds = grpc::SslCredentials(ssl_opts);
    } else {
        creds = grpc::InsecureChannelCredentials();
    }
    
    channel = grpc::CreateChannel(target, creds);
    stub = chess_contest::BotProvisioning::NewStub(channel);
    
    std::cout << "ProvisionerAgent initialized for " << target << std::endl;
}

ProvisionerAgent::~ProvisionerAgent() {
    std::cout << "ProvisionerAgent shutting down." << std::endl;
}

void ProvisionerAgent::run() {
    std::cout << "Starting Provisioner mode - connecting to server..." << std::endl;
    
    grpc::ClientContext context;
    auto stream = stub->RegisterProvisioner(&context);
    
    if (!stream) {
        std::cerr << "Failed to create RegisterProvisioner stream." << std::endl;
        return;
    }
    
    // Send initial status message: READY
    chess_contest::ProvisionerMessage init_msg;
    init_msg.set_status(chess_contest::ProvisionerMessage::READY);
    init_msg.set_capacity(10); // Default capacity
    init_msg.set_api_key(config.api_key);
    
    std::cout << "[PROVISIONER SEND] ProvisionerMessage:\n"
              << "  status: READY\n"
              << "  capacity: " << init_msg.capacity() << "\n"
              << "  api_key: " << (config.api_key.empty() ? "[not set]" : "[set]") << std::endl;
    
    if (!stream->Write(init_msg)) {
        std::cerr << "Failed to send initial READY message." << std::endl;
        return;
    }
    
    std::cout << "Listening for spawn instructions from provisioner server..." << std::endl;
    
    // Loop to read instructions from server
    chess_contest::ProvisionerInstruction instruction;
    while (stream->Read(&instruction)) {
        std::cout << "\n[PROVISIONER RECV] ProvisionerInstruction:\n"
                  << "  instruction_id: " << instruction.instruction_id() << "\n"
                  << "  type: " << instruction.type() 
                  << " (" << (instruction.type() == chess_contest::ProvisionerInstruction::SPAWN_BOT ? "SPAWN_BOT" : "UNKNOWN") << ")" << std::endl;
        
        if (instruction.type() == chess_contest::ProvisionerInstruction::SPAWN_BOT) {
            if (instruction.has_payload()) {
                const auto& spawn_request = instruction.payload();
                std::cout << "[PROVISIONER RECV] SpawnBotRequest:\n"
                          << "  match_id: " << spawn_request.match_id() << "\n"
                          << "  target_elo: " << spawn_request.target_elo() << "\n"
                          << "  time_control: " << spawn_request.time_control() << "\n"
                          << "  fen: " << (spawn_request.has_fen() ? spawn_request.fen() : "[not set]") << std::endl;
                
                // Spawn child process to handle this bot
                spawn_child_process(spawn_request);
                
                // Send status update: still READY
                chess_contest::ProvisionerMessage status_msg;
                status_msg.set_status(chess_contest::ProvisionerMessage::READY);
                status_msg.set_capacity(10);
                status_msg.set_api_key(config.api_key);
                
                std::cout << "[PROVISIONER SEND] ProvisionerMessage:\n"
                          << "  status: READY\n"
                          << "  capacity: " << status_msg.capacity() << "\n"
                          << "  api_key: " << (config.api_key.empty() ? "[not set]" : "[set]") << std::endl;
                
                if (!stream->Write(status_msg)) {
                    std::cerr << "Failed to send status update after spawn." << std::endl;
                    break;
                }
            } else {
                std::cerr << "SPAWN_BOT instruction missing payload." << std::endl;
            }
        } else {
            std::cout << "Unknown instruction type: " << instruction.type() << std::endl;
        }
    }
    
    grpc::Status status = stream->Finish();
    if (!status.ok()) {
        std::cerr << "RegisterProvisioner RPC failed: " << status.error_code() 
                  << ": " << status.error_message() << std::endl;
    } else {
        std::cout << "RegisterProvisioner stream closed." << std::endl;
    }
}

void ProvisionerAgent::spawn_child_process(const chess_contest::SpawnBotRequest& request) {
    // Get current executable path
    char exe_path[1024];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        std::cerr << "Failed to get current executable path. Using './stockfish' as fallback." << std::endl;
        len = snprintf(exe_path, sizeof(exe_path), "./stockfish");
    }
    exe_path[len] = '\0';
    
    // Construct command string
    std::ostringstream cmd;
    cmd << exe_path << " "
        << config.agent_name << ".env"  // Use the same config file base
        << " --game-id " << request.match_id()
        << " --elo " << request.target_elo()
        << " --api-key " << config.api_key;
    
    std::string command = cmd.str();
    std::cout << "\n[SPAWN COMMAND] Executing child bot process:\n"
              << "  " << command << "\n" << std::endl;
    
    // Launch in a detached thread to avoid blocking the provisioner loop
    std::thread([command]() {
        int result = std::system(command.c_str());
        if (result == -1) {
            std::cerr << "[SPAWN ERROR] Failed to spawn child process: " << command << std::endl;
        } else {
            std::cout << "[SPAWN COMPLETE] Child process completed with exit code: " << result << std::endl;
        }
    }).detach();
}

} // namespace Stockfish
