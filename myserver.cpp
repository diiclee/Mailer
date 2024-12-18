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
#define PORT 6543 // Port number

///////////////////////////////////////////////////////////////////////////////

int abortRequested = 0; 
int create_socket = -1; 
int new_socket = -1;    

///////////////////////////////////////////////////////////////////////////////

// functions
void *clientCommunication(void *data);
void signalHandler(int sig);

///////////////////////////////////////////////////////////////////////////////

int main(void)
{
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
    address.sin_port = htons(PORT);       

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
            if (abortRequested)
            {
                perror("Receive error after shutdown request");
            }
            else
            {
                perror("Receive error");
            }
            break;
        }

        if (size == 0) // client closed the connection
        {
            printf("Client closed remote socket\n");
            break;
        }

        buffer[size] = '\0'; 
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

            // create mails folder if not already created
            fs::path receiverFolder = "mails/" + receiver;
            if (!fs::exists(receiverFolder))
            {
                fs::create_directories(receiverFolder);
            }

            // save the mail
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

                if (send(*current_socket, "OK\r\n", 4, 0) == -1)
                {
                    perror("Failed to send confirmation");
                    return NULL;
                }
            }
            else
            {
                std::cerr << "Error opening file: " << mailFile << std::endl;
            }
        }
        // process LIST 
        else if (message.rfind("LIST", 0) == 0)
        {
            std::istringstream stream(message);
            std::string command, username;
            std::getline(stream, command);  
            std::getline(stream, username); 

            fs::path userFolder = "mails/" + username;
            std::ostringstream response;
            std::vector<fs::path> files;
            std::ostringstream subjects;

            if (!fs::exists(userFolder) || fs::is_empty(userFolder))
            {
                response << "Count of messages of user: 0\n";
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
                        std::cerr << "Error opening file: " << file << std::endl;
                        continue;
                    }

                    std::string line;
                    while (std::getline(mailFile, line))
                    {
                        if (line.rfind("Subject: ", 0) == 0)
                        {
                            subjects << line.substr(9) << "\n";
                            break;
                        }
                    }
                    mailFile.close();
                    ++messageCount;
                }

                response << "Count of messages of user: " << messageCount << "\n";
                response << subjects.str();
            }

            // send response to client
            if (send(*current_socket, response.str().c_str(), response.str().size(), 0) == -1)
            {
                perror("Failed to send LIST response");
                return NULL;
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

            fs::path messageFile = "mails/" + username + "/" + messageNumber + "mail.txt";

            if (!fs::exists(messageFile))
            {
                if (send(*current_socket, "ERR\n", 4, 0) == -1)
                {
                    perror("Failed to send ERR response for READ");
                }
                return NULL;
            }

            std::ifstream mailFile(messageFile);
            if (!mailFile.is_open())
            {
                if (send(*current_socket, "ERR\n", 4, 0) == -1)
                {
                    perror("Failed to send ERR response for READ");
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
                perror("Failed to send READ response");
                return NULL;
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

            fs::path messageFile = "mails/" + username + "/" + messageNumber + "mail.txt";

            if (fs::exists(messageFile))
            {
                fs::remove(messageFile);

                if (send(*current_socket, "OK\n", 3, 0) == -1)
                {
                    perror("Failed to send OK response for DEL");
                }
            }
            else
            {
                if (send(*current_socket, "ERR\n", 4, 0) == -1)
                {
                    perror("Failed to send ERR response for DEL");
                }
            }
        }
        // handle unknown commands
        else
        {
            if (send(*current_socket, "ERR\n", 4, 0) == -1)
            {
                perror("Failed to send ERR for unknown command");
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

        // close client socket if open
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

        // close server socket if open
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
