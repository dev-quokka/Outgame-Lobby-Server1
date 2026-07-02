#pragma once
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <string>
#include <chrono>

#include "FailCode.h"

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
	char token[MAX_JWT_TOKEN_LEN] = {};   // JWT 토큰
	char userId[MAX_USER_ID_LEN] = {};
};

struct USER_LOBBY_CONNECT_RESPONSE : PACKET_HEADER {
	bool isSuccess = false;
};



// ************* FRIEND *************

// 유저 검색 패킷
struct USER_SEARCH_REQUEST : PACKET_HEADER {
	char userId[MAX_USER_ID_LEN] = {};
};

struct USER_SEARCH_RESPONSE : PACKET_HEADER {
	char     userId[MAX_USER_ID_LEN] = {};
	uint16_t userLevel = 0;
	uint8_t  onlineStatus = 0;  // 0=오프라인, 1=로비, 2=게임중
	bool     isSuccess = false;  // 유저 없으면 false
};


// 친구 요청 패킷
struct FRIEND_REQUEST_REQUEST : PACKET_HEADER {
	char targetId[MAX_USER_ID_LEN] = {};  // 요청 보낼 유저 ID
};

struct FRIEND_REQUEST_RESPONSE : PACKET_HEADER {
	char    targetId[MAX_USER_ID_LEN] = {};
	bool    isSuccess = false;
	uint8_t failCode = 0;
	// 0=성공, 1=이미 친구, 2=이미 요청중, 3=유저 없음, 4=서버오류
};


// 친구 요청 알림 패킷 (상대방이 받는 패킷)
struct FRIEND_REQUEST_NOTIFY : PACKET_HEADER {
	char     senderId[MAX_USER_ID_LEN] = {};
	uint32_t senderPk = 0; // 상태가 바뀐 친구 pk
	uint16_t senderLevel = 0;
	uint8_t  onlineStatus = 1;  // 0=오프라인, 1=로비, 2=게임중
};

// 친구 요청 수락/거부 알림 패킷
struct FRIEND_ACCEPT_NOTIFY : PACKET_HEADER {
	uint32_t friendPk = 0;  // 상태가 바뀐 친구 pk
	uint8_t  accept = 0;  // 0=수락, 1=거절
};


// 친구 온/오프/게임중 상태 변경 알림 패킷
struct FRIEND_STATUS_NOTIFY : PACKET_HEADER {
	uint32_t friendPk = 0;  // 상태가 바뀐 친구 pk
	uint8_t  onlineStatus = 0;  // 0=오프라인, 1=로비, 2=게임중
};

// 받은 친구 요청에 대한 응답 패킷 (수락했는지 거절했는지)
struct FRIEND_ACTION_REQUEST : PACKET_HEADER {
	char    targetId[MAX_USER_ID_LEN] = {};  // 대상 유저 ID
	uint8_t action = 0;  // 0=수락, 1=거절
};

struct FRIEND_ACTION_RESPONSE : PACKET_HEADER {
	char    targetId[MAX_USER_ID_LEN] = {};
	bool    isSuccess = false;
};

struct COSTUME_CHANGE_NOTIFY_TO_USER : PACKET_HEADER {
	char     userId[MAX_USER_ID_LEN] = {};
	uint32_t userPk = 0;
	uint32_t itemCode = 0;
	uint8_t  slot = 0;
};



// ************* COSTUME *************

// 코스튬 변경 요청
struct COSTUME_CHANGE_REQUEST : PACKET_HEADER {
	uint32_t itemCode = 0;  // 변경할 아이템 코드
	uint8_t  slot = 0;  // 1=head, 2=body, 3=legs, 4=feet
};

// 코스튬 변경 응답
struct COSTUME_CHANGE_RESPONSE : PACKET_HEADER {
	uint32_t itemCode = 0;
	uint8_t  slot = 0;
	uint8_t  failCode = 0; 	// 0=성공, 1=인벤에 없음, 2=잘못된 슬롯, 3=서버오류
	bool     isSuccess = false;
};

// 다른 파티원 옷 변경 전달 받는 패킷 (모든 파티원에게 전달)
struct COSTUME_CHANGE_NOTIFY : PACKET_HEADER {
	char     userId[MAX_USER_ID_LEN] = {};
	uint32_t userPk = 0;
	uint32_t itemCode = 0;
	uint8_t  slot = 0;
};



enum class PACKET_ID : uint16_t {

	// ======================= CENTER SERVER (1~ ) =======================

	// SYSTEM (1~)
	USER_LOBBY_CONNECT_REQUEST = 1,
	USER_LOBBY_CONNECT_RESPONSE = 2,


	USER_SEARCH_REQUEST = 25,
	USER_SEARCH_RESPONSE = 26,


	FRIEND_REQUEST_REQUEST = 30,
	FRIEND_REQUEST_RESPONSE = 31,
	FRIEND_REQUEST_NOTIFY = 32,

	FRIEND_STATUS_NOTIFY = 33,
	FRIEND_ACCEPT_NOTIFY = 34,

	FRIEND_ACTION_REQUEST = 35,
	FRIEND_ACTION_RESPONSE = 36,



	COSTUME_CHANGE_REQUEST = 51,
	COSTUME_CHANGE_RESPONSE = 52,

	COSTUME_CHANGE_NOTIFY = 55,
};