#include "filewindow.h"
#include "ui_filewindow.h"
#include "dialognewitem.h"
#include <QFileInfo>
#include <QMessageBox>

// ─── CONSTRUCTOR / DESTRUCTOR ─────────────────────────────────────────────────

FileWindow::FileWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::FileWindow)
    , socket(nullptr)
    , isDownloading(false)
    , downloadFileSize(0)
    , receivedBytes(0)
    , localFile(nullptr)
    , isUploading(false)
    , uploadFile(nullptr)
{
    ui->setupUi(this);

    // Editör sekmesi başlangıcı — sadece bir sekme olsun
    ui->tab_editor->setTabText(0, "Editor");
    while (ui->tab_editor->count() > 1)
        ui->tab_editor->removeTab(1);

    // Dosya listesi ikon boyutu
    ui->widget_directory_list->setIconSize(QSize(32, 32));

    // Editör stili (karanlık tema)
    ui->text_edit_editor->setStyleSheet("color: white; background-color: #2b2b2b;");

    // Adres çubuğunu başlangıç konumuna ayarla
    ui->label_path->setText("/");

    // Sağ tık menüsünü etkinleştir
    ui->widget_directory_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->widget_directory_list, &QListWidget::customContextMenuRequested,
            this, &FileWindow::show_context_menu);
}

FileWindow::~FileWindow()
{
    // Açık dosyaları temizle (crash önlemek için)
    if (localFile) {
        if (localFile->isOpen()) localFile->close();
        delete localFile;
    }
    if (uploadFile) {
        if (uploadFile->isOpen()) uploadFile->close();
        delete uploadFile;
    }
    delete ui;
}

// ─── SOKET DEVRALMA ──────────────────────────────────────────────────────────

void FileWindow::setSocket(QTcpSocket *s)
{
    socket = s;

    // Eski bağlantıları temizle, bu pencere için yeniden kur
    socket->disconnect();
    connect(socket, &QTcpSocket::readyRead, this, &FileWindow::onReadyRead);
    connect(ui->widget_directory_list, &QListWidget::itemDoubleClicked,
            this, &FileWindow::onItemDoubleClicked);

    // Soket hazır, ilk dosya listesini iste
    if (socket->isOpen())
        socket->write("LIST_FILES");
}

// ─── VERİ OKUMA VE İŞLEME ────────────────────────────────────────────────────

/*
 * TCP'den gelen tüm veriler buraya düşer.
 * İki farklı mod var:
 *   1. İndirme modu (isDownloading == true): gelen byte'lar doğrudan diske yazılır.
 *   2. Komut modu: gelen string parse edilip ilgili işlem yapılır.
 */
void FileWindow::onReadyRead()
{
    // --- Mod 1: İndirme devam ediyor ---
    if (isDownloading) {
        QByteArray chunk = socket->readAll();

        if (localFile && localFile->isOpen()) {
            localFile->write(chunk);
            receivedBytes += chunk.size();
        }

        // Beklenen boyuta ulaşıldıysa indirme tamamdır
        if (receivedBytes >= downloadFileSize) {
            isDownloading = false;
            localFile->close();
            delete localFile;
            localFile = nullptr;
            QMessageBox::information(this, "Başarılı", "Dosya indirildi.");
        }
        return;
    }

    // --- Mod 2: Normal komut yanıtı ---
    QByteArray data = socket->readAll();

    // SIZE mesajı: indirme başlamak üzere, boyutu öğren
    if (data.startsWith("SIZE|")) {
        // Format: "SIZE|12345" — ardından binary veri gelebilir (aynı pakette)
        int pipeIdx = data.indexOf('|');
        int dataStart = pipeIdx + 1;

        // Boyut string'inin nerede bittiğini bul
        while (dataStart < data.size() && data[dataStart] >= '0' && data[dataStart] <= '9')
            dataStart++;

        QString sizeStr = QString::fromUtf8(data.mid(pipeIdx + 1, dataStart - pipeIdx - 1));
        bool ok;
        downloadFileSize = sizeStr.toLongLong(&ok);

        if (!ok || !localFile || !localFile->isOpen()) return;

        if (downloadFileSize == 0) {
            // Boş dosya
            localFile->close();
            delete localFile;
            localFile = nullptr;
            QMessageBox::information(this, "Başarılı", "Boş dosya indirildi.");
            return;
        }

        // İndirme moduna geç
        isDownloading = true;
        receivedBytes = 0;

        // SIZE ile aynı pakette gelen veri varsa hemen yaz
        if (dataStart < data.size()) {
            QByteArray leftover = data.mid(dataStart);
            localFile->write(leftover);
            receivedBytes += leftover.size();

            if (receivedBytes >= downloadFileSize) {
                isDownloading = false;
                localFile->close();
                delete localFile;
                localFile = nullptr;
                QMessageBox::information(this, "Başarılı", "Dosya indirildi.");
            }
        }
        return;
    }

    QString message = QString::fromUtf8(data);

    // Hata mesajı
    if (message.startsWith("ERR|")) {
        QMessageBox::warning(this, "Hata", message.mid(4));

        // İndirme başlamadan hata geldiyse yarım dosyayı sil
        if (localFile) {
            localFile->close();
            localFile->remove();
            delete localFile;
            localFile = nullptr;
            isDownloading = false;
        }
        return;
    }

    // Bilgi mesajı (kaydetme vb.)
    if (message.startsWith("MSG|")) {
        QMessageBox::information(this, "Sunucu", message.mid(4));
        return;
    }

    // Dosya içeriği (CAT komutu yanıtı)
    if (message.startsWith("CONTENT|")) {
        int pipe1 = message.indexOf('|');
        int pipe2 = message.indexOf('|', pipe1 + 1);
        if (pipe2 == -1) return;

        QString fileName = message.mid(pipe1 + 1, pipe2 - pipe1 - 1);
        QString content  = message.mid(pipe2 + 1);

        currentOpenFile = fileName;
        ui->text_edit_editor->setPlainText(content);
        ui->tab_editor->setTabText(0, fileName);
        return;
    }

    // Upload onayı: sunucu hazır, dosyayı gönder
    if (isUploading && message.startsWith("READY_TO_UPLOAD")) {
        // Tüm dosyayı tek seferde yaz — Qt soketi arka planda buffer'layıp gönderir
        QByteArray fileData = uploadFile->readAll();
        socket->write(fileData);

        uploadFile->close();
        delete uploadFile;
        uploadFile = nullptr;
        isUploading = false;

        QMessageBox::information(this, "Başarılı", "Dosya yüklendi.");
        return;
    }

    // Yukarıdakilerin hiçbiri değilse dosya listesidir
    updateFileList(message);
}

// ─── İNDİRME ─────────────────────────────────────────────────────────────────

void FileWindow::start_download(const QString &fileName)
{
    // Kullanıcıdan kayıt yeri iste
    QString savePath = QFileDialog::getSaveFileName(
        this, "Dosyayı Kaydet", QDir::homePath() + "/" + fileName);
    if (savePath.isEmpty()) return;

    // Kayıt dosyasını aç
    if (localFile) delete localFile;
    localFile = new QFile(savePath);

    if (!localFile->open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Hata", "Dosya diske kaydedilemedi.");
        delete localFile;
        localFile = nullptr;
        return;
    }

    socket->write(("DOWNLOAD|" + fileName).toUtf8());
}

void FileWindow::on_button_download_a_file_or_folder_clicked()
{
    QListWidgetItem *item = ui->widget_directory_list->currentItem();
    if (!item) {
        QMessageBox::warning(this, "Uyarı", "Lütfen bir dosya seçin.");
        return;
    }

    bool isDir = item->data(Qt::UserRole).toBool();
    if (isDir) {
        QMessageBox::information(this, "Bilgi", "Klasör indirme henüz desteklenmiyor.");
        return;
    }

    start_download(item->data(Qt::UserRole + 1).toString());
}

// ─── YÜKLEME ─────────────────────────────────────────────────────────────────

void FileWindow::on_button_upload_a_file_or_folder_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Yüklenecek Dosyayı Seç");
    if (filePath.isEmpty()) return;

    QFileInfo fi(filePath);

    if (uploadFile) delete uploadFile;
    uploadFile = new QFile(filePath);

    if (!uploadFile->open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Hata", "Dosya açılamadı.");
        delete uploadFile;
        uploadFile = nullptr;
        return;
    }

    // Sunucuya boyutu bildir, onay beklenir (READY_TO_UPLOAD)
    QString cmd = "UPLOAD|" + fi.fileName() + "|" + QString::number(fi.size());
    socket->write(cmd.toUtf8());
    isUploading = true;
}

// ─── SAĞ TIK MENÜSÜ ──────────────────────────────────────────────────────────

void FileWindow::show_context_menu(const QPoint &pos)
{
    QListWidgetItem *item = ui->widget_directory_list->itemAt(pos);
    if (!item) return;

    QMenu menu(this);
    menu.addAction("Open",     this, &FileWindow::open_item);
    menu.addAction("Download", this, &FileWindow::on_button_download_a_file_or_folder_clicked);
    menu.addAction("Delete",   this, &FileWindow::delete_item);
    menu.exec(ui->widget_directory_list->mapToGlobal(pos));
}

void FileWindow::delete_item()
{
    QListWidgetItem *item = ui->widget_directory_list->currentItem();
    if (!item) return;

    QString name = item->data(Qt::UserRole + 1).toString();

    auto reply = QMessageBox::question(this, "Sil",
        "'" + name + "' silinecek. Emin misin?\nBu işlem geri alınamaz.",
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes)
        socket->write(("DELETE|" + name).toUtf8());
}

void FileWindow::open_item()
{
    QListWidgetItem *item = ui->widget_directory_list->currentItem();
    if (item) onItemDoubleClicked(item);
}

// ─── DOSYA LİSTESİ ───────────────────────────────────────────────────────────

/*
 * Sunucudan gelen dosya listesi string'ini parse eder.
 * Format: "dosya.txt|1024;klasor|DIR;..."
 */
void FileWindow::updateFileList(const QString &data)
{
    ui->widget_directory_list->clear();

    if (data == "EMPTY") return;

    for (const QString &entry : data.split(';', Qt::SkipEmptyParts)) {
        QStringList parts = entry.split('|');
        if (parts.size() < 2) continue;

        QString name  = parts[0];
        QString meta  = parts[1];
        bool    isDir = (meta == "DIR");

        QListWidgetItem *listItem = new QListWidgetItem();
        listItem->setIcon(getIconForFile(name, isDir));
        listItem->setData(Qt::UserRole,     isDir);
        listItem->setData(Qt::UserRole + 1, name);

        if (isDir) {
            listItem->setForeground(QBrush(QColor("#00ff00")));
            listItem->setText(name);
        } else {
            listItem->setForeground(QBrush(QColor("#ffffff")));
            listItem->setText(name + "  (" + meta + " bytes)");
        }

        ui->widget_directory_list->addItem(listItem);
    }
}

// ─── ÇİFT TIKLAMA ────────────────────────────────────────────────────────────

void FileWindow::onItemDoubleClicked(QListWidgetItem *item)
{
    bool    isDir = item->data(Qt::UserRole).toBool();
    QString name  = item->data(Qt::UserRole + 1).toString();

    if (isDir) {
        // Klasöre gir, geçmişe ekle
        historyStack.push(name);
        forwardStack.clear();
        updateAddressBar();
        socket->write(("CD|" + name).toUtf8());
    } else {
        if (is_file_readable(name)) {
            // Metin dosyası: editörde göster
            socket->write(("CAT|" + name).toUtf8());
        } else {
            // Binary dosya: indirme teklif et
            auto reply = QMessageBox::question(this, "Önizleme Yok",
                "Bu dosya editörde açılamaz. İndirmek ister misin?",
                QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::Yes)
                start_download(name);
        }
    }
}

// ─── DOSYA KAYDETME ──────────────────────────────────────────────────────────

void FileWindow::on_button_save_file_clicked()
{
    if (currentOpenFile.isEmpty()) {
        QMessageBox::warning(this, "Hata", "Açık dosya yok.");
        return;
    }

    QString content = ui->text_edit_editor->toPlainText();
    socket->write(("WRITE|" + currentOpenFile + "|" + content).toUtf8());
}

// ─── NAVİGASYON ──────────────────────────────────────────────────────────────

void FileWindow::on_button_back_clicked()
{
    if (historyStack.isEmpty()) return;

    forwardStack.push(historyStack.pop());
    updateAddressBar();
    socket->write("CD|..");
}

void FileWindow::on_button_next_clicked()
{
    if (forwardStack.isEmpty()) return;

    QString next = forwardStack.pop();
    historyStack.push(next);
    updateAddressBar();
    socket->write(("CD|" + next).toUtf8());
}

void FileWindow::updateAddressBar()
{
    QString path = "/";
    for (const QString &dir : historyStack)
        path += dir + "/";
    ui->label_path->setText(path);
}

// ─── YENİ ÖĞE DİYALOGLARI ───────────────────────────────────────────────────

void FileWindow::on_button_create_a_file_clicked()
{
    DialogNewItem dialog(this);
    dialog.setMode(DialogNewItem::FileMode);
    if (dialog.exec() != QDialog::Accepted) return;

    QString name = dialog.getName().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "Uyarı", "Dosya adı boş olamaz.");
        return;
    }

    socket->write(("TOUCH|" + name + dialog.getExtension()).toUtf8());
}

void FileWindow::on_button_create_a_folder_clicked()
{
    DialogNewItem dialog(this);
    dialog.setMode(DialogNewItem::FolderMode);
    if (dialog.exec() != QDialog::Accepted) return;

    QString name = dialog.getName().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, "Uyarı", "Klasör adı boş olamaz.");
        return;
    }

    socket->write(("MKDIR|" + name).toUtf8());
}

// ─── ÇIKIŞ ───────────────────────────────────────────────────────────────────

void FileWindow::on_button_logout_clicked()
{
    if (socket && socket->isOpen())
        socket->disconnectFromHost();

    // MainWindow'u uyar, kendini göstersin
    emit loggedOut();
    this->close();
}

// ─── YARDIMCILAR ─────────────────────────────────────────────────────────────

/*
 * Uzantıya göre dosyanın editörde okunabilir olup olmadığını belirler.
 */
bool FileWindow::is_file_readable(const QString &fileName)
{
    QString ext = QFileInfo(fileName).suffix().toLower();
    static const QStringList readable = {
        "txt","c","cpp","h","hpp","py","js","ts","json","xml",
        "html","htm","css","sql","log","ini","md","sh","yaml","yml"
    };
    return readable.contains(ext);
}

/*
 * Dosya adı ve türüne göre uygun ikonu döner.
 * İkonlar resources.qrc içindeki images/ klasöründen gelir.
 */
QIcon FileWindow::getIconForFile(const QString &name, bool isDir)
{
    if (isDir) return QIcon(":/new/prefix1/images/ic_folder.png");

    QString ext = QFileInfo(name).suffix().toLower();

    if (ext == "cpp" || ext == "h" || ext == "hpp" || ext == "c")
        return QIcon(":/new/prefix1/images/ic_cpp.png");
    if (ext == "py" || ext == "pyw")
        return QIcon(":/new/prefix1/images/ic_py.png");
    if (ext == "json")
        return QIcon(":/new/prefix1/images/ic_json.png");
    if (ext == "xml" || ext == "ui")
        return QIcon(":/new/prefix1/images/ic_xml.png");
    if (ext == "sql" || ext == "sqlite" || ext == "db")
        return QIcon(":/new/prefix1/images/ic_sql.png");
    if (ext == "html" || ext == "htm" || ext == "css" || ext == "js")
        return QIcon(":/new/prefix1/images/ic_html.png");
    if (ext == "txt" || ext == "log" || ext == "ini" || ext == "md")
        return QIcon(":/new/prefix1/images/ic_txt.png");
    if (ext == "pdf")
        return QIcon(":/new/prefix1/images/ic_pdf.png");
    if (ext == "docx" || ext == "doc")
        return QIcon(":/new/prefix1/images/ic_word.png");
    if (ext == "xlsx" || ext == "xls" || ext == "csv")
        return QIcon(":/new/prefix1/images/ic_excel.png");
    if (ext == "pptx" || ext == "ppt")
        return QIcon(":/new/prefix1/images/ic_ppt.png");
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "gif" || ext == "ico")
        return QIcon(":/new/prefix1/images/ic_image.png");
    if (ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "mov")
        return QIcon(":/new/prefix1/images/ic_video.png");
    if (ext == "mp3" || ext == "wav" || ext == "ogg" || ext == "flac")
        return QIcon(":/new/prefix1/images/ic_audio.png");
    if (ext == "zip" || ext == "rar" || ext == "7z" || ext == "tar" || ext == "gz")
        return QIcon(":/new/prefix1/images/ic_zip.png");
    if (ext == "exe" || ext == "msi" || ext == "bat" || ext == "sh")
        return QIcon(":/new/prefix1/images/ic_exe.png");

    return QIcon(":/new/prefix1/images/ic_file.png");
}
