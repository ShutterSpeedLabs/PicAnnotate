#include "picannotate.h"
#include "ui_picannotate.h"

PicAnnotate::PicAnnotate(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::PicAnnotate)
{
    ui->setupUi(this);
}

PicAnnotate::~PicAnnotate()
{
    delete ui;
}
