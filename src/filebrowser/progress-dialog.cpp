#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QPushButton>
#include <QDesktopServices>
#include <QDebug>
#include <climits>

#include "utils/utils.h"
#include "progress-dialog.h"

FileBrowserProgressDialog::FileBrowserProgressDialog(FileNetworkTask *task, QWidget *parent)
        : QProgressDialog(parent),
          task_(task)
{
    setWindowModality(Qt::NonModal);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowIcon(QIcon(":/images/seafile.png"));

    QVBoxLayout *layout_ = new QVBoxLayout;
    progress_bar_ = new QProgressBar;
    description_label_ = new QLabel;

    layout_->addWidget(description_label_);
    layout_->addWidget(progress_bar_);

    QHBoxLayout *hlayout_ = new QHBoxLayout;
    more_details_label_ = new QLabel;
    more_details_label_->setText(tr("Pending"));
    QPushButton *cancel_button_ = new QPushButton(tr("Cancel"));
    QWidget *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    hlayout_->addWidget(more_details_label_);
    hlayout_->addWidget(spacer);
    hlayout_->addWidget(cancel_button_);
    hlayout_->setContentsMargins(-1, 0, -1, 6);
    layout_->setContentsMargins(-1, 0, -1, 6);
    layout_->addLayout(hlayout_);

    setLayout(layout_);
    setLabel(description_label_);
    setBar(progress_bar_);
    setCancelButton(cancel_button_);

    initTaskInfo();

    connect(task_, SIGNAL(progressUpdate(qint64, qint64)),
            this, SLOT(onProgressUpdate(qint64, qint64)));
    connect(task_, SIGNAL(finished(bool)), this, SLOT(onTaskFinished(bool)));
    connect(task_, SIGNAL(retried(int)), this, SLOT(initTaskInfo()));
    connect(this, SIGNAL(canceled()), this, SLOT(cancel()));
}

FileBrowserProgressDialog::~FileBrowserProgressDialog()
{
}

void FileBrowserProgressDialog::initTaskInfo()
{
    if (task_->canceled()) {
        return;
    }
    QString title, label;
    if (task_->type() == FileNetworkTask::Upload) {
        title = tr("Upload");
        label = tr("Uploading %1");
    } else {
        title = tr("Download");
        label = tr("Downloading %1");
    }
    setWindowTitle(title);
    setLabelText(label.arg(task_->fileName()));

    more_details_label_->setText("");

    setMaximum(0);
    setValue(0);
}

void FileBrowserProgressDialog::onProgressUpdate(qint64 processed_bytes, qint64 total_bytes)
{
    // Ignore the updates if the task has been cancelled, because we may already
    // have already rejected this dialog.
    if (task_->canceled()) {
        return;
    }
    // if the value is less than the maxmium, this dialog will close itself
    // add this guard for safety
    if (processed_bytes >= total_bytes)
        total_bytes = processed_bytes + 1;

    if (total_bytes > INT_MAX) {
        if (maximum() != INT_MAX)
            setMaximum(INT_MAX);

        // Avoid overflow
        double progress = double(processed_bytes) * INT_MAX / total_bytes;
        setValue((int)progress);
    } else {
        if (maximum() != total_bytes)
            setMaximum(total_bytes);

        setValue(processed_bytes);
    }

    more_details_label_->setText(tr("%1 of %2")
                            .arg(::readableFileSizeV2(processed_bytes))
                            .arg(::readableFileSizeV2(total_bytes)));
}

void FileBrowserProgressDialog::onTaskFinished(bool success)
{
    // printf ("FileBrowserProgressDialog: onTaskFinished\n");
    if (task_->canceled()) {
        return;
    }
    if (success) {
        // printf ("progress dialog: task success\n");
        accept();
    } else {
        // printf ("progress dialog: task failed\n");
        reject();
    }
}

void FileBrowserProgressDialog::cancel()
{
    if (task_->canceled()) {
        return;
    }
    task_->cancel();
    reject();
}
