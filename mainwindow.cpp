#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>

// ─── CONSTRUCTOR / DESTRUCTOR ─────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , fileWindow(nullptr)
{
    ui->setupUi(this);

    // --- Soket kurulumu ---
    tcpSocket = new QTcpSocket(this);

    // Bağlantı kurulduğunda onConnected() çağrılır (artık waitForConnected yok)
    connect(tcpSocket, &QTcpSocket::connected,    this, &MainWindow::onConnected);
    connect(tcpSocket, &QTcpSocket::readyRead,    this, &MainWindow::onReadyRead);
    connect(tcpSocket, &QTcpSocket::disconnected, this, &MainWindow::onDisconnected);
    connect(tcpSocket, &QAbstractSocket::errorOccurred, this, &MainWindow::onConnectionError);

    // --- Timeout timer ---
    // Sunucu 5 saniyede cevap vermezse bağlantıyı iptal et
    connectTimer = new QTimer(this);
    connectTimer->setSingleShot(true);
    connect(connectTimer, &QTimer::timeout, this, &MainWindow::onConnectionTimeout);

    // --- Remember Me: son kullanılan bilgileri yükle ---
    QSettings settings("SoDrive", "FileClient");
    ui->input_ip_address->setText(settings.value("last_ip").toString());
    ui->input_port->setText(settings.value("last_port").toString());
    ui->input_username->setText(settings.value("last_username").toString());

    // Kayıtlı IP varsa "Remember Me" kutusunu işaretli göster
    if (!ui->input_ip_address->text().isEmpty())
        ui->button_remember_me->setChecked(true);
}

MainWindow::~MainWindow()
{
    delete ui;
}

// ─── CONNECT BUTONU ───────────────────────────────────────────────────────────

void MainWindow::on_button_connect_clicked()
{
    QString ip       = ui->input_ip_address->text().trimmed();
    QString portStr  = ui->input_port->text().trimmed();
    pendingUsername  = ui->input_username->text().trimmed();
    pendingPassword  = ui->input_password->text();

    if (ip.isEmpty() || portStr.isEmpty() || pendingUsername.isEmpty() || pendingPassword.isEmpty()) {
        QMessageBox::warning(this, "Eksik Bilgi", "Lütfen tüm alanları doldurun.");
        return;
    }

    // --- Remember Me ayarını kaydet ---
    QSettings settings("SoDrive", "FileClient");
    if (ui->button_remember_me->isChecked()) {
        settings.setValue("last_ip",       ip);
        settings.setValue("last_port",     portStr);
        settings.setValue("last_username", pendingUsername);
    } else {
        settings.clear();
    }

    ui->button_connect->setEnabled(false);
    ui->button_connect->setText("Connecting...");

    // Async bağlantı başlat — sonucu onConnected() veya onConnectionError() ile alırız
    tcpSocket->connectToHost(ip, portStr.toUShort());

    // 5 saniye içinde connected() gelmezse timeout
    connectTimer->start(5000);
}

// ─── SOKET OLAYLARI ──────────────────────────────────────────────────────────

void MainWindow::onConnected()
{
    // TCP bağlantısı kuruldu, giriş isteği gönder
    connectTimer->stop();
    QString loginMsg = "LOGIN|" + pendingUsername + "|" + pendingPassword;
    tcpSocket->write(loginMsg.toUtf8());
}

void MainWindow::onConnectionTimeout()
{
    // 5 saniyede bağlantı kurulamadı
    tcpSocket->abort();
    resetConnectButton();
    QMessageBox::critical(this, "Bağlantı Hatası",
                          "Sunucuya bağlanılamadı (timeout).\n"
                          "IP adresi ve port numarasını kontrol edin.");
}

void MainWindow::onReadyRead()
{
    // Sunucudan giriş sonucu geldi
    QString response = QString::fromUtf8(tcpSocket->readAll());

    if (response.startsWith("OK")) {
        // Giriş başarılı: login ekranını gizle, dosya yöneticisini aç
        this->hide();

        fileWindow = new FileWindow();
        fileWindow->setSocket(tcpSocket);

        // Logout sinyali gelince bu pencereyi yeniden göster
        connect(fileWindow, &FileWindow::loggedOut, this, &MainWindow::onLoggedOut);

        fileWindow->show();

    } else if (response.startsWith("ERR")) {
        // Giriş başarısız: şifreyi temizle, butonu sıfırla
        ui->input_password->clear();
        resetConnectButton();
        QMessageBox::critical(this, "Giriş Başarısız",
                              "Kullanıcı adı veya şifre hatalı.");
        tcpSocket->disconnectFromHost();
    }
}

void MainWindow::onDisconnected()
{
    resetConnectButton();
}

void MainWindow::onConnectionError(QAbstractSocket::SocketError socketError)
{
    // Timeout zaten ayrıca ele alınıyor, çift mesaj göstermeyelim
    if (socketError == QAbstractSocket::SocketTimeoutError) return;

    connectTimer->stop();
    resetConnectButton();
    QMessageBox::critical(this, "Bağlantı Hatası",
                          "Bağlantı hatası: " + tcpSocket->errorString());
}

void MainWindow::onLoggedOut()
{
    // FileWindow kapandı, bu pencereyi temiz halde tekrar göster
    fileWindow = nullptr;
    ui->input_password->clear();
    resetConnectButton();
    this->show();
}

// ─── YARDIMCI ─────────────────────────────────────────────────────────────────

void MainWindow::resetConnectButton()
{
    ui->button_connect->setEnabled(true);
    ui->button_connect->setText("Connect");
}
