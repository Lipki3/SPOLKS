#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ctime>

#pragma comment(lib, "ws2_32.lib")

#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <winsock2.h>
#include <fstream>
#include <map>
#include <mutex>
#pragma comment(lib, "ws2_32.lib")

struct ClientInfo {
    std::string currentFile;
    int i;
};

std::map<std::string, ClientInfo> g_clients;

struct Packet {
    int sequenceNumber;
    std::vector<char> data;
};

const int HEARTBEAT_TIMEOUT = 10000; // Таймаут на получение контрольного пакета (10 секунд).

bool IsClientConnected(SOCKET clientSocket) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(clientSocket, &fds);

    // Установите тайм-аут на 0 секунд и 0 миллисекунд для проверки немедленно.
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    int result = select(0, &fds, nullptr, nullptr, &timeout);
    if (result == SOCKET_ERROR) {
        // Обработайте ошибку select() здесь, если это необходимо.
        return false;
    }

    // Если соксет находится в наборе, это означает, что клиент подключен.
    return FD_ISSET(clientSocket, &fds) != 0;
}
void ReceiveFile(SOCKET clientSocket, const std::string& filename) {
    std::ofstream outputFile(filename, std::ios::binary | std::ios::out);
    if (!outputFile) {
        std::cerr << "Failed to open file for upload" << std::endl;

        return;
    }

    char buffer[1024];
    int bytesRead;
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
            // Прошло время ожидания без получения данных от клиента.
            std::cerr << "Receive timeout" << std::endl;
            break;
        }

        bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (bytesRead <= 0) {
            // Проверяем, если bytesRead меньше или равно нулю,
            // что может указывать на ошибку или закрытие соединения клиента.
            break;
        }
        outputFile.write(buffer, bytesRead);
    }

    outputFile.close();

    if (bytesRead < 0) {
        std::cerr << "Receive error" << std::endl;
    }
    else {
        std::cout << "File received successfully" << std::endl;
    }
}


void SendFile(SOCKET clientSocket, const std::string& filename, std::string ip) {
    std::ifstream inputFile(filename, std::ios::binary);
    if (!inputFile) {
        std::cerr << "Failed to open file for download" << std::endl;

        std::string m = "THIS FILE IS NOT ON THE SERVER\n";
        int bytesSent = send(clientSocket, m.c_str(), m.size(), 0); // Отправляем сообщение клиенту.
        if (bytesSent == SOCKET_ERROR) {
            std::cerr << "Send message (THIS FILE IS NOT ON THE SERVER) failed" << std::endl; // Если отправка данных не удалась, выводим сообщение об ошибке.
        }

        return;
    }
    ClientInfo clientInfo;
    clientInfo.currentFile = filename; // Пример установки значения currentFile



    char buffer[1024];
    int bytesRead;
    int i = 0;
    auto it = g_clients.find((std::string) ip);
    if (it != g_clients.end()) inputFile.seekg(g_clients[ip].i * sizeof(buffer), std::ios::beg);
    while (!inputFile.eof()) { //&& IsClientConnected(clientSocket)

        inputFile.read(buffer, sizeof(buffer));
        bytesRead = inputFile.gcount();
        if (bytesRead > 0) {
            send(clientSocket, buffer, bytesRead, 0);
            i++;
        }
        if (IsClientConnected(clientSocket)) {
            clientInfo.i = i;
            g_clients[ip] = clientInfo;
            inputFile.close();
            return;
        }
    }
    g_clients.erase(ip);
    inputFile.close();
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
         timeval timeout;
         fd_set readSet;
        while (true) {
            // Установка времени ожидания.
           
            timeout.tv_sec = 5; // Например, 5 секунд.
            timeout.tv_usec = 0;

            
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

void SendFileUsingUDP(SOCKET udpSocket, const std::string& filename, const sockaddr_in& clientAddr) {
    std::ifstream inputFile(filename, std::ios::binary);
    if (!inputFile) {
        std::cerr << "Failed to open file for download" << std::endl;

        std::string m = "THIS FILE IS NOT ON THE SERVER\n";
        int bytesSent = sendto(udpSocket, m.c_str(), m.size(), 0, (sockaddr*)&clientAddr, sizeof(clientAddr));
        if (bytesSent == SOCKET_ERROR) {
            std::cerr << "Send message (THIS FILE IS NOT ON THE SERVER) failed" << std::endl;
        }

        return;
    }

    char buffer[1024];
    int bytesRead;
    int i = 0;

    while (!inputFile.eof()) {
        inputFile.read(buffer, sizeof(buffer));
        bytesRead = inputFile.gcount();

        if (bytesRead > 0) {
            int bytesSent = sendto(udpSocket, buffer, bytesRead, 0, (sockaddr*)&clientAddr, sizeof(clientAddr));
            if (bytesSent == SOCKET_ERROR) {
                std::cerr << "Send file data failed" << std::endl;
                break;
            }
            i++;
        }
    }

    inputFile.close();

    if (bytesRead < 0) {
        std::cerr << "Read file error" << std::endl;
    }

    std::cout << "File sent successfully" << std::endl;
}

void ReceiveFileUsingUDP(SOCKET udpSocket, const std::string& filename) {
    std::ofstream outputFile(filename, std::ios::binary | std::ios::out);
    if (!outputFile) {
        std::cerr << "Failed to open file for upload" << std::endl;
        return;
    }

    char buffer[1024];
    int bytesRead=0;
    sockaddr_in fromAddr;
    int fromAddrSize = sizeof(fromAddr);
    fd_set readSet;
    timeval timeout;

    while (true) {
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
        bytesRead = recvfrom(udpSocket, buffer, sizeof(buffer), 0, (sockaddr*)&fromAddr, &fromAddrSize);

        if (bytesRead == SOCKET_ERROR) {
            std::cerr << "Receive error" << std::endl;
            break;
        }

        if (bytesRead == 0) {
            // Клиент закончил передачу.
            break;
        }

        outputFile.write(buffer, bytesRead);
    }

    

    outputFile.close();

    if (bytesRead < 0) {
        std::cerr << "Receive error" << std::endl;
    }
    else {
        std::cout << "File received successfully" << std::endl;
    }
}

void HandleClient(SOCKET clientSocket, std::string ip) {
    char buffer[1024]; // Создаем буфер для приема данных от клиента.
    memset(buffer, 0, sizeof(buffer)); // Заполняем буфер нулями.

    std::string requestData;  // Создаем строку для хранения команды от клиента.


    // Читаем данные от клиента, пока не встретим символ новой строки
    while (true) {
        while (true) {
            int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0); // Принимаем данные от клиента.
            if (bytesRead == SOCKET_ERROR) {
                std::cerr << "Receive error" << std::endl; // Если прием данных не удался, выводим сообщение об ошибке.
                closesocket(clientSocket); // Закрываем соксет клиента.
                return;
            }
            else if (bytesRead == 0) {
                // Клиент закрыл соединение
                closesocket(clientSocket); // Закрываем соксет клиента, если он закрыл соединение.
                return;
            }

            buffer[bytesRead] = '\0'; // Добавляем нулевой символ, чтобы сделать буфер строкой.
            requestData += buffer;

            // Если найден символ новой строки, это означает конец команды.
            if (requestData.find('\n') != std::string::npos) {
                break;
            }
        }

        // Обрезаем символ новой строки, если он есть в конце.
        size_t newlinePos = requestData.find('\n');
        if (newlinePos != std::string::npos) {
            requestData.erase(newlinePos);
        }

        // Обрабатываем команду requestData, как это делалось ранее.

        if (requestData.find("ECHO") == 0) { // Если команда начинается с "ECHO".
            std::string message = requestData.substr(5); // Извлекаем текст сообщения.
            message += '\n'; // Добавляем символ новой строки.
            int bytesSent = send(clientSocket, message.c_str(), message.size(), 0); // Отправляем сообщение клиенту.
            if (bytesSent == SOCKET_ERROR) {
                std::cerr << "Send failed" << std::endl; // Если отправка данных не удалась, выводим сообщение об ошибке.
            }
        }
        else if (requestData.find("TIME") == 0) { // Если команда начинается с "TIME".
            time_t currentTime = time(nullptr); // Получаем текущее время.
            char timeBuffer[26];
            ctime_s(timeBuffer, sizeof(timeBuffer), &currentTime); // Преобразуем время в строку.
            std::string timeString(timeBuffer);
            timeString += '\n'; // Добавляем символ новой строки.
            int bytesSent = send(clientSocket, timeString.c_str(), timeString.size(), 0); // Отправляем время клиенту.
            if (bytesSent == SOCKET_ERROR) {
                std::cerr << "Send failed" << std::endl; // Если отправка данных не удалась, выводим сообщение об ошибке.
            }
        }
        else if (requestData.find("UPLOAD") == 0) { // Если команда начинается с "UPLOAD".
            std::string filename = requestData.substr(7); // Извлекаем имя файла.
            ReceiveFile(clientSocket, filename);
        }
        else if (requestData.find("DOWNLOAD") == 0) { // Если команда начинается с "DOWNLOAD".
            std::string filename = requestData.substr(9); // Извлекаем имя файла.
            SendFile(clientSocket, filename, ip);
        }
        else if (requestData.find("CLOSE") == 0 || requestData.find("EXIT") == 0 || requestData.find("QUIT") == 0) {
            closesocket(clientSocket); // Закрываем соксет клиента.
            std::cout << "Client disconnected\n"; // Выводим сообщение о отключении клиента.
            return;
        }
        else {
            std::string errorMessage = "Unknown command\n"; // Если команда не распознана.
            int bytesSent = send(clientSocket, errorMessage.c_str(), errorMessage.size(), 0); // Отправляем сообщение об ошибке клиенту.
            if (bytesSent == SOCKET_ERROR) {
                std::cerr << "Send failed" << std::endl; // Если отправка данных не удалась, выводим сообщение об ошибке.
            }
        }
        requestData = ""; // Очищаем строку запроса для следующей команды.
    }
}
bool condition = false;

std::atomic<bool> g_terminateHeartbeatThread = false;
void TimerCallback(SOCKET udpSocket) {
    // Здесь вы можете проверить ваше условие
     // Замените на ваше условие
    if (condition) {
        condition = false;
    }
    else {
        std::cout << "HEARTBEAT TIMEOUT!\n";
        g_terminateHeartbeatThread = true;
        
    }
}




int main() {
    WSADATA wsaData; // Инициализируем структуру для работы с Winsock.
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { // Запускаем Winsock с версией 2.2.
        std::cerr << "Failed to initialize Winsock" << std::endl; // Если инициализация Winsock не удалась, выводим сообщение об ошибке.
        return 1; // Завершаем программу с кодом ошибки.
    }

    int protocolChoice;
    std::cout << "Выберите протокол: " << std::endl;
    std::cout << "1. TCP" << std::endl;
    std::cout << "2. UDP" << std::endl;
    std::cin >> protocolChoice;

    if (protocolChoice != 1 && protocolChoice != 2) {
        std::cerr << "Неверный выбор протокола" << std::endl;
        WSACleanup();
        return 1;
    }

    if (protocolChoice == 1) {
        SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket == INVALID_SOCKET) { // Проверяем, успешно ли создан сокет.
            std::cerr << "Failed to create socket" << std::endl; // Если создание соксета не удалось, выводим сообщение об ошибке.
            return 1; // Завершаем программу с кодом ошибки.
        }

        sockaddr_in serverAddr; // Создаем структуру для хранения информации о сервере.
        serverAddr.sin_family = AF_INET; // Указываем семейство адресов (IPv4).
        serverAddr.sin_port = htons(23); // Устанавливаем порт (здесь порт 23 - порт Telnet, можно изменить).
        serverAddr.sin_addr.s_addr = INADDR_ANY; // Принимаем соединения с любых доступных интерфейсов.

        if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Bind failed" << std::endl; // Привязываем соксет к адресу и порту. Если не удалось, выводим сообщение об ошибке.
            closesocket(serverSocket); // Закрываем сокет.
            WSACleanup(); // Освобождаем ресурсы Winsock.
            return 1; // Завершаем программу с кодом ошибки.
        }

        if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) { // Начинаем слушать входящие соединения.
            std::cerr << "Listen failed" << std::endl; // Если слушать не удалось, выводим сообщение об ошибке.
            closesocket(serverSocket); // Закрываем сокет.
            WSACleanup(); // Освобождаем ресурсы Winsock.
            return 1; // Завершаем программу с кодом ошибки.
        }

        std::cout << "Server is listening on port 23" << std::endl; // Выводим сообщение о том, что сервер слушает на указанном порту.

        while (true) { // Бесконечный цикл для обработки клиентских соединений.
            SOCKET clientSocket = accept(serverSocket, nullptr, nullptr); // Принимаем входящее соединение.
            if (clientSocket == INVALID_SOCKET) { // Проверяем, успешно ли принято соединение.
                std::cerr << "Accept failed" << std::endl; // Если принятие соединения не удалось, выводим сообщение об ошибке.
                continue; // Продолжаем цикл и пытаемся принять следующее соединение.
            }
            sockaddr_in clientAddr; // Создаем структуру для хранения информации о клиенте.
            int clientAddrSize = sizeof(clientAddr);
            char* clientIP = NULL;
            if (getpeername(clientSocket, (sockaddr*)&clientAddr, &clientAddrSize) == 0) {
                clientIP = inet_ntoa(clientAddr.sin_addr); // Получаем IP-адрес клиента.
                int clientPort = ntohs(clientAddr.sin_port); // Получаем порт клиента.

                std::cout << "Client connected from " << clientIP << std::endl; // Выводим информацию о подключившемся клиенте.

            }
            auto it = g_clients.find((std::string) clientIP);

            if (it != g_clients.end()) {
                int bytesSent = send(clientSocket, g_clients[clientIP].currentFile.c_str(), g_clients[clientIP].currentFile.size(), 0); // Отправляем сообщение клиенту.
                if (bytesSent == SOCKET_ERROR) {
                    std::cerr << "Send failed" << std::endl; // Если отправка данных не удалась, выводим сообщение об ошибке.
                }
                SendFile(clientSocket, g_clients[clientIP].currentFile, (std::string) clientIP);
            }
            // Создаем новый поток для обработки клиента.
            std::thread clientThread(HandleClient, clientSocket, clientIP);
            clientThread.detach(); // Отсоединяем поток, чтобы он мог работать независимо.




        }
        closesocket(serverSocket); // Закрываем соксет сервера.
    }
    else if (protocolChoice == 2) {
        // Код для UDP
        while (true) {
        SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, 0); // Создаем сокет для UDP.

        if (udpSocket == INVALID_SOCKET) {
            std::cerr << "Failed to create socket" << std::endl;
            return 1;
        }

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(7891); // Устанавливаем порт (можно изменить).
        serverAddr.sin_addr.s_addr = INADDR_ANY;

        if (bind(udpSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Bind failed" << std::endl;
            closesocket(udpSocket);
            WSACleanup();
            return 1;
        }
        
        std::cout << "Server is listening on port 7891" << std::endl;
        int k = 0;
        std::thread heartbeatThread;
        while (true) {
            
            sockaddr_in clientAddr;
            int clientAddrSize = sizeof(clientAddr);
            char buffer[1024];
            memset(buffer, 0, sizeof(buffer)); // Заполняем буфер нулями.
            int bytesReceived = recvfrom(udpSocket, buffer, sizeof(buffer), 0, (sockaddr*)&clientAddr, &clientAddrSize);
           // if (bytesReceived == SOCKET_ERROR && g_terminateHeartbeatThread) {
            //  
            //    return 100;
           // }
            if (bytesReceived == SOCKET_ERROR) {
                std::cerr << "Receive failed" << std::endl;

                continue;
            }
            
            if (k == 0) {
                heartbeatThread = std::thread([udpSocket]() {
                    while (!g_terminateHeartbeatThread) {
                        TimerCallback(udpSocket);
                        std::this_thread::sleep_for(std::chrono::milliseconds(10000));
                    }
                    //if (g_terminateHeartbeatThread) {
                    //
                    //    closesocket(udpSocket);
                    //}
                    });
            }
            k = 1;
            
            
            char* clientIP = inet_ntoa(clientAddr.sin_addr);
            std::cout << "Data received from client " << clientIP <<std::endl;
            
            // Обработка данных в буфере
            std::string requestData(buffer, bytesReceived);

            // Обработка команд, как ранее
            if (requestData.find("ECHO") == 0) {
                std::string message = requestData.substr(5);
                message += '\n';
                int bytesSent = sendto(udpSocket, message.c_str(), message.size(), 0, (sockaddr*)&clientAddr, clientAddrSize);
                if (bytesSent == SOCKET_ERROR) {
                    std::cerr << "Send failed" << std::endl;
                }
            }
            else if (requestData.find("TIME") == 0) {
                time_t currentTime = time(nullptr); // Получаем текущее время.
                char timeBuffer[26];
                ctime_s(timeBuffer, sizeof(timeBuffer), &currentTime); // Преобразуем время в строку.
                std::string timeString(timeBuffer);
                timeString += '\n'; // Добавляем символ новой строки.
                int bytesSent = sendto(udpSocket, timeString.c_str(), timeString.size(), 0, (sockaddr*)&clientAddr, clientAddrSize);
                if (bytesSent == SOCKET_ERROR) {
                    std::cerr << "Send failed" << std::endl;
                }
            }
            else if (requestData.find("UPLOAD") == 0) {
                std::string filename = requestData.substr(7); // Извлекаем имя файла.
                //ReceiveFileUsingUDP(udpSocket, filename);
                ReceiveFileWithSequenceControl(udpSocket, filename, clientAddr);
            }
            else if (requestData.find("DOWNLOAD") == 0) {
                std::string filename = requestData.substr(9); // Извлекаем имя файла.
                //SendFileUsingUDP(udpSocket, filename, clientAddr); 
                SendFileWithSequenceControl(udpSocket, filename, clientAddr);
            }
            else if (requestData.find("CLOSE") == 0 || requestData.find("EXIT") == 0 || requestData.find("QUIT") == 0) {
                closesocket(udpSocket);
                std::cout << "Client disconnected\n";
            }
            else if (requestData.find("HEARTBEAT") == 0 ) {
                condition = true;
                
            }
            else {
                std::string errorMessage = "Unknown command\n";
                int bytesSent = sendto(udpSocket, errorMessage.c_str(), errorMessage.size(), 0, (sockaddr*)&clientAddr, clientAddrSize);
                if (bytesSent == SOCKET_ERROR) {
                    std::cerr << "Send failed" << std::endl;
                }
            }

        }
         // Закрываем соксет сервера.
        }
    }

    WSACleanup(); // Освобождаем ресурсы Winsock.

    return 0; // Завершаем программу с кодом успешного выполнения.
}

