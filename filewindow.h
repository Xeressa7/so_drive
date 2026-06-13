#ifndef FILEWINDOW_H
#define FILEWINDOW_H

#include <QWidget>
#include <QTcpSocket>
#include <QListWidgetItem>
#include <QStack>
#include <QMenu>
#include <QAction>
#include <QPoint>
#include <QCursor>
#include <QFile>
#include <QFileDialog>
#include <QProcess>
#include <QApplication>
namespace Ui {
class FileWindow;
}

class FileWindow : public QWidget
{
    Q_OBJECT

public:
    explicit FileWindow(QWidget *parent = nullptr);
    ~FileWindow();

    void setSocket(QTcpSocket *s);

private slots:
    // --- TEMEL NETWORK FONKSİYONLARI ---
    void onReadyRead();
    void onItemDoubleClicked(QListWidgetItem *item);

    // --- SAĞ TIK MENÜSÜ SLOTLARI ---
    void show_context_menu(const QPoint &pos);
    void delete_item();
    void open_item();

    // --- NAVİGASYON BUTONLARI ---
    void on_button_back_clicked();
    void on_button_next_clicked();

    // --- DOSYA YÖNETİM BUTONLARI ---
    void on_button_create_a_file_clicked();
    void on_button_create_a_folder_clicked();

    // --- DOWNLOAD & UPLOAD BUTONLARI ---
    void on_button_download_a_file_or_folder_clicked();
    void on_button_upload_a_file_or_folder_clicked();
    // -----------------------------------

    void on_button_logout_clicked();
    void on_button_save_file_clicked();

private:
    Ui::FileWindow *ui;
    QTcpSocket *socket;

    // --- HAFIZA (İleri-Geri Gitmek İçin) ---
    QStack<QString> historyStack;
    QStack<QString> forwardStack;

    // --- EDİTÖR ---
    QString current_open_file;

    // --- DOWNLOAD DEĞİŞKENLERİ ---
    bool is_downloading;           // İndirme modu açık mı
    qint64 download_file_size;     // İnecek dosyanın toplam boyutu
    qint64 received_bytes;         // Şu ana kadar inen
    QFile *local_file;             // Bilgisayara kaydettiğimiz dosya

    // --- UPLOAD DEĞİŞKENLERİ ---
    bool is_uploading;             // Yükleme modu açık mı
    QFile *upload_file;            // Sunucuya gönderdiğimiz dosya

    // --- YARDIMCI FONKSİYONLAR ---
    void start_download(const QString &file_name); // İndirmeyi başlatan degisken
    void updateFileList(const QString &data);
    QIcon getIconForFile(const QString &name, bool isDir);
    void updateAddressBar();
    bool is_file_readable(const QString &file_name);
};

#endif
