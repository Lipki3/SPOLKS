// lab4-server2.cpp : Этот файл содержит функцию "main". Здесь начинается и заканчивается выполнение программы.
//
#define _CRT_SECURE_NO_WARNINGS
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
#include <stack>
#pragma comment(lib, "ws2_32.lib")

struct ClientInfo {
    std::string currentFile;
    int i;
};

std::map<std::string, ClientInfo> g_clients;

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
    fd_set readSet;
    timeval timeout;
    while (true) {
        while (true) {
            FD_ZERO(&readSet);
            FD_SET(clientSocket, &readSet);

            timeout.tv_sec = 120; // Установите желаемое время ожидания в секундах.
            timeout.tv_usec = 0;

            int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);

            if (selectResult == SOCKET_ERROR) {
                std::cerr << "Select error" << std::endl;
                break;
            }
            else if (selectResult == 0) {
                // Прошло время ожидания без получения данных от сервера.
                std::cerr << "Client timed out\n";
                closesocket(clientSocket);
                return;
            }
            int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0); // Принимаем данные от клиента.
            if (bytesRead == SOCKET_ERROR) {
                std::cerr << "Receive errorrrrrrrrrrr:" << clientSocket << '\n'<<ip<< '\n' << WSAGetLastError() << std::endl; // Если прием данных не удался, выводим сообщение об ошибке.
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


const int Nmin = 2;
const int Nmax = 3;
static int num = 2;
struct ProcessINF {
    SOCKET clientSocket;  // Сокет клиента
    std::string clientIP; // IP-адрес клиента
    DWORD pid;
};
std::stack<ProcessINF> Stack;


int HandleProcess() {
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
    serverAddr.sin_port = htons(24); // Устанавливаем порт (здесь порт 23 - порт Telnet, можно изменить).
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

    std::cout << "Server is listening on port 24" << std::endl; // Выводим сообщение о том, что сервер слушает на указанном порту.

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

            std::cout << "Process connected from " << clientIP << std::endl; // Выводим информацию о подключившемся клиенте.

        }
        ProcessINF a;
        a.clientIP = clientIP;
        a.clientSocket = clientSocket;
        a.pid = GetCurrentProcessId();
        Stack.push(a);
       
    }

    closesocket(serverSocket); // Закрываем соксет сервера.
    WSACleanup(); // Освобождаем ресурсы Winsock.
    return 0;
}

SOCKET ConvertProcessSocket(SOCKET oldsocket, DWORD source_pid)
{
    HANDLE source_handle = OpenProcess(PROCESS_ALL_ACCESS,
        FALSE, source_pid);
    HANDLE newhandle;
    DuplicateHandle(source_handle, (HANDLE)oldsocket,
        GetCurrentProcess(), &newhandle, 0, FALSE,
        DUPLICATE_CLOSE_SOURCE | DUPLICATE_SAME_ACCESS);
    CloseHandle(source_handle);
    return (SOCKET)newhandle;
}

int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "Russian");
    if (argc == 1) {


        std::thread processThread(HandleProcess);
       processThread.detach(); // Отсоединяем поток, чтобы он мог работать независимо.

        LPCWSTR applicationPath = L"C:\\Users\\user\\source\\repos\\lab4-server2\\Debug\\lab4-server2.exe";

        // Создаем массив, чтобы хранить дескрипторы процессов
        PROCESS_INFORMATION childProcesses[Nmax];

        for (int i = 0; i < Nmin; i++) {
            STARTUPINFO si;
            PROCESS_INFORMATION pi;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            ZeroMemory(&pi, sizeof(pi));

            // Передаем дескрипторы каналов в параметры запуска дочернего процесса
           
            si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
            si.dwFlags |= STARTF_USESTDHANDLES;
            LPCWSTR commandLineArgs = L"1 22";
            if (CreateProcess(applicationPath, (LPWSTR)commandLineArgs, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
                std::cout << "Process #" << i << " is working." << std::endl;
                childProcesses[i] = pi;  // Сохраняем информацию о дочернем процессе
            }
            else {
                std::cout << "Не удалось запустить процесс #" << i << ". Error: " << GetLastError() << std::endl;
            }
        }

        

        

        // Дождитесь завершения всех процессов
       // for (int i = 0; i < Nmin; i++) {
       //     WaitForSingleObject(pi.hProcess, INFINITE);
       // }

        
       //CloseHandle(pi.hProcess);
       // CloseHandle(pi.hThread);
        
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
            if (Stack.empty() && num < Nmax) {
                STARTUPINFO si;
                PROCESS_INFORMATION pi;
                ZeroMemory(&si, sizeof(si));
                si.cb = sizeof(si);
                ZeroMemory(&pi, sizeof(pi));

                // Передаем дескрипторы каналов в параметры запуска дочернего процесса

                si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
                si.dwFlags |= STARTF_USESTDHANDLES;
                LPCWSTR commandLineArgs = L"1 22";
                if (CreateProcess(applicationPath, (LPWSTR)commandLineArgs, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
                    std::cout << "Process #" << num  << " is working." << std::endl;
                    childProcesses[num] = pi;  // Сохраняем информацию о дочернем процессе
                    num++;
                }
                else {
                    std::cout << "Не удалось запустить процесс #" << num  << ". Error: " << GetLastError() << std::endl;
                }
               
            }
            else if (Stack.empty() && num == Nmax) {
                std:: cout << "Подключено максимальное количество клиентов!" << std::endl;
                continue;
            }
            Sleep(1000);
            ProcessINF a = Stack.top();
            Stack.pop();
            
         
            std::string message = std::to_string(clientSocket) + '\n' + clientIP + '\n' + std::to_string(GetCurrentProcessId());
            int bytesSent = send(a.clientSocket, message.c_str(), message.size(), 0); // Отправляем сообщение клиенту.
            if (bytesSent == SOCKET_ERROR) {
                std::cerr << "Send failed" << std::endl; // Если отправка данных не удалась, выводим сообщение об ошибке.
            }
            
           // HandleClient(clientSocket, clientIP);
        }

        closesocket(serverSocket); // Закрываем соксет сервера.
        WSACleanup(); // Освобождаем ресурсы Winsock.
    }
    else {

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
        serverAddr.sin_port = htons(24);
        serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // IP-адрес сервера.

        if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "Failed to connect to server" << std::endl;
            closesocket(clientSocket);
            WSACleanup();
            return 1;
        }
        SOCKET socketValue = NULL;
       
        std::string clientIP = "";
        char buffer[1024]; // Буфер для чтения сообщения
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);

      if (bytesRead == SOCKET_ERROR) {
           std::cerr << "Receive failed" << std::endl;
        }
      else if (bytesRead > 0) {
          // Преобразование буфера в строку
          buffer[bytesRead] = '\0'; // Устанавливаем нулевой символ в конце буфера
          std::string message(buffer);

          // Разбиваем строку на сокет, IP-адрес и PID
          size_t firstNewlinePos = message.find('\n');
          if (firstNewlinePos != std::string::npos) {
              std::string socketStr = message.substr(0, firstNewlinePos);
              size_t secondNewlinePos = message.find('\n', firstNewlinePos + 1);
              if (secondNewlinePos != std::string::npos) {
                  std::string clientIP = message.substr(firstNewlinePos + 1, secondNewlinePos - firstNewlinePos - 1);
                  std::string pidStr = message.substr(secondNewlinePos + 1);

                  // Преобразуем сокет и PID в числа
                  int socketValue = std::stoi(socketStr);
                  int pid = std::stoi(pidStr);

                  // Теперь у вас есть сокет, IP-адрес и PID клиента
                  std::cerr << "Received message: Socket=" << socketValue << ", IP=" << clientIP << ", PID=" << pid << std::endl;
                  SOCKET neww = ConvertProcessSocket(socketValue, pid);
                  std::cerr << "New socket = " << neww << std::endl;
                  HandleClient(neww, clientIP);

                  exit(0);
              }
          }
      }
            
    }

    return 0; // Завершаем программу с кодом успешного выполнения.

}

