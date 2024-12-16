#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>

///////////////////////////////////////////////////////////////////////////////

#define BUF 1024
#define PORT 6543

///////////////////////////////////////////////////////////////////////////////

void send_message(int create_socket, const std::string& sender, const std::string& receiver, const std::string& subject, const std::string& message) {
    std::string buffer;

    // Nachricht formatieren
    buffer = "SEND\n" + sender + "\n" + receiver + "\n" + subject + "\n" + message + "\n.\n";

    // Nachricht senden
    if (send(create_socket, buffer.c_str(), buffer.size(), 0) == -1) {
        perror("Fehler beim Senden der Nachricht");
        return;
    }

    // Serverantwort lesen
    char recv_buffer[BUF];
    int size = recv(create_socket, recv_buffer, BUF - 1, 0);
    if (size == -1) {
        perror("Fehler beim Empfangen der Serverantwort");
    } else if (size == 0) {
        std::cout << "Server hat die Verbindung geschlossen." << std::endl;
    } else {
        recv_buffer[size] = '\0';
        std::cout << "Server: " << recv_buffer << std::endl;
    }
}

void list_messages(int create_socket, const std::string& username) {
    std::string buffer = "LIST\n" + username + "\n";

    // LIST-Nachricht senden
    if (send(create_socket, buffer.c_str(), buffer.size(), 0) == -1) {
        perror("Fehler beim Senden der LIST-Anfrage");
        return;
    }

    // Serverantwort lesen
    char recv_buffer[BUF];
    int size = recv(create_socket, recv_buffer, BUF - 1, 0);
    if (size == -1) {
        perror("Fehler beim Empfangen der Serverantwort");
    } else if (size == 0) {
        std::cout << "Server hat die Verbindung geschlossen." << std::endl;
    } else {
        recv_buffer[size] = '\0';
        std::cout << "<< " << recv_buffer << std::endl;
    }
}

void read_message(int create_socket, const std::string& username, const std::string& message_number) {
    std::string buffer = "READ\n" + username + "\n" + message_number + "\n";

    // READ-Nachricht senden
    if (send(create_socket, buffer.c_str(), buffer.size(), 0) == -1) {
        perror("Fehler beim Senden der READ-Anfrage");
        return;
    }

    // Serverantwort lesen
    char recv_buffer[BUF];
    int size = recv(create_socket, recv_buffer, BUF - 1, 0);
    if (size == -1) {
        perror("Fehler beim Empfangen der Serverantwort");
    } else if (size == 0) {
        std::cout << "Server hat die Verbindung geschlossen." << std::endl;
    } else {
        recv_buffer[size] = '\0';
        std::cout << "<< " << recv_buffer << std::endl;
    }
}

void delete_message(int create_socket, const std::string& username, const std::string& message_number) {
    std::string buffer = "DEL\n" + username + "\n" + message_number + "\n";

    // DEL-Nachricht senden
    if (send(create_socket, buffer.c_str(), buffer.size(), 0) == -1) {
        perror("Fehler beim Senden der DEL-Anfrage");
        return;
    }

    // Serverantwort lesen
    char recv_buffer[BUF];
    int size = recv(create_socket, recv_buffer, BUF - 1, 0);
    if (size == -1) {
        perror("Fehler beim Empfangen der Serverantwort");
    } else if (size == 0) {
        std::cout << "Server hat die Verbindung geschlossen." << std::endl;
    } else {
        recv_buffer[size] = '\0';
        std::cout << "<< " << recv_buffer << std::endl;
    }
}

int main(int argc, char **argv) {
    int create_socket;
    char buffer[BUF];
    struct sockaddr_in address;
    bool isQuit = false;

    ////////////////////////////////////////////////////////////////////////////
    // CREATE A SOCKET
    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket error");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // INIT ADDRESS
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);

    if (argc < 2) {
        inet_aton("127.0.0.1", &address.sin_addr);
    } else {
        inet_aton(argv[1], &address.sin_addr);
    }

    ////////////////////////////////////////////////////////////////////////////
    // CREATE A CONNECTION
    if (connect(create_socket, (struct sockaddr *)&address, sizeof(address)) == -1) {
        perror("Connect error - no server available");
        return EXIT_FAILURE;
    }

    std::cout << "Connection with server (" << inet_ntoa(address.sin_addr) << ") established" << std::endl;

    ////////////////////////////////////////////////////////////////////////////
    // RECEIVE DATA
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

            if (command == "quit") {
                isQuit = true;
            } else if (command == "SEND") {
                // Eingabe für SEND-Befehl sammeln
                std::string sender, receiver, subject, message, line;

                std::cout << "Absender: ";
                std::getline(std::cin, sender);

                std::cout << "Empfänger: ";
                std::getline(std::cin, receiver);

                std::cout << "Betreff: ";
                std::getline(std::cin, subject);

                std::cout << "Nachricht (mehrzeilig, beende mit '.'): \n";
                while (std::getline(std::cin, line) && line != ".") {
                    message += line + "\n";
                }

                // SEND-Nachricht senden
                send_message(create_socket, sender, receiver, subject, message);
            } else if (command == "LIST") {
                // LIST-Befehl verarbeiten
                std::string username;
                std::cout << "Benutzername: ";
                std::getline(std::cin, username);

                list_messages(create_socket, username);
            } else if (command == "READ") {
                // READ-Befehl verarbeiten
                std::string username, message_number;
                std::cout << "Benutzername: ";
                std::getline(std::cin, username);
                std::cout << "Nachrichtennummer: ";
                std::getline(std::cin, message_number);

                read_message(create_socket, username, message_number);
            } else if (command == "DEL") {
                // DEL-Befehl verarbeiten
                std::string username, message_number;
                std::cout << "Benutzername: ";
                std::getline(std::cin, username);
                std::cout << "Nachrichtennummer: ";
                std::getline(std::cin, message_number);

                delete_message(create_socket, username, message_number);
            } else {
                // Andere Befehle senden
                std::cerr << "Unbekannter Befehl: " << command << std::endl;
            }
        }
    } while (!isQuit);

    ////////////////////////////////////////////////////////////////////////////
    // CLOSES THE DESCRIPTOR
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
