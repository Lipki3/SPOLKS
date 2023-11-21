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
#include<functional>
#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>
#pragma comment(lib, "ws2_32.lib")


struct ClientInfo {
    std::string currentFile;
    int i;
};

std::map<std::string, ClientInfo> g_clients;
std::map<SOCKET, std::thread::id> socketToThreadMap;
//std::vector<std::thread> threadPool;
int const Nmin = 2;
int const Nmax = 5;
class ThreadPool {
public:
    ThreadPool(int numThreads, int maxThreads)
        : numThreads(numThreads), maxThreads(maxThreads), stop(false) {
        for (int i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queueMutex);
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });

                        if (stop && tasks.empty()) {
                            return;
                        }

                        task = tasks.front();
                        tasks.pop();
                    }
                    task();
                }
                });
        }
    }

    template <typename Function>
    void AddTask(Function&& func) {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            tasks.emplace(std::forward<Function>(func));
        }

        if (numThreads < maxThreads) {
            std::unique_lock<std::mutex> lock(threadMutex);
            if (numThreads < maxThreads) {
                workers.emplace_back([this] {
                    while (true) {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(queueMutex);
                            condition.wait(lock, [this] { return stop || !tasks.empty(); });

                            if (stop && tasks.empty()) {
                                return;
                            }

                            task = tasks.front();
                            tasks.pop();
                        }
                        task();
                    }
                    });
                ++numThreads;
            }
        }
        condition.notify_one();
    }
   

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers) {
            worker.join();
        }
    }

private:
    int numThreads;
    int maxThreads;
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::mutex threadMutex; // Для управления количеством потоков
    std::condition_variable condition;
    bool stop;

    
};



std::shared_ptr<ThreadPool> CreateThreadPool(int numThreads, int maxThreads) {
    return std::make_shared<ThreadPool>(numThreads, maxThreads);
}


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



int main() {
    std::shared_ptr<ThreadPool> pool = CreateThreadPool(Nmin,Nmax);
    WSADATA wsaData; // Инициализируем структуру для работы с Winsock.
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) { // Запускаем Winsock с версией 2.2.
        std::cerr << "Failed to initialize Winsock" << std::endl; // Если инициализация Winsock не удалась, выводим сообщение об ошибке.
        return 1; // Завершаем программу с кодом ошибки.
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0); // Создаем сокет для TCP/IP соединения.
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
    int j = 0;
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
      //  std::thread clientThread(HandleClient, clientSocket, clientIP);
      //  clientThread.detach(); // Отсоединяем поток, чтобы он мог работать независимо.

        pool->AddTask([j, clientSocket, clientIP] {
            auto i = std::this_thread::get_id();
            std::cout << "Task " << j << " is executed by thread " << std::this_thread::get_id() << std::endl;
            //socketToThreadMap[clientSocket] = std::this_thread::;
            HandleClient(clientSocket, clientIP);
            });
        j++;

    }

    closesocket(serverSocket); // Закрываем соксет сервера.
    WSACleanup(); // Освобождаем ресурсы Winsock.

    return 0; // Завершаем программу с кодом успешного выполнения.
}