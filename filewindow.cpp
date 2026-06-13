#include "filewindow.h"
#include "ui_filewindow.h"
#include "dialognewitem.h"
#include <QFileInfo>
#include <QDebug>
#include <QMessageBox>

FileWindow::FileWindow(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::FileWindow)
{
    ui->setupUi(this);

    // varsayılan tabları silme
    ui->tab_editor->setTabText(0, "Editor");
    while (ui->tab_editor->count() > 1) {
        ui->tab_editor->removeTab(1);
    }

    ui->widget_directory_list->setIconSize(QSize(32, 32));
    ui->textEdit->setFixedHeight(30);
    ui->textEdit->setText("/");

    ui->text_edit_editor->setStyleSheet("color: white; background-color: #2b2b2b;");

    ui->widget_directory_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->widget_directory_list, &QListWidget::customContextMenuRequested, this, &FileWindow::show_context_menu);

    is_downloading = false;
    is_uploading = false;
    local_file = nullptr;
    upload_file = nullptr;
    download_file_size = 0;
    received_bytes = 0;
}

FileWindow::~FileWindow()
{
    if (local_file) {
        if (local_file->isOpen()) local_file->close();
        delete local_file;
    }
    if (upload_file) {
        if (upload_file->isOpen()) upload_file->close();
        delete upload_file;
    }
    delete ui;
}

// --- SOKET AYARLARI ---
void FileWindow::setSocket(QTcpSocket *s)
{
    this->socket = s;
    socket->disconnect();

    connect(socket, &QTcpSocket::readyRead, this, &FileWindow::onReadyRead);
    connect(ui->widget_directory_list, &QListWidget::itemDoubleClicked, this, &FileWindow::onItemDoubleClicked);

    qDebug() << "Soket FileWindow'a devredildi.";

    if(this->socket->isOpen()) {
        this->socket->write("LIST_FILES");
    }
}

// --- VERİ OKUMA VE İŞLEME MERKEZİ ---
void FileWindow::onReadyRead()
{
    if (is_downloading) {
        QByteArray data = socket->readAll();

        if (local_file && local_file->isOpen()) {
            qint64 written = local_file->write(data);
            received_bytes += written;
            socket->flush(); // Veriyi diske itele
        }

        if (received_bytes >= download_file_size) {
            is_downloading = false;

            if (local_file) {
                local_file->close();
                delete local_file;
                local_file = nullptr;
            }
            QMessageBox::information(this, "Success", "File downloaded successfully!");

        }
        return;
    }

    // --- 2. NORMAL MOD (KOMUTLAR) ---
    QByteArray data = socket->readAll();

    if (data.startsWith("SIZE|")) {
        int split_index = data.indexOf('|');

        int data_start_index = data.length();
        QString packet_str = QString::fromUtf8(data);

        for (int i = split_index + 1; i < data.length(); ++i) {
            if (!packet_str[i].isDigit()) {
                data_start_index = i;
                break;
            }
        }

        QString size_str = packet_str.mid(split_index + 1, data_start_index - (split_index + 1));

        bool ok;
        download_file_size = size_str.toLongLong(&ok);

        if (ok && local_file && local_file->isOpen()) {
            is_downloading = true;
            received_bytes = 0;
            qDebug() << "Indirme basladi. Boyut:" << download_file_size;

            //0 byte exp fix
            if (download_file_size == 0) {
                is_downloading = false;
                local_file->close();
                delete local_file;
                local_file = nullptr;
                QMessageBox::information(this, "Success", "Empty file downloaded.");
                return;
            }

            if (data_start_index < data.length()) {
                QByteArray leftover = data.mid(data_start_index);
                local_file->write(leftover);
                received_bytes += leftover.size();

                // tek bufferda biterse
                if (received_bytes >= download_file_size) {
                    is_downloading = false;
                    local_file->close();
                    delete local_file;
                    local_file = nullptr;
                    QMessageBox::information(this, "Success", "File downloaded successfully!");
                }
            }
        } else {
            qDebug() << "HATA: SIZE geldi ama dosya hazir degil veya boyut hatali.";
        }
        return;
    }

    QString message = QString::fromUtf8(data);

    // hata kontrolü
    if(message.startsWith("ERR|")) {
        QMessageBox::warning(this, "Error", message.mid(4));

        if (local_file) {
            local_file->close();
            local_file->remove();
            delete local_file;
            local_file = nullptr;
            is_downloading = false;
        }
        return;
    }

    // mesaj kontrolü
    if (message.startsWith("MSG|")) {
        QMessageBox::information(this, "Server Message", message.mid(4));
        return;
    }

    // preview içerik kontrol
    if (message.startsWith("CONTENT|")) {
        int first_pipe = message.indexOf('|');
        int second_pipe = message.indexOf('|', first_pipe + 1);

        if (second_pipe != -1) {
            QString file_name = message.mid(first_pipe + 1, second_pipe - first_pipe - 1);
            QString content = message.mid(second_pipe + 1);

            current_open_file = file_name;
            ui->text_edit_editor->setPlainText(content);
            ui->tab_editor->setTabText(0, file_name);
        }
        return;
    }

    // --- UPLOAD ONAYI  ---
    if (is_uploading && message.startsWith("READY_TO_UPLOAD")) {
        qDebug() << "Sunucu hazir, dosya gonderiliyor...";

        while (!upload_file->atEnd()) {
            QByteArray chunk = upload_file->read(4096);
            socket->write(chunk);
            socket->flush();
            socket->waitForBytesWritten(10);
        }

        upload_file->close();
        delete upload_file;
        upload_file = nullptr;
        is_uploading = false;

        QMessageBox::information(this, "Success", "File uploaded successfully!");
        return;
    }

    updateFileList(message);
}

// --- İNDİRME ONAY ---
void FileWindow::start_download(const QString &file_name) {
    // 1. Kullanıcıya sor
    QString save_path = QFileDialog::getSaveFileName(this, "Save File", QDir::homePath() + "/" + file_name);
    if (save_path.isEmpty()) return;

    // 2. Dosyayı hazırla
    if (local_file) { delete local_file; local_file = nullptr; }
    local_file = new QFile(save_path);

    if (!local_file->open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Error", "Could not save file to disk!");
        delete local_file;
        local_file = nullptr;
        return;
    }

    QString command = "DOWNLOAD|" + file_name;
    socket->write(command.toUtf8());
}

// --- DOWNLOAD BUTONU ---
void FileWindow::on_button_download_a_file_or_folder_clicked()
{
    QListWidgetItem *item = ui->widget_directory_list->currentItem();
    if (!item) {
        QMessageBox::warning(this, "Warning", "Please select a file to download.");
        return;
    }

    bool is_dir = item->data(Qt::UserRole).toBool();
    if (is_dir) {
        QMessageBox::information(this, "Info", "Folder download is not supported yet.");
        return;
    }

    QString real_name = item->data(Qt::UserRole + 1).toString();
    start_download(real_name);
}

// --- UPLOAD BUTONU ---
void FileWindow::on_button_upload_a_file_or_folder_clicked()
{
    QString file_path = QFileDialog::getOpenFileName(this, "Select File to Upload");
    if (file_path.isEmpty()) return;

    QFileInfo fi(file_path);
    QString file_name = fi.fileName();
    qint64 file_size = fi.size();

    if (upload_file) { delete upload_file; upload_file = nullptr; }
    upload_file = new QFile(file_path);

    if (!upload_file->open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Error", "Could not open local file!");
        return;
    }

    QString command = "UPLOAD|" + file_name + "|" + QString::number(file_size);
    socket->write(command.toUtf8());

    is_uploading = true;
    qDebug() << "Upload basligi gonderildi:" << command;
}

// --- SAĞ TIK MENÜ ---
void FileWindow::show_context_menu(const QPoint &pos)
{
    QListWidgetItem *item = ui->widget_directory_list->itemAt(pos);
    if (!item) return;

    QMenu contextMenu(tr("Context menu"), this);
    QAction actionOpen("Open", this);
    QAction actionDownload("Download", this);
    QAction actionDelete("Delete", this);

    contextMenu.addAction(&actionOpen);
    contextMenu.addAction(&actionDownload);
    contextMenu.addAction(&actionDelete);

    connect(&actionOpen, &QAction::triggered, this, &FileWindow::open_item);
    connect(&actionDownload, &QAction::triggered, this, &FileWindow::on_button_download_a_file_or_folder_clicked);
    connect(&actionDelete, &QAction::triggered, this, &FileWindow::delete_item);

    contextMenu.exec(ui->widget_directory_list->mapToGlobal(pos));
}

// --- SİLME İŞLEMİ ---
void FileWindow::delete_item()
{
    QListWidgetItem *item = ui->widget_directory_list->currentItem();
    if (!item) return;

    QString real_name = item->data(Qt::UserRole + 1).toString();

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Delete",
                                  "Are you sure you want to delete '" + real_name + "'?\nThis action cannot be undone.",
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        QString command = "DELETE|" + real_name;
        socket->write(command.toUtf8());
    }
}

// --- AÇMA İŞLEMİ ---
void FileWindow::open_item()
{
    QListWidgetItem *item = ui->widget_directory_list->currentItem();
    if (item) {
        onItemDoubleClicked(item);
    }
}

// --- LİSTE GÜNCELLEME ---
void FileWindow::updateFileList(const QString &data)
{
    if (data == "EMPTY") {
        ui->widget_directory_list->clear();
        return;
    }

    ui->widget_directory_list->clear();
    QStringList items = data.split(';', Qt::SkipEmptyParts);

    for (const QString &item : items) {
        QStringList parts = item.split('|');
        if (parts.size() < 2) continue;

        QString name = parts[0];
        QString meta = parts[1];

        QListWidgetItem *listItem = new QListWidgetItem();
        bool isDir = (meta == "DIR");

        listItem->setIcon(getIconForFile(name, isDir));
        listItem->setData(Qt::UserRole, isDir);
        listItem->setData(Qt::UserRole + 1, name);

        if (isDir) {
            listItem->setForeground(QBrush(QColor("#00ff00")));
            listItem->setText(name);
        } else {
            listItem->setForeground(QBrush(QColor("#ffffff")));
            listItem->setText(name + " (" + meta + " bytes)");
        }
        ui->widget_directory_list->addItem(listItem);
    }
}

// --- ÇİFT TIKLAMA ---
void FileWindow::onItemDoubleClicked(QListWidgetItem *item)
{
    bool isDir = item->data(Qt::UserRole).toBool();
    QString real_name = item->data(Qt::UserRole + 1).toString();

    if (isDir) {
        historyStack.push(real_name);
        forwardStack.clear();
        updateAddressBar();

        QString command = "CD|" + real_name;
        socket->write(command.toUtf8());
    }
    else {
        if (is_file_readable(real_name)) {
            socket->write(("CAT|" + real_name).toUtf8());
        } else {
            QMessageBox::StandardButton reply = QMessageBox::question(this, "Preview",
                                                                      "This file cannot be previewed. Do you want to download it?", QMessageBox::Yes | QMessageBox::No);

            if (reply == QMessageBox::Yes) {
                start_download(real_name);
            }
        }
    }
}

// --- KAYDET BUTONU ---
void FileWindow::on_button_save_file_clicked()
{
    if (current_open_file.isEmpty()) {
        QMessageBox::warning(this, "Error", "No file is currently open!");
        return;
    }
    QString content = ui->text_edit_editor->toPlainText();
    QString command = "WRITE|" + current_open_file + "|" + content;
    socket->write(command.toUtf8());
}

// --- NAVİGASYON ---
void FileWindow::on_button_back_clicked()
{
    if (historyStack.isEmpty()) return;
    QString currentFolder = historyStack.pop();
    forwardStack.push(currentFolder);
    updateAddressBar();
    socket->write("CD|..");
}

void FileWindow::on_button_next_clicked()
{
    if (forwardStack.isEmpty()) return;
    QString nextFolder = forwardStack.pop();
    historyStack.push(nextFolder);
    updateAddressBar();
    QString command = "CD|" + nextFolder;
    socket->write(command.toUtf8());
}

// --- CREATE FILE ---
void FileWindow::on_button_create_a_file_clicked()
{
    DialogNewItem dialog(this);
    dialog.setMode(DialogNewItem::FileMode);

    if (dialog.exec() == QDialog::Accepted) {
        QString name = dialog.getName();
        QString ext = dialog.getExtension();
        if (name.isEmpty()) { QMessageBox::warning(this, "Warning", "File name cannot be empty!"); return; }
        QString fullName = name + ext;
        QString command = "TOUCH|" + fullName;
        socket->write(command.toUtf8());
    }
}

// --- CREATE FOLDER ---
void FileWindow::on_button_create_a_folder_clicked()
{
    DialogNewItem dialog(this);
    dialog.setMode(DialogNewItem::FolderMode);
    if (dialog.exec() == QDialog::Accepted) {
        QString name = dialog.getName();
        if (name.isEmpty()){ QMessageBox::warning(this, "Warning", "Folder name cannot be empty!"); return; }
        QString command = "MKDIR|" + name;
        socket->write(command.toUtf8());
    }
}

void FileWindow::on_button_logout_clicked() {
    if(socket && socket->isOpen()) {
        socket->disconnectFromHost();
    }
    QProcess::startDetached(qApp->arguments()[0], qApp->arguments());
    qApp->quit();
}

// --- YARDIMCILAR ---
void FileWindow::updateAddressBar()
{
    QString path = "/";
    for(const QString &dir : historyStack) {
        path += dir + "/";
    }
    ui->textEdit->setText(path);
}

bool FileWindow::is_file_readable(const QString &file_name) {
    QFileInfo fi(file_name);
    QString ext = fi.suffix().toLower();
    QStringList readable_exts = {"txt", "c", "cpp", "h", "hpp", "py", "js", "json", "xml", "html", "css", "sql", "log", "ini", "md"};
    return readable_exts.contains(ext);
}

QIcon FileWindow::getIconForFile(const QString &name, bool isDir)
{
    QString prefix = ":/new/prefix1/images/";
    if (isDir) return QIcon(prefix + "ic_folder.png");
    QFileInfo fileInfo(name);
    QString ext = fileInfo.suffix().toLower();

    if (ext == "cpp" || ext == "h" || ext == "hpp" || ext == "c") return QIcon(prefix + "ic_cpp.png");
    if (ext == "py" || ext == "pyw") return QIcon(prefix + "ic_py.png");
    if (ext == "json") return QIcon(prefix + "ic_json.png");
    if (ext == "xml" || ext == "ui") return QIcon(prefix + "ic_xml.png");
    if (ext == "sql" || ext == "sqlite" || ext == "db") return QIcon(prefix + "ic_sql.png");
    if (ext == "html" || ext == "htm" || ext == "css" || ext == "js") return QIcon(prefix + "ic_html.png");
    if (ext == "txt" || ext == "log" || ext == "ini") return QIcon(prefix + "ic_txt.png");
    if (ext == "pdf") return QIcon(prefix + "ic_pdf.png");
    if (ext == "docx" || ext == "doc") return QIcon(prefix + "ic_word.png");
    if (ext == "xlsx" || ext == "xls" || ext == "csv") return QIcon(prefix + "ic_excel.png");
    if (ext == "pptx" || ext == "ppt") return QIcon(prefix + "ic_ppt.png");
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "ico" || ext == "bmp" || ext == "gif") return QIcon(prefix + "ic_image.png");
    if (ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "mov" || ext == "flv") return QIcon(prefix + "ic_video.png");
    if (ext == "mp3" || ext == "wav" || ext == "ogg" || ext == "flac") return QIcon(prefix + "ic_audio.png");
    if (ext == "zip" || ext == "rar" || ext == "7z" || ext == "tar" || ext == "gz") return QIcon(prefix + "ic_zip.png");
    if (ext == "exe" || ext == "msi" || ext == "bat" || ext == "sh") return QIcon(prefix + "ic_exe.png");

    return QIcon(prefix + "ic_file.png");
}
