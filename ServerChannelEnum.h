#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>


//  =========================== SERVER INFO  ===========================

enum class ServerType : uint16_t {
	// Login Server (0)
	LoginServer = 0,

	// Lobby Server (1~)
	LobbyServer01 = 1,
	LobbyServer02 = 2
};

struct ServerAddress {
	std::string ip;
	uint16_t port;
};

inline std::unordered_map<ServerType, ServerAddress> ServerAddressMap = {
	{ ServerType::LoginServer,   { "127.0.0.1", 9001 } },
	{ ServerType::LobbyServer01, { "127.0.0.1", 9011 } },
	{ ServerType::LobbyServer02, { "127.0.0.1", 9012 } }
};

inline std::unordered_map<ServerType, std::string> ServerNamesMap = {
	{ ServerType::LoginServer,   "LoginServer" },
	{ ServerType::LobbyServer01, "LobbyServer01" },
	{ ServerType::LobbyServer02, "LobbyServer02" }
};

inline std::string GetServerName(const ServerType serverType_) {
	return ServerNamesMap[serverType_];
}

constexpr int SERVER_ID = 1;
constexpr ServerType SERVER_TYPE = ServerType::LobbyServer01;
