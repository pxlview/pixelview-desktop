// Pixelview modification: fill the normal masked receive form, never start media.
#include <utility/PixelviewDeepLinkInbox.hpp>
#include <QStatusBar>

void OBSBasic::ApplyPixelviewDeepLink(const std::optional<pixelview::DeepLink> &link)
{
	if (isClosing() || pixelviewShutdownPending) return;
	if (PixelviewModeBusy()) {
		statusBar()->showMessage(QStringLiteral("Session link not opened while busy. Stop the current operation and open the link again."), 10000);
		return;
	}
	if (!link) {
		statusBar()->showMessage(QStringLiteral("Invalid Pixelview session link."), 10000);
		return;
	}
	SelectPixelviewMode(1);
	if (!pixelviewReceiving) {
		statusBar()->showMessage(QStringLiteral("Could not open the receiving form."), 10000);
		return;
	}
	{
		// Do not expose an intermediate session/password pair to form observers.
		const QSignalBlocker idBlocker(pixelviewReceiveId);
		const QSignalBlocker passwordBlocker(pixelviewReceivePassword);
		pixelviewReceiveId->setText(link->session);
		pixelviewReceivePassword->setText(link->password);
		pixelviewReceivePasswordOrigin = PixelviewReceiveOrigin();
		pixelviewReceiveCredentialUnavailable = false;
	}
	SetPixelviewReceiveStatus(QStringLiteral("Session link filled. Check receiver name, then click Start receiving."), ReceiveStatusTone::Idle);
	SavePixelviewReceive();
	show(); raise(); activateWindow();
}
