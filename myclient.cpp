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
            } 
            else if (command == "DEL") {
    std::string username, messageNumber;

    // Benutzername eingeben
    std::cout << "Benutzername: ";
    std::getline(std::cin, username);

    // Nachrichtennummer eingeben
    std::cout << "Nachrichtennummer: ";
    std::getline(std::cin, messageNumber);

    // DEL-Befehl senden
    std::string buffer = "DEL\n";
    if (send(create_socket, buffer.c_str(), buffer.size(), 0) == -1) {
        perror("Fehler beim Senden des DEL-Befehls");
        return EXIT_FAILURE; // FIX: gültiger Rückgabewert
    }
    usleep(100000);

    // Benutzernamen senden
    username += "\n"; 
    if (send(create_socket, username.c_str(), username.size(), 0) == -1) {
        perror("Fehler beim Senden des Benutzernamens");
        return EXIT_FAILURE; // FIX
    }
    usleep(100000);

    // Serverantwort empfangen
    char recv_buffer[BUF];
    int size = recv(create_socket, recv_buffer, BUF - 1, 0);
    if (size <= 0) {
        perror("Fehler beim Empfangen des Nachrichtennummer-Prompts");
        return EXIT_FAILURE; // FIX
    }
    recv_buffer[size] = '\0';
    std::cout << recv_buffer;

    // Nachrichtennummer senden
    messageNumber += "\n";
    if (send(create_socket, messageNumber.c_str(), messageNumber.size(), 0) == -1) {
        perror("Fehler beim Senden der Nachrichtennummer");
        return EXIT_FAILURE; // FIX
    }
    usleep(100000);

    // Serverantwort empfangen
    size = recv(create_socket, recv_buffer, BUF - 1, 0);
    if (size > 0) {
        recv_buffer[size] = '\0';
        std::cout << "<< " << recv_buffer << std::endl;
    } else {
        perror("Fehler beim Empfangen der Serverantwort");
        return EXIT_FAILURE; // FIX
    }
}

else {
                // Andere Befehle senden
                if (send(create_socket, command.c_str(), command.size() + 1, 0) == -1) {
                    perror("send error");
                    break;
                }

                // Feedback vom Server
                size = recv(create_socket, buffer, BUF - 1, 0);
                if (size == -1) {
                    perror("recv error");
                    break;
                } else if (size == 0) {
                    std::cout << "Server closed remote socket" << std::endl;
                    break;
                } else {
                    buffer[size] = '\0';
                    std::cout << "<< " << buffer << std::endl;
                }
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
