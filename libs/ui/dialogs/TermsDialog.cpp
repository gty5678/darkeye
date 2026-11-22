#include "ui/dialogs/TermsDialog.h"

#include "darkeye_ui/components/DesignButton.h"
#include "darkeye_ui/components/DesignLabel.h"
#include "darkeye_ui/theme/IconProvider.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace darkeye
{

TermsDialog::TermsDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("TermsDialog"));
    setWindowTitle(QStringLiteral("用户使用条款"));
    setWindowIcon(IconProvider::builtIn(QStringLiteral("house")));
    setModal(true);
    setFixedSize(500, 400);

    auto *layout = new QVBoxLayout(this);
    auto *text = new DesignLabel({}, this);
    text->setObjectName(QStringLiteral("TermsText"));
    text->setTextFormat(Qt::RichText);
    text->setWordWrap(true);
    text->setText(QStringLiteral(
        "<h3>欢迎使用 <b>暗之眼</b>！</h3>"
        "<p>—— 帮助你在黑暗界中睁开一只眼，探索广阔的暗黑界。</p>"
        "<p><b>在使用前，请仔细阅读以下使用条款：</b></p>"
        "<ol>"
        "<li>本软件仅供学习与研究用途。</li>"
        "<li>本软件为免费个人使用，未经许可不得用于商业用途。</li>"
        "<li>本软件作者编写出该软件旨在学习 Python,C++,Qt，提高编程水平</li>"
        "<li>用户在使用本软件前，请用户了解并遵守当地法律法规，如果本软件使用过程中存在违反当地法律法规的行为，请勿使用该软件</li>"
        "<li>用户需自行承担使用风险,若用户在当地产生一切违法行为由用户承担。</li>"
        "<li>本软件不会收集任何数据，所有数据均存储在个人用户电脑上。</li>"
        "<li>开发者不对因使用本软件造成的任何损失负责。</li>"
        "<li>开发者不对数据丢失或损坏负责。</li>"
        "<li>本软件仅供 18 岁以上成年人使用。</li>"
        "<li>请不要在微信里传播软件。</li>"
        "<li>源代码和二进制程序请在下载后24小时内删除。</li>"
        "<li>本条款的最终解释权归开发者所有。</li>"
        "<li>若用户不同意上述条款任意一条，请勿使用本软件。</li>"
        "</ol>"
        "<p>点击 <b>“我同意”</b> 表示您已阅读并接受以上内容。</p>"));
    layout->addWidget(text, 1);

    auto *buttons = new QHBoxLayout;
    auto *agree = new DesignButton(QStringLiteral("我同意"), this);
    agree->setObjectName(QStringLiteral("TermsAgreeButton"));
    agree->setVariant(QStringLiteral("primary"));
    auto *disagree = new DesignButton(QStringLiteral("不同意"), this);
    disagree->setObjectName(QStringLiteral("TermsDisagreeButton"));
    buttons->addWidget(agree);
    buttons->addWidget(disagree);
    layout->addLayout(buttons);

    connect(agree, &QPushButton::clicked, this, &QDialog::accept);
    connect(disagree, &QPushButton::clicked, this, &QDialog::reject);
}

} // namespace darkeye
