#include "mainchatwindow.h"
#include "appsettings.h"
#include "mainwindow.h"
#include "ui_chatsettingsform.h"
#include "ui_chatwidget.h"

#include <QAction>
#include <QClipboard>
#include <QCompleter>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QImageReader>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardPaths>
#include <QToolBar>
#include <QUrl>

#include <czatlib/chatblocker.h>
#include <czatlib/chatsession.h>
#include <czatlib/message.h>
#include <czatlib/userlistmodel.h>

namespace {
int getOptimalUserListWidth(QWidget *widget) {
  // max nick length is 16 characters
  static const QString worstCase = QLatin1String("wwwwwwwwwwwwwwww");
  auto font = widget->font();
  font.setBold(true);
  return QFontMetrics(font).size(Qt::TextSingleLine, worstCase).width();
}

QString
imageDefaultPath(const QString &channel, const QString &nickname,
                 const QByteArray &format,
                 const QDateTime &datetime = QDateTime::currentDateTime()) {
  return QString(QLatin1String("%1/czateria_%2_%3_%4.%5"))
      .arg(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
           channel, nickname,
           datetime.toString(QLatin1String("yyyyMMddHHmmss")),
           QLatin1String(format.constData()));
}

void saveImage(const QByteArray &data, const QString &fileName) {
  if (!fileName.isNull()) {
    QFile f(fileName);
    f.open(QIODevice::WriteOnly);
    f.write(data);
  }
}

void showImageDialog(QWidget *parent, const QString &nickname,
                     const QString &channel, const QByteArray &data,
                     const QByteArray &format) {
  auto imgDialog = new QDialog(parent);
  imgDialog->setAttribute(Qt::WA_DeleteOnClose);
  imgDialog->setModal(false);
  imgDialog->setWindowTitle(MainChatWindow::tr("Image from %1").arg(nickname));
  auto layout = new QVBoxLayout;
  auto label = new QLabel;
  auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Save);
  buttonBox->setCenterButtons(true);
  auto defaultPath = imageDefaultPath(channel, nickname, format);
  QObject::connect(buttonBox->button(QDialogButtonBox::Save),
                   &QPushButton::clicked, parent, [=](auto) {
                     auto fileName = QFileDialog::getSaveFileName(
                         parent,
                         MainChatWindow::tr("Save image from %1").arg(nickname),
                         defaultPath);
                     saveImage(data, fileName);
                   });
  auto image =
      QImage::fromData(reinterpret_cast<const uchar *>(data.constData()),
                       data.size(), format.constData());
  label->setPixmap(QPixmap::fromImage(image));
  label->setScaledContents(true);
  layout->addWidget(label);
  layout->addWidget(buttonBox);
  imgDialog->setLayout(layout);
  imgDialog->show();
}

QCompleter *createNicknameCompleter(Czateria::UserListModel *userlist,
                                    QObject *parent) {
  auto rv = new QCompleter(userlist, parent);
  rv->setCompletionRole(Qt::DisplayRole);
  rv->setCaseSensitivity(Qt::CaseInsensitive);
  rv->setCompletionMode(QCompleter::InlineCompletion);
  return rv;
}

QString getImageFilter() {
  static QString cached_result;
  if (!cached_result.isNull()) {
    return cached_result;
  }
  auto rv = MainChatWindow::tr("Images (");
  auto formats = QImageReader::supportedImageFormats();
  for (auto &&format : formats) {
    rv.append(QString(QLatin1String("*.%1"))
                  .arg(QString::fromUtf8(format.constData()).toLower()));
    if (&format != &formats.back()) {
      rv.append(QLatin1Char(' '));
    }
  }
  rv.append(QLatin1Char(')'));
  cached_result = rv;
  return rv;
}

QString explainBlockCause(Czateria::ChatSession::BlockCause why) {
  using c = Czateria::ChatSession::BlockCause;
  switch (why) {
  case c::Nick:
    return MainChatWindow::tr("nick");
  case c::Avatar:
    return MainChatWindow::tr("avatar");
  case c::Behaviour:
    return MainChatWindow::tr("behaviour");
  default:
    return QString();
  }
}

QString getKickBanMsgStr(const QString &blockTypeStr,
                         Czateria::ChatSession::BlockCause why,
                         const QString &adminNick = QString()) {
  auto bannedBy = adminNick.isEmpty()
                      ? QString()
                      : MainChatWindow::tr(" by %1").arg(adminNick);
  auto causeStr = why == Czateria::ChatSession::BlockCause::Unknown
                      ? QString()
                      : MainChatWindow::tr(" for inappropriate %1")
                            .arg(explainBlockCause(why));
  return QString(QLatin1String("You were %1%2%3"))
      .arg(blockTypeStr, bannedBy, causeStr);
}
} // namespace

class MainChatWindow::SettingsDialog : public QDialog {
public:
  SettingsDialog(MainChatWindow *parent)
      : QDialog(parent), mChatWindow(*parent), ui(new Ui::ChatSettingsForm()) {
    setWindowTitle(QString(QLatin1String("%1/%2 : Settings"))
                       .arg(parent->mChatSession->channel(),
                            parent->mChatSession->nickname()));
    setAttribute(Qt::WA_DeleteOnClose);
    setModal(true);

    auto lay = new QVBoxLayout;
    auto lbl = new QLabel(
        tr("This is the per-session settings window.\nIf you want to modify "
           "global defaults, open the settings window from the main window."));
    lbl->setAlignment(Qt::AlignCenter);
    lay->addWidget(lbl);

    auto form = new QWidget(this);
    ui->setupUi(form);
    lay->addWidget(form);

    auto btns =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    lay->addWidget(btns);

    setLayout(lay);

    ui->autoAcceptPrivs->setChecked(mChatWindow.mAutoAcceptPrivs);
    ui->autoSavePictures->setChecked(mChatWindow.mAutoSavePictures);
    ui->discardUnaccepted->setChecked(mChatWindow.mIgnoreUnacceptedMessages);
    ui->useEmojiIcons->setChecked(mChatWindow.ui->tabWidget->shouldUseEmoji());
  }

  ~SettingsDialog() { delete ui; }

private:
  MainChatWindow &mChatWindow;
  Ui::ChatSettingsForm *const ui;

  void accept() override {
    mChatWindow.mAutoAcceptPrivs = ui->autoAcceptPrivs->isChecked();
    mChatWindow.mAutoSavePictures = ui->autoSavePictures->isChecked();
    mChatWindow.mIgnoreUnacceptedMessages = ui->discardUnaccepted->isChecked();
    mChatWindow.ui->tabWidget->setUseEmoji(ui->useEmojiIcons->isChecked());
    QDialog::accept();
  }
};

MainChatWindow::MainChatWindow(QSharedPointer<Czateria::LoginSession> login,
                               Czateria::AvatarHandler &avatars,
                               const Czateria::Room &room,
                               const AppSettings &settings,
                               const Czateria::ChatBlocker &blocker,
                               Czateria::ChatSessionListener *listener,
                               MainWindow *mainWin)
    : QMainWindow(nullptr), ui(new Ui::ChatWidget), mMainWindow(mainWin),
      mChatSession(new Czateria::ChatSession(login, avatars, room, blocker,
                                             listener, this)),
      mSortProxy(new QSortFilterProxyModel(this)),
      mNicknameCompleter(
          createNicknameCompleter(mChatSession->userListModel(), this)),
      mShowChannelListAction(
          new QAction(QIcon(QLatin1String(":/icons/czateria.png")),
                      tr("Show channel list"), this)),
      mSendImageAction(
          new QAction(QIcon(QLatin1String(":/icons/file-picture-icon.png")),
                      tr("Send an image"), this)),
      mSettingsAction(new QAction(QIcon(QLatin1String(":/icons/settings.png")),
                                  tr("Open the settings window"), this)),
      mAutoAcceptPrivs(settings.autoAcceptPrivs),
      mAutoSavePictures(settings.savePicturesAutomatically),
      mIgnoreUnacceptedMessages(settings.ignoreUnacceptedMessages) {
  QIcon icon;
  icon.addFile(QString::fromUtf8(":/icons/czateria.png"), QSize(),
               QIcon::Normal, QIcon::Off);
  setWindowIcon(icon);
  setAcceptDrops(true);
  auto centralWidget = new QWidget(this);
  ui->setupUi(centralWidget);
  setWindowTitle(mChatSession->channel());
  setCentralWidget(centralWidget);
  auto toolbar = new QToolBar;
  addToolBar(Qt::TopToolBarArea, toolbar);

  mShowChannelListAction->setToolTip(tr("Show channel list"));
  mShowChannelListAction->setStatusTip(tr("Shows the channel list window"));
  connect(mShowChannelListAction, &QAction::triggered, mShowChannelListAction,
          [=](auto) { mMainWindow->show(); });
  toolbar->addAction(mShowChannelListAction);

  connect(mSendImageAction, &QAction::triggered, this, [=](auto) {
    auto filename = QFileDialog::getOpenFileName(
        this, tr("Select an image file"), QString(), getImageFilter());
    if (filename.isEmpty()) {
      return;
    }
    auto image = QImage(filename);
    if (image.isNull()) {
      QMessageBox::critical(
          this, tr("Not an image"),
          tr("The selected file does not appear to be an image"));
      return;
    }
    sendImageToCurrent(image);
  });
  mSendImageAction->setToolTip(tr("Send an image"));
  mSendImageAction->setStatusTip(
      tr("Sends an image to your conversation partner"));
  mSendImageAction->setEnabled(false);
  toolbar->addAction(mSendImageAction);

  mSettingsAction->setToolTip(tr("Opens the settings window"));
  connect(mSettingsAction, &QAction::triggered, this, [=](auto) {
    auto d = new SettingsDialog(this);
    d->exec();
  });
  toolbar->addAction(mSettingsAction);

  ui->tabWidget->setUseEmoji(settings.useEmojiIcons);

  auto desiredWidth = getOptimalUserListWidth(ui->listView);
  ui->widget_3->setMaximumSize(QSize(desiredWidth, QWIDGETSIZE_MAX));
  ui->widget_3->setMinimumSize(QSize(desiredWidth, 0));

  mSortProxy->setSourceModel(mChatSession->userListModel());
  mSortProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
  mSortProxy->setSortLocaleAware(true);
  mSortProxy->setDynamicSortFilter(true);
  void (QSortFilterProxyModel::*setFilterFn)(const QString &) =
      &QSortFilterProxyModel::setFilterRegExp;
  connect(ui->lineEdit_2, &QLineEdit::textChanged, mSortProxy, setFilterFn);

  ui->listView->setModel(mSortProxy);
  ui->listView->setUserListModel(mChatSession->userListModel());
  ui->listView->setAvatarHandler(&avatars);

  ui->nicknameLabel->setText(mChatSession->nickname());
  setAttribute(Qt::WA_DeleteOnClose);

  connect(mChatSession, &Czateria::ChatSession::roomMessageReceived,
          ui->tabWidget, &ChatWindowTabWidget::displayRoomMessage);
  connect(mChatSession, &Czateria::ChatSession::privateMessageReceived,
          ui->tabWidget, &ChatWindowTabWidget::displayPrivateMessage);
  connect(mChatSession, &Czateria::ChatSession::privateMessageReceived, this,
          &MainChatWindow::notifyActivity);
  connect(mChatSession, &Czateria::ChatSession::newPrivateConversation, this,
          &MainChatWindow::onNewPrivateConversation);
  connect(mChatSession, &Czateria::ChatSession::privateConversationCancelled,
          this, &MainChatWindow::onPrivateConversationCancelled);
  connect(mChatSession, &Czateria::ChatSession::userLeft, this,
          &MainChatWindow::onUserLeft);
  connect(mChatSession, &Czateria::ChatSession::privateConversationStateChanged,
          ui->tabWidget,
          &ChatWindowTabWidget::onPrivateConversationStateChanged);
  connect(mChatSession, &Czateria::ChatSession::nicknameAssigned,
          ui->nicknameLabel, &QLabel::setText);
  connect(mChatSession, &Czateria::ChatSession::imageReceived, this,
          [=](auto &&nickname, auto &&data, auto &&format) {
            const auto datetime = QDateTime::currentDateTime();
            const auto time = datetime.toString(QLatin1String("HH:mm:ss"));
            if (mAutoSavePictures) {
              auto defaultPath = imageDefaultPath(mChatSession->channel(),
                                                  nickname, format, datetime);
              ui->tabWidget->addMessageToPrivateChat(
                  nickname,
                  tr("[%1] Image saved as %2").arg(time).arg(defaultPath));
              saveImage(data, defaultPath);
            } else {
              showImageDialog(this, nickname, mChatSession->channel(), data,
                              format);
              ui->tabWidget->addMessageToPrivateChat(
                  nickname, tr("[%1] Image received").arg(time));
            }
            notifyActivity();
          });
  connect(mChatSession, &Czateria::ChatSession::sessionExpired, this, [=]() {
    QMessageBox::information(
        this, tr("Session expired"),
        tr("Your session has expired.\nPlease log back in."));
  });
  connect(mChatSession, &Czateria::ChatSession::sessionError, this, [=]() {
    QMessageBox::critical(
        this, tr("Communication error"),
        tr("An unknown error has occurred.\nPlease try logging in again, "
           "perhaps with a different nickname."));
  });

  connect(ui->tabWidget, &ChatWindowTabWidget::privateConversationClosed, this,
          [=](auto &&nickname) {
            mChatSession->notifyPrivateConversationClosed(nickname);
            updateWindowTitle();
          });
  connect(ui->tabWidget, &ChatWindowTabWidget::currentChanged, this,
          [=](int tabIdx) {
            // disable completer for private conversations
            ui->lineEdit->setCompleter(tabIdx == 0 ? mNicknameCompleter
                                                   : nullptr);
            updateWindowTitle();
          });
  connect(mChatSession, &Czateria::ChatSession::banned, this,
          [=](auto why, auto &&who) {
            QMessageBox::information(this, tr("Banned"),
                                     getKickBanMsgStr(tr("banned"), why, who));
          });
  connect(mChatSession, &Czateria::ChatSession::kicked, this, [=](auto why) {
    QMessageBox::information(this, tr("Kicked"),
                             getKickBanMsgStr(tr("kicked"), why));
  });
  connect(mChatSession, &Czateria::ChatSession::imageDelivered, this,
          [=](auto &&nick) {
            ui->tabWidget->addMessageToPrivateChat(
                nick, tr("[%1] Image delivered")
                          .arg(QDateTime::currentDateTime().toString(
                              QLatin1String("HH:mm:ss"))));
          });

  connect(ui->lineEdit, &QLineEdit::returnPressed, this,
          &MainChatWindow::onReturnPressed);
  ui->lineEdit->setCompleter(mNicknameCompleter);

  connect(ui->listView, &QAbstractItemView::doubleClicked, this,
          &MainChatWindow::onUserNameDoubleClicked);
  connect(ui->listView, &UserListView::mouseMiddleClicked, this,
          &MainChatWindow::onUserNameMiddleClicked);

  connect(ui->tabWidget, &QTabWidget::currentChanged, this,
          [=](auto idx) { mSendImageAction->setEnabled(idx != 0); });
  connect(ui->tabWidget, &ChatWindowTabWidget::privateConversationAccepted,
          this, [=](auto &&nickname) {
            doAcceptPrivateConversation(nickname);
            mainWin->removeNotification(this, nickname);
          });
  connect(ui->tabWidget, &ChatWindowTabWidget::privateConversationRejected,
          this, [=](auto &&nickname) {
            mChatSession->rejectPrivateConversation(nickname);
            mainWin->removeNotification(this, nickname);
          });

  ui->lineEdit->installEventFilter(this);

  mChatSession->start();
}

MainChatWindow::~MainChatWindow() { delete ui; }

void MainChatWindow::onPrivateConvNotificationAccepted(
    const QString &nickname) {
  ui->tabWidget->openPrivateMessageTab(nickname);
  doAcceptPrivateConversation(nickname);
}

void MainChatWindow::onPrivateConvNotificationRejected(
    const QString &nickname) {
  mChatSession->rejectPrivateConversation(nickname);
  ui->tabWidget->closePrivateConversationTab(nickname);
}

void MainChatWindow::onNewPrivateConversation(const QString &nickname) {
  if (mAutoAcceptPrivs || ui->tabWidget->privTabIsOpen(nickname)) {
    ui->tabWidget->openPrivateMessageTab(nickname);
    doAcceptPrivateConversation(nickname);
  } else {
    ui->tabWidget->askAcceptPrivateMessage(nickname);
    mMainWindow->displayNotification(this, nickname, mChatSession->channel());
  }
}

void MainChatWindow::onReturnPressed() {
  auto text = ui->lineEdit->text();
  auto currentNickname = ui->tabWidget->getCurrentNickname();
  if (currentNickname.isNull()) {
    mChatSession->sendRoomMessage(text);
  } else if (mChatSession->canSendMessage(currentNickname)) {
    mChatSession->sendPrivateMessage(currentNickname, text);
  } else {
    return;
  }
  ui->lineEdit->clear();
  ui->tabWidget->addMessageToCurrent(
      {QDateTime::currentDateTime(), text, mChatSession->nickname()});
}

void MainChatWindow::onUserNameDoubleClicked(const QModelIndex &proxyIdx) {
  auto idx = mSortProxy->mapToSource(proxyIdx);
  auto nickname = mChatSession->userListModel()->data(idx).toString();
  if (nickname != mChatSession->nickname()) {
    ui->tabWidget->openPrivateMessageTab(nickname);
    ui->lineEdit->setFocus(Qt::OtherFocusReason);
  }
}

void MainChatWindow::onUserNameMiddleClicked() {
  auto idx =
      mSortProxy->mapToSource(ui->listView->selectionModel()->currentIndex());
  auto nickname = mChatSession->userListModel()->data(idx).toString();
  ui->lineEdit->insert(nickname);
}

void MainChatWindow::doAcceptPrivateConversation(const QString &nickname) {
  mChatSession->acceptPrivateConversation(nickname);
  ui->lineEdit->setFocus(Qt::OtherFocusReason);
  notifyActivity();
}

void MainChatWindow::notifyActivity() {
  QApplication::alert(this);
  updateWindowTitle();
}

void MainChatWindow::updateWindowTitle() {
  auto unreadPrivs = ui->tabWidget->countUnreadPrivateTabs();
  auto channelName = mChatSession->channel();
  auto windowTitle = channelName;
  if (unreadPrivs) {
    windowTitle =
        QString(QLatin1String("[%1] %2")).arg(unreadPrivs).arg(channelName);
  }
  setWindowTitle(windowTitle);
}

void MainChatWindow::sendImageToCurrent(const QImage &image) {
  mChatSession->sendImage(ui->tabWidget->getCurrentNickname(), image);
  ui->tabWidget->addMessageToCurrent(
      tr("[%1] Image sent")
          .arg(QDateTime::currentDateTime().toString(
              QLatin1String("HH:mm:ss"))));
}

bool MainChatWindow::sendImageFromMime(const QMimeData *mime) {
  Q_ASSERT(!ui->tabWidget->getCurrentNickname().isEmpty());
  QImage img;
  if (mime->hasImage()) {
    img = qvariant_cast<QImage>(mime->imageData());
  } else if (mime->hasUrls()) {
    auto urls = mime->urls();
    auto &&url = urls[0];
    if (url.isLocalFile()) {
      img = QImage(url.toLocalFile());
    }
  }
  if (!img.isNull()) {
    sendImageToCurrent(img);
    return true;
  } else {
    return false;
  }
}

void MainChatWindow::onUserLeft(const QString &nickname) {
  mMainWindow->removeNotification(this, nickname);
  ui->tabWidget->writeConversationState(
      nickname, tr("User logged out"),
      QIcon(QLatin1String(":/icons/door_out.png")));
}

void MainChatWindow::onPrivateConversationCancelled(const QString &nickname) {
  mMainWindow->removeNotification(this, nickname);
  if (mIgnoreUnacceptedMessages) {
    ui->tabWidget->closePrivateConversationTab(nickname);
    updateWindowTitle();
  }
}

void MainChatWindow::dragEnterEvent(QDragEnterEvent *ev) {
  if (ui->tabWidget->getCurrentNickname().isEmpty()) {
    return;
  }

  const auto mime = ev->mimeData();
  bool has_data = false;
  if (mime->hasImage()) {
    has_data = true;
  } else if (mime->hasUrls()) {
    auto urls = mime->urls();
    auto &&url = urls[0];
    if (url.isLocalFile()) {
      // TODO caching this value would be nice, but we have no means of
      // modifying the event's QMimeData. currently the QImage needs to be
      // constructed for a second time in dropEvent().
      auto img = QImage(url.toLocalFile());
      has_data = !img.isNull();
    }
  }

  if (has_data) {
    ev->acceptProposedAction();
  }
}

void MainChatWindow::dropEvent(QDropEvent *ev) {
  sendImageFromMime(ev->mimeData());
  ev->acceptProposedAction();
}

bool MainChatWindow::eventFilter(QObject *obj, QEvent *ev) {
  if (obj == ui->lineEdit && ev->type() == QEvent::KeyPress) {
    auto keyEv = static_cast<QKeyEvent *>(ev);
    if (keyEv == QKeySequence::Paste &&
        !ui->tabWidget->getCurrentNickname().isEmpty()) {
      const auto clipboard = QGuiApplication::clipboard();
      const QClipboard::Mode mode =
          clipboard->supportsSelection() &&
                  (keyEv->modifiers() == (Qt::CTRL | Qt::SHIFT)) &&
                  keyEv->key() == Qt::Key_Insert
              ? QClipboard::Selection
              : QClipboard::Clipboard;
      if (sendImageFromMime(clipboard->mimeData(mode))) {
        return true;
      }
    }
  }
  return QMainWindow::eventFilter(obj, ev);
}
