#include "OutGameLobbyServer.h"

// ========================== INITIALIZATION ===========================

bool OutGameLobbyServer::init() {
    int port_ = ServerAddressMap[ServerType::LoginServer].port;

    WSADATA wsadata;
    MaxThreadCnt = maxThreadCount; // Set the number of worker threads

    if (WSAStartup(MAKEWORD(2, 2), &wsadata)) {
        std::cout << "Failed to WSAStartup" << std::endl;
        return false;
    }

    serverSkt = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);
    if (serverSkt == INVALID_SOCKET) {
        std::cout << "Failed to Create Server Socket" << std::endl;
        return false;
    }

    SOCKADDR_IN addr;
    addr.sin_port = htons(port_);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(serverSkt, (SOCKADDR*)&addr, sizeof(addr))) {
        std::cout << "Failed to Bind :" << WSAGetLastError() << std::endl;
        return false;
    }

    if (listen(serverSkt, SOMAXCONN)) {
        std::cout << "Failed to listen" << std::endl;
        return false;
    }

    sIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MaxThreadCnt);
    if (sIOCPHandle == NULL) {
        std::cout << "Failed to Create IOCP Handle" << std::endl;
        return false;
    }

    auto bIOCPHandle = CreateIoCompletionPort((HANDLE)serverSkt, sIOCPHandle, (uint32_t)0, 0);
    if (bIOCPHandle == nullptr) {
        std::cout << "Failed to Bind IOCP Handle" << std::endl;
        return false;
    }

    overLappedManager = new OverLappedManager;
    overLappedManager->init();

    RedisManager::GetInstance().R("127.0.0.1", 6379); // 레디스 연결
    auto& redis = RedisManager::GetInstance().GetRedis();

    bool m = MySQLConnectionPool::GetInstance().init();
    if (!m) {

    }

    heartbeat_.Start();
    subscriber_.Start();

    return true;
}

bool OutGameLobbyServer::StartWork() {
    if (!CreateWorkThread()) {
        return false;
    }

    if (!CreateAccepterThread()) {
        return false;
    }

    connUsersManager = new ConnUsersManager(maxClientCount);

    for (int i = 0; i < maxClientCount; i++) { // Create a user object
        if (i == 11) continue;

        ConnUser* connUser = new ConnUser(MAX_CIRCLE_SIZE, i, sIOCPHandle, overLappedManager);

        AcceptQueue.push(connUser);
        connUsersManager->InsertUser(i, connUser);
    }

    return true;
}


void OutGameLobbyServer::ServerEnd() {
    WorkRun = false;
    AccepterRun = false;

    for (int i = 0; i < workThreads.size(); i++) {
        PostQueuedCompletionStatus(sIOCPHandle, 0, 0, nullptr);
    }

    for (int i = 0; i < workThreads.size(); i++) { // Shutdown worker threads
        if (workThreads[i].joinable()) {
            workThreads[i].join();
        }
    }
    for (int i = 0; i < acceptThreads.size(); i++) { // Shutdown accept threads
        if (acceptThreads[i].joinable()) {
            acceptThreads[i].join();
        }
    }

    heartbeat_.Stop();
    subscriber_.Stop();

    CloseHandle(sIOCPHandle);
    closesocket(serverSkt);
    WSACleanup();

    std::cout << "Wait 5 Seconds Before Shutdown" << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5)); // Wait 5 seconds before server shutdown
    std::cout << "Center Server Shutdown" << std::endl;
}


// ========================= THREAD MANAGEMENT =========================

bool OutGameLobbyServer::CreateWorkThread() {
    WorkRun = true;

    try {
        auto threadCnt = MaxThreadCnt;
        for (int i = 0; i < threadCnt; i++) {
            workThreads.emplace_back([this]() { WorkThread(); });
        }
    }
    catch (const std::system_error& e) {
        std::cerr << "Failed to Create Work Threads : " << e.what() << std::endl;
        return false;
    }

    return true;
}

bool OutGameLobbyServer::CreateAccepterThread() {
    AccepterRun = true;

    try {
        auto threadCnt = MaxThreadCnt / 4 + 1;
        for (int i = 0; i < threadCnt; i++) {
            workThreads.emplace_back([this]() { AccepterThread(); });
        }
    }
    catch (const std::system_error& e) {
        std::cerr << "Failed to Create Accepter Threads : " << e.what() << std::endl;
        return false;
    }

    return true;
}

void OutGameLobbyServer::WorkThread() {
    LPOVERLAPPED lpOverlapped = NULL;
    ConnUser* connUser = nullptr;
    DWORD dwIoSize = 0;
    bool gqSucces = TRUE;

    while (WorkRun) {
        gqSucces = GetQueuedCompletionStatus(
            sIOCPHandle,
            &dwIoSize,
            (PULONG_PTR)&connUser,
            &lpOverlapped,
            INFINITE
        );

        if (gqSucces && dwIoSize == 0 && lpOverlapped == NULL) {
            WorkRun = false;
            continue;
        }

        auto overlappedEx = (OverlappedEx*)lpOverlapped;
        uint16_t connObjNum = overlappedEx->connObjNum;
        connUser = connUsersManager->FindUser(connObjNum);

        if (!gqSucces || (dwIoSize == 0 && overlappedEx->taskType != TaskType::ACCEPT)) { // User Disconnected

            if (overlappedEx->taskType == TaskType::NEWRECV || overlappedEx->taskType == TaskType::NEWSEND) { // 새로 만든 오버랩이면 해제
                delete[] overlappedEx->wsaBuf.buf;
                delete overlappedEx;
            }
            else { // 풀에서 가져온거면 풀로 반납
                overLappedManager->returnOvLap(overlappedEx);
            }

            connUser->Reset(); // Reset 
            AcceptQueue.push(connUser);

            std::cout << "socket " << connUser->GetSocket() << " Disconnected" << std::endl;
            continue;
        }

        if (overlappedEx->taskType == TaskType::ACCEPT) { // User Connect
            if (connUser->ConnUserRecv()) {
                std::cout << "socket " << connUser->GetSocket() << " Connection Requset" << std::endl;
            }
            else { // Bind Fail
                connUser->Reset(); // Reset ConnUser
                AcceptQueue.push(connUser);
                std::cout << "socket " << connUser->GetSocket() << " Connection Fail" << std::endl;
            }
        }
        else if (overlappedEx->taskType == TaskType::RECV) {
            connUser->ConnUserRecv(); // Wsarecv Again
            overLappedManager->returnOvLap(overlappedEx);
        }
        else if (overlappedEx->taskType == TaskType::SEND) {
            overLappedManager->returnOvLap(overlappedEx);
            connUser->SendComplete();
        }
        else if (overlappedEx->taskType == TaskType::NEWRECV) {
            connUser->ConnUserRecv(); // Wsarecv Again
            delete[] overlappedEx->wsaBuf.buf;
            delete overlappedEx;
        }
        else if (overlappedEx->taskType == TaskType::NEWSEND) {
            connUser->SendComplete();
            delete[] overlappedEx->wsaBuf.buf;
            delete overlappedEx;
        }
    }
}

void OutGameLobbyServer::AccepterThread() {
    ConnUser* connUser;

    while (AccepterRun) {
        if (AcceptQueue.pop(connUser)) { // AcceptQueue not empty
            if (!connUser->PostAccept(serverSkt)) {
                AcceptQueue.push(connUser);
            }
        }
        else { // AcceptQueue empty
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}