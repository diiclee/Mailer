#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem> // C++17 erforderlich
#include <algorithm>  // for std::sort
#include <vector>
namespace fs = std::filesystem;

///////////////////////////////////////////////////////////////////////////////

#define BUF 1024
#define PORT 6543

///////////////////////////////////////////////////////////////////////////////

int abortRequested = 0;
int create_socket = -1;
int new_socket = -1;

///////////////////////////////////////////////////////////////////////////////

void *clientCommunication(void *data);
void signalHandler(int sig);

///////////////////////////////////////////////////////////////////////////////

int main(void)
{
    socklen_t addrlen;
    struct sockaddr_in address, cliaddress;
    int reuseValue = 1;

    ////////////////////////////////////////////////////////////////////////////
    // SIGNAL HANDLER
    if (signal(SIGINT, signalHandler) == SIG_ERR)
    {
        perror("signal can not be registered");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // CREATE A SOCKET
    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Socket error");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // SET SOCKET OPTIONS
    if (setsockopt(create_socket, SOL_SOCKET, SO_REUSEADDR, &reuseValue, sizeof(reuseValue)) == -1)
    {
        perror("set socket options - reuseAddr");
        return EXIT_FAILURE;
    }

    if (setsockopt(create_socket, SOL_SOCKET, SO_REUSEPORT, &reuseValue, sizeof(reuseValue)) == -1)
    {
        perror("set socket options - reusePort");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // INIT ADDRESS
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    ////////////////////////////////////////////////////////////////////////////
    // ASSIGN AN ADDRESS WITH PORT TO SOCKET
    if (bind(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1)
    {
        perror("bind error");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // ALLOW CONNECTION ESTABLISHING
    if (listen(create_socket, 5) == -1)
    {
        perror("listen error");
        return EXIT_FAILURE;
    }

    while (!abortRequested)
    {
        printf("Waiting for connections...\n");

        /////////////////////////////////////////////////////////////////////////
        // ACCEPTS CONNECTION SETUP
        addrlen = sizeof(struct sockaddr_in);
        if ((new_socket = accept(create_socket, (struct sockaddr *)&cliaddress, &addrlen)) == -1)
        {
            if (abortRequested)
            {
                perror("accept error after aborted");
            }
            else
            {
                perror("accept error");
            }
            break;
        }

        /////////////////////////////////////////////////////////////////////////
        // START CLIENT
        printf("Client connected from %s:%d...\n",
               inet_ntoa(cliaddress.sin_addr),
               ntohs(cliaddress.sin_port));
        clientCommunication(&new_socket);
        new_socket = -1;
    }

    // CLOSE SERVER SOCKET
    if (create_socket != -1)
    {
        if (shutdown(create_socket, SHUT_RDWR) == -1)
        {
            perror("shutdown create_socket");
        }
        if (close(create_socket) == -1)
        {
            perror("close create_socket");
        }
        create_socket = -1;
    }

    return EXIT_SUCCESS;
}

void *clientCommunication(void *data)
{
    char buffer[BUF];
    int size;
    int *current_socket = (int *)data;

    // SEND WELCOME MESSAGE
    strcpy(buffer, "Welcome to myserver!\r\nPlease enter your commands...\r\n");
    if (send(*current_socket, buffer, strlen(buffer), 0) == -1)
    {
        perror("send failed");
        return NULL;
    }

    do
    {
        size = recv(*current_socket, buffer, BUF - 1, 0);
        if (size == -1)
        {
            if (abortRequested)
            {
                perror("recv error after aborted");
            }
            else
            {
                perror("recv error");
            }
            break;
        }

        if (size == 0)
        {
            printf("Client closed remote socket\n");
            break;
        }

        buffer[size] = '\0';
        std::string message(buffer);

        if (message.rfind("SEND", 0) == 0)
        {
            std::istringstream stream(message);
            std::string command, sender, receiver, subject, msgBody, line;

            std::getline(stream, command);  // "SEND"
            std::getline(stream, sender);   // Sender
            std::getline(stream, receiver); // Empfänger
            std::getline(stream, subject);  // Betreff

            while (std::getline(stream, line) && line != ".")
            {
                msgBody += line + "\n";
            }

            // CREATE RECEIVER FOLDER IF NOT EXISTS
            fs::path receiverFolder = "Received_Mails/" + receiver;
            if (!fs::exists(receiverFolder))
            {
                fs::create_directories(receiverFolder);
            }

            // SAVE MAIL
            int mailCount = std::distance(fs::directory_iterator(receiverFolder), fs::directory_iterator{});
            fs::path mailFile = receiverFolder / (std::to_string(mailCount + 1) + "mail.txt");

            std::ofstream outFile(mailFile);
            if (outFile.is_open())
            {
                outFile << "Absender: " << sender << "\n";
                outFile << "Empfänger: " << receiver << "\n";
                outFile << "Betreff: " << subject << "\n\n";
                outFile << msgBody;
                outFile.close();

                if (send(*current_socket, "OK\r\n", 4, 0) == -1)
                {
                    perror("send answer failed");
                    return NULL;
                }
            }
            else
            {
                std::cerr << "Fehler beim Öffnen der Datei: " << mailFile << std::endl;
            }
        }
        else if (message.rfind("LIST", 0) == 0)
        {
            std::istringstream stream(message);
            std::string command, username;
            std::getline(stream, command);  // "LIST"
            std::getline(stream, username); // Username

            fs::path userFolder = "Received_Mails/" + username;
            std::ostringstream response;
            std::vector<fs::path> files;
            std::ostringstream subjects;

            if (!fs::exists(userFolder) || fs::is_empty(userFolder))
            {
                response << "Count of messages of user:  0\n";
            }
            else
            {
                int messageCount = 0;

                for (const auto &entry : fs::directory_iterator(userFolder))
                {
                    if (entry.is_regular_file())
                    {
                        files.push_back(entry.path());
                    }
                }

                std::sort(files.begin(), files.end());

                for (const auto &file : files)
                {
                    std::ifstream mailFile(file);
                    if (!mailFile.is_open())
                    {
                        std::cerr << "Fehler beim Öffnen der Datei: " << file << std::endl;
                        continue;
                    }

                    std::string line;
                    while (std::getline(mailFile, line))
                    {
                        if (line.rfind("Betreff: ", 0) == 0)
                        {
                            subjects << line.substr(9) << "\n";
                            break;
                        }
                    }
                    mailFile.close();
                    ++messageCount;
                }

                response << "Count of messages of user:  " << messageCount << "\n";
                response << subjects.str();
            }

            if (send(*current_socket, response.str().c_str(), response.str().size(), 0) == -1)
            {
                perror("send failed when sending LIST response");
                return NULL;
            }
        }
        else if (message.rfind("READ", 0) == 0)
        {
            std::istringstream stream(message);
            std::string command, username, messageNumber;
            std::getline(stream, command);        // READ
            std::getline(stream, username);       // Benutzername
            std::getline(stream, messageNumber);  // Nachrichtennummer

            fs::path messageFile = "Received_Mails/" + username + "/" + messageNumber + "mail.txt";

            if (!fs::exists(messageFile))
            {
                if (send(*current_socket, "ERR\n", 4, 0) == -1)
                {
                    perror("send failed when sending ERR");
                }
                return NULL;
            }

            std::ifstream mailFile(messageFile);
            if (!mailFile.is_open())
            {
                if (send(*current_socket, "ERR\n", 4, 0) == -1)
                {
                    perror("send failed when sending ERR");
                }
                return NULL;
            }

            std::ostringstream fullMessage;
            std::string line;
            while (std::getline(mailFile, line))
            {
                fullMessage << line << "\n";
            }
            mailFile.close();

            std::string response = "OK\n" + fullMessage.str();
            if (send(*current_socket, response.c_str(), response.size(), 0) == -1)
            {
                perror("send failed when sending READ message");
                return NULL;
            }
        }
        else if (message.rfind("DEL", 0) == 0)
        {
            std::istringstream stream(message);
            std::string command, username, messageNumber;
            std::getline(stream, command);        // DEL
            std::getline(stream, username);       // Benutzername
            std::getline(stream, messageNumber);  // Nachrichtennummer

            fs::path messageFile = "Received_Mails/" + username + "/" + messageNumber + "mail.txt";

            if (fs::exists(messageFile))
            {
                fs::remove(messageFile);

                if (send(*current_socket, "OK\n", 3, 0) == -1)
                {
                    perror("send failed when sending OK for DEL");
                }
            }
            else
            {
                if (send(*current_socket, "ERR\n", 4, 0) == -1)
                {
                    perror("send failed when sending ERR for DEL");
                }
            }
        }
        else
        {
            if (send(*current_socket, "ERR\n", 4, 0) == -1)
            {
                perror("send failed for unknown command");
            }
        }

    } while (strcmp(buffer, "quit") != 0 && !abortRequested);

    // CLOSE CLIENT SOCKET
    if (*current_socket != -1)
    {
        if (shutdown(*current_socket, SHUT_RDWR) == -1)
        {
            perror("shutdown new_socket");
        }
        if (close(*current_socket) == -1)
        {
            perror("close new_socket");
        }
        *current_socket = -1;
    }

    return NULL;
}

void signalHandler(int sig)
{
    if (sig == SIGINT)
    {
        printf("abort Requested... ");
        abortRequested = 1;

        if (new_socket != -1)
        {
            if (shutdown(new_socket, SHUT_RDWR) == -1)
            {
                perror("shutdown new_socket");
            }
            if (close(new_socket) == -1)
            {
                perror("close new_socket");
            }
            new_socket = -1;
        }

        if (create_socket != -1)
        {
            if (shutdown(create_socket, SHUT_RDWR) == -1)
            {
                perror("shutdown create_socket");
            }
            if (close(create_socket) == -1)
            {
                perror("close create_socket");
            }
            create_socket = -1;
        }
    }
    else
    {
        exit(sig);
    }
}
