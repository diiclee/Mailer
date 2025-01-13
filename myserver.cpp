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
#define MAX_LOGIN_ATTEMPTS 3
#define BLACKLIST_DURATION std::chrono::minutes(1)

///////////////////////////////////////////////////////////////////////////////
std::mutex session_mutex;
std::unordered_map<std::string, std::chrono::time_point<std::chrono::system_clock>> blacklist;
std::unordered_map<int, std::string> active_sessions; // Maps socket to username
std::mutex mail_mutex;                                // Mutex for mail operations
std::string mailFolder;                               // Mail folder path

///////////////////////////////////////////////////////////////////////////////
// LDAP Configuration
const char *ldapUri = "ldap://ldap.technikum-wien.at:389";
const int ldapVersion = LDAP_VERSION3;
const char *ldapBase = "dc=technikum-wien,dc=at";

///////////////////////////////////////////////////////////////////////////////

// Authenticate user with LDAP
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

    // Construct the LDAP bind user string
    char ldapBindUser[256];
    sprintf(ldapBindUser, "uid=%s,ou=people,%s", username.c_str(), ldapBase);

    // set credentials
    BerValue credentials;
    credentials.bv_val = const_cast<char *>(password.c_str());
    credentials.bv_len = password.length();

    // authenticate user
    BerValue *servercredp;
    rc = ldap_sasl_bind_s(ldapHandle, ldapBindUser, LDAP_SASL_SIMPLE, &credentials, nullptr, nullptr, &servercredp);

    if (rc != LDAP_SUCCESS)
    {
        std::cerr << "LDAP bind failed: " << ldap_err2string(rc) << std::endl;
        ldap_unbind_ext_s(ldapHandle, nullptr, nullptr);
        return false;
    }

    // clean up and return success
    ldap_unbind_ext_s(ldapHandle, nullptr, nullptr);
    return true;
}

///////////////////////////////////////////////////////////////////////////////

// Blacklist persistence file
const std::string blacklistFile = "blacklist.txt";

// Load the blacklist from file
void load_blacklist()
{
    std::ifstream file(blacklistFile);
    if (file.is_open())
    {
        std::string ip;
        long long expiry_time;
        while (file >> ip >> expiry_time)
        {
            blacklist[ip] = std::chrono::system_clock::time_point(std::chrono::milliseconds(expiry_time));
        }
        file.close();
    }
}

// Save the blacklist to file
void save_blacklist()
{
    std::ofstream file(blacklistFile, std::ios::trunc);
    if (file.is_open())
    {
        for (const auto &entry : blacklist)
        {
            auto expiry_time = std::chrono::duration_cast<std::chrono::milliseconds>(entry.second.time_since_epoch()).count();
            file << entry.first << " " << expiry_time << std::endl;
        }
        file.close();
    }
}

// Check if an IP is blacklisted
bool check_blacklist(const std::string &ip)
{
    std::lock_guard<std::mutex> lock(session_mutex);
    auto now = std::chrono::system_clock::now();
    if (blacklist.find(ip) != blacklist.end())
    {
        if (now < blacklist[ip])
        {
            std::cerr << "IP " << ip << " is blacklisted.\n";
            return true; // IP is still blacklisted
        }
        else
        {
            blacklist.erase(ip); // Remove expired entry
            save_blacklist();
        }
    }
    return false;
}

// Add an IP to the blacklist
void blacklist_ip(const std::string &ip)
{
    std::lock_guard<std::mutex> lock(session_mutex);
    auto expiry_time = std::chrono::system_clock::now() + BLACKLIST_DURATION;
    blacklist[ip] = expiry_time;
    save_blacklist();
    std::cerr << "Blacklisted IP: " << ip << " for " << std::chrono::duration_cast<std::chrono::seconds>(BLACKLIST_DURATION).count() << " seconds.\n";
}

///////////////////////////////////////////////////////////////////////////////

void handle_send(int client_socket, const std::string &username, const std::string &command)
{
    std::istringstream stream(command);
    std::string receiver, subject, line;
    std::ostringstream message_buffer;

    std::getline(stream, receiver);
    std::getline(stream, subject);

    while (std::getline(stream, line) && line != ".")
    {
        message_buffer << line << "\n";
    }

    std::string message = message_buffer.str();

    // Ensure receiver folder exists
    fs::path receiverFolder = fs::path(mailFolder) / receiver;
    if (!fs::exists(receiverFolder))
    {
        fs::create_directories(receiverFolder);
    }

    // Save message
    int messageCount = std::distance(fs::directory_iterator(receiverFolder), fs::directory_iterator{});
    fs::path messageFile = receiverFolder / (std::to_string(messageCount + 1) + ".txt");

    std::ofstream outFile(messageFile);
    if (outFile.is_open())
    {
        outFile << "From: " << username << "\n";
        outFile << "Subject: " << subject << "\n\n";
        outFile << message;
        outFile.close();
        send(client_socket, "OK\n", 3, 0);
    }
    else
    {
        send(client_socket, "ERR\n", 4, 0);
    }
}

void handle_list(int client_socket, const std::string &username)
{
    fs::path userFolder = fs::path(mailFolder) / username;
    std::ostringstream response;

    if (!fs::exists(userFolder) || fs::is_empty(userFolder))
    {
        response << "0\n";
    }
    else
    {
        int count = 0;
        for (const auto &entry : fs::directory_iterator(userFolder))
        {
            if (entry.is_regular_file())
            {
                count++;
                std::ifstream mailFile(entry.path());
                std::string line;
                while (std::getline(mailFile, line))
                {
                    if (line.rfind("Subject: ", 0) == 0)
                    {
                        response << line.substr(9) << "\n";
                        break;
                    }
                }
            }
        }
        response << count << "\n";
    }

    send(client_socket, response.str().c_str(), response.str().size(), 0);
}

void handle_read(int client_socket, const std::string &username, int message_number)
{
    fs::path userFolder = fs::path(mailFolder) / username;
    fs::path messageFile = userFolder / (std::to_string(message_number) + ".txt");

    if (fs::exists(messageFile))
    {
        std::ifstream mailFile(messageFile);
        std::ostringstream content;
        content << "OK\n";
        content << mailFile.rdbuf();
        send(client_socket, content.str().c_str(), content.str().size(), 0);
    }
    else
    {
        send(client_socket, "ERR\n", 4, 0);
    }
}

void handle_del(int client_socket, const std::string &username, int message_number)
{
    fs::path userFolder = fs::path(mailFolder) / username;
    fs::path messageFile = userFolder / (std::to_string(message_number) + ".txt");

    if (fs::exists(messageFile))
    {
        fs::remove(messageFile);
        send(client_socket, "OK\n", 3, 0);
    }
    else
    {
        send(client_socket, "ERR\n", 4, 0);
    }
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

        // If client is not authenticated, only accept LOGIN or QUIT commands
        if (!authenticated)
        {
            if (command.rfind("LOGIN", 0) == 0)
            {
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
            else
            {
                send(client_socket, "ERR Please LOGIN first.\n", 25, 0);
            }
            continue;
        }

        // Process commands for authenticated users
        if (command.rfind("SEND", 0) == 0)
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
        else if (command == "QUIT")
        {
            close(client_socket);
            return;
        }
        else
        {
            send(client_socket, "ERR Unknown command.\n", 22, 0);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <port> <mail_folder>" << std::endl;
        return EXIT_FAILURE;
    }

    int port = std::stoi(argv[1]);
    mailFolder = argv[2];

    // load blacklist from file
    load_blacklist();

    if (!fs::exists(mailFolder))
    {
        fs::create_directories(mailFolder);
    }

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
    server_addr.sin_port = htons(port);

    // Bind the socket to the specified port
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

    std::cout << "Server running on port " << port << std::endl;

    // accept and handle client connections in separate threads
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

    // save blacklist and clean up
    save_blacklist();
    close(server_socket);
    return EXIT_SUCCESS;
}