// spolks_2.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
//
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <fstream>

#pragma comment(lib, "ws2_32.lib")

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

int main() {
    WSADATA wsaData;


    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Failed to initialize Winsock" << std::endl;
        return 1;
    }

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
    WSACleanup();

    return 0;
}