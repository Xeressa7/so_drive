# ─── so_drive Server — Makefile (Linux / macOS) ──────────────────────────────
#
# Kullanım:
#   make          → derle (sqlite3.c yoksa otomatik indirir)
#   make clean    → object dosyalarını sil
#   make distclean → her şeyi sil (sqlite3 dahil)

CXX      = g++
CC       = gcc
CXXFLAGS = -std=c++17 -O2 -Wall
CFLAGS   = -O2
TARGET   = server

# Linux: -ldl gerekli (SQLite'ın içindeki dlopen için)
# macOS'ta -ldl olmadan da derlenir, zararı yok
LDFLAGS  = -lpthread -ldl

# SQLite amalgamation — 3.47.2 (Aralık 2024)
SQLITE_VERSION = 3470200
SQLITE_YEAR    = 2024
SQLITE_ZIP     = sqlite-amalgamation-$(SQLITE_VERSION).zip
SQLITE_URL     = https://www.sqlite.org/$(SQLITE_YEAR)/$(SQLITE_ZIP)

# ─────────────────────────────────────────────────────────────────────────────

all: $(TARGET)

# sqlite3.c yoksa indir ve çıkart
sqlite3.c sqlite3.h:
	@echo "[*] SQLite amalgamation indiriliyor ($(SQLITE_URL))..."
	@curl -L -o $(SQLITE_ZIP) "$(SQLITE_URL)" 2>/dev/null || \
	 wget -O  $(SQLITE_ZIP) "$(SQLITE_URL)"
	@unzip -j $(SQLITE_ZIP) "*/sqlite3.c" "*/sqlite3.h"
	@rm -f    $(SQLITE_ZIP)
	@echo "[OK] SQLite hazir."

# sqlite3.c C kodu olduğundan C derleyicisiyle ayrı derlenir
sqlite3.o: sqlite3.c sqlite3.h
	$(CC) $(CFLAGS) -c sqlite3.c -o sqlite3.o

server.o: server.cpp
	$(CXX) $(CXXFLAGS) -c server.cpp -o server.o

$(TARGET): server.o sqlite3.o
	$(CXX) server.o sqlite3.o -o $(TARGET) $(LDFLAGS)
	@echo ""
	@echo "[OK] Derleme tamamlandi: ./$(TARGET)"
	@echo "     Port: 12345  |  Kullanici: admin  |  Sifre: admin123"
	@echo ""

clean:
	rm -f *.o $(TARGET) $(TARGET).exe

# sqlite3 kaynak dosyalarını da siler (make ile yeniden indirilir)
distclean: clean
	rm -f sqlite3.c sqlite3.h

.PHONY: all clean distclean
