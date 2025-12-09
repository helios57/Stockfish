#ifndef AGENT_CONFIG_H
#define AGENT_CONFIG_H

#include <string>

namespace Stockfish {

struct AgentConfig {
    std::string api_key;
    std::string agent_name;
    std::string server;
    int server_port;
    bool use_tls;
    std::string game_mode;
    std::string time_control;
    bool wait_for_challenge;
    std::string specific_opponent_agent_id;
    bool auto_accept_draw;
    int skill_level;
    bool limit_strength;
    int elo;
    int hash;
    bool ponder;
    int multi_pv;
    int threads;

    static AgentConfig load();
};

} // namespace Stockfish

#endif // AGENT_CONFIG_H
