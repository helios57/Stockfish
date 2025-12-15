#ifndef PROVISIONER_AGENT_H
#define PROVISIONER_AGENT_H

#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>
#include "chess_contest.grpc.pb.h"
#include "agent_config.h"

namespace Stockfish {

class ProvisionerAgent {
public:
    ProvisionerAgent(const AgentConfig& cfg);
    ~ProvisionerAgent();

    // Starts the gRPC stream to RegisterProvisioner
    void run();

private:
    // Spawns a child process to handle a bot request
    void spawn_child_process(const chess_contest::SpawnBotRequest& request);

    AgentConfig config;
    std::shared_ptr<grpc::Channel> channel;
    std::unique_ptr<chess_contest::BotProvisioning::Stub> stub;
};

} // namespace Stockfish

#endif // PROVISIONER_AGENT_H
