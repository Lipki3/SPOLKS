// lab3-client.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
//
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>
#pragma comment(lib, "ws2_32.lib")

struct Packet {
    int sequenceNumber;
    std::vector<char> data;
};

const int HEARTBEAT_INTERVAL = 5000;  // Интервал отправки контрольных пакетов (5 секунд).

void SendHeartbeatPacket(SOCKET udpSocket, sockaddr_in serverAddr) {
    std::string heartbeatMessage = "HEARTBEAT"; // Пример сообщения контрольного пакета.
    int bytesSent = sendto(udpSocket, heartbeatMessage.c_str(), heartbeatMessage.size(), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));

    if (bytesSent == SOCKET_ERROR) {
        std::cerr << "Failed to send heartbeat packet" << std::endl;
    }
}
void UploadFile(SOCKET clientSocket, const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);

    if (!file.is_open()) {
        std::cerr << "Failed to open file for upload" << std::endl;
        return;
    }

    std::string uploadCommand = "UPLOAD " + filename + "\n";
    send(clientSocket, uploadCommand.c_str(), uploadCommand.size(), 0);

    char buffer[1024];
    while (!file.eof()) {
        file.read(buffer, sizeof(buffer));
        int bytesRead = file.gcount();
        send(clientSocket, buffer, bytesRead, 0);
    }

    file.close();
}

void DownloadFile(SOCKET clientSocket, const std::string& filename) {
    std::ofstream file(filename, std::ios::binary | std::ios::out);

    if (!file.is_open()) {
        std::cerr << "Failed to open file for download" << std::endl;
        return;
    }

    std::string downloadCommand = "DOWNLOAD " + filename + "\n";
    if (send(clientSocket, downloadCommand.c_str(), downloadCommand.size(), 0) == SOCKET_ERROR) {
        std::cerr << "Failed to send download command" << std::endl;
        file.close();
        return;
    }

    char buffer[1024];
    int bytesRead = -1;
    fd_set readSet;
    timeval timeout;

    while (true) {
        FD_ZERO(&readSet);
        FD_SET(clientSocket, &readSet);

        timeout.tv_sec = 5; // желаемое время ожидания в секундах.
        timeout.tv_usec = 0;

        int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);

        if (selectResult == SOCKET_ERROR) {
            std::cerr << "Select error" << std::endl;
            break;
        }
        else if (selectResult == 0) {
            // Прошло время ожидания без получения данных от сервера.
            std::cerr << "Receive timeout" << std::endl;
            break;
        }

        bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);
        std::string myString(buffer);
        if (myString.find("THIS FILE IS NOT ON THE SERVER\n") == 0) {
            std::cerr << "THIS FILE IS NOT ON THE SERVER\n" << std::endl;
            return;
        }
        if (bytesRead <= 0) {
            // Проверяем, если bytesRead меньше или равно нулю,
            // что может указывать на ошибку или закрытие соединения сервером.
            break;
        }
        file.write(buffer, bytesRead);
    }

    file.close();

    if (bytesRead < 0) {
        std::cerr << "Receive error" << std::endl;
    }
    else {
        std::cout << "File downloaded successfully" << std::endl;
    }
}
void Download2(SOCKET clientSocket, const std::string& filename) {
    std::ofstream file(filename, std::ios::binary | std::ios::out);

    if (!file.is_open()) {
        std::cerr << "Failed to open file for download" << std::endl;
        return;
    }



    char buffer[1024];
    int bytesRead = -1;
    fd_set readSet;
    timeval timeout;

    while (true) {
        FD_ZERO(&readSet);
        FD_SET(clientSocket, &readSet);

        timeout.tv_sec = 5; // Установите желаемое время ожидания в секундах.
        timeout.tv_usec = 0;

        int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);

        if (selectResult == SOCKET_ERROR) {
            std::cerr << "Select error" << std::endl;
            break;
        }
        else if (selectResult == 0) {
            // Прошло время ожидания без получения данных от сервера.
            std::cerr << "Receive timeout" << std::endl;
            break;
        }

        bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);

        if (bytesRead <= 0) {
            // Проверяем, если bytesRead меньше или равно нулю,
            // что может указывать на ошибку или закрытие соединения сервером.
            break;
        }
        file.write(buffer, bytesRead);
    }

    file.close();
    if (bytesRead < 0) {
        std::cerr << "Untransmitted file not found" << std::endl;
    }
    else {
        std::cout << "File was recieved successfully" << std::endl;
    }
}

void SendAcknowledgment(SOCKET udpSocket, const sockaddr_in& clientAddr, int sequenceNumber) {
    // Отправить подтверждение (ACK) обратно клиенту.
    char ackData[4];
    ackData[0] = sequenceNumber >> 24;
    ackData[1] = (sequenceNumber >> 16) & 0xFF;
    ackData[2] = (sequenceNumber >> 8) & 0xFF;
    ackData[3] = sequenceNumber & 0xFF;
    int bytesSent = sendto(udpSocket, ackData, sizeof(ackData), 0, (sockaddr*)&clientAddr, sizeof(clientAddr));
    if (bytesSent == SOCKET_ERROR) {
        std::cerr << "Send acknowledgment failed" << std::endl;
    }
}

void SendFileWithSequenceControl(SOCKET udpSocket, const std::string& filename, const sockaddr_in& serverAddr) {
    std::ifstream inputFile(filename, std::ios::binary);
    if (!inputFile) {
        std::cerr << "Failed to open file for upload" << std::endl;
        return;
    }

    char buffer[1024];
    int bytesRead;
    int sequenceNumber = 0;

    while (!inputFile.eof()) {

        // Чтение данных из файла.
        inputFile.read(buffer, sizeof(buffer));
        bytesRead = inputFile.gcount();

        // Создание пакета с номером последовательности.
        char packetData[1028];
        packetData[0] = sequenceNumber >> 24;
        packetData[1] = (sequenceNumber >> 16) & 0xFF;
        packetData[2] = (sequenceNumber >> 8) & 0xFF;
        packetData[3] = sequenceNumber & 0xFF;
        memcpy(packetData + 4, buffer, bytesRead);

        // Отправка пакета.
        int bytesSent = sendto(udpSocket, packetData, bytesRead + 4, 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
        if (bytesSent == SOCKET_ERROR) {
            std::cerr << "Send file data failed" << std::endl;
            break;
        }

        // Ожидание подтверждения от сервера.
        char ackData[4];
        int fromAddrSize = sizeof(serverAddr);
        int selectResult;
        int expectedSequenceNumber = sequenceNumber;

        while (true) {
            // Установка времени ожидания.
            timeval timeout;
            timeout.tv_sec = 5; // Например, 5 секунд.
            timeout.tv_usec = 0;

            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(udpSocket, &readSet);

            selectResult = select(0, &readSet, nullptr, nullptr, &timeout);

            if (selectResult == SOCKET_ERROR) {
                std::cerr << "Select error" << std::endl;
                break;
            }

            if (selectResult == 0) {
                // Прошло время ожидания без получения подтверждения.
                std::cerr << "Acknowledge timeout" << std::endl;
            }
            else {
                int bytesReceived = recvfrom(udpSocket, ackData, sizeof(ackData), 0, (sockaddr*)&serverAddr, &fromAddrSize);

                if (bytesReceived == SOCKET_ERROR) {
                    std::cerr << "Receive acknowledgment error" << std::endl;
                    break;
                }

                int receivedSequenceNumber = (static_cast<unsigned char>(ackData[0]) << 24) |
                    (static_cast<unsigned char>(ackData[1]) << 16) |
                    (static_cast<unsigned char>(ackData[2]) << 8) |
                    static_cast<unsigned char>(ackData[3]);

                if (receivedSequenceNumber == expectedSequenceNumber) {
                    // Правильное подтверждение получено.
                    break;
                }
            }
        }

        // Если подтверждение получено, увеличиваем номер последовательности.
        if (selectResult != SOCKET_ERROR) {
            sequenceNumber++;
        }
    }

    inputFile.close();

    if (bytesRead < 0) {
        std::cerr << "Read file error" << std::endl;
    }

    std::cout << "File uploaded successfully" << std::endl;
}

void ReceiveFileWithSequenceControl(SOCKET udpSocket, const std::string& filename, const sockaddr_in& clientAddr) {
    std::ofstream outputFile(filename, std::ios::binary);
    if (!outputFile) {
        std::cerr << "Failed to open file for download" << std::endl;
        return;
    }

    std::vector<Packet> receivedPackets;
    int expectedSequenceNumber = 0;
    fd_set readSet;
    timeval timeout;
    while (true) {
        char buffer[1028];
        int bytesRead;
        int fromAddrSize = sizeof(clientAddr);

        FD_ZERO(&readSet);
        FD_SET(udpSocket, &readSet);

        timeout.tv_sec = 5; // Установите желаемое время ожидания в секундах.
        timeout.tv_usec = 0;

        int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);

        if (selectResult == SOCKET_ERROR) {
            std::cerr << "Select error" << std::endl;
            break;
        }
        else if (selectResult == 0) {
            // Прошло время ожидания без получения данных от клиента.
            std::cerr << "Receive timeout" << std::endl;
            break;
        }
        bytesRead = recvfrom(udpSocket, buffer, sizeof(buffer), 0, (sockaddr*)&clientAddr, &fromAddrSize);
        if (bytesRead == 0) {
            // Завершение передачи.
            break;
        }
        if (bytesRead == SOCKET_ERROR) {
            std::cerr << "Receive error" << std::endl;
            break;
        }

        int sequenceNumber = (static_cast<unsigned char>(buffer[0]) << 24) |
            (static_cast<unsigned char>(buffer[1]) << 16) |
            (static_cast<unsigned char>(buffer[2]) << 8) |
            static_cast<unsigned char>(buffer[3]);

        if (sequenceNumber == expectedSequenceNumber) {
            // Пакет в правильной последовательности.
            Packet packet;
            packet.sequenceNumber = sequenceNumber;
            packet.data.assign(buffer + 4, buffer + bytesRead);
            receivedPackets.push_back(packet);
            
        }
       

        SendAcknowledgment(udpSocket, clientAddr, expectedSequenceNumber);
        
        while (!receivedPackets.empty() && receivedPackets[0].sequenceNumber == expectedSequenceNumber) {
            // Записываем данные в файл и удаляем пакет из буфера.
            outputFile.write(receivedPackets[0].data.data(), receivedPackets[0].data.size());
            receivedPackets.erase(receivedPackets.begin());
            expectedSequenceNumber++;
        }
    }

    outputFile.close();
    std::cout << "File received successfully" << std::endl;
}

void UploadFileUsingUDP(SOCKET udpSocket, const std::string& filename, const sockaddr_in& serverAddr) {
    std::ifstream inputFile(filename, std::ios::binary);
    if (!inputFile) {
        std::cerr << "Failed to open file for upload" << std::endl;
        return;
    }

    char buffer[1024];
    int bytesRead;

    while (!inputFile.eof()) {
        inputFile.read(buffer, sizeof(buffer));
        bytesRead = inputFile.gcount();

        if (bytesRead > 0) {
            int bytesSent = sendto(udpSocket, buffer, bytesRead, 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
            if (bytesSent == SOCKET_ERROR) {
                std::cerr << "Send file data failed" << std::endl;
                break;
            }
        }
    }

    inputFile.close();

    if (bytesRead < 0) {
        std::cerr << "Read file error" << std::endl;
    }

    std::cout << "File uploaded successfully" << std::endl;
}

void DownloadFileUsingUDP(SOCKET udpSocket, const std::string& filename, const sockaddr_in& serverAddr) {
    std::ofstream outputFile(filename, std::ios::binary);
    if (!outputFile) {
        std::cerr << "Failed to open file for download" << std::endl;
        return;
    }

    char buffer[1024];
    int bytesRead = 0;
    int totalBytesReceived = 0;
    fd_set readSet;
    timeval timeout;
    while (true) {

        
        int fromAddrSize = sizeof(serverAddr);
        bytesRead = recvfrom(udpSocket, buffer, sizeof(buffer), 0, (sockaddr*)&serverAddr, &fromAddrSize);

        if (bytesRead == SOCKET_ERROR) {
            std::cerr << "Receive error" << std::endl;
            break;
        }

        if (bytesRead == 0) {
            // Завершение передачи.
            break;
        }

        outputFile.write(buffer, bytesRead);
        totalBytesReceived += bytesRead;
    }

    outputFile.close();

    if (bytesRead < 0) {
        std::cerr << "Receive error" << std::endl;
    }
    else {
        std::cout << "File received successfully. Total bytes received: " << totalBytesReceived << std::endl;
    }
}

void HandleUDPClient(SOCKET udpSocket, const sockaddr_in& serverAddr) {
    char buffer[1024];
    int timeout = 5000; // 5 секунд.
    setsockopt(udpSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(int));

    std::string userInput;

    while (true) {
        std::cout << "Enter a command ('QUIT' to exit): ";
        rewind(stdin);
        std::getline(std::cin, userInput);

        if (userInput == "EXIT" || userInput == "QUIT" || userInput == "CLOSE") {
            break;
        }
        else if (userInput.find("UPLOAD") == 0) {
            sendto(udpSocket, userInput.c_str(), strlen(userInput.c_str()), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
            std::string filename = userInput.substr(7); // Извлекаем имя файла.
           //UploadFileUsingUDP(udpSocket, filename, serverAddr);
            SendFileWithSequenceControl(udpSocket, filename, serverAddr);
        }
        else if (userInput.find("DOWNLOAD") == 0) {
            sendto(udpSocket, userInput.c_str(), strlen(userInput.c_str()), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
            std::string filename = userInput.substr(9); // Извлекаем имя файла.
           //DownloadFileUsingUDP(udpSocket, filename, serverAddr);
            ReceiveFileWithSequenceControl(udpSocket, filename, serverAddr);
        }
        else {
            userInput += '\n'; // Добавляем символ новой строки.

            int bytesSent = sendto(udpSocket, userInput.c_str(), userInput.size(), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
            if (bytesSent == SOCKET_ERROR) {
                std::cerr << "Send failed" << std::endl;
                break;
            }

            sockaddr_in fromAddr;
            int fromAddrSize = sizeof(fromAddr);
            int bytesRead = recvfrom(udpSocket, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&fromAddr, &fromAddrSize);
            if (bytesRead == SOCKET_ERROR) {
                std::cerr << "Receive error" << std::endl;
                break;
            }

            buffer[bytesRead] = '\0';
            std::cout << "Server response: " << buffer << std::endl;
        }
    }
}


int main() {

    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock" << std::endl;
        return 1;
    }
    int protocolChoice;
    std::cout << "Choose protokol: " << std::endl;
    std::cout << "1. TCP" << std::endl;
    std::cout << "2. UDP" << std::endl;
    std::cin >> protocolChoice;

    if (protocolChoice != 1 && protocolChoice != 2) {
        std::cerr << "Неверный выбор протокола" << std::endl;
        WSACleanup();
        return 1;
    }

    if (protocolChoice == 1) {
        SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Failed to create socket" << std::endl;
            WSACleanup();
            return 1;
        }

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(23);
        serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // IP-адрес сервера.

        if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Failed to connect to server" << std::endl;
            closesocket(clientSocket);
            WSACleanup();
            return 1;
        }
        char buffer[1024];
        int bytesRead = -1;
        int timeout = 5000; // 5 секунд.
        setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(int));
        bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

        if (bytesRead == SOCKET_ERROR) {
        }
        else {
            std::string str(buffer);
            Download2(clientSocket, str.substr(0, bytesRead));
        }

        std::string userInput;

        while (true) {


            std::cout << "Enter a command ('QUIT' to exit): ";
            std::getline(std::cin, userInput);

            if (userInput == "EXIT" || userInput == "QUIT" || userInput == "CLOSE") {
                break;
            }
            else if (userInput.find("UPLOAD") == 0) {
                std::string filename = userInput.substr(7); // Извлекаем имя файла.
                UploadFile(clientSocket, filename);
            }
            else if (userInput.find("DOWNLOAD") == 0) {
                std::string filename = userInput.substr(9); // Извлекаем имя файла.
                DownloadFile(clientSocket, filename);
            }
            else {
                userInput += '\n'; // Добавляем символ новой строки.

                int bytesSent = send(clientSocket, userInput.c_str(), userInput.size(), 0);
                if (bytesSent == SOCKET_ERROR) {
                    std::cerr << "Send failed" << std::endl;
                    break;
                }

                int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
                if (bytesRead == SOCKET_ERROR) {
                    std::cerr << "Receive error" << std::endl;
                    break;
                }

                buffer[bytesRead] = '\0';
                std::cout << "Server response: " << buffer << std::endl;
            }
        }
        closesocket(clientSocket);
    }
    else if (protocolChoice == 2) {
        SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
        if (udpSocket == INVALID_SOCKET) {
            std::cerr << "Failed to create socket" << std::endl;
            WSACleanup();
            return 1;
        }

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(7891);
        serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // IP-адрес сервера.
        std::thread heartbeatThread([&]() {
            while (true) {
                SendHeartbeatPacket(udpSocket,serverAddr);
                std::this_thread::sleep_for(std::chrono::milliseconds(HEARTBEAT_INTERVAL));
            }
            });
        
        HandleUDPClient(udpSocket, serverAddr);

        closesocket(udpSocket);
    }
    
    WSACleanup();

    return 0;
}