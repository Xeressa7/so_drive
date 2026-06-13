#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>          
#include <filesystem>     
#include <fstream>        
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sqlite3.h>

namespace fs = std::filesystem;

#define PORT 12345 
#define BUFFER_SIZE 4096 // 4 MB PAKET SINIRI

std::mutex logMutex; 

// Thread-Safe Loglama
void threadSafeLog(const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex); 
    std::cout << "[SERVER LOG]: " << message << std::endl;
}

// Veritabanı Kontrolü
bool checkLogin(const std::string& username, const std::string& password, std::string& userFolder) {
    sqlite3* db;
    sqlite3_stmt* stmt;
    
    if (sqlite3_open("User_Informations.sqlite", &db) != SQLITE_OK) {
        threadSafeLog("Database could not be opened!");
        return false;
    }

    std::string sql = "SELECT UserFolder FROM UserInformation WHERE Username = ? AND Password = ?;"; 
    
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, 0) != SQLITE_OK) {
        threadSafeLog("SQL statement could not be prepared.");
        sqlite3_close(db);
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);

    bool isAuthenticated = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* folder = sqlite3_column_text(stmt, 0);
        userFolder = std::string(reinterpret_cast<const char*>(folder));
        isAuthenticated = true;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return isAuthenticated;
}

// Dosya Listeleme
std::string listFiles(const std::string& path) {
    if (!fs::exists(path)) return "Folder Not Found";

    if (fs::is_empty(path)) {
        return "EMPTY"; // Protocol keyword, remains same
    }

    std::string fileList = "";
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_regular_file() || entry.is_directory()) {
            fileList += entry.path().filename().string() + "|";
            if (entry.is_directory())
                fileList += "DIR;";
            else
                fileList += std::to_string(entry.file_size()) + ";"; 
        }
    }
    return fileList;
}

// İstemci Yönetimi
void handleClient(int clientSocket) {
    char buffer[BUFFER_SIZE] = {0};
    
    std::string rootFolder;   // Kullanıcının ana klasörü (Sandbox)
    std::string currentPath;  // Kullanıcının o anki konumu

    bool isLoggedIn = false;

    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int valread = read(clientSocket, buffer, BUFFER_SIZE);
        
        if (valread <= 0) {
            threadSafeLog("Client disconnected.");
            break;
        }

        std::string request(buffer);
        
        // --- LOGIN İŞLEMİ ---
        if (request.rfind("LOGIN|", 0) == 0) {
            size_t firstPipe = request.find('|');
            size_t secondPipe = request.find('|', firstPipe + 1);
            
            if (firstPipe != std::string::npos && secondPipe != std::string::npos) {
                std::string user = request.substr(firstPipe + 1, secondPipe - firstPipe - 1);
                std::string pass = request.substr(secondPipe + 1);
                
                if (checkLogin(user, pass, rootFolder)) {
                    isLoggedIn = true;
                    currentPath = rootFolder; 

                    threadSafeLog("User Logged In: " + user);
                    
                    if (!fs::exists(rootFolder)) {
                         fs::create_directory(rootFolder);
                         threadSafeLog("User folder created: " + rootFolder);
                    }

                    std::string msg = "OK"; 
                    send(clientSocket, msg.c_str(), msg.length(), 0);
                } else {
                    std::string msg = "ERR|Login Failed"; // İngilizceye çevrildi
                    send(clientSocket, msg.c_str(), msg.length(), 0);
                }
            }
        }
        // --- DİZİN DEĞİŞTİRME (CD) ---
        else if (request.rfind("CD|", 0) == 0 && isLoggedIn) {
            std::string targetDir = request.substr(3);
            fs::path yeniYol;

            if (targetDir == "..") {
                yeniYol = fs::path(currentPath).parent_path();
            } else {
                yeniYol = fs::path(currentPath) / targetDir;
            }

            // GÜVENLİK KONTROLÜ (SANDBOX)
            if (yeniYol.string().find(rootFolder) != 0 || !fs::exists(yeniYol) || !fs::is_directory(yeniYol)) {
                std::string msg = "ERR|Invalid Directory"; // İngilizceye çevrildi
                send(clientSocket, msg.c_str(), msg.length(), 0);
            } else {
                currentPath = yeniYol.string();
                
                std::string list = listFiles(currentPath);
                send(clientSocket, list.c_str(), list.length(), 0);
            }
        }
        // --- DOSYA LİSTESİ İSTEĞİ ---
        else if (request == "LIST_FILES" && isLoggedIn) {
             std::string list = listFiles(currentPath);
             send(clientSocket, list.c_str(), list.length(), 0);
        }
        
        // --- DOSYA İNDİRME İSTEĞİ (DOWNLOAD) ---
        else if (request.rfind("DOWNLOAD|", 0) == 0 && isLoggedIn) {
            std::string filename = request.substr(9);
            std::string filepath = currentPath + "/" + filename;

            if (fs::exists(filepath)) {
                std::ifstream file(filepath, std::ios::binary | std::ios::ate);
                long fileSize = file.tellg();
                file.seekg(0, std::ios::beg);

                std::string header = "SIZE|" + std::to_string(fileSize);
                send(clientSocket, header.c_str(), header.length(), 0);
                
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                char fileBuf[BUFFER_SIZE];
                while (!file.eof()) {
                    file.read(fileBuf, BUFFER_SIZE);
                    int bytesRead = file.gcount();
                    if (bytesRead > 0) {
                        send(clientSocket, fileBuf, bytesRead, 0);
                    }
                }
                file.close();
                threadSafeLog("File sent: " + filename);
            } else {
                std::string msg = "ERR|File Not Found"; // İngilizceye çevrildi
                send(clientSocket, msg.c_str(), msg.length(), 0);
            }
        }
                
                    // --- DOSYA YÜKLEME (UPLOAD) ---
        else if (request.rfind("UPLOAD|", 0) == 0 && isLoggedIn) {
            // Format: UPLOAD|dosyaadi|boyut
            size_t firstPipe = request.find('|');
            size_t secondPipe = request.find('|', firstPipe + 1);

            if (secondPipe != std::string::npos) {
                std::string fileName = request.substr(firstPipe + 1, secondPipe - firstPipe - 1);
                long long fileSize = std::stoll(request.substr(secondPipe + 1));

                fs::path filePath = fs::path(currentPath) / fileName;
                threadSafeLog("UPLOAD Request: " + fileName + " (" + std::to_string(fileSize) + " bytes)");

                // 1. İstemciye "Hazırım, gönder" de
                std::string readyMsg = "READY_TO_UPLOAD";
                send(clientSocket, readyMsg.c_str(), readyMsg.length(), 0);

                // 2. Dosyayı Yazma Modunda Aç
                std::ofstream outfile(filePath, std::ios::binary);
                if (!outfile.is_open()) {
                    threadSafeLog("ERROR: Could not create upload file.");
                    // Hata olsa bile döngüyü kırmak için continue diyoruz
                    continue; 
                }

                // 3. Veriyi Bekle ve Yaz (Bloklayan Döngü)
                long long totalRead = 0;
                char fileBuf[BUFFER_SIZE];

                while (totalRead < fileSize) {
                    // Ne kadar okuyayım? (Kalan miktar buffer'dan küçükse sadece kalanı oku)
                    // Bu kısım hassas, buffer taşmasın.
                    int bytesToRead = BUFFER_SIZE;
                    if (fileSize - totalRead < BUFFER_SIZE) {
                        bytesToRead = fileSize - totalRead;
                    }

                    int valread = read(clientSocket, fileBuf, bytesToRead);
                    if (valread <= 0) break; // Bağlantı koptu

                    outfile.write(fileBuf, valread);
                    totalRead += valread;
                }

        outfile.close();
        threadSafeLog("Upload Completed: " + fileName);

        // 4. İşlem bitince listeyi güncelle
        std::string list = listFiles(currentPath);
        send(clientSocket, list.c_str(), list.length(), 0);
    }
}

        // --- KLASÖR OLUŞTURMA (MKDIR) ---
        else if (request.rfind("MKDIR|", 0) == 0 && isLoggedIn) {
            std::string folderName = request.substr(6);
            fs::path newPath = fs::path(currentPath) / folderName;

            threadSafeLog("MKDIR Request: " + folderName);

            if (newPath.string().find(rootFolder) != 0) {
                 threadSafeLog("ERROR: Attempt to access outside root!");
            }
            else if (fs::exists(newPath)) {
                 threadSafeLog("ERROR: Folder already exists.");
            }
            else {
                if (fs::create_directory(newPath)) {
                    threadSafeLog("Folder created: " + newPath.string());
                    std::string list = listFiles(currentPath);
                    send(clientSocket, list.c_str(), list.length(), 0);
                } else {
                    threadSafeLog("ERROR: Folder could not be created.");
                }
            }
        }
        
        // --- SİLME İŞLEMİ (DELETE) ---
else if (request.rfind("DELETE|", 0) == 0 && isLoggedIn) {
    std::string targetName = request.substr(7); // "DELETE|" sonrası
    fs::path targetPath = fs::path(currentPath) / targetName;

    threadSafeLog("DELETE Request: " + targetName);

    // Güvenlik: Root dışına çıkamasın (Sandbox)
    if (targetPath.string().find(rootFolder) != 0) {
         threadSafeLog("ERROR: Attempt to delete outside root!");
         std::string msg = "ERR|Permission Denied.";
         send(clientSocket, msg.c_str(), msg.length(), 0);
    }
    else if (!fs::exists(targetPath)) {
         threadSafeLog("ERROR: Target not found.");
         std::string msg = "ERR|File or folder not found.";
         send(clientSocket, msg.c_str(), msg.length(), 0);
    }
    else {
        try {
            // remove_all: Hem dosyayı hem de (dolu olsa bile) klasörü siler
            fs::remove_all(targetPath);
            threadSafeLog("Deleted: " + targetPath.string());

            // İşlem başarılı, listeyi güncelle
            std::string list = listFiles(currentPath);
            send(clientSocket, list.c_str(), list.length(), 0);
        } catch (const fs::filesystem_error& e) {
             threadSafeLog("ERROR: Delete failed. " + std::string(e.what()));
             std::string msg = "ERR|Could not delete item.";
             send(clientSocket, msg.c_str(), msg.length(), 0);
        }
    }
}

        // --- DOSYA OLUŞTURMA (TOUCH) ---
        else if (request.rfind("TOUCH|", 0) == 0 && isLoggedIn) {
            std::string fileName = request.substr(6);
            fs::path newFilePath = fs::path(currentPath) / fileName;

            threadSafeLog("TOUCH Request: " + fileName);

            if (newFilePath.string().find(rootFolder) != 0) {
                 threadSafeLog("ERROR: Attempt to access outside root!");
            }
            else if (fs::exists(newFilePath)) {
                 threadSafeLog("ERROR: File already exists.");
            }
            else {
                std::ofstream outfile(newFilePath);
                if (outfile.good()) {
                    outfile.close();
                    threadSafeLog("File created: " + newFilePath.string());
                    std::string list = listFiles(currentPath);
                    send(clientSocket, list.c_str(), list.length(), 0);
                } else {
                    threadSafeLog("ERROR: File could not be created.");
                }
            }
        }

        // --- DOSYA OKUMA (CAT) - Preview İçin ---
        else if (request.rfind("CAT|", 0) == 0 && isLoggedIn) {
            std::string fileName = request.substr(4);
            fs::path filePath = fs::path(currentPath) / fileName;
            
            threadSafeLog("CAT Request (Read): " + fileName);

            if (fs::exists(filePath) && fs::is_regular_file(filePath)) {
                std::ifstream file(filePath);
                if (file.is_open()) {
                    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    file.close();

                    std::string response = "CONTENT|" + fileName + "|" + content;
                    send(clientSocket, response.c_str(), response.length(), 0);
                } else {
                    std::string msg = "ERR|File could not be opened"; // İngilizceye çevrildi
                    send(clientSocket, msg.c_str(), msg.length(), 0);
                }
            } else {
                std::string msg = "ERR|File not found"; // İngilizceye çevrildi
                send(clientSocket, msg.c_str(), msg.length(), 0);
            }
        }

        // --- DOSYA KAYDETME (WRITE) - Save Butonu İçin ---
        else if (request.rfind("WRITE|", 0) == 0 && isLoggedIn) {
            // Format: WRITE|dosyaadi|icerik
            size_t firstPipe = request.find('|');
            size_t secondPipe = request.find('|', firstPipe + 1);
            
            if (secondPipe != std::string::npos) {
                std::string fileName = request.substr(firstPipe + 1, secondPipe - firstPipe - 1);
                std::string content = request.substr(secondPipe + 1); 

                fs::path filePath = fs::path(currentPath) / fileName;
                threadSafeLog("WRITE Request (Save): " + fileName);

                // Dosyayı tamamen üzerine yaz (Truncate mode)
                std::ofstream outfile(filePath);
                if (outfile.is_open()) {
                    outfile << content;
                    outfile.close();
                    
                    std::string msg = "MSG|File saved successfully."; // İngilizceye çevrildi
                    send(clientSocket, msg.c_str(), msg.length(), 0);
                } else {
                    std::string msg = "ERR|Write error."; // İngilizceye çevrildi
                    send(clientSocket, msg.c_str(), msg.length(), 0);
                }
            }
        }
    }

    close(clientSocket);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket error");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("Bind error");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) < 0) { 
        perror("Listen");
        exit(EXIT_FAILURE);
    }

    threadSafeLog("Server started. Port: " + std::to_string(PORT));

    while (true) {
        if ((new_socket = accept(server_fd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept");
            continue;
        }

        threadSafeLog("New connection accepted.");

        std::thread clientThread(handleClient, new_socket);
        clientThread.detach(); 
    }

    return 0;
}