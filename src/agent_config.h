#ifndef AGENT_CONFIG_H
#define AGENT_CONFIG_H

#include <string>

namespace Stockfish {

struct AgentConfig {
    std::string api_key;
    std::string agent_name;
    std::string agent_group;
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

    // Defensive time management settings
    double time_usage_multiplier; // e.g., 0.9 to use only 90% of available time
    int time_safety_margin_ms;    // e.g., 500 to reserve 500ms as buffer

    // Provisioner mode settings
    bool provisioner_mode;
    std::string target_game_id;
    int overridden_elo;

    static AgentConfig load(int argc, char* argv[]);
};

} // namespace Stockfish

#endif // AGENT_CONFIG_H
