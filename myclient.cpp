#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

///////////////////////////////////////////////////////////////////////////////

#define BUF 1024
#define PORT 6543

///////////////////////////////////////////////////////////////////////////////

// Function to validate the username
bool checkUsernameLength(const std::string &username) {
    if(username.length() > 8)
        return false;
    for (char c : username) {
        if (!isalnum(c)) {
            return false;
        }
    }
    return true;
}

// Function to validate the subject
bool checkSubjectLength(const std::string &subject) {
    return subject.length() <= 80;
}

void send_mails(int create_socket, const std::string &sender, const std::string &receiver, const std::string &subject, const std::string &message)
{
    std::string buffer;

    // format message
    buffer = "SEND\n" + sender + "\n" + receiver + "\n" + subject + "\n" + message + "\n.\n";

    // send message
    if (send(create_socket, buffer.c_str(), buffer.size(), 0) == -1)
    {
        perror("Error while sending the message");
        return;
    }

    // read server response
    char recv_buffer[BUF];
    int size = recv(create_socket, recv_buffer, BUF - 1, 0);
    if (size == -1)
    {
        perror("Error while receiving the server response");
    }
    else if (size == 0)
    {
        std::cout << "Server closed the connection." << std::endl;
    }
    else
    {
        recv_buffer[size] = '\0';
        std::cout << "Server: " << recv_buffer << std::endl;
    }
}

void list_mails(int create_socket, const std::string &username)
{
    std::string buffer = "LIST\n" + username + "\n";

    // send LIST request
    if (send(create_socket, buffer.c_str(), buffer.size(), 0) == -1)
    {
        perror("Error while sending the LIST request");
        return;
    }

    // read server response
    char recv_buffer[BUF];
    int size = recv(create_socket, recv_buffer, BUF - 1, 0);
    if (size == -1)
    {
        perror("Error while receiving the server response");
    }
    else if (size == 0)
    {
        std::cout << "Server closed the connection." << std::endl;
    }
    else
    {
        recv_buffer[size] = '\0';
        std::cout << "<< " << recv_buffer << std::endl;
    }
}

void read_mails(int create_socket, const std::string &username, const std::string &message_number)
{
    std::string buffer = "READ\n" + username + "\n" + message_number + "\n";

    // send READ request
    if (send(create_socket, buffer.c_str(), buffer.size(), 0) == -1)
    {
        perror("Error while sending the READ request");
        return;
    }

    // read server response
    char recv_buffer[BUF];
    int size = recv(create_socket, recv_buffer, BUF - 1, 0);
    if (size == -1)
    {
        perror("Error while receiving the server response");
    }
    else if (size == 0)
    {
        std::cout << "Server closed the connection." << std::endl;
    }
    else
    {
        recv_buffer[size] = '\0';
        std::cout << "<< " << recv_buffer << std::endl;
    }
}

void delete_mails(int create_socket, const std::string &username, const std::string &message_number)
{
    std::string buffer = "DEL\n" + username + "\n" + message_number + "\n";

    // send DEL request
    if (send(create_socket, buffer.c_str(), buffer.size(), 0) == -1)
    {
        perror("Error while sending the DELETE request");
        return;
    }

    // read server response
    char recv_buffer[BUF];
    int size = recv(create_socket, recv_buffer, BUF - 1, 0);
    if (size == -1)
    {
        perror("Error while receiving the server response");
    }
    else if (size == 0)
    {
        std::cout << "Server closed the connection." << std::endl;
    }
    else
    {
        recv_buffer[size] = '\0';
        std::cout << "<< " << recv_buffer << std::endl;
    }
}

// display commands to the user
void display_commands()
{
    std::cout << "Choose your command:" << std::endl;
    std::cout << "SEND" << std::endl;
    std::cout << "LIST" << std::endl;
    std::cout << "READ" << std::endl;
    std::cout << "DEL" << std::endl;
    std::cout << "QUIT" << std::endl;
}

int main(int argc, char **argv)
{
    int create_socket;
    char buffer[BUF];
    struct sockaddr_in address;
    bool isQuit = false;

    ////////////////////////////////////////////////////////////////////////////
    // CREATE A SOCKET
    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Socket error");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // INIT ADDRESS
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);

    // default to localhost if no address is provided
    if (argc < 2)
    {
        inet_aton("127.0.0.1", &address.sin_addr);
    }
    else
    {
        inet_aton(argv[1], &address.sin_addr);
    }

    ////////////////////////////////////////////////////////////////////////////
    // CREATE A CONNECTION
    if (connect(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1)
    {
        perror("Connect error - no server available");
        return EXIT_FAILURE;
    }

    std::cout << "Connection with server (" << inet_ntoa(address.sin_addr) << ") established" << std::endl;

    ////////////////////////////////////////////////////////////////////////////
    // RECEIVE DATA
    int size = recv(create_socket, buffer, BUF - 1, 0);
    if (size == -1)
    {
        perror("recv error");
    }
    else if (size == 0)
    {
        std::cout << "Server closed remote socket" << std::endl;
    }
    else
    {
        buffer[size] = '\0';
        std::cout << buffer;
    }

    do
    {
        // display commands and prompt user for input
        display_commands();
        std::cout << ">> ";
        if (std::cin.getline(buffer, BUF))
        {
            std::string command(buffer);

            if (command == "QUIT")
            {
                isQuit = true;
            }
            else if (command == "SEND")
            {
                // collect inputs for the SEND command
                std::string sender, receiver, subject, message, line;

                while(true){
                    std::cout << "Sender: ";
                    std::getline(std::cin, sender);
                    if(checkUsernameLength(sender))
                        break;
                    std::cout << "Username too long! Must be less than 8 characters!" << std::endl;
                }

                while(true){
                    std::cout << "Receiver: ";
                    std::getline(std::cin, receiver);
                    if(checkUsernameLength(receiver))
                        break;
                    std::cout << "Username too long! Must be less than 8 characters!" << std::endl;
                }

                while(true){
                    std::cout << "Subject: ";
                    std::getline(std::cin, subject);
                    if(checkSubjectLength(subject))
                        break;
                    std::cout << "Subject too long. Subject must be less than 80 characters long." << std::endl;
                }

                std::cout << "Message (multi-line, end with '.'): \n";
                while (std::getline(std::cin, line) && line != ".")
                {
                    message += line + "\n";
                }

                // send SEND command
                send_mails(create_socket, sender, receiver, subject, message);
            }
            else if (command == "LIST")
            {
                // handle LIST 
                std::string username;
                std::cout << "Username: ";
                std::getline(std::cin, username);

                list_mails(create_socket, username);
            }
            else if (command == "READ")
            {
                // handle READ 
                std::string username, message_number;
                std::cout << "Username: ";
                std::getline(std::cin, username);
                std::cout << "Message number: ";
                std::getline(std::cin, message_number);

                read_mails(create_socket, username, message_number);
            }
            else if (command == "DEL")
            {
                // handle DEL
                std::string username, message_number;
                std::cout << "Username: ";
                std::getline(std::cin, username);
                std::cout << "Message number: ";
                std::getline(std::cin, message_number);

                delete_mails(create_socket, username, message_number);
            }
            else
            {
                // unknown command
                std::cerr << "Unknown command: " << command << std::endl;
            }
        }
    } while (!isQuit);

    ////////////////////////////////////////////////////////////////////////////
    // CLOSE THE SOCKET
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