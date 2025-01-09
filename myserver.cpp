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
#include <filesystem>
#include <algorithm>
#include <vector>

namespace fs = std::filesystem;

///////////////////////////////////////////////////////////////////////////////

#define BUF 1024  // Buffer size

///////////////////////////////////////////////////////////////////////////////

int abortRequested = 0;
int create_socket = -1;
int new_socket = -1;

// Globale Variable für den Mail-Ordner
std::string mailFolder;

///////////////////////////////////////////////////////////////////////////////

// functions
void *clientCommunication(void *data);
void signalHandler(int sig);

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        std::cerr << "Usage: " << argv[0] << " <port> <mail_folder>" << std::endl;
        return EXIT_FAILURE;
    }

    int port = std::stoi(argv[1]); // Port aus Argument 1 lesen
    mailFolder = argv[2];          // Ordnername in die globale Variable speichern

    // Ordner prüfen/erstellen
    if (!fs::exists(mailFolder))
    {
        fs::create_directories(mailFolder);
        std::cout << "Created mail folder: " << mailFolder << std::endl;
    }
    else
    {
        std::cout << "Using existing mail folder: " << mailFolder << std::endl;
    }

    socklen_t addrlen;
    struct sockaddr_in address, cliaddress;
    int reuseValue = 1;

    ////////////////////////////////////////////////////////////////////////////
    // SET UP SIGNAL HANDLER
    if (signal(SIGINT, signalHandler) == SIG_ERR)
    {
        perror("Failed to register signal handler");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // CREATE A SOCKET
    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Failed to create socket");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // SET SOCKET OPTIONS
    if (setsockopt(create_socket, SOL_SOCKET, SO_REUSEADDR, &reuseValue, sizeof(reuseValue)) == -1)
    {
        perror("Failed to set socket options - SO_REUSEADDR");
        return EXIT_FAILURE;
    }

    if (setsockopt(create_socket, SOL_SOCKET, SO_REUSEPORT, &reuseValue, sizeof(reuseValue)) == -1)
    {
        perror("Failed to set socket options - SO_REUSEPORT");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // INITIALIZE SERVER ADDRESS
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    ////////////////////////////////////////////////////////////////////////////
    // BIND SOCKET TO AN ADDRESS
    if (bind(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1)
    {
        perror("Failed to bind socket");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // START LISTENING FOR CONNECTIONS
    if (listen(create_socket, 5) == -1)
    {
        perror("Failed to listen on socket");
        return EXIT_FAILURE;
    }

    // main server loop
    while (!abortRequested)
    {
        printf("Waiting for connections...\n");

        /////////////////////////////////////////////////////////////////////////
        // ACCEPT INCOMING CONNECTIONS
        addrlen = sizeof(struct sockaddr_in);
        if ((new_socket = accept(create_socket, (struct sockaddr *)&cliaddress, &addrlen)) == -1)
        {
            if (abortRequested)
            {
                perror("Accept error after shutdown request");
            }
            else
            {
                perror("Accept error");
            }
            break;
        }

        /////////////////////////////////////////////////////////////////////////
        // HANDLE CLIENT COMMUNICATION
        printf("Client connected from %s:%d...\n",
               inet_ntoa(cliaddress.sin_addr),
               ntohs(cliaddress.sin_port));
        clientCommunication(&new_socket);
        new_socket = -1;
    }

    // close server socket
    if (create_socket != -1)
    {
        if (shutdown(create_socket, SHUT_RDWR) == -1)
        {
            perror("Failed to shutdown server socket");
        }
        if (close(create_socket) == -1)
        {
            perror("Failed to close server socket");
        }
        create_socket = -1;
    }

    return EXIT_SUCCESS;
}

// handle client communication
// handle client communication
void *clientCommunication(void *data)
{
    char buffer[BUF];
    int size;
    int *current_socket = (int *)data;

    strcpy(buffer, "Welcome!\r\n");
    if (send(*current_socket, buffer, strlen(buffer), 0) == -1)
    {
        perror("Failed to send welcome message");
        return NULL;
    }

    // communication loop
    do
    {
        size = recv(*current_socket, buffer, BUF - 1, 0); // data from client
        if (size == -1)
        {
            perror("Receive error");
            break;
        }

        if (size == 0) // client closed the connection
        {
            printf("Client closed remote socket\n");
            break;
        }

        buffer[size] = '\0'; // Null-terminierte Zeichenkette
        std::string message(buffer);

        // process SEND
if (message.rfind("SEND", 0) == 0)
{
    std::istringstream stream(message);
    std::string command, sender, receiver, subject, msgBody, line;

    std::getline(stream, command);  
    std::getline(stream, sender);   
    std::getline(stream, receiver); 
    std::getline(stream, subject);  

    while (std::getline(stream, line) && line != ".")
    {
        msgBody += line + "\n";
    }

    // Empfängerordner basierend auf mailFolder erstellen
    fs::path receiverFolder = fs::path(mailFolder) / receiver;
    if (!fs::exists(receiverFolder))
    {
        fs::create_directories(receiverFolder);
        std::cout << "Created folder for receiver: " << receiverFolder << std::endl;
    }

    // Nachricht speichern
    int mailCount = std::distance(fs::directory_iterator(receiverFolder), fs::directory_iterator{});
    fs::path mailFile = receiverFolder / (std::to_string(mailCount + 1) + "mail.txt");

    std::ofstream outFile(mailFile);
    if (outFile.is_open())
    {
        outFile << "Sender: " << sender << "\n";
        outFile << "Receiver: " << receiver << "\n";
        outFile << "Subject: " << subject << "\n\n";
        outFile << msgBody;
        outFile.close();

        std::cout << "Message saved successfully to: " << mailFile << std::endl;

        // Bestätigung an den Client senden
        if (send(*current_socket, "OK\r\n", 4, 0) == -1)
        {
            perror("Failed to send confirmation");
            return NULL;
        }
    }
    else
    {
        std::cerr << "Error opening file: " << mailFile << std::endl;
        if (send(*current_socket, "ERR\r\n", 5, 0) == -1)
        {
            perror("Failed to send error message");
        }
    }
}

        // process LIST
        else if (message.rfind("LIST", 0) == 0)
        {
            std::istringstream stream(message);
            std::string command, username;
            std::getline(stream, command);
            std::getline(stream, username);

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

            if (send(*current_socket, response.str().c_str(), response.str().length(), 0) == -1)
            {
                perror("Failed to send LIST response");
            }
        }
        // process READ
        else if (message.rfind("READ", 0) == 0)
        {
            std::istringstream stream(message);
            std::string command, username, messageNumber;
            std::getline(stream, command);
            std::getline(stream, username);
            std::getline(stream, messageNumber);

            fs::path messageFile = fs::path(mailFolder) / username / (messageNumber + "mail.txt");
            if (fs::exists(messageFile))
            {
                std::ifstream mailFile(messageFile);
                std::ostringstream content;
                content << "OK\n";
                content << mailFile.rdbuf();
                if (send(*current_socket, content.str().c_str(), content.str().length(), 0) == -1)
                {
                    perror("Failed to send READ response");
                }
            }
            else
            {
                if (send(*current_socket, "ERR\n", 4, 0) == -1)
                {
                    perror("Failed to send READ error response");
                }
            }
        }
        // process DEL
        else if (message.rfind("DEL", 0) == 0)
        {
            std::istringstream stream(message);
            std::string command, username, messageNumber;
            std::getline(stream, command);
            std::getline(stream, username);
            std::getline(stream, messageNumber);

            fs::path messageFile = fs::path(mailFolder) / username / (messageNumber + "mail.txt");
            if (fs::exists(messageFile))
            {
                fs::remove(messageFile);
                if (send(*current_socket, "OK\n", 3, 0) == -1)
                {
                    perror("Failed to send DEL confirmation");
                }
            }
            else
            {
                if (send(*current_socket, "ERR\n", 4, 0) == -1)
                {
                    perror("Failed to send DEL error response");
                }
            }
        }
        // unknown command
        else
        {
            if (send(*current_socket, "ERR\n", 4, 0) == -1)
            {
                perror("Failed to send unknown command error");
            }
        }

    } while (strcmp(buffer, "QUIT") != 0 && !abortRequested);

    // close client socket
    if (*current_socket != -1)
    {
        if (shutdown(*current_socket, SHUT_RDWR) == -1)
        {
            perror("Failed to shutdown client socket");
        }
        if (close(*current_socket) == -1)
        {
            perror("Failed to close client socket");
        }
        *current_socket = -1;
    }

    return NULL;
}


// signal handler to clean up sockets on shutdown
void signalHandler(int sig)
{
    if (sig == SIGINT)
    {
        printf("Abort requested... ");
        abortRequested = 1;

        if (new_socket != -1)
        {
            if (shutdown(new_socket, SHUT_RDWR) == -1)
            {
                perror("Failed to shutdown client socket");
            }
            if (close(new_socket) == -1)
            {
                perror("Failed to close client socket");
            }
            new_socket = -1;
        }

        if (create_socket != -1)
        {
            if (shutdown(create_socket, SHUT_RDWR) == -1)
            {
                perror("Failed to shutdown server socket");
            }
            if (close(create_socket) == -1)
            {
                perror("Failed to close server socket");
            }
            create_socket = -1;
        }
    }
    else
    {
        exit(sig);
    }
}
