#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    //soketi tanımlama
    tcpSocket = new QTcpSocket(this);

    // bağlantı soket
    connect(tcpSocket, &QTcpSocket::readyRead, this, &MainWindow::onReadyRead);
    connect(tcpSocket, &QTcpSocket::disconnected, this, &MainWindow::onDisconnected);
    // Error signal handling
    connect(tcpSocket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &MainWindow::onConnectionError);

    //remember me ayarı
    QSettings settings("MyCompany", "FileClient");

    ui->input_ip_address->setText(settings.value("last_ip").toString());
    ui->input_port->setText(settings.value("last_port").toString());
    ui->input_username->setText(settings.value("last_username").toString());

    if(!ui->input_ip_address->text().isEmpty()) {
        ui->button_remember_me->setChecked(true);
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

// --- CONNECT BUTTON CLICKED ---
void MainWindow::on_button_connect_clicked()
{
    //UI dan verileri al
    QString ip = ui->input_ip_address->text();
    QString portStr = ui->input_port->text();
    QString username = ui->input_username->text();
    QString password = ui->input_password->text();

    if(ip.isEmpty() || portStr.isEmpty() || username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, "Input Error", "Please fill in all fields!");
        return;
    }

    ui->button_connect->setEnabled(false);
    ui->button_connect->setText("Connecting...");

    QSettings settings("MyCompany", "FileClient");

    if(ui->button_remember_me->isChecked()) {
        settings.setValue("last_ip", ip);
        settings.setValue("last_port", portStr);
        settings.setValue("last_username", username);
    } else {
        settings.clear();
    }

    // --- CONNECT TO SERVER ---
    tcpSocket->connectToHost(ip, portStr.toUShort());

    // 3 saniye timeout
    if(tcpSocket->waitForConnected(3000)) {
        QString loginMessage = "LOGIN|" + username + "|" + password;
        tcpSocket->write(loginMessage.toUtf8());
    } else {
        ui->button_connect->setEnabled(true);
        ui->button_connect->setText("Connect");
        QMessageBox::critical(this, "Connection Error", "Could not connect to server!\nPlease check IP and Port.");
    }
}

// --- DATA RECEIVED FROM SERVER ---
void MainWindow::onReadyRead()
{
    QByteArray data = tcpSocket->readAll();
    QString message = QString::fromUtf8(data);

    if(message.startsWith("OK")) {
        this->hide();
        dosyaEkrani = new FileWindow();

        dosyaEkrani->setSocket(this->tcpSocket);

        dosyaEkrani->show();
    }
    else if(message.startsWith("ERR")) {
        QMessageBox::critical(this, "Login Failed", "Server Error: " + message);

        ui->button_connect->setEnabled(true);
        ui->button_connect->setText("Connect");
        tcpSocket->disconnectFromHost();
    }

}

void MainWindow::onDisconnected()
{
    ui->button_connect->setEnabled(true);
    ui->button_connect->setText("Connect");
}

void MainWindow::onConnectionError(QAbstractSocket::SocketError socketError)
{
    ui->button_connect->setEnabled(true);
    ui->button_connect->setText("Connect");
}
