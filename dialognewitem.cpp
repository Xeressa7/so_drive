#include "dialognewitem.h"
#include "ui_dialognewitem.h"

DialogNewItem::DialogNewItem(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogNewItem)
{
    ui->setupUi(this);

    // ok ve cancel butonlarının işlevi
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setMode(FileMode);
}

DialogNewItem::~DialogNewItem()
{
    delete ui;
}

void DialogNewItem::setMode(Mode m)
{
    currentMode = m;

    if (currentMode == FolderMode) {
        this->setWindowTitle("Create New Folder");
        //klasörde uzantı seçme yok, comboboxı gizle
        ui->combo_type->setVisible(false);
    }
    else {
        this->setWindowTitle("Create New File");
        //dosyada var, göster
        ui->combo_type->setVisible(true);
    }
}

QString DialogNewItem::getName() const
{
    return ui->input_name->text();
}

QString DialogNewItem::getExtension() const
{
    if (currentMode == FolderMode) return "";
    return ui->combo_type->currentText();
}
