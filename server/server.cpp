/*
 * so_drive - TCP Dosya Sunucusu (Cross-Platform)
 *
 * Windows ve Linux'ta derlenir. Harici bağımlılık yoktur;
 * SQLite amalgamation (sqlite3.c) bu dizine dahildir.
 *
 * ── Derleme ─────────────────────────────────────────────────
 *
 *   Linux / macOS:
 *     g++ server.cpp sqlite3.c -o server -std=c++17 -lpthread -ldl
 *
 *   Windows (MinGW):
 *     g++ server.cpp sqlite3.c -o server.exe -std=c++17 -lpthread -lws2_32
 *
 *   Veya: build.bat çalıştır (MinGW PATH'te olmalı)
 *
 * ── Kullanıcı Yönetimi ───────────────────────────────────────
 *   İlk çalıştırmada admin/admin123 kullanıcısı otomatik oluşur.
 *   Yeni kullanıcı eklemek için main() içindeki addUser() satırını aç.
 */

// ─── PLATFORM BAĞIMSIZ SOKET KATMANI ─────────────────────────────────────────

#ifdef _WIN32
    // Windows: Winsock2
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>

    typedef SOCKET   socket_t;
    typedef int      socklen_t;   // Windows'ta accept() int* ister

    // Winsock soket fonksiyonlarını POSIX isimlerine eşle
    #define CLOSE_SOCKET(s)          closesocket(s)
    #define READ_SOCKET(s, buf, len) recv((s), (buf), (len), 0)
    #define SOCKET_INVALID           INVALID_SOCKET
    #define SOCKET_ERR               SOCKET_ERROR

#else
    // Linux / macOS: POSIX
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>

    typedef int socket_t;

    #define CLOSE_SOCKET(s)          close(s)
    #define READ_SOCKET(s, buf, len) read((s), (buf), (len))
    #define SOCKET_INVALID           (-1)
    #define SOCKET_ERR               (-1)
#endif

// ─── STANDART KÜTÜPHANELER ────────────────────────────────────────────────────

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <cstring>
#include "sqlite3.h"   // Proje dizinindeki amalgamation header

namespace fs = std::filesystem;

#define PORT        12345
#define BUFFER_SIZE 4096  // 4 KB — tek seferde okunacak maksimum veri

// Birden fazla thread aynı anda log yazmasın
std::mutex logMutex;

// ─── YARDIMCI FONKSİYONLAR ───────────────────────────────────────────────────

void log(const std::string& msg) {
    std::lock_guard<std::mutex> lock(logMutex);
    std::cout << "[SERVER] " << msg << std::endl;
}

/*
 * Veritabanını başlatır.
 * - Tablo yoksa oluşturur.
 * - Hiç kullanıcı yoksa admin/admin123 test kullanıcısı ekler.
 *
 * NOT: Gerçek bir uygulamada şifreler bcrypt gibi bir
 * algoritmayla hash'lenmelidir. Burada düz metin kullanılmıştır.
 */
void initDatabase() {
    sqlite3* db;

    if (sqlite3_open("User_Informations.sqlite", &db) != SQLITE_OK) {
        log("HATA: Veritabani acilamadi — " + std::string(sqlite3_errmsg(db)));
        return;
    }

    // Tablo yoksa oluştur
    const char* createSQL =
        "CREATE TABLE IF NOT EXISTS UserInformation ("
        "  Username   TEXT NOT NULL UNIQUE,"
        "  Password   TEXT NOT NULL,"
        "  UserFolder TEXT NOT NULL"
        ");";

    char* errMsg = nullptr;
    if (sqlite3_exec(db, createSQL, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        log("HATA: Tablo olusturulamadi — " + std::string(errMsg));
        sqlite3_free(errMsg);
        sqlite3_close(db);
        return;
    }

    // Hiç kullanıcı yoksa varsayılan test kullanıcısı ekle
    sqlite3_stmt* stmt;
    int userCount = 0;

    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM UserInformation;", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            userCount = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }

    if (userCount == 0) {
        fs::create_directories("./users/admin");

        sqlite3_exec(db,
            "INSERT INTO UserInformation (Username, Password, UserFolder) "
            "VALUES ('admin', 'admin123', './users/admin');",
            nullptr, nullptr, nullptr);

        log("Varsayilan kullanici olusturuldu — admin / admin123");
        log("Kullanici klasoru: ./users/admin");
    }

    sqlite3_close(db);
    log("Veritabani hazir: User_Informations.sqlite");
}

/*
 * Yeni kullanıcı ekler. main() içinden çağrılır, sonra yorum satırına alınır.
 * Örnek: addUser("ali", "sifre123", "./users/ali");
 */
bool addUser(const std::string& username, const std::string& password, const std::string& folder) {
    sqlite3* db;
    if (sqlite3_open("User_Informations.sqlite", &db) != SQLITE_OK) return false;

    fs::create_directories(folder);

    sqlite3_stmt* stmt;
    bool ok = false;

    if (sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO UserInformation (Username, Password, UserFolder) VALUES (?, ?, ?);",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, folder.c_str(),   -1, SQLITE_STATIC);
        ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    if (ok) log("Kullanici eklendi: " + username);
    return ok;
}

// ─── KİMLİK DOĞRULAMA ────────────────────────────────────────────────────────

/*
 * Kullanıcı adı ve şifreyi veritabanıyla karşılaştırır.
 * Başarılıysa sandbox klasörünü 'userFolder' çıkış parametresine yazar.
 */
bool checkLogin(const std::string& username, const std::string& password, std::string& userFolder) {
    sqlite3* db;
    if (sqlite3_open("User_Informations.sqlite", &db) != SQLITE_OK) return false;

    sqlite3_stmt* stmt;
    bool ok = false;

    if (sqlite3_prepare_v2(db,
        "SELECT UserFolder FROM UserInformation WHERE Username = ? AND Password = ?;",
        -1, &stmt, nullptr) == SQLITE_OK)
    {
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            userFolder = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            ok = true;
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return ok;
}

// ─── DOSYA LİSTELEME ─────────────────────────────────────────────────────────

/*
 * Verilen klasördeki öğeleri listeler.
 * Format: "dosya.txt|1024;klasor|DIR;..."
 */
std::string listFiles(const std::string& path) {
    if (!fs::exists(path))  return "ERR|Klasor bulunamadi";
    if (fs::is_empty(path)) return "EMPTY";

    std::string result;
    for (const auto& entry : fs::directory_iterator(path)) {
        if (!entry.is_regular_file() && !entry.is_directory()) continue;
        result += entry.path().filename().string() + "|";
        result += entry.is_directory() ? "DIR" : std::to_string(entry.file_size());
        result += ";";
    }
    return result;
}

// ─── İSTEMCİ YÖNETİMİ ────────────────────────────────────────────────────────

/*
 * Her istemci için ayrı thread'de çalışır.
 * Gelen komutları okur, parse eder, yanıt gönderir.
 */
void handleClient(socket_t clientSocket) {
    char buffer[BUFFER_SIZE];

    std::string rootFolder;   // Kullanıcının kök klasörü (sandbox sınırı)
    std::string currentPath;  // Şu anki dizin
    bool loggedIn = false;

    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytesRead = (int)READ_SOCKET(clientSocket, buffer, BUFFER_SIZE - 1);

        if (bytesRead <= 0) {
            log("Istemci baglantisi kesildi.");
            break;
        }

        std::string req(buffer);

        // ── GİRİŞ ────────────────────────────────────────────────────────────
        if (req.rfind("LOGIN|", 0) == 0) {
            size_t p1 = req.find('|');
            size_t p2 = req.find('|', p1 + 1);
            if (p1 == std::string::npos || p2 == std::string::npos) continue;

            std::string user = req.substr(p1 + 1, p2 - p1 - 1);
            std::string pass = req.substr(p2 + 1);

            if (checkLogin(user, pass, rootFolder)) {
                loggedIn    = true;
                currentPath = rootFolder;
                if (!fs::exists(rootFolder)) fs::create_directories(rootFolder);
                log("Giris: " + user);
                send(clientSocket, "OK", 2, 0);
            } else {
                log("Giris basarisiz: " + user);
                std::string msg = "ERR|Login Failed";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
            }
            continue;
        }

        if (!loggedIn) continue;

        // ── DİZİN DEĞİŞTİRME ─────────────────────────────────────────────────
        if (req.rfind("CD|", 0) == 0) {
            std::string target = req.substr(3);
            fs::path newPath = (target == "..")
                ? fs::path(currentPath).parent_path()
                : fs::path(currentPath) / target;

            // Sandbox: rootFolder dışına çıkılamaz
            std::string resolved = fs::weakly_canonical(newPath).string();
            std::string root     = fs::weakly_canonical(rootFolder).string();

            if (resolved.find(root) != 0 || !fs::exists(newPath) || !fs::is_directory(newPath)) {
                std::string msg = "ERR|Invalid Directory";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
                continue;
            }

            currentPath = newPath.string();
            std::string list = listFiles(currentPath);
            send(clientSocket, list.c_str(), (int)list.size(), 0);
            continue;
        }

        // ── DOSYA LİSTESİ ─────────────────────────────────────────────────────
        if (req == "LIST_FILES") {
            std::string list = listFiles(currentPath);
            send(clientSocket, list.c_str(), (int)list.size(), 0);
            continue;
        }

        // ── DOSYA İNDİRME ─────────────────────────────────────────────────────
        if (req.rfind("DOWNLOAD|", 0) == 0) {
            std::string filename = req.substr(9);
            fs::path filepath = fs::path(currentPath) / filename;

            // Sandbox kontrolü
            if (fs::weakly_canonical(filepath).string()
                    .find(fs::weakly_canonical(rootFolder).string()) != 0)
            {
                std::string msg = "ERR|Permission Denied";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
                continue;
            }

            if (!fs::exists(filepath)) {
                std::string msg = "ERR|File Not Found";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
                continue;
            }

            std::ifstream file(filepath, std::ios::binary | std::ios::ate);
            long fileSize = (long)file.tellg();
            file.seekg(0, std::ios::beg);

            // Önce boyutu gönder
            std::string header = "SIZE|" + std::to_string(fileSize);
            send(clientSocket, header.c_str(), (int)header.size(), 0);

            // İstemcinin SIZE'ı işlemesi için kısa bekleme
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            // Dosyayı chunk'lar halinde gönder
            char fileBuf[BUFFER_SIZE];
            while (file.read(fileBuf, BUFFER_SIZE) || file.gcount() > 0)
                send(clientSocket, fileBuf, (int)file.gcount(), 0);

            file.close();
            log("Dosya gonderildi: " + filename);
            continue;
        }

        // ── DOSYA YÜKLEME ─────────────────────────────────────────────────────
        if (req.rfind("UPLOAD|", 0) == 0) {
            // Format: UPLOAD|dosyaadi|boyut
            size_t p1 = req.find('|');
            size_t p2 = req.find('|', p1 + 1);
            if (p2 == std::string::npos) continue;

            std::string fileName = req.substr(p1 + 1, p2 - p1 - 1);
            long long   fileSize = std::stoll(req.substr(p2 + 1));
            fs::path    filePath = fs::path(currentPath) / fileName;

            if (fs::weakly_canonical(filePath).string()
                    .find(fs::weakly_canonical(rootFolder).string()) != 0)
            {
                std::string msg = "ERR|Permission Denied";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
                continue;
            }

            // İstemciye "gönder" sinyali ver
            std::string ready = "READY_TO_UPLOAD";
            send(clientSocket, ready.c_str(), (int)ready.size(), 0);

            // Veriyi al ve diske yaz
            std::ofstream outfile(filePath, std::ios::binary);
            if (!outfile.is_open()) { log("HATA: Dosya yazılamadı: " + fileName); continue; }

            long long totalRead = 0;
            char      fileBuf[BUFFER_SIZE];

            while (totalRead < fileSize) {
                int toRead = (int)std::min((long long)BUFFER_SIZE, fileSize - totalRead);
                int got    = (int)READ_SOCKET(clientSocket, fileBuf, toRead);
                if (got <= 0) break;
                outfile.write(fileBuf, got);
                totalRead += got;
            }
            outfile.close();
            log("Upload tamamlandi: " + fileName);

            std::string list = listFiles(currentPath);
            send(clientSocket, list.c_str(), (int)list.size(), 0);
            continue;
        }

        // ── KLASÖR OLUŞTURMA ──────────────────────────────────────────────────
        if (req.rfind("MKDIR|", 0) == 0) {
            std::string folderName = req.substr(6);
            fs::path    newPath    = fs::path(currentPath) / folderName;

            if (fs::weakly_canonical(newPath).string()
                    .find(fs::weakly_canonical(rootFolder).string()) != 0)
            {
                std::string msg = "ERR|Permission Denied";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
                continue;
            }

            if (fs::exists(newPath)) {
                std::string msg = "ERR|Folder already exists";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
                continue;
            }

            if (fs::create_directory(newPath)) {
                std::string list = listFiles(currentPath);
                send(clientSocket, list.c_str(), (int)list.size(), 0);
                log("Klasor olusturuldu: " + folderName);
            } else {
                std::string msg = "ERR|Could not create folder";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
            }
            continue;
        }

        // ── SİLME ─────────────────────────────────────────────────────────────
        if (req.rfind("DELETE|", 0) == 0) {
            std::string targetName = req.substr(7);
            fs::path    targetPath = fs::path(currentPath) / targetName;

            if (fs::weakly_canonical(targetPath).string()
                    .find(fs::weakly_canonical(rootFolder).string()) != 0)
            {
                std::string msg = "ERR|Permission Denied";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
                continue;
            }

            if (!fs::exists(targetPath)) {
                std::string msg = "ERR|Not found";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
                continue;
            }

            try {
                fs::remove_all(targetPath);
                log("Silindi: " + targetName);
                std::string list = listFiles(currentPath);
                send(clientSocket, list.c_str(), (int)list.size(), 0);
            } catch (const fs::filesystem_error& e) {
                std::string msg = "ERR|Delete failed: " + std::string(e.what());
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
            }
            continue;
        }

        // ── DOSYA OLUŞTURMA ───────────────────────────────────────────────────
        if (req.rfind("TOUCH|", 0) == 0) {
            std::string fileName    = req.substr(6);
            fs::path    newFilePath = fs::path(currentPath) / fileName;

            if (fs::weakly_canonical(newFilePath).string()
                    .find(fs::weakly_canonical(rootFolder).string()) != 0)
            {
                std::string msg = "ERR|Permission Denied";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
                continue;
            }

            if (fs::exists(newFilePath)) {
                std::string msg = "ERR|File already exists";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
                continue;
            }

            std::ofstream f(newFilePath);
            if (f.good()) {
                f.close();
                std::string list = listFiles(currentPath);
                send(clientSocket, list.c_str(), (int)list.size(), 0);
                log("Dosya olusturuldu: " + fileName);
            } else {
                std::string msg = "ERR|Could not create file";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
            }
            continue;
        }

        // ── DOSYA OKUMA (önizleme) ────────────────────────────────────────────
        if (req.rfind("CAT|", 0) == 0) {
            std::string fileName = req.substr(4);
            fs::path    filePath = fs::path(currentPath) / fileName;

            if (!fs::exists(filePath) || !fs::is_regular_file(filePath)) {
                std::string msg = "ERR|File not found";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
                continue;
            }

            std::ifstream file(filePath);
            if (!file.is_open()) {
                std::string msg = "ERR|Cannot open file";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
                continue;
            }

            std::string content((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
            file.close();

            std::string response = "CONTENT|" + fileName + "|" + content;
            send(clientSocket, response.c_str(), (int)response.size(), 0);
            continue;
        }

        // ── DOSYA KAYDETME ────────────────────────────────────────────────────
        if (req.rfind("WRITE|", 0) == 0) {
            // Format: WRITE|dosyaadi|icerik
            size_t p1 = req.find('|');
            size_t p2 = req.find('|', p1 + 1);
            if (p2 == std::string::npos) continue;

            std::string fileName = req.substr(p1 + 1, p2 - p1 - 1);
            std::string content  = req.substr(p2 + 1);
            fs::path    filePath = fs::path(currentPath) / fileName;

            std::ofstream outfile(filePath);
            if (outfile.is_open()) {
                outfile << content;
                outfile.close();
                std::string msg = "MSG|File saved successfully.";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
                log("Kaydedildi: " + fileName);
            } else {
                std::string msg = "ERR|Write error";
                send(clientSocket, msg.c_str(), (int)msg.size(), 0);
            }
            continue;
        }
    }

    CLOSE_SOCKET(clientSocket);
}

// ─── ANA PROGRAM ──────────────────────────────────────────────────────────────

int main() {

#ifdef _WIN32
    // Windows: Winsock'u başlat (her uygulama için bir kez yapılmalı)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup basarisiz!" << std::endl;
        return 1;
    }
#endif

    initDatabase();

    /*
     * Yeni kullanıcı eklemek için bu satırı açıp derle/çalıştır,
     * sonra tekrar yorum satırına al:
     *
     * addUser("ali", "sifre123", "./users/ali");
     */

    // Sunucu soketi oluştur
    socket_t serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == SOCKET_INVALID) { perror("socket"); return 1; }

    // Yeniden başlatmada port hemen kullanılabilsin
    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(PORT);

    if (bind(serverFd, (sockaddr*)&address, sizeof(address)) == SOCKET_ERR) { perror("bind");   return 1; }
    if (listen(serverFd, 10)                                 == SOCKET_ERR) { perror("listen"); return 1; }

    log("Sunucu basladi. Port: " + std::to_string(PORT));
    log("Ayni makinede test icin: 127.0.0.1:" + std::to_string(PORT));

    while (true) {
        socklen_t addrLen    = sizeof(address);
        socket_t clientSocket = accept(serverFd, (sockaddr*)&address, &addrLen);

        if (clientSocket == SOCKET_INVALID) { perror("accept"); continue; }

        log("Yeni baglanti.");
        std::thread(handleClient, clientSocket).detach();
    }

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
