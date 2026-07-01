#pragma once
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <string>
#include <chrono>

constexpr uint16_t MAX_IP_LEN = 32;
constexpr uint16_t MAX_SERVER_USERS = 128;
constexpr uint16_t MAX_JWT_TOKEN_LEN = 257;

struct DataPacket {
	uint32_t dataSize;
	uint16_t connObjNum;
	DataPacket(uint32_t dataSize_, uint16_t connObjNum_) : dataSize(dataSize_), connObjNum(connObjNum_) {}
	DataPacket() = default;
};

struct PacketInfo
{
	uint16_t packetId = 0;
	uint16_t dataSize = 0;
	uint16_t connObjNum = 0;
	char* pData = nullptr;
};

struct PACKET_HEADER
{
	uint16_t PacketLength;
	uint16_t PacketId;
};


// ======================= LOBBY SERVER =======================

struct USER_LOBBY_CONNECT_REQUEST : PACKET_HEADER {
	char token[MAX_JWT_TOKEN_LEN];   // JWT 토큰
	char userId[MAX_USER_ID_LEN];
};

struct USER_LOBBY_CONNECT_RESPONSE : PACKET_HEADER {
	bool isSuccess = false;
};

struct FRIEND_STATUS_NOTIFY : PACKET_HEADER {
	uint32_t friendPk = 0;  // 상태가 바뀐 친구 pk
	uint8_t  onlineStatus = 0;  // 0=오프라인, 1=로비, 2=게임중
};

// 받은 친구 요청에 대한 응답 패킷 (수락했는지 거절했는지)
struct FRIEND_ACTION_REQUEST : PACKET_HEADER {
	char    targetId[MAX_USER_ID_LEN] = {};  // 대상 유저 ID
	uint8_t action = 0;  // 0=수락, 1=거절/삭제
};

struct FRIEND_ACTION_RESPONSE : PACKET_HEADER {
	char    targetId[MAX_USER_ID_LEN] = {};
	bool    isSuccess = false;
};


enum class PACKET_ID : uint16_t {

	// ======================= CENTER SERVER (1~ ) =======================

	// SYSTEM (1~)
	USER_LOBBY_CONNECT_REQUEST = 1,
	USER_LOBBY_CONNECT_RESPONSE = 2,


	FRIEND_STATUS_NOTIFY = 30,
	FRIEND_ACTION_REQUEST = 31,
	FRIEND_ACTION_RESPONSE = 32,

};