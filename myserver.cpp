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
            // PARSE MESSAGE
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

                // SEND RESPONSE
                /*
                std::string response = "Mail gespeichert für " + receiver + ".\r\n";
                if (send(*current_socket, response.c_str(), response.size(), 0) == -1)
                {
                    perror("send answer failed");
                    return NULL;
                }*/

                // Sende "OK" bei erfolgreichem Speichern
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
if (message.find("LIST") == 0) // LIST-Befehl erkannt
{
    std::cout << "[DEBUG] LIST command detected." << std::endl;

    // Fordere den Benutzer auf, den Benutzernamen einzugeben
    std::string prompt = "Benutzernamen:\n";
    if (send(*current_socket, prompt.c_str(), prompt.size(), 0) == -1)
    {
        perror("[DEBUG] send failed when prompting for username");
        return NULL;
    }

    // Empfange den Benutzernamen
    size = recv(*current_socket, buffer, BUF - 1, 0);
    if (size == -1)
    {
        perror("[DEBUG] recv error when reading username");
        return NULL;
    }
    if (size == 0)
    {
        std::cout << "[DEBUG] Client hat die Verbindung geschlossen." << std::endl;
        return NULL;
    }

    buffer[size] = '\0';
    std::string username(buffer);
    username.erase(username.find_last_not_of("\r\n") + 1); // Entferne \r\n
    std::cout << "[DEBUG] Username received: '" << username << "'" << std::endl;

    // Pfad des Benutzerverzeichnisses
    fs::path userFolder = "Received_Mails/" + username;
    std::ostringstream response;   // Für die finale Antwort
    std::vector<fs::path> files;   // Für die Dateisortierung
    std::ostringstream subjects;  // Für die Betreffzeilen

    if (!fs::exists(userFolder) || fs::is_empty(userFolder))
    {
        std::cout << "[DEBUG] User folder does not exist or is empty: " << userFolder << std::endl;
        response << "Count of messages of user:  0\n";
    }
    else
    {
        std::cout << "[DEBUG] Processing mails for user: " << username << std::endl;
        int messageCount = 0;

        // Sammle alle Dateien im Verzeichnis
        for (const auto &entry : fs::directory_iterator(userFolder))
        {
            if (entry.is_regular_file())
            {
                files.push_back(entry.path());
            }
        }

        // Sortiere die Dateien nach Namen (aufsteigend)
        std::sort(files.begin(), files.end());

        // Verarbeite die Dateien in sortierter Reihenfolge
        for (const auto &file : files)
        {
            std::cout << "[DEBUG] Processing file: " << file << std::endl;
            std::ifstream mailFile(file);
            if (!mailFile.is_open())
            {
                std::cerr << "[DEBUG] Failed to open file: " << file << std::endl;
                continue;
            }

            std::string line;
            while (std::getline(mailFile, line))
            {
                if (line.rfind("Betreff: ", 0) == 0) // Finde "Betreff: "
                {
                    std::cout << "[DEBUG] Found subject: " << line.substr(9) << std::endl;
                    subjects << line.substr(9) << "\n"; // Nur den Betreff speichern
                    break;
                }
            }
            mailFile.close();
            ++messageCount;
        }

        // Nachrichtenzähler hinzufügen
        response << "Count of messages of user:  " << messageCount << "\n";
        response << subjects.str(); // Betreffzeilen hinzufügen
    }

    std::cout << "[DEBUG] Response to client: " << response.str() << std::endl;

    if (send(*current_socket, response.str().c_str(), response.str().size(), 0) == -1)
    {
        perror("[DEBUG] send failed when sending response");
        return NULL;
    }
}
if (message.find("READ") == 0) // READ-Befehl erkannt
{
    std::cout << "[DEBUG] READ command detected." << std::endl;

    // Fordere den Benutzer auf, den Benutzernamen einzugeben
    std::string prompt = "Benutzernamen:\n";
    if (send(*current_socket, prompt.c_str(), prompt.size(), 0) == -1)
    {
        perror("[DEBUG] send failed when prompting for username");
        return NULL;
    }

    // Empfange den Benutzernamen
    size = recv(*current_socket, buffer, BUF - 1, 0);
    if (size == -1)
    {
        perror("[DEBUG] recv error when reading username");
        return NULL;
    }
    if (size == 0)
    {
        std::cout << "[DEBUG] Client hat die Verbindung geschlossen." << std::endl;
        return NULL;
    }

    buffer[size] = '\0';
    std::string username(buffer);
    username.erase(username.find_last_not_of("\r\n") + 1); // Entferne \r\n
    std::cout << "[DEBUG] Username received: '" << username << "'" << std::endl;

    // Fordere die Nummer der Nachricht an
    prompt = "Nachrichtennummer:\n";
    if (send(*current_socket, prompt.c_str(), prompt.size(), 0) == -1)
    {
        perror("[DEBUG] send failed when prompting for message number");
        return NULL;
    }

    // Empfange die Nachrichtennummer
    size = recv(*current_socket, buffer, BUF - 1, 0);
    if (size == -1)
    {
        perror("[DEBUG] recv error when reading message number");
        return NULL;
    }
    if (size == 0)
    {
        std::cout << "[DEBUG] Client hat die Verbindung geschlossen." << std::endl;
        return NULL;
    }

    buffer[size] = '\0';
    std::string messageNumber(buffer);
    messageNumber.erase(messageNumber.find_last_not_of("\r\n") + 1); // Entferne \r\n
    std::cout << "[DEBUG] Message number received: '" << messageNumber << "'" << std::endl;

    // Pfad zur Datei basierend auf Benutzername und Nachrichtennummer
    fs::path messageFile = "Received_Mails/" + username + "/" + messageNumber + "mail.txt";

    if (!fs::exists(messageFile))
    {
        std::cout << "[DEBUG] Message file does not exist: " << messageFile << std::endl;
        if (send(*current_socket, "ERR\n", 4, 0) == -1)
        {
            perror("[DEBUG] send failed when sending ERR");
        }
        return NULL;
    }

    // Lese die vollständige Nachricht aus der Datei
    std::ifstream mailFile(messageFile);
    if (!mailFile.is_open())
    {
        std::cerr << "[DEBUG] Failed to open file: " << messageFile << std::endl;
        if (send(*current_socket, "ERR\n", 4, 0) == -1)
        {
            perror("[DEBUG] send failed when sending ERR");
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

    // Sende die vollständige Nachricht mit "OK"
    std::string response = "OK\n" + fullMessage.str();
    if (send(*current_socket, response.c_str(), response.size(), 0) == -1)
    {
        perror("[DEBUG] send failed when sending full message");
        return NULL;
    }

    std::cout << "[DEBUG] Message sent successfully: " << messageFile << std::endl;
}

if (message.find("DEL") == 0) // DEL-Befehl erkannt
{
    std::cout << "[DEBUG] DEL command detected." << std::endl;

    // Benutzernamen abfragen
    std::string prompt = "Benutzernamen:\n";
    if (send(*current_socket, prompt.c_str(), prompt.size(), 0) == -1)
    {
        perror("[DEBUG] send failed when prompting for username");
        return NULL;
    }

    // Benutzername empfangen
    size = recv(*current_socket, buffer, BUF - 1, 0);
    if (size <= 0)
    {
        perror("[DEBUG] recv error when reading username");
        return NULL;
    }
    buffer[size] = '\0';
    std::string username(buffer);
    username.erase(username.find_last_not_of("\r\n") + 1);

    // Nachrichtennummer abfragen
    prompt = "Nachrichtennummer:\n";
    if (send(*current_socket, prompt.c_str(), prompt.size(), 0) == -1)
    {
        perror("[DEBUG] send failed when prompting for message number");
        return NULL;
    }

    // Nachrichtennummer empfangen
    size = recv(*current_socket, buffer, BUF - 1, 0);
    if (size <= 0)
    {
        perror("[DEBUG] recv error when reading message number");
        return NULL;
    }
    buffer[size] = '\0';
    std::string messageNumber(buffer);
    messageNumber.erase(messageNumber.find_last_not_of("\r\n") + 1);

    // Pfad zur Nachrichtendatei
    fs::path messageFile = "Received_Mails/" + username + "/" + messageNumber + "mail.txt";
    std::cout << "[DEBUG] Trying to delete: " << messageFile << std::endl;

    if (fs::exists(messageFile))
    {
        // Datei löschen
        fs::remove(messageFile);
        std::cout << "[DEBUG] Deleted: " << messageFile << std::endl;

        if (send(*current_socket, "OK\n", 3, 0) == -1)
        {
            perror("[DEBUG] send failed when sending OK");
        }
    }
    else
    {
        std::cout << "[DEBUG] Message file does not exist: " << messageFile << std::endl;

        if (send(*current_socket, "ERR\n", 4, 0) == -1)
        {
            perror("[DEBUG] send failed when sending ERR");
        }
    }

    // **WICHTIG: Verbleibende Daten im Socket ignorieren**
    while (recv(*current_socket, buffer, BUF - 1, MSG_DONTWAIT) > 0)
    {
        // Ignoriere verbleibende Daten
    }

    // Rückkehr aus dem DEL-Befehl
    continue;
}

        /*
                else
                {
                    // SEND "OK" FOR OTHER COMMANDS
                    if (send(*current_socket, "ERR", 3, 0) == -1)
                    {
                        perror("send answer failed");
                        return NULL;
                    }
                }*/
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
