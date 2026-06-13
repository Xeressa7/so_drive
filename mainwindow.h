#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QSettings>
#include <QTimer>
#include "filewindow.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Kullanıcı "Connect" butonuna bastığında
    void on_button_connect_clicked();

    // TCP bağlantısı kurulduğunda (async — waitForConnected kullanılmaz)
    void onConnected();

    // Bağlantı 5 saniyede kurulamazsa
    void onConnectionTimeout();

    // Sunucudan veri geldiğinde (giriş sonucu)
    void onReadyRead();

    // Soket koptuğunda UI'ı sıfırla
    void onDisconnected();

    // Soket hatası
    void onConnectionError(QAbstractSocket::SocketError socketError);

    // FileWindow'dan logout sinyali geldiğinde — pencereyi yeniden göster
    void onLoggedOut();

private:
    Ui::MainWindow *ui;
    QTcpSocket     *tcpSocket;
    QTimer         *connectTimer;
    FileWindow     *fileWindow;

    // Async bağlantı sırasında saklanan kullanıcı bilgileri
    QString pendingUsername;
    QString pendingPassword;

    // Bağlantı başarısız olduğunda UI'ı başlangıç haline döndür
    void resetConnectButton();
};

#endif
