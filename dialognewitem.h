#ifndef DIALOGNEWITEM_H
#define DIALOGNEWITEM_H

#include <QDialog>

namespace Ui {
class DialogNewItem;
}

class DialogNewItem : public QDialog
{
    Q_OBJECT

public:
    explicit DialogNewItem(QWidget *parent = nullptr);
    ~DialogNewItem();

    enum Mode {
        FileMode,
        FolderMode
    };

    void setMode(Mode m);
    QString getName() const;
    QString getExtension() const;

private:
    Ui::DialogNewItem *ui;
    Mode currentMode;
};

#endif
