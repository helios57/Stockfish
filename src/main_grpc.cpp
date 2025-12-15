#include <iostream>
#include <memory>

#include "bitboard.h"
#include "nnue/features/full_threats.h"
#include "position.h"
#include "misc.h"

#include "agent_config.h"
#include "grpc_agent.h"

using namespace Stockfish;

int main(int argc, char* argv[]) {
    std::cout << engine_info() << std::endl;
    std::cout << "Starting gRPC agent..." << std::endl;

    Bitboards::init();
    Position::init();
    Eval::NNUE::Features::init_threat_offsets();

    AgentConfig config = AgentConfig::load(argc > 1 ? argv[1] : nullptr);
    
    GrpcAgent agent(config);
    agent.start(); // This blocks

    return 0;
}
