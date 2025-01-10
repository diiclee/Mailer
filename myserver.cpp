// Updated TW-Mailer Server Code
#include <iostream>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <map>
#include <chrono>
#include <fstream>
#include <vector>
#include <arpa/inet.h>
#include <unistd.h>
#include <ldap.h>
#include <signal.h>
#include <filesystem>
#include <sstream>
namespace fs = std::filesystem;

///////////////////////////////////////////////////////////////////////////////
#define BUF 1024
#define PORT 6543
#define MAX_LOGIN_ATTEMPTS 3
#define BLACKLIST_DURATION std::chrono::minutes(1)

///////////////////////////////////////////////////////////////////////////////
std::mutex session_mutex;
std::unordered_map<std::string, std::chrono::time_point<std::chrono::system_clock>> blacklist;
std::unordered_map<int, std::string> active_sessions; // Maps socket to username
std::map<std::string, std::vector<std::string>> mail_spool; // Maps username to their emails
std::mutex mail_mutex; // Mutex for mail operations

///////////////////////////////////////////////////////////////////////////////
// LDAP Configuration
const char *ldapUri = "ldap://ldap.technikum-wien.at:389";
const int ldapVersion = LDAP_VERSION3;
const char *ldapBase = "dc=technikum-wien,dc=at";

///////////////////////////////////////////////////////////////////////////////
bool ldap_authenticate(const std::string &username, const std::string &password)
{
    LDAP *ldapHandle;
    int rc = ldap_initialize(&ldapHandle, ldapUri);
    if (rc != LDAP_SUCCESS)
    {
        std::cerr << "LDAP initialization failed: " << ldap_err2string(rc) << std::endl;
        return false;
    }

    ldap_set_option(ldapHandle, LDAP_OPT_PROTOCOL_VERSION, &ldapVersion);

    char ldapBindUser[256];
    sprintf(ldapBindUser, "uid=%s,ou=people,%s", username.c_str(), ldapBase);

    BerValue credentials;
    credentials.bv_val = const_cast<char *>(password.c_str());
    credentials.bv_len = password.length();

    BerValue *servercredp;
    rc = ldap_sasl_bind_s(ldapHandle, ldapBindUser, LDAP_SASL_SIMPLE, &credentials, nullptr, nullptr, &servercredp);

    if (rc != LDAP_SUCCESS)
    {
        std::cerr << "LDAP bind failed: " << ldap_err2string(rc) << std::endl;
        ldap_unbind_ext_s(ldapHandle, nullptr, nullptr);
        return false;
    }

    ldap_unbind_ext_s(ldapHandle, nullptr, nullptr);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
bool check_blacklist(const std::string &ip)
{
    std::lock_guard<std::mutex> lock(session_mutex);
    auto now = std::chrono::system_clock::now();
    if (blacklist.find(ip) != blacklist.end() && now < blacklist[ip])
    {
        return true;
    }
    return false;
}

void blacklist_ip(const std::string &ip)
{
    std::lock_guard<std::mutex> lock(session_mutex);
    blacklist[ip] = std::chrono::system_clock::now() + BLACKLIST_DURATION;
    std::ofstream blacklist_file("blacklist.txt", std::ios::app);
    if (blacklist_file.is_open())
    {
        blacklist_file << ip << std::endl;
    }
}

///////////////////////////////////////////////////////////////////////////////
void handle_send(int client_socket, const std::string &username, const std::string &command)
{
    std::istringstream stream(command);
    std::string receiver, subject, message, line;
    std::getline(stream, receiver);
    std::getline(stream, subject);
    std::ostringstream message_buffer;

    while (std::getline(stream, line) && line != ".")
    {
        message_buffer << line << "\n";
    }

    message = message_buffer.str();

    std::lock_guard<std::mutex> lock(mail_mutex);
    mail_spool[receiver].emplace_back("From: " + username + "\nSubject: " + subject + "\n" + message);

    send(client_socket, "OK\n", 3, 0);
}

void handle_list(int client_socket, const std::string &username)
{
    std::lock_guard<std::mutex> lock(mail_mutex);
    const auto &messages = mail_spool[username];

    std::ostringstream response;
    response << messages.size() << "\n";
    for (size_t i = 0; i < messages.size(); ++i)
    {
        size_t subject_start = messages[i].find("Subject: ") + 9;
        size_t subject_end = messages[i].find("\n", subject_start);
        response << messages[i].substr(subject_start, subject_end - subject_start) << "\n";
    }

    send(client_socket, response.str().c_str(), response.str().size(), 0);
}

void handle_read(int client_socket, const std::string &username, int message_number)
{
    std::lock_guard<std::mutex> lock(mail_mutex);
    const auto &messages = mail_spool[username];

    if (message_number < 1 || message_number > static_cast<int>(messages.size()))
    {
        send(client_socket, "ERR\n", 4, 0);
        return;
    }

    send(client_socket, ("OK\n" + messages[message_number - 1]).c_str(), messages[message_number - 1].size() + 3, 0);
}

void handle_del(int client_socket, const std::string &username, int message_number)
{
    std::lock_guard<std::mutex> lock(mail_mutex);
    auto &messages = mail_spool[username];

    if (message_number < 1 || message_number > static_cast<int>(messages.size()))
    {
        send(client_socket, "ERR\n", 4, 0);
        return;
    }

    messages.erase(messages.begin() + message_number - 1);
    send(client_socket, "OK\n", 3, 0);
}

///////////////////////////////////////////////////////////////////////////////
void handle_client(int client_socket, const std::string &client_ip)
{
    char buffer[BUF];
    int login_attempts = 0;
    bool authenticated = false;
    std::string username;

    send(client_socket, "Welcome! Please LOGIN to proceed.\n", 36, 0);

    while (true)
    {
        memset(buffer, 0, BUF);
        int size = recv(client_socket, buffer, BUF - 1, 0);
        if (size <= 0)
        {
            close(client_socket);
            return;
        }

        std::string command(buffer);
        command.erase(command.find_last_not_of(" \n\r") + 1);

        if (command.rfind("LOGIN", 0) == 0)
        {
            if (authenticated)
            {
                send(client_socket, "Already logged in.\n", 20, 0);
                continue;
            }

            size_t first_space = command.find(' ');
            size_t second_space = command.find(' ', first_space + 1);
            if (first_space == std::string::npos || second_space == std::string::npos)
            {
                send(client_socket, "ERR Invalid LOGIN format.\n", 27, 0);
                continue;
            }

            username = command.substr(first_space + 1, second_space - first_space - 1);
            std::string password = command.substr(second_space + 1);

            if (ldap_authenticate(username, password))
            {
                std::lock_guard<std::mutex> lock(session_mutex);
                active_sessions[client_socket] = username;
                authenticated = true;
                send(client_socket, "OK\n", 3, 0);
            }
            else
            {
                login_attempts++;
                if (login_attempts >= MAX_LOGIN_ATTEMPTS)
                {
                    blacklist_ip(client_ip);
                    send(client_socket, "ERR Blacklisted.\n", 17, 0);
                    close(client_socket);
                    return;
                }
                else
                {
                    send(client_socket, "ERR Invalid credentials.\n", 26, 0);
                }
            }
        }
        else if (command == "QUIT")
        {
            close(client_socket);
            return;
        }
        else if (!authenticated)
        {
            send(client_socket, "ERR Please LOGIN first.\n", 25, 0);
        }
        else if (command.rfind("SEND", 0) == 0)
        {
            handle_send(client_socket, username, command.substr(5));
        }
        else if (command == "LIST")
        {
            handle_list(client_socket, username);
        }
        else if (command.rfind("READ", 0) == 0)
        {
            int message_number = std::stoi(command.substr(5));
            handle_read(client_socket, username, message_number);
        }
        else if (command.rfind("DEL", 0) == 0)
        {
            int message_number = std::stoi(command.substr(4));
            handle_del(client_socket, username, message_number);
        }
        else
        {
            send(client_socket, "ERR Unknown command.\n", 22, 0);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
int main()
{
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0)
    {
        perror("Socket creation failed");
        return EXIT_FAILURE;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Bind failed");
        return EXIT_FAILURE;
    }

    if (listen(server_socket, 10) < 0)
    {
        perror("Listen failed");
        return EXIT_FAILURE;
    }

    std::cout << "Server running on port " << PORT << std::endl;

    while (true)
    {
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0)
        {
            perror("Accept failed");
            continue;
        }

        std::string client_ip = inet_ntoa(client_addr.sin_addr);
        if (check_blacklist(client_ip))
        {
            send(client_socket, "ERR Blacklisted.\n", 17, 0);
            close(client_socket);
            continue;
        }

        std::thread(handle_client, client_socket, client_ip).detach();
    }

    close(server_socket);
    return EXIT_SUCCESS;
}
