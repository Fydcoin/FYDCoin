// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2019 The FYD developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "overviewpage.h"
#include "ui_overviewpage.h"

#include "bitcoinunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "init.h"
#include "obfuscation.h"
#include "obfuscationconfig.h"
#include "optionsmodel.h"
#include "transactionfilterproxy.h"
#include "transactionrecord.h"
#include "transactiontablemodel.h"
#include "walletmodel.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QSettings>
#include <QTimer>

#define DECORATION_SIZE 48
#define ICON_OFFSET 14
#define NUM_ITEMS 4

extern CWallet* pwalletMain;

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate() : QAbstractItemDelegate(), unit(BitcoinUnits::FYD)
    {
    }

    inline void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QRect mainRect = option.rect;
        mainRect.moveLeft(ICON_OFFSET);
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace =  -40;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2 * ypad) / 2;
        QRect amountRect(mainRect.left() + xspace , mainRect.top() + ypad, mainRect.width() - xspace - ICON_OFFSET, halfheight);
        QRect addressRect(mainRect.left() + xspace , mainRect.top() + ypad + halfheight, mainRect.width() - xspace - ICON_OFFSET, halfheight);
        icon.paint(painter, decorationRect);


        QString type = index.data(TransactionTableModel::TypeRole).toString();
        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();

        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = COLOR_BLACK;
        if (value.canConvert<QBrush>()) {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(COLOR_STAKE);
        QRect boundingRect;

        if(address.size()>30){
        address = address.leftJustified(15,'.',true);

          address = address + "...)";

      }
        painter->drawText(addressRect, Qt::AlignCenter | Qt::AlignVCenter, address, &boundingRect);

        if (index.data(TransactionTableModel::WatchonlyRole).toBool()) {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect watchonlyRect(boundingRect.right() + 5, mainRect.top() + ypad + halfheight, 16, halfheight);
            iconWatchonly.paint(painter, watchonlyRect);
        }

        



        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true, BitcoinUnits::separatorAlways);
        QString st = amountText.leftJustified(amountText.size()-10,'.',true);
        st = st + " FYD ";
        if (!confirmed) {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight | Qt::AlignVCenter, st);

        painter->setPen(COLOR_BLACK);
        painter->drawText(amountRect, Qt::AlignCenter | Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
};
#include "overviewpage.moc"

OverviewPage::OverviewPage(QWidget* parent) : QWidget(parent),
                                              ui(new Ui::OverviewPage),
                                              clientModel(0),
                                              walletModel(0),
                                              currentBalance(-1),
                                              currentUnconfirmedBalance(-1),
                                              currentImmatureBalance(-1),
                                              currentZerocoinBalance(-1),
                                              currentUnconfirmedZerocoinBalance(-1),
                                              currentimmatureZerocoinBalance(-1),
                                              currentWatchOnlyBalance(-1),
                                              currentWatchUnconfBalance(-1),
                                              currentWatchImmatureBalance(-1),
                                              currentEarnings(-1),
                                              currentMasternodeEarnings(-1),
                                              currentStakeEarnings(-1),
                                              txdelegate(new TxViewDelegate()),
                                              filter(0),
                                              filter1(0)
{
    nDisplayUnit = 0; // just make sure it's not unitialized
    ui->setupUi(this);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // Recent Rewards
    ui->listRewards->setItemDelegate(txdelegate);
    ui->listRewards->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listRewards->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listRewards->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listRewards, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // init "out of sync" warning labels
    ui->labelWalletStatus->setText("(" + tr("out of sync") + ")");
    ui->labelTransactionsStatus->setText("(" + tr("out of sync") + ")");

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
}

void OverviewPage::handleTransactionClicked(const QModelIndex& index)
{
    if (filter){
        emit transactionClicked(filter->mapToSource(index));
      }
      if (filter1)
          emit transactionClicked(filter1->mapToSource(index));

}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::getPercentage(CAmount nUnlockedBalance, CAmount nZerocoinBalance, QString& sFYDPercentage, QString& szFYDPercentage)
{
    int nPrecision = 2;
    double dzPercentage = 0.0;

    if (nZerocoinBalance <= 0){
        dzPercentage = 0.0;
    }
    else{
        if (nUnlockedBalance <= 0){
            dzPercentage = 100.0;
        }
        else{
            dzPercentage = 100.0 * (double)(nZerocoinBalance / (double)(nZerocoinBalance + nUnlockedBalance));
        }
    }

    double dPercentage = 100.0 - dzPercentage;

    szFYDPercentage = "(" + QLocale(QLocale::system()).toString(dzPercentage, 'f', nPrecision) + " %)";
    sFYDPercentage = "(" + QLocale(QLocale::system()).toString(dPercentage, 'f', nPrecision) + " %)";

}

void OverviewPage::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                              const CAmount& zerocoinBalance, const CAmount& unconfirmedZerocoinBalance, const CAmount& immatureZerocoinBalance,
                              const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance,
                              const CAmount& earnings, const CAmount& masternodeEarnings, const CAmount& stakeEarnings)
{
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    currentZerocoinBalance = zerocoinBalance;
    currentUnconfirmedZerocoinBalance = unconfirmedZerocoinBalance;
    currentimmatureZerocoinBalance = immatureZerocoinBalance;
    currentWatchOnlyBalance = watchOnlyBalance;
    currentWatchUnconfBalance = watchUnconfBalance;
    currentWatchImmatureBalance = watchImmatureBalance;

    currentEarnings = earnings;
    currentMasternodeEarnings = masternodeEarnings;
    currentStakeEarnings = stakeEarnings;

    CAmount nLockedBalance = 0;
    CAmount nWatchOnlyLockedBalance = 0;
    if (pwalletMain) {
        nLockedBalance = pwalletMain->GetLockedCoins();
        nWatchOnlyLockedBalance = pwalletMain->GetLockedWatchOnlyBalance();
    }

    // FYD Balance
    CAmount nTotalBalance = balance + unconfirmedBalance;
    CAmount fydAvailableBalance = balance - immatureBalance - nLockedBalance;
    CAmount nUnlockedBalance = nTotalBalance - nLockedBalance;

    // FYD Watch-Only Balance
    CAmount nTotalWatchBalance = watchOnlyBalance + watchUnconfBalance;
    CAmount nAvailableWatchBalance = watchOnlyBalance - watchImmatureBalance - nWatchOnlyLockedBalance;

    // zFYD Balance
    CAmount matureZerocoinBalance = zerocoinBalance - unconfirmedZerocoinBalance - immatureZerocoinBalance;

    // Percentages
    QString szPercentage = "";
    QString sPercentage = "";
    getPercentage(nUnlockedBalance, zerocoinBalance, sPercentage, szPercentage);
    // Combined balances
    CAmount availableTotalBalance = fydAvailableBalance + matureZerocoinBalance;
    CAmount sumTotalBalance = nTotalBalance + zerocoinBalance;

    // FYD labels
    ui->labelBalance->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, fydAvailableBalance, false, BitcoinUnits::separatorAlways));
    ui->labelUnconfirmed->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, unconfirmedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelImmature->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, immatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelLockedBalance->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, nLockedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, nTotalBalance, false, BitcoinUnits::separatorAlways));

    // Watchonly labels
    ui->labelWatchAvailable->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, nAvailableWatchBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchPending->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, watchUnconfBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchImmature->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, watchImmatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchLocked->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, nWatchOnlyLockedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchTotal->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, nTotalWatchBalance, false, BitcoinUnits::separatorAlways));

    // Combined labels
    //ui->labelBalancez->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, availableTotalBalance, false, BitcoinUnits::separatorAlways));
    //ui->labelTotalz->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, sumTotalBalance, false, BitcoinUnits::separatorAlways));

    // Earnings labels
    ui->labelzBalance->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, earnings, false, BitcoinUnits::separatorAlways));
    ui->labelMasternodeRewards->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, masternodeEarnings, false, BitcoinUnits::separatorAlways));
    ui->labelStakeRewards->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, stakeEarnings, false, BitcoinUnits::separatorAlways));

    // Adjust bubble-help according to AutoMint settings
    QString automintHelp = tr("Current percentage of zFYD.\nIf AutoMint is enabled this percentage will settle around the configured AutoMint percentage (default = 10%).\n");
    bool fEnableZeromint = GetBoolArg("-enablezeromint", Params().ZeroCoinEnabled());
    int nZeromintPercentage = GetArg("-zeromintpercentage", 10);
    if (fEnableZeromint) {
        automintHelp += tr("AutoMint is currently enabled and set to ") + QString::number(nZeromintPercentage) + "%.\n";
        automintHelp += tr("To disable AutoMint add 'enablezeromint=0' in fyd.conf.");
    }
    else {
        automintHelp += tr("AutoMint is currently disabled.\nTo enable AutoMint change 'enablezeromint=0' to 'enablezeromint=1' in fyd.conf");
    }

    // Only show most balances if they are non-zero for the sake of simplicity
    QSettings settings;
    bool settingShowAllBalances = !settings.value("fHideZeroBalances").toBool();

    bool showSumAvailable = settingShowAllBalances || sumTotalBalance != availableTotalBalance;
    //ui->labelBalanceTextz->setVisible(showSumAvailable);
    //ui->labelBalancez->setVisible(showSumAvailable);

    bool showWatchOnly = nTotalWatchBalance != 0;

    // FYD Available
    bool showFYDAvailable = settingShowAllBalances || fydAvailableBalance != nTotalBalance;
    bool showWatchOnlyFYDAvailable = showFYDAvailable || nAvailableWatchBalance != nTotalWatchBalance;
    ui->labelBalanceText->setVisible(showFYDAvailable || showWatchOnlyFYDAvailable);
    ui->labelBalance->setVisible(showFYDAvailable || showWatchOnlyFYDAvailable);
    ui->labelWatchAvailable->setVisible(showWatchOnlyFYDAvailable && showWatchOnly);

    // FYD Pending
    bool showFYDPending = settingShowAllBalances || unconfirmedBalance != 0;
    bool showWatchOnlyFYDPending = showFYDPending || watchUnconfBalance != 0;
    ui->labelPendingText->setVisible(showFYDPending || showWatchOnlyFYDPending);
    ui->labelUnconfirmed->setVisible(showFYDPending || showWatchOnlyFYDPending);
    ui->labelWatchPending->setVisible(showWatchOnlyFYDPending && showWatchOnly);

    // FYD Immature
    bool showFYDImmature = settingShowAllBalances || immatureBalance != 0;
    bool showWatchOnlyImmature = showFYDImmature || watchImmatureBalance != 0;
    ui->labelImmatureText->setVisible(showFYDImmature || showWatchOnlyImmature);
    ui->labelImmature->setVisible(showFYDImmature || showWatchOnlyImmature); // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelWatchImmature->setVisible(showWatchOnlyImmature && showWatchOnly); // show watch-only immature balance

    // FYD Locked
    bool showFYDLocked = settingShowAllBalances || nLockedBalance != 0;
    bool showWatchOnlyFYDLocked = showFYDLocked || nWatchOnlyLockedBalance != 0;
    ui->labelLockedBalanceText->setVisible(showFYDLocked || showWatchOnlyFYDLocked);
    ui->labelLockedBalance->setVisible(showFYDLocked || showWatchOnlyFYDLocked);
    ui->labelWatchLocked->setVisible(showWatchOnlyFYDLocked && showWatchOnly);

    // Masternode and Stake Earnings.
    ui->labelStakeRewards->setVisible(true);
    ui->labelStakeRewardsText->setVisible(true);
    ui->labelMasternodeRewards->setVisible(true);
    ui->labelMasternodeRewardsText->setVisible(true);

    // Percent split
    //bool showPercentages = ! (zerocoinBalance == 0 && nTotalBalance == 0);
    ui->labelFYDPercent->setVisible(false);
    ui->labelzFYDPercent->setVisible(false);

    static int cachedTxLocks = 0;

    if (cachedTxLocks != nCompleteTXLocks) {
        cachedTxLocks = nCompleteTXLocks;
        ui->listTransactions->update();
        ui->listRewards->update();
    }
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    ui->labelSpendable->setVisible(showWatchOnly);      // show spendable label (only when watch-only is active)
    ui->labelWatchonly->setVisible(showWatchOnly);      // show watch-only label
    ui->labelWatchAvailable->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchPending->setVisible(showWatchOnly);   // show watch-only pending balance
    ui->labelWatchLocked->setVisible(showWatchOnly);     // show watch-only total balance
    ui->labelWatchTotal->setVisible(showWatchOnly);     // show watch-only total balance

    if (!showWatchOnly) {
        ui->labelWatchImmature->hide();
    } else {
        ui->labelBalance->setIndent(20);
        ui->labelUnconfirmed->setIndent(20);
        ui->labelLockedBalance->setIndent(20);
        ui->labelImmature->setIndent(20);
        ui->labelTotal->setIndent(20);
    }
}

void OverviewPage::setClientModel(ClientModel* model)
{
    this->clientModel = model;
    if (model) {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel* model)
{
    this->walletModel = model;
    if (model && model->getOptionsModel()) {
        // Set up transaction list
        filter = new TransactionFilterProxy();
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->setTypeFilter(TransactionFilterProxy::TYPE(TransactionRecord::RecvWithAddress) | TransactionFilterProxy::TYPE(TransactionRecord::RecvFromOther) | TransactionFilterProxy::TYPE(TransactionRecord::SendToAddress) | TransactionFilterProxy::TYPE(TransactionRecord::SendToOther) | TransactionFilterProxy::TYPE(TransactionRecord::SendToSelf));
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        filter1 = new TransactionFilterProxy();
        filter1->setSourceModel(model->getTransactionTableModel());
        filter1->setLimit(NUM_ITEMS);
        filter1->setDynamicSortFilter(true);
        filter1->setSortRole(Qt::EditRole);
        filter1->setShowInactive(false);
        filter1->setTypeFilter(TransactionFilterProxy::TYPE(TransactionRecord::MNReward) | TransactionFilterProxy::TYPE(TransactionRecord::StakeMint) | TransactionFilterProxy::TYPE(TransactionRecord::StakeZFYD));


        filter1->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter);
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        ui->listRewards->setModel(filter1);
        ui->listRewards->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(),
                   model->getZerocoinBalance(), model->getUnconfirmedZerocoinBalance(), model->getImmatureZerocoinBalance(),
                   model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance(),
                   model->getEarnings(), model->getMasternodeEarnings(), model->getStakeEarnings());
        connect(model, SIGNAL(balanceChanged(CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount)), this,
                       SLOT(setBalance(CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        connect(model->getOptionsModel(), SIGNAL(hideZeroBalancesChanged(bool)), this, SLOT(updateDisplayUnit()));
        connect(model->getOptionsModel(), SIGNAL(hideOrphansChanged(bool)), this, SLOT(hideOrphans(bool)));

        updateWatchOnlyLabels(model->haveWatchOnly());
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));
    }

    // update the display unit, to not use the default ("FYD")
    updateDisplayUnit();

    // Hide orphans
    QSettings settings;
    hideOrphans(settings.value("fHideOrphans", false).toBool());
}

void OverviewPage::updateDisplayUnit()
{
    if (walletModel && walletModel->getOptionsModel()) {
        nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();
        if (currentBalance != -1)
            setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance, currentZerocoinBalance, currentUnconfirmedZerocoinBalance, currentimmatureZerocoinBalance,
                currentWatchOnlyBalance, currentWatchUnconfBalance, currentWatchImmatureBalance,
                currentEarnings, currentMasternodeEarnings, currentStakeEarnings);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = nDisplayUnit;

        ui->listTransactions->update();
        ui->listRewards->update();
    }
}

void OverviewPage::updateAlerts(const QString& warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}

void OverviewPage::hideOrphans(bool fHide)
{
    if (filter)
        filter->setHideOrphans(fHide);
}
