/*
 * so_drive - TCP Dosya Sunucusu
 *
 * Her bağlanan istemci için ayrı bir thread açılır.
 * Kimlik doğrulama SQLite veritabanı üzerinden yapılır.
 * Her kullanıcının erişimi kendi klasörüyle sınırlıdır (sandbox).
 *
 * Derleme:
 *   g++ server.cpp -o server -std=c++17 -lpthread -lsqlite3
 *
 * Gereksinimler:
 *   sudo apt install g++ libsqlite3-dev
 */

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

#define PORT        12345
#define BUFFER_SIZE 4096  // 4 KB — tek seferde okunacak maksimum veri

// Birden fazla thread aynı anda log yazmasın diye mutex
std::mutex logMutex;

// ─── YARDIMCI FONKSİYONLAR ──────────────────────────────────────────────────

void log(const std::string& msg) {
    std::lock_guard<std::mutex> lock(logMutex);
    std::cout << "[SERVER] " << msg << std::endl;
}

/*
 * Veritabanını başlatır: tablo yoksa oluşturur, hiç kullanıcı yoksa
 * "admin / admin123" test kullanıcısı ekler.
 *
 * NOT: Gerçek bir uygulamada şifreler düz metin yerine bcrypt gibi
 * bir algoritmayla hash'lenmelidir. Bu proje için basitlik adına
 * düz metin kullanılmıştır.
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
    const char* countSQL = "SELECT COUNT(*) FROM UserInformation;";
    sqlite3_stmt* stmt;
    int userCount = 0;

    if (sqlite3_prepare_v2(db, countSQL, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            userCount = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }

    if (userCount == 0) {
        // ./users/admin klasörünü oluştur
        fs::create_directories("./users/admin");

        const char* insertSQL =
            "INSERT INTO UserInformation (Username, Password, UserFolder) "
            "VALUES ('admin', 'admin123', './users/admin');";

        sqlite3_exec(db, insertSQL, nullptr, nullptr, nullptr);
        log("Varsayilan kullanici olusturuldu — Kullanici: admin | Sifre: admin123");
        log("Kullanici klasoru: ./users/admin");
    }

    sqlite3_close(db);
    log("Veritabani hazir: User_Informations.sqlite");
}

/*
 * Yeni kullanıcı eklemek için yardımcı fonksiyon.
 * Sunucu koduna doğrudan çağrı ekleyerek kullanılabilir.
 *
 * Örnek: addUser("ali", "sifre123", "./users/ali");
 */
bool addUser(const std::string& username, const std::string& password, const std::string& folder) {
    sqlite3* db;
    if (sqlite3_open("User_Informations.sqlite", &db) != SQLITE_OK)
        return false;

    fs::create_directories(folder);

    const char* sql =
        "INSERT OR IGNORE INTO UserInformation (Username, Password, UserFolder) "
        "VALUES (?, ?, ?);";

    sqlite3_stmt* stmt;
    bool success = false;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, folder.c_str(),   -1, SQLITE_STATIC);
        success = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return success;
}

// ─── KİMLİK DOĞRULAMA ───────────────────────────────────────────────────────

/*
 * Kullanıcı adı ve şifreyi veritabanıyla karşılaştırır.
 * Başarılıysa kullanıcının sandbox klasörünü 'userFolder' çıkış parametresine yazar.
 */
bool checkLogin(const std::string& username, const std::string& password, std::string& userFolder) {
    sqlite3* db;
    sqlite3_stmt* stmt;

    if (sqlite3_open("User_Informations.sqlite", &db) != SQLITE_OK) {
        log("HATA: Veritabani acilamadi!");
        return false;
    }

    const char* sql =
        "SELECT UserFolder FROM UserInformation "
        "WHERE Username = ? AND Password = ?;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        log("HATA: SQL hazirlanamadi.");
        sqlite3_close(db);
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);

    bool authenticated = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* folder = sqlite3_column_text(stmt, 0);
        userFolder = reinterpret_cast<const char*>(folder);
        authenticated = true;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return authenticated;
}

// ─── DOSYA LİSTELEME ────────────────────────────────────────────────────────

/*
 * Verilen klasördeki dosya ve klasörleri listeler.
 * Format: "dosya.txt|1024;klasor|DIR;..."
 */
std::string listFiles(const std::string& path) {
    if (!fs::exists(path))
        return "ERR|Klasor bulunamadi";

    if (fs::is_empty(path))
        return "EMPTY";

    std::string result;
    for (const auto& entry : fs::directory_iterator(path)) {
        if (!entry.is_regular_file() && !entry.is_directory())
            continue;

        result += entry.path().filename().string() + "|";
        result += entry.is_directory() ? "DIR" : std::to_string(entry.file_size());
        result += ";";
    }
    return result;
}

// ─── İSTEMCİ YÖNETİMİ ───────────────────────────────────────────────────────

/*
 * Her istemci için ayrı bir thread'de çalışır.
 * Gelen komutları parse edip uygun işlemi gerçekleştirir.
 */
void handleClient(int clientSocket) {
    char buffer[BUFFER_SIZE];

    std::string rootFolder;   // Kullanıcının sandbox kök klasörü (dışına çıkamaz)
    std::string currentPath;  // Kullanıcının şu anki konumu
    bool loggedIn = false;

    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytesRead = read(clientSocket, buffer, BUFFER_SIZE - 1);

        if (bytesRead <= 0) {
            log("Istemci baglantisi kesildi.");
            break;
        }

        std::string req(buffer);

        // ── GİRİŞ ───────────────────────────────────────────────────────────
        if (req.rfind("LOGIN|", 0) == 0) {
            size_t p1 = req.find('|');
            size_t p2 = req.find('|', p1 + 1);
            if (p1 == std::string::npos || p2 == std::string::npos) continue;

            std::string user = req.substr(p1 + 1, p2 - p1 - 1);
            std::string pass = req.substr(p2 + 1);

            if (checkLogin(user, pass, rootFolder)) {
                loggedIn = true;
                currentPath = rootFolder;

                // Kullanıcı klasörü yoksa oluştur
                if (!fs::exists(rootFolder))
                    fs::create_directories(rootFolder);

                log("Giris basarili: " + user);
                send(clientSocket, "OK", 2, 0);
            } else {
                log("Giris basarisiz: " + user);
                std::string msg = "ERR|Login Failed";
                send(clientSocket, msg.c_str(), msg.size(), 0);
            }
            continue;
        }

        // Giriş yapılmadan diğer komutlar çalışmasın
        if (!loggedIn) continue;

        // ── DİZİN DEĞİŞTİRME ────────────────────────────────────────────────
        if (req.rfind("CD|", 0) == 0) {
            std::string target = req.substr(3);
            fs::path newPath;

            if (target == "..")
                newPath = fs::path(currentPath).parent_path();
            else
                newPath = fs::path(currentPath) / target;

            // Sandbox güvenlik kontrolü: rootFolder dışına çıkılamaz
            std::string resolved = fs::weakly_canonical(newPath).string();
            if (resolved.find(fs::weakly_canonical(rootFolder).string()) != 0
                || !fs::exists(newPath) || !fs::is_directory(newPath))
            {
                std::string msg = "ERR|Invalid Directory";
                send(clientSocket, msg.c_str(), msg.size(), 0);
                continue;
            }

            currentPath = newPath.string();
            std::string list = listFiles(currentPath);
            send(clientSocket, list.c_str(), list.size(), 0);
            continue;
        }

        // ── DOSYA LİSTESİ ───────────────────────────────────────────────────
        if (req == "LIST_FILES") {
            std::string list = listFiles(currentPath);
            send(clientSocket, list.c_str(), list.size(), 0);
            continue;
        }

        // ── DOSYA İNDİRME ───────────────────────────────────────────────────
        if (req.rfind("DOWNLOAD|", 0) == 0) {
            std::string filename = req.substr(9);
            fs::path filepath = fs::path(currentPath) / filename;

            // Sandbox kontrolü
            if (fs::weakly_canonical(filepath).string()
                    .find(fs::weakly_canonical(rootFolder).string()) != 0)
            {
                std::string msg = "ERR|Permission Denied";
                send(clientSocket, msg.c_str(), msg.size(), 0);
                continue;
            }

            if (!fs::exists(filepath)) {
                std::string msg = "ERR|File Not Found";
                send(clientSocket, msg.c_str(), msg.size(), 0);
                continue;
            }

            std::ifstream file(filepath, std::ios::binary | std::ios::ate);
            long fileSize = file.tellg();
            file.seekg(0, std::ios::beg);

            // Önce boyutu gönder, sonra veriyi
            std::string header = "SIZE|" + std::to_string(fileSize);
            send(clientSocket, header.c_str(), header.size(), 0);

            // İstemcinin SIZE mesajını işlemesi için kısa bekleme
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            char fileBuf[BUFFER_SIZE];
            while (file.read(fileBuf, BUFFER_SIZE) || file.gcount() > 0) {
                send(clientSocket, fileBuf, file.gcount(), 0);
            }
            file.close();
            log("Dosya gonderildi: " + filename);
            continue;
        }

        // ── DOSYA YÜKLEME ────────────────────────────────────────────────────
        if (req.rfind("UPLOAD|", 0) == 0) {
            // Format: UPLOAD|dosyaadi|boyut
            size_t p1 = req.find('|');
            size_t p2 = req.find('|', p1 + 1);
            if (p2 == std::string::npos) continue;

            std::string fileName = req.substr(p1 + 1, p2 - p1 - 1);
            long long fileSize   = std::stoll(req.substr(p2 + 1));
            fs::path filePath    = fs::path(currentPath) / fileName;

            // Sandbox kontrolü
            if (fs::weakly_canonical(filePath).string()
                    .find(fs::weakly_canonical(rootFolder).string()) != 0)
            {
                std::string msg = "ERR|Permission Denied";
                send(clientSocket, msg.c_str(), msg.size(), 0);
                continue;
            }

            log("Upload basliyor: " + fileName + " (" + std::to_string(fileSize) + " byte)");

            // İstemciye hazır olduğunu bildir
            std::string ready = "READY_TO_UPLOAD";
            send(clientSocket, ready.c_str(), ready.size(), 0);

            // Dosyayı al ve diske yaz
            std::ofstream outfile(filePath, std::ios::binary);
            if (!outfile.is_open()) {
                log("HATA: Dosya yazma acilamadi: " + fileName);
                continue;
            }

            long long totalRead = 0;
            char fileBuf[BUFFER_SIZE];

            while (totalRead < fileSize) {
                int toRead = (int)std::min((long long)BUFFER_SIZE, fileSize - totalRead);
                ssize_t got = read(clientSocket, fileBuf, toRead);
                if (got <= 0) break;
                outfile.write(fileBuf, got);
                totalRead += got;
            }
            outfile.close();
            log("Upload tamamlandi: " + fileName);

            // Yükleme sonrası dosya listesini güncelle
            std::string list = listFiles(currentPath);
            send(clientSocket, list.c_str(), list.size(), 0);
            continue;
        }

        // ── KLASÖR OLUŞTURMA ─────────────────────────────────────────────────
        if (req.rfind("MKDIR|", 0) == 0) {
            std::string folderName = req.substr(6);
            fs::path newPath = fs::path(currentPath) / folderName;

            if (fs::weakly_canonical(newPath).string()
                    .find(fs::weakly_canonical(rootFolder).string()) != 0)
            {
                std::string msg = "ERR|Permission Denied";
                send(clientSocket, msg.c_str(), msg.size(), 0);
                continue;
            }

            if (fs::exists(newPath)) {
                std::string msg = "ERR|Folder already exists";
                send(clientSocket, msg.c_str(), msg.size(), 0);
                continue;
            }

            if (fs::create_directory(newPath)) {
                log("Klasor olusturuldu: " + folderName);
                std::string list = listFiles(currentPath);
                send(clientSocket, list.c_str(), list.size(), 0);
            } else {
                std::string msg = "ERR|Could not create folder";
                send(clientSocket, msg.c_str(), msg.size(), 0);
            }
            continue;
        }

        // ── SİLME ────────────────────────────────────────────────────────────
        if (req.rfind("DELETE|", 0) == 0) {
            std::string targetName = req.substr(7);
            fs::path targetPath = fs::path(currentPath) / targetName;

            if (fs::weakly_canonical(targetPath).string()
                    .find(fs::weakly_canonical(rootFolder).string()) != 0)
            {
                std::string msg = "ERR|Permission Denied";
                send(clientSocket, msg.c_str(), msg.size(), 0);
                continue;
            }

            if (!fs::exists(targetPath)) {
                std::string msg = "ERR|File or folder not found";
                send(clientSocket, msg.c_str(), msg.size(), 0);
                continue;
            }

            try {
                fs::remove_all(targetPath);
                log("Silindi: " + targetName);
                std::string list = listFiles(currentPath);
                send(clientSocket, list.c_str(), list.size(), 0);
            } catch (const fs::filesystem_error& e) {
                std::string msg = "ERR|Could not delete: " + std::string(e.what());
                send(clientSocket, msg.c_str(), msg.size(), 0);
            }
            continue;
        }

        // ── DOSYA OLUŞTURMA ──────────────────────────────────────────────────
        if (req.rfind("TOUCH|", 0) == 0) {
            std::string fileName = req.substr(6);
            fs::path newFilePath = fs::path(currentPath) / fileName;

            if (fs::weakly_canonical(newFilePath).string()
                    .find(fs::weakly_canonical(rootFolder).string()) != 0)
            {
                std::string msg = "ERR|Permission Denied";
                send(clientSocket, msg.c_str(), msg.size(), 0);
                continue;
            }

            if (fs::exists(newFilePath)) {
                std::string msg = "ERR|File already exists";
                send(clientSocket, msg.c_str(), msg.size(), 0);
                continue;
            }

            std::ofstream outfile(newFilePath);
            if (outfile.good()) {
                outfile.close();
                log("Dosya olusturuldu: " + fileName);
                std::string list = listFiles(currentPath);
                send(clientSocket, list.c_str(), list.size(), 0);
            } else {
                std::string msg = "ERR|Could not create file";
                send(clientSocket, msg.c_str(), msg.size(), 0);
            }
            continue;
        }

        // ── DOSYA OKUMA (önizleme) ───────────────────────────────────────────
        if (req.rfind("CAT|", 0) == 0) {
            std::string fileName = req.substr(4);
            fs::path filePath = fs::path(currentPath) / fileName;

            if (!fs::exists(filePath) || !fs::is_regular_file(filePath)) {
                std::string msg = "ERR|File not found";
                send(clientSocket, msg.c_str(), msg.size(), 0);
                continue;
            }

            std::ifstream file(filePath);
            if (!file.is_open()) {
                std::string msg = "ERR|File could not be opened";
                send(clientSocket, msg.c_str(), msg.size(), 0);
                continue;
            }

            std::string content((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
            file.close();

            std::string response = "CONTENT|" + fileName + "|" + content;
            send(clientSocket, response.c_str(), response.size(), 0);
            continue;
        }

        // ── DOSYA KAYDETME ───────────────────────────────────────────────────
        if (req.rfind("WRITE|", 0) == 0) {
            // Format: WRITE|dosyaadi|icerik
            size_t p1 = req.find('|');
            size_t p2 = req.find('|', p1 + 1);
            if (p2 == std::string::npos) continue;

            std::string fileName = req.substr(p1 + 1, p2 - p1 - 1);
            std::string content  = req.substr(p2 + 1);
            fs::path filePath    = fs::path(currentPath) / fileName;

            std::ofstream outfile(filePath);
            if (outfile.is_open()) {
                outfile << content;
                outfile.close();
                std::string msg = "MSG|File saved successfully.";
                send(clientSocket, msg.c_str(), msg.size(), 0);
                log("Dosya kaydedildi: " + fileName);
            } else {
                std::string msg = "ERR|Write error";
                send(clientSocket, msg.c_str(), msg.size(), 0);
            }
            continue;
        }
    }

    close(clientSocket);
}

// ─── ANA PROGRAM ─────────────────────────────────────────────────────────────

int main() {
    // Sunucu başlamadan önce veritabanını hazırla
    initDatabase();

    /*
     * Yeni kullanıcı eklemek istersen aşağıdaki satırı açıp derle,
     * çalıştır, sonra yorum satırına al:
     *
     * addUser("kullanici", "sifre", "./users/kullanici");
     */

    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) { perror("socket"); return 1; }

    // Sunucu yeniden başlatıldığında port hemen kullanılabilsin
    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port        = htons(PORT);

    if (bind(serverFd, (sockaddr*)&address, sizeof(address)) < 0) { perror("bind"); return 1; }
    if (listen(serverFd, 10) < 0)                                  { perror("listen"); return 1; }

    log("Sunucu basladi. Port: " + std::to_string(PORT));

    while (true) {
        socklen_t addrLen = sizeof(address);
        int clientSocket = accept(serverFd, (sockaddr*)&address, &addrLen);
        if (clientSocket < 0) { perror("accept"); continue; }

        log("Yeni baglanti kabul edildi.");

        // Her istemci için ayrı thread aç, detach et (thread kendi sonlanır)
        std::thread(handleClient, clientSocket).detach();
    }

    return 0;
}
