#ifndef FILEWINDOW_H
#define FILEWINDOW_H

#include <QWidget>
#include <QTcpSocket>
#include <QListWidgetItem>
#include <QStack>
#include <QMenu>
#include <QFile>
#include <QFileDialog>

namespace Ui {
class FileWindow;
}

class FileWindow : public QWidget
{
    Q_OBJECT

public:
    explicit FileWindow(QWidget *parent = nullptr);
    ~FileWindow();

    // MainWindow'dan giriş yapılmış soketi devralır
    void setSocket(QTcpSocket *socket);

signals:
    // Kullanıcı çıkış yaptığında MainWindow bu sinyali dinler
    void loggedOut();

private slots:
    // Sunucudan veri geldiğinde — hem indirme hem komut yanıtları buraya gelir
    void onReadyRead();

    // Dosya listesinde çift tıklama
    void onItemDoubleClicked(QListWidgetItem *item);

    // Sağ tık menüsü
    void show_context_menu(const QPoint &pos);
    void delete_item();
    void open_item();

    // Navigasyon
    void on_button_back_clicked();
    void on_button_next_clicked();

    // Toolbar butonları
    void on_button_create_a_file_clicked();
    void on_button_create_a_folder_clicked();
    void on_button_download_a_file_or_folder_clicked();
    void on_button_upload_a_file_or_folder_clicked();
    void on_button_save_file_clicked();
    void on_button_logout_clicked();

private:
    Ui::FileWindow *ui;
    QTcpSocket     *socket;

    // Gezinti geçmişi (geri/ileri butonları için)
    QStack<QString> historyStack;
    QStack<QString> forwardStack;

    // Editörde şu an açık dosyanın adı (kaydetmek için gerekli)
    QString currentOpenFile;

    // İndirme durumu
    bool    isDownloading;
    qint64  downloadFileSize;
    qint64  receivedBytes;
    QFile  *localFile;

    // Yükleme durumu
    bool   isUploading;
    QFile *uploadFile;

    // Yardımcı fonksiyonlar
    void start_download(const QString &fileName);
    void updateFileList(const QString &data);
    void updateAddressBar();
    bool is_file_readable(const QString &fileName);
    QIcon getIconForFile(const QString &name, bool isDir);
};

#endif
