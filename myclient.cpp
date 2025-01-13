#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <chrono>
#include <thread>

///////////////////////////////////////////////////////////////////////////////

#define BUF 1024

///////////////////////////////////////////////////////////////////////////////

// Function to validate the username
bool checkUsernameLength(const std::string &username) {
    if (username.length() > 8)
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

void display_commands() {
    std::cout << "\nChoose your command:" << std::endl;
    std::cout << "SEND" << std::endl;
    std::cout << "LIST" << std::endl;
    std::cout << "READ" << std::endl;
    std::cout << "DEL" << std::endl;
    std::cout << "QUIT" << std::endl;
}

void login(int create_socket) {
    std::string username, password;

    while (true) {
        while (true) {
            std::cout << "Username: ";
            std::getline(std::cin, username);
            if (checkUsernameLength(username))
                break;
            std::cout << "Username too long or contains invalid characters! Must be less than 8 alphanumeric characters." << std::endl;
        }

        std::cout << "Password: ";
        std::getline(std::cin, password);

        // Format LOGIN command
        std::string buffer = "LOGIN " + username + " " + password + "\n";

        // Send LOGIN request
        if (send(create_socket, buffer.c_str(), buffer.size(), 0) == -1) {
            perror("Error while sending the LOGIN request");
            return;
        }

        // Read server response
        char recv_buffer[BUF];
        int size = recv(create_socket, recv_buffer, BUF - 1, 0);
        if (size == -1) {
            perror("Error while receiving the server response");
        } else if (size == 0) {
            std::cout << "Server closed the connection." << std::endl;
            exit(EXIT_FAILURE);
        } else {
            recv_buffer[size] = '\0';
            std::string response(recv_buffer);
            if (response.find("OK") != std::string::npos) {
                std::cout << "Login successful." << std::endl;
                display_commands();  // Display available commands after login
                return;
            } else if (response.find("ERR Blacklisted") != std::string::npos) {
                std::cerr << "You have been blacklisted. Please try again later." << std::endl;
                exit(EXIT_FAILURE);
            } else if (response.find("ERR Invalid credentials") != std::string::npos) {
                std::cerr << "Invalid credentials. Try again." << std::endl;
            } else {
                std::cerr << "Login failed: " << response << std::endl;
                exit(EXIT_FAILURE);
            }
        }
    }
}


//Sender excluded, bc login logic implemented
void send_mails(int create_socket, const std::string &receiver, const std::string &subject, const std::string &message) {
    std::string buffer;

    // Format message
    buffer = "SEND\n" + receiver + "\n" + subject + "\n" + message + "\n.\n";

    // Send message
    if (send(create_socket, buffer.c_str(), buffer.size(), 0) == -1) {
        perror("Error while sending the message");
        return;
    }

    // Read server response
    char recv_buffer[BUF];
    int size = recv(create_socket, recv_buffer, BUF - 1, 0);
    if (size == -1) {
        perror("Error while receiving the server response");
    } else if (size == 0) {
        std::cout << "Server closed the connection." << std::endl;
    } else {
        recv_buffer[size] = '\0';
        std::cout << "Server: " << recv_buffer << std::endl;
    }
}

// username removed due to login implementation
void list_mails(int create_socket) {
    std::string buffer = "LIST\n";

    // Send LIST request
    if (send(create_socket, buffer.c_str(), buffer.size(), 0) == -1) {
        perror("Error while sending the LIST request");
        return;
    }

    // Read server response
    char recv_buffer[BUF];
    int size = recv(create_socket, recv_buffer, BUF - 1, 0);
    if (size == -1) {
        perror("Error while receiving the server response");
    } else if (size == 0) {
        std::cout << "Server closed the connection." << std::endl;
    } else {
        recv_buffer[size] = '\0';
        std::cout << "<< " << recv_buffer << std::endl;
    }
}

//username removed
void read_mails(int create_socket, const std::string &message_number) {
    std::string buffer = "READ\n" + message_number + "\n";

    // Send READ request
    if (send(create_socket, buffer.c_str(), buffer.size(), 0) == -1) {
        perror("Error while sending the READ request");
        return;
    }

    // Read server response
    char recv_buffer[BUF];
    int size = recv(create_socket, recv_buffer, BUF - 1, 0);
    if (size == -1) {
        perror("Error while receiving the server response");
    } else if (size == 0) {
        std::cout << "Server closed the connection." << std::endl;
    } else {
        recv_buffer[size] = '\0';
        std::cout << "<< " << recv_buffer << std::endl;
    }
}

void delete_mails(int create_socket, const std::string &message_number) {
    std::string buffer = "DEL\n" + message_number + "\n";

    // Send DEL request
    if (send(create_socket, buffer.c_str(), buffer.size(), 0) == -1) {
        perror("Error while sending the DELETE request");
        return;
    }

    // Read server response
    char recv_buffer[BUF];
    int size = recv(create_socket, recv_buffer, BUF - 1, 0);
    if (size == -1) {
        perror("Error while receiving the server response");
    } else if (size == 0) {
        std::cout << "Server closed the connection." << std::endl;
    } else {
        recv_buffer[size] = '\0';
        std::cout << "<< " << recv_buffer << std::endl;
    }
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv) {
    int create_socket;
    char buffer[BUF];
    struct sockaddr_in address;
    bool isQuit = false;
    bool isLoggedIn = false; // Track if the user has logged in

    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <server_ip> <port>" << std::endl;
        return EXIT_FAILURE;
    }

    std::string server_ip = argv[1];
    int port = std::stoi(argv[2]); // Port aus dem zweiten Argument

    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket error");
        return EXIT_FAILURE;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (argc < 2) {
        inet_aton("127.0.0.1", &address.sin_addr);
    } else {
        inet_aton(argv[1], &address.sin_addr);
    }

    if (connect(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("Connect error - no server available");
        return EXIT_FAILURE;
    }

    std::cout << "Connection with server (" << inet_ntoa(address.sin_addr) << ") established" << std::endl;

    int size = recv(create_socket, buffer, BUF - 1, 0);
    if (size == -1) {
        perror("recv error");
    } else if (size == 0) {
        std::cout << "Server closed remote socket" << std::endl;
    } else {
        buffer[size] = '\0';
        std::cout << buffer;
    }

    do {
        std::cout << ">> ";        
        if (std::cin.getline(buffer, BUF)) {
            std::string command(buffer);

            if (command == "QUIT") {
                isQuit = true;
            } else if (!isLoggedIn && command != "LOGIN") {
                std::cerr << "You must LOGIN first.\n";
                continue;
            } 

            if (command == "QUIT") {
                isQuit = true;
            } else if (command == "LOGIN") {
                login(create_socket);
                isLoggedIn = true; // Mark as logged in after successful login
            } else if (command == "SEND") {
                std::string receiver, subject, message, line;

                while (true) {
                    std::cout << "Receiver: ";
                    std::getline(std::cin, receiver);
                    if (checkUsernameLength(receiver))
                        break;
                    std::cout << "Invalid receiver username!" << std::endl;
                }

                while (true) {
                    std::cout << "Subject: ";
                    std::getline(std::cin, subject);
                    if (checkSubjectLength(subject))
                        break;
                    std::cout << "Subject too long. Must be less than 80 characters." << std::endl;
                }

                std::cout << "Message (multi-line, end with '.'): \n";
                while (std::getline(std::cin, line) && line != ".") {
                    message += line + "\n";
                }

                send_mails(create_socket, receiver, subject, message);

            } else if (command == "LIST") {
                list_mails(create_socket);
            } else if (command == "READ") {
                std::string message_number;
                std::cout << "Message number: ";
                std::getline(std::cin, message_number);
                read_mails(create_socket, message_number);
            } else if (command == "DEL") {
                std::string message_number;
                std::cout << "Message number: ";
                std::getline(std::cin, message_number);
                delete_mails(create_socket, message_number);
            } else {
                std::cerr << "Unknown command: " << command << std::endl;
            }
        }
    } while (!isQuit);

    if (create_socket != -1) {
        if (shutdown(create_socket, SHUT_RDWR) == -1) {
            perror("shutdown create_socket");
        }
        if (close(create_socket) == -1) {
            perror("close create_socket");
        }
        create_socket = -1;
    }

    return EXIT_SUCCESS;
}