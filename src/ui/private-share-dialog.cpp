#include <QComboBox>
#include <QCompleter>
#include <QLineEdit>
#include <QPainter>
#include <QResizeEvent>
#include <QStringList>
#include "api/api-error.h"
#include "api/requests.h"
#include "private-share-dialog.h"
#include "seafile-applet.h"
#include "utils/file-utils.h"
#include "utils/utils.h"

namespace
{
enum {
    COLUMN_NAME = 0,
    COLUMN_PERMISSION,
    MAX_COLUMN,
};

enum {
    INDEX_USER_NAME = 0,
    INDEX_GROUP_NAME,
};

const int kPermissionColumnWidth = 150;
const int kNameColumnWidth = 300;
const int kDefaultColumnHeight = 40;
const int kIndicatorIconWidth = 10;
const int kIndicatorIconHeight = 8;

const int kMarginLeft = 2;
const int kMarginTop = 2;
const int kPadding = 2;
const int kMarginBetweenPermissionAndIndicator = 10;

const QColor kSelectedItemBackgroundcColor("#F9E0C7");
const QColor kItemBackgroundColor("white");
const QColor kItemBottomBorderColor("#f3f3f3");
const QColor kItemColor("black");

} // namespace

PrivateShareDialog::PrivateShareDialog(const Account& account,
                                       const QString& repo_id,
                                       const QString& repo_name,
                                       const QString& path,
                                       bool to_group,
                                       QWidget* parent)
    : QDialog(parent),
      account_(account),
      repo_id_(repo_id),
      repo_name_(repo_name),
      path_(path),
      to_group_(to_group),
      request_in_progress_(false)
{
    setupUi(this);

    setWindowTitle(
        tr("Share %1")
            .arg(path.length() <= 1 ? repo_name : ::getBaseName(path)));
    setWindowIcon(QIcon(":/images/seafile.png"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

#if defined(Q_OS_MAC)
    layout()->setContentsMargins(6, 6, 6, 6);
    layout()->setSpacing(5);
#endif

    username_input_ = new QLineEdit(this);
    groupname_input_ = new QComboBox(this);

    mInputStack->insertWidget(INDEX_USER_NAME, username_input_);
    mInputStack->insertWidget(INDEX_GROUP_NAME, groupname_input_);

    if (to_group) {
        mInputStack->setCurrentIndex(INDEX_GROUP_NAME);
        groupname_input_->setEditable(true);
        groupname_input_->clearEditText();
        // The place holder text for the line editor of the combo box must be
        // set
        // after setEditable(true), because lineEdit() returns NULL before that.
        groupname_input_->lineEdit()->setPlaceholderText(
            tr("Enter the group name"));
        groupname_input_->completer()->setCompletionMode(
            QCompleter::PopupCompletion);
        groupname_input_->clearEditText();
    }
    else {
        mInputStack->setCurrentIndex(INDEX_USER_NAME);
        username_input_->setPlaceholderText(tr("Enter the email address"));
    }
    mOkBtn->setEnabled(false);
    mPermission->setCurrentIndex(0);
    createTable();

    fetchContacsForCompletion();

    connect(mOkBtn, SIGNAL(clicked()), this, SLOT(onOkBtnClicked()));
    connect(mCancelBtn, SIGNAL(clicked()), this, SLOT(reject()));

    connect(table_, SIGNAL(clicked(const QModelIndex&)), mStatusText,
            SLOT(clear()));
    connect(lineEdit(), SIGNAL(textChanged(const QString&)), mStatusText,
            SLOT(clear()));
    connect(lineEdit(), SIGNAL(textChanged(const QString&)), this,
            SLOT(onNameInputEdited()));
    connect(model_, SIGNAL(modelReset()), this, SLOT(selectFirstRow()));

    if (!to_group_) {
        connect(lineEdit(), SIGNAL(textChanged(const QString&)), this,
                SLOT(updateUserEmail()));
    }

    adjustSize();
    disableInputs();
}

void PrivateShareDialog::updateUserEmail()
{
    QRegExp re(QString("^[^ ]+ <(.*)>$"));
    QString name = lineEdit()->text().trimmed();
    if (re.exactMatch(name)) {
        lineEdit()->setText(re.cap(1));
    }
}

void PrivateShareDialog::selectFirstRow()
{
    // for (int i = 0; i < model_->rowCount(); i++) {
    //     table_->openPersistentEditor(model_->index(i, COLUMN_PERMISSION));
    // }

    // Select the first row of the table, so that the indicator would be
    // painted, to tell the user the permission is editable.
    if (!table_->currentIndex().isValid()) {
        table_->setCurrentIndex(model_->index(0, 0));
    }
}

void PrivateShareDialog::createTable()
{
    table_ = new SharedItemsTableView(this);
    // table_->setEditTriggers(QAbstractItemView::AllEditTriggers);
    model_ = new SharedItemsTableModel(
        to_group_ ? SHARE_TO_GROUP : SHARE_TO_USER, this);
    table_->setModel(model_);
    QVBoxLayout* vlayout = (QVBoxLayout*)mFrame->layout();
    vlayout->insertWidget(1, table_);

    table_->setItemDelegate(new SharedItemDelegate(this));

    connect(model_, SIGNAL(updateShareItem(int, SharePermission)), this,
            SLOT(onUpdateShareItem(int, SharePermission)));
    connect(model_, SIGNAL(updateShareItem(const QString&, SharePermission)),
            this, SLOT(onUpdateShareItem(const QString&, SharePermission)));
    connect(model_, SIGNAL(removeShareItem(int, SharePermission)), this,
            SLOT(onRemoveShareItem(int, SharePermission)));
    connect(model_, SIGNAL(removeShareItem(const QString&, SharePermission)),
            this, SLOT(onRemoveShareItem(const QString&, SharePermission)));
}

void PrivateShareDialog::onNameInputEdited()
{
    mOkBtn->setEnabled(!lineEdit()->text().trimmed().isEmpty());
}

void PrivateShareDialog::fetchContacsForCompletion()
{
    contacts_request_.reset(new FetchGroupsAndContactsRequest(account_));
    contacts_request_->send();
    connect(contacts_request_.data(),
            SIGNAL(success(const QList<SeafileGroup>&,
                           const QList<SeafileContact>&)),
            this, SLOT(onFetchContactsSuccess(const QList<SeafileGroup>&,
                                              const QList<SeafileContact>&)));
    connect(contacts_request_.data(), SIGNAL(failed(const ApiError&)), this,
            SLOT(onFetchContactsFailed(const ApiError&)));
}

void PrivateShareDialog::onUpdateShareItem(int group_id,
                                           SharePermission permission)
{
    request_.reset(new PrivateShareRequest(account_, repo_id_, path_, QString(),
                                           group_id, permission, SHARE_TO_GROUP,
                                           PrivateShareRequest::UPDATE_SHARE));

    connect(request_.data(), SIGNAL(success()), this,
            SLOT(onUpdateShareSuccess()));
    connect(request_.data(), SIGNAL(failed(const ApiError&)), this,
            SLOT(onUpdateShareFailed(const ApiError&)));

    // disableInputs();
    request_in_progress_ = true;
    request_->send();
}

void PrivateShareDialog::onUpdateShareItem(const QString& email,
                                           SharePermission permission)
{
    request_.reset(new PrivateShareRequest(account_, repo_id_, path_, email, 0,
                                           permission, SHARE_TO_USER,
                                           PrivateShareRequest::UPDATE_SHARE));

    connect(request_.data(), SIGNAL(success()), this,
            SLOT(onUpdateShareSuccess()));
    connect(request_.data(), SIGNAL(failed(const ApiError&)), this,
            SLOT(onUpdateShareFailed(const ApiError&)));

    // disableInputs();
    request_in_progress_ = true;
    request_->send();
}

void PrivateShareDialog::onUpdateShareSuccess()
{
    request_in_progress_ = false;
    // seafApplet->messageBox(tr("Shared successfully"), this);
    if (to_group_) {
        GroupShareInfo info;
        info.group = groups_[info.group.id];
        info.permission = request_.data()->permission();
        model_->addNewShareInfo(info);
    }
    else {
        UserShareInfo info;
        info.user.email = request_.data()->userName();
        info.user.nickname = contacts_[info.user.email].nickname;
        info.permission = request_.data()->permission();
        model_->addNewShareInfo(info);
    }
    model_->shareOperationSuccess();
    // enableInputs();
    mStatusText->setText(tr("Updated successfully"));
}

void PrivateShareDialog::onUpdateShareFailed(const ApiError& error)
{
    // enableInputs();
    request_in_progress_ = false;
    showWarning(tr("Share Operation Failed: %1").arg(error.toString()));
    model_->shareOperationFailed(request_->shareOperation());
}

void PrivateShareDialog::onRemoveShareItem(int group_id,
                                           SharePermission permission)
{
    request_.reset(new PrivateShareRequest(account_, repo_id_, path_, QString(),
                                           group_id, permission, SHARE_TO_GROUP,
                                           PrivateShareRequest::REMOVE_SHARE));

    connect(request_.data(), SIGNAL(success()), this,
            SLOT(onRemoveShareSuccess()));
    connect(request_.data(), SIGNAL(failed(const ApiError&)), this,
            SLOT(onRemoveShareFailed(const ApiError&)));

    // disableInputs();
    request_in_progress_ = true;
    request_->send();
}

void PrivateShareDialog::onRemoveShareSuccess()
{
    // enableInputs();
    request_in_progress_ = false;
    model_->shareOperationSuccess();
    mStatusText->setText(tr("Removed successfully"));
}

void PrivateShareDialog::onRemoveShareFailed(const ApiError& error)
{
    request_in_progress_ = false;
    showWarning(tr("Share Operation Failed: %1").arg(error.toString()));
    model_->shareOperationFailed(request_->shareOperation());
    // enableInputs();
}

void PrivateShareDialog::onRemoveShareItem(const QString& email,
                                           SharePermission permission)
{
    request_.reset(new PrivateShareRequest(account_, repo_id_, path_, email, 0,
                                           permission, SHARE_TO_USER,
                                           PrivateShareRequest::REMOVE_SHARE));

    connect(request_.data(), SIGNAL(success()), this,
            SLOT(onRemoveShareSuccess()));
    connect(request_.data(), SIGNAL(failed(const ApiError&)), this,
            SLOT(onRemoveShareFailed(const ApiError&)));

    // disableInputs();
    request_in_progress_ = true;
    request_->send();
}


void PrivateShareDialog::onFetchContactsSuccess(
    const QList<SeafileGroup>& groups, const QList<SeafileContact>& contacts)
{
    QStringList candidates;
    if (to_group_) {
        foreach (const SeafileGroup& group, groups) {
            candidates << group.name;
            groups_[group.id] = group;
        }
    }
    else {
        foreach (const SeafileContact& contact, contacts) {
            contacts_[contact.email] = contact;
            if (!contact.nickname.isEmpty()) {
                candidates << QString("%1 <%2>")
                                  .arg(contact.nickname)
                                  .arg(contact.email);
            }
            else {
                candidates << contact.email;
            }
        }
    }

    if (!candidates.isEmpty()) {
        if (!to_group_) {
            completer_.reset(new QCompleter(candidates));
#if (QT_VERSION >= QT_VERSION_CHECK(5, 2, 0))
            completer_->setFilterMode(Qt::MatchContains);
#endif
            username_input_->setCompleter(completer_.data());
        }
        else {
            groupname_input_->addItems(candidates);
            groupname_input_->clearEditText();
        }
    }

    get_shared_items_request_.reset(
        new GetPrivateShareItemsRequest(account_, repo_id_, path_));

    connect(get_shared_items_request_.data(),
            SIGNAL(success(const QList<GroupShareInfo>&,
                           const QList<UserShareInfo>&)),
            this, SLOT(onGetSharedItemsSuccess(const QList<GroupShareInfo>&,
                                               const QList<UserShareInfo>&)));
    connect(get_shared_items_request_.data(), SIGNAL(failed(const ApiError&)),
            this, SLOT(onGetSharedItemsFailed(const ApiError&)));

    get_shared_items_request_->send();
}

void PrivateShareDialog::onGetSharedItemsSuccess(
    const QList<GroupShareInfo>& group_shares,
    const QList<UserShareInfo>& user_shares)
{
    model_->setShareInfo(group_shares, user_shares);
    selectFirstRow();
    enableInputs();
}

void PrivateShareDialog::onGetSharedItemsFailed(const ApiError& error)
{
    showWarning(tr("Failed to get share information of the folder"));
    reject();
}

void PrivateShareDialog::onFetchContactsFailed(const ApiError& error)
{
    showWarning(tr("Failed to get your groups and contacts information"));
    reject();
}

SharePermission PrivateShareDialog::currentPermission()
{
    return mPermission->currentIndex() == 0 ? READ_WRITE : READ_ONLY;
}

bool PrivateShareDialog::validateInputs()
{
    QString name = lineEdit()->text().trimmed();
    if (name.isEmpty()) {
        showWarning(to_group_ ? tr("Please enter the username")
                              : tr("Please enter the group name"));
        return false;
    }

    SharePermission permission = currentPermission();

    if (to_group_) {
        SeafileGroup group;
        bool found = false;
        foreach (const SeafileGroup& g, groups_.values()) {
            if (g.name == name) {
                group = g;
                found = true;
            }
        }
        if (!found) {
            showWarning(tr("No such group \"%1\"").arg(name));
            return false;
        }
        if (model_->shareExists(group.id)) {
            GroupShareInfo info = model_->shareInfo(group.id);
            if (info.permission == permission) {
                showWarning(tr("Already shared to group %1").arg(name));
            }
            return false;
        }
    }
    else {
        if (model_->shareExists(name)) {
            UserShareInfo info = model_->shareInfo(name);
            if (info.permission == permission) {
                showWarning(tr("Already shared to user %1").arg(name));
                return false;
            }
        }
    }

    return true;
}

SeafileGroup PrivateShareDialog::findGroup(const QString& name)
{
    foreach (const SeafileGroup& group, groups_.values()) {
        if (group.name == name) {
            return group;
        }
    }
    return SeafileGroup();
}

void PrivateShareDialog::onOkBtnClicked()
{
    if (!validateInputs()) {
        return;
    }
    if (request_in_progress_) {
        showWarning(tr("The previous operation is still in progres"));
        return;
    }

    // disableInputs();
    QString username;
    SeafileGroup group;
    QString name = lineEdit()->text().trimmed();
    if (to_group_) {
        group = findGroup(name);
    }
    else {
        username = name;
    }
    request_.reset(new PrivateShareRequest(
        account_, repo_id_, path_, username, group.id, currentPermission(),
        to_group_ ? SHARE_TO_GROUP : SHARE_TO_USER,
        PrivateShareRequest::ADD_SHARE));

    connect(request_.data(), SIGNAL(success()), this, SLOT(onShareSuccess()));

    connect(request_.data(), SIGNAL(failed(const ApiError&)), this,
            SLOT(onShareFailed(const ApiError&)));

    request_in_progress_ = true;
    request_->send();

    if (to_group_) {
        GroupShareInfo info;
        info.group = group;
        info.permission = currentPermission();
        model_->addNewShareInfo(info);
    }
    else {
        UserShareInfo info;
        info.user.email = name;
        info.user.nickname = contacts_[info.user.email].nickname;
        info.permission = currentPermission();
        model_->addNewShareInfo(info);
    }
}

void PrivateShareDialog::disableInputs()
{
    toggleInputs(false);
}

void PrivateShareDialog::enableInputs()
{
    toggleInputs(true);
}

void PrivateShareDialog::toggleInputs(bool enabled)
{
    groupname_input_->setEnabled(enabled);
    username_input_->setEnabled(enabled);
    mOkBtn->setEnabled(enabled);
    mCancelBtn->setEnabled(enabled);
    mPermission->setEnabled(enabled);
}

void PrivateShareDialog::onShareSuccess()
{
    // seafApplet->messageBox(tr("Shared successfully"), this);
    request_in_progress_ = false;
    model_->shareOperationSuccess();
    // enableInputs();
    mStatusText->setText(tr("Shared successfully"));
}

void PrivateShareDialog::onShareFailed(const ApiError& error)
{
    request_in_progress_ = false;
    // enableInputs();
    model_->shareOperationFailed(PrivateShareRequest::ADD_SHARE);
    showWarning(tr("Share Operation Failed: %1").arg(error.toString()));
}

void PrivateShareDialog::showWarning(const QString& msg)
{
    seafApplet->warningBox(msg, this);
}

SharedItemsHeadView::SharedItemsHeadView(QWidget* parent)
    : QHeaderView(Qt::Horizontal, parent)
{
    setDefaultAlignment(Qt::AlignLeft);
    setStretchLastSection(false);
    setCascadingSectionResizes(true);
    setHighlightSections(false);
    setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 0, 0))
    setSectionResizeMode(QHeaderView::ResizeToContents);
#else
    setResizeMode(QHeaderView::ResizeToContents);
#endif
}

QSize SharedItemsHeadView::sectionSizeFromContents(int index) const
{
    QSize size = QHeaderView::sectionSizeFromContents(index);
    SharedItemsTableView* table = (SharedItemsTableView*)parent();
    SharedItemsTableModel* model =
        (SharedItemsTableModel*)(table->sourceModel());
    if (model) {
        size.setWidth(index == COLUMN_NAME ? model->nameColumnWidth()
                                           : kPermissionColumnWidth);
    }
    return size;
}

SharedItemsTableView::SharedItemsTableView(QWidget* parent)
    : QTableView(parent), source_model_(0)
{
    setHorizontalHeader(new SharedItemsHeadView(this));
    verticalHeader()->hide();

    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);

    setMouseTracking(true);
    setShowGrid(false);
    setContentsMargins(0, 5, 0, 5);
    // setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void SharedItemsTableView::setModel(QAbstractItemModel* model)
{
    QTableView::setModel(model);
    source_model_ = qobject_cast<SharedItemsTableModel*>(model);
}

void SharedItemsTableView::resizeEvent(QResizeEvent* event)
{
    QTableView::resizeEvent(event);
    if (source_model_)
        source_model_->onResize(event->size());
}


SharedItemsTableModel::SharedItemsTableModel(ShareType share_type,
                                             QObject* parent)
    : QAbstractTableModel(parent),
      share_type_(share_type),
      name_column_width_(kNameColumnWidth)
{
}


int SharedItemsTableModel::columnCount(const QModelIndex& parent) const
{
    return MAX_COLUMN;
}

int SharedItemsTableModel::rowCount(const QModelIndex& parent) const
{
    return share_type_ == SHARE_TO_USER ? user_shares_.size()
                                        : group_shares_.size();
}

QVariant SharedItemsTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    if (role != Qt::DisplayRole && role != Qt::EditRole &&
        role != Qt::SizeHintRole && role != Qt::ToolTipRole) {
        return QVariant();
    }

    int row = index.row(), column = index.column();

    if (role == Qt::EditRole && column != COLUMN_PERMISSION) {
        return QVariant();
    }

    if (role == Qt::ToolTipRole) {
        if (column == COLUMN_PERMISSION) {
            return tr("Double click to edit");
        }
        else {
            if (isGroupShare()) {
                const GroupShareInfo& info = group_shares_[row];
                if (!info.group.owner.isEmpty()) {
                    return tr("Created by %1").arg(info.group.owner);
                }
            }
            else {
                return user_shares_[row].user.email;
            }
        }
        return QVariant();
    }

    // DisplayRole

    if (role == Qt::SizeHintRole) {
        QSize qsize(0, kDefaultColumnHeight);
        if (column == COLUMN_NAME) {
            qsize.setWidth(name_column_width_);
        }
        else {
            qsize.setWidth(kPermissionColumnWidth);
        }
        return qsize;
    }

    if (isGroupShare()) {
        if (row >= group_shares_.size()) {
            return QVariant();
        }
        const GroupShareInfo& info = group_shares_[row];

        if (column == COLUMN_NAME) {
            return info.group.name;
        }
        else if (column == COLUMN_PERMISSION) {
            if (role == Qt::DisplayRole) {
                return info.permission == READ_WRITE ? tr("Read Write")
                                                     : tr("Read Only");
            }
            else {
                return info.permission == READ_WRITE ? 0 : 1;
            }
        }
    }
    else {
        if (row >= user_shares_.size()) {
            return QVariant();
        }
        const UserShareInfo& info = user_shares_[row];

        if (column == COLUMN_NAME) {
            return info.user.nickname.isEmpty() ? info.user.email
                                                : info.user.nickname;
        }
        else if (column == COLUMN_PERMISSION) {
            if (role == Qt::DisplayRole) {
                return info.permission == READ_WRITE ? tr("Read Write")
                                                     : tr("Read Only");
            }
            else {
                return info.permission == READ_WRITE ? 0 : 1;
            }
        }
    }

    return QVariant();
}

void SharedItemsTableModel::onResize(const QSize& size)
{
    name_column_width_ = size.width() - kPermissionColumnWidth;
    if (rowCount() == 0) {
        emit dataChanged(index(0, COLUMN_NAME),
                         index(rowCount() - 1, COLUMN_NAME));
    }
}

bool SharedItemsTableModel::isGroupShare() const
{
    return share_type_ == SHARE_TO_GROUP;
}

QVariant SharedItemsTableModel::headerData(int section,
                                           Qt::Orientation orientation,
                                           int role) const
{
    if (orientation == Qt::Vertical) {
        return QVariant();
    }

    if (section == COLUMN_NAME) {
        if (role == Qt::DisplayRole) {
            return isGroupShare() ? tr("Group") : tr("User");
        }
    }
    else if (section == COLUMN_PERMISSION) {
        if (role == Qt::DisplayRole) {
            return tr("Permission");
        }
    }


    return QVariant();
}


void SharedItemsTableModel::setShareInfo(
    const QList<GroupShareInfo>& group_shares,
    const QList<UserShareInfo>& user_shares)
{
    beginResetModel();
    group_shares_ = group_shares;
    user_shares_ = user_shares;
    endResetModel();
}

void SharedItemsTableModel::addNewShareInfo(UserShareInfo newinfo)
{
    previous_user_shares_ = user_shares_;
    beginResetModel();
    bool exists;
    for (int i = 0; i < user_shares_.size(); i++) {
        UserShareInfo& info = user_shares_[i];
        if (info.user.email == newinfo.user.email) {
            exists = true;
            info.permission = newinfo.permission;
        }
    }
    if (!exists) {
        user_shares_.prepend(newinfo);
    }
    endResetModel();
}

void SharedItemsTableModel::addNewShareInfo(GroupShareInfo newinfo)
{
    previous_group_shares_ = group_shares_;
    beginResetModel();
    bool exists;
    for (int i = 0; i < group_shares_.size(); i++) {
        GroupShareInfo& info = group_shares_[i];
        if (info.group.id == newinfo.group.id) {
            exists = true;
            info.permission = newinfo.permission;
        }
    }
    if (!exists) {
        group_shares_.prepend(newinfo);
    }
    endResetModel();
}

bool SharedItemsTableModel::shareExists(int group_id)
{
    foreach (const GroupShareInfo& info, group_shares_) {
        if (info.group.id == group_id) {
            return true;
        }
    }
    return false;
}

bool SharedItemsTableModel::shareExists(const QString& email)
{
    foreach (const UserShareInfo& info, user_shares_) {
        if (info.user.email == email) {
            return true;
        }
    }
    return false;
}

GroupShareInfo SharedItemsTableModel::shareInfo(int group_id)
{
    foreach (const GroupShareInfo& info, group_shares_) {
        if (info.group.id == group_id) {
            return info;
        }
    }
    return GroupShareInfo();
}

UserShareInfo SharedItemsTableModel::shareInfo(const QString& email)
{
    foreach (const UserShareInfo& info, user_shares_) {
        if (info.user.email == email) {
            return info;
        }
    }
    return UserShareInfo();
}

Qt::ItemFlags SharedItemsTableModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::ItemIsEnabled;

    if (index.column() == COLUMN_PERMISSION) {
        return QAbstractItemModel::flags(index) | Qt::ItemIsEditable;
    }
    else {
        return QAbstractItemModel::flags(index);
    }
}

bool SharedItemsTableModel::removeRows(int row,
                                       int count,
                                       const QModelIndex& parent)
{
    beginRemoveRows(parent, row, row);
    if (isGroupShare()) {
        group_shares_.removeAt(row);
    }
    else {
        user_shares_.removeAt(row);
    }
    endRemoveRows();
    return true;
}

bool SharedItemsTableModel::setData(const QModelIndex& index,
                                    const QVariant& value,
                                    int role)
{
    PrivateShareDialog* dialog = (PrivateShareDialog*)QObject::parent();
    if (dialog->requestInProgress()) {
        dialog->showWarning(tr("The previous operation is still in progres"));
        return false;
    }
    if (!index.isValid() || role != Qt::EditRole) {
        return false;
    }
    int permission = value.toInt();
    int row = index.row();
    if (isGroupShare()) {
        previous_group_shares_ = group_shares_;
        GroupShareInfo& info = group_shares_[row];
        if (permission == 3) {
            emit removeShareItem(info.group.id, info.permission);
            removed_group_share_ = info;
            removeRows(row, 1);
        }
        else if (permission == info.permission) {
        }
        else {
            emit updateShareItem(info.group.id, (SharePermission)permission);
            info.permission =
                info.permission == READ_ONLY ? READ_WRITE : READ_ONLY;
            emit dataChanged(index, index);
        }
    }
    else {
        previous_user_shares_ = user_shares_;
        UserShareInfo& info = user_shares_[row];
        if (permission == 3) {
            emit removeShareItem(info.user.email, info.permission);
            removed_user_share_ = info;
            removeRows(row, 1);
        }
        else if (permission == info.permission) {
        }
        else {
            emit updateShareItem(info.user.email, (SharePermission)permission);
            info.permission =
                info.permission == READ_ONLY ? READ_WRITE : READ_ONLY;
            emit dataChanged(index, index);
        }
    }

    return true;
}

void SharedItemsTableModel::shareOperationSuccess()
{
}

void SharedItemsTableModel::shareOperationFailed(
    PrivateShareRequest::ShareOperation op)
{
    beginResetModel();
    if (isGroupShare()) {
        group_shares_ = previous_group_shares_;
    }
    else {
        user_shares_ = previous_user_shares_;
    }
    endResetModel();
}

SharedItemDelegate::SharedItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

QWidget* SharedItemDelegate::createEditor(QWidget* parent,
                                          const QStyleOptionViewItem& option,
                                          const QModelIndex& index) const
{
    QComboBox* combobox = new QComboBox(parent);
    combobox->addItem(tr("Read Write"));
    combobox->addItem(tr("Read Only"));
    combobox->insertSeparator(2);
    combobox->addItem(tr("Remove Share"));
    return combobox;
}

void SharedItemDelegate::setEditorData(QWidget* editor,
                                       const QModelIndex& index) const
{
    int value = index.model()->data(index, Qt::EditRole).toInt();

    QComboBox* combobox = static_cast<QComboBox*>(editor);
    combobox->setCurrentIndex(value);
}

void SharedItemDelegate::setModelData(QWidget* editor,
                                      QAbstractItemModel* model,
                                      const QModelIndex& index) const
{
    QComboBox* combobox = static_cast<QComboBox*>(editor);
    model->setData(index, combobox->currentIndex(), Qt::EditRole);
}

void SharedItemDelegate::updateEditorGeometry(
    QWidget* editor,
    const QStyleOptionViewItem& option,
    const QModelIndex& index) const
{
    QComboBox* combobox = static_cast<QComboBox*>(editor);
    combobox->setGeometry(option.rect);
    // combobox->showPopup();
}

void SharedItemDelegate::paint(QPainter* painter,
                               const QStyleOptionViewItem& option,
                               const QModelIndex& index) const
{
    const SharedItemsTableModel* model =
        static_cast<const SharedItemsTableModel*>(index.model());

    QRect option_rect = option.rect;
    bool selected = false;
    // draw item's background
    painter->save();
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option_rect, kSelectedItemBackgroundcColor);
        selected = true;
    }
    else
        painter->fillRect(option_rect, kItemBackgroundColor);
    painter->restore();

    // draw item's border for the first row only
    static const QPen borderPen(kItemBottomBorderColor, 1);
    // if (index.row() == 0) {
    //     painter->save();
    //     painter->setPen(borderPen);
    //     painter->drawLine(option_rect.topLeft(), option_rect.topRight());
    //     painter->restore();
    // }
    // draw item's border under the bottom
    painter->save();
    painter->setPen(borderPen);
    painter->drawLine(option_rect.bottomLeft(), option_rect.bottomRight());
    painter->restore();

    QPoint text_pos(kMarginLeft + kPadding, kMarginTop + kPadding);
    text_pos += option.rect.topLeft();

    QSize size = model->data(index, Qt::SizeHintRole).value<QSize>();
    QString text = model->data(index, Qt::DisplayRole).value<QString>();
    QFont font = model->data(index, Qt::FontRole).value<QFont>();
    QRect text_rect(text_pos, size);
    painter->save();
    painter->setPen(kItemColor);
    painter->setFont(font);
    painter->drawText(text_rect,
                      Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, text,
                      &text_rect);
    painter->restore();

    if (selected && index.column() == COLUMN_PERMISSION) {
        int h = option.rect.height();
        QPoint indicator_pos = option.rect.bottomRight() -
                               QPoint(40, h - (h - kIndicatorIconHeight) / 2);
        indicator_pos.setX(text_rect.topRight().x() +
                           kMarginBetweenPermissionAndIndicator);
        QRect indicator_rect(indicator_pos,
                             QSize(kIndicatorIconWidth, kIndicatorIconHeight));

        QPainterPath path;
        path.moveTo(indicator_rect.topLeft());
        path.lineTo(indicator_rect.topRight());
        path.lineTo(indicator_rect.bottomRight() -
                    QPoint((indicator_rect.width() / 2), 0));
        path.lineTo(indicator_rect.topLeft());

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::HighQualityAntialiasing);
        painter->fillPath(path, QBrush(kItemColor));
        painter->restore();
    }
}