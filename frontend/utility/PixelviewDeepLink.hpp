// Pixelview modification: strict player-link parsing; no authentication or logging.
#pragma once
#include <QString>
#include <QUrl>
#include <QSet>
#include <QRegularExpression>
#include <optional>

namespace pixelview {
struct DeepLink { QString session; QString password; };
inline bool linkTextSafe(const QString &text)
{
	for (auto c : text)
		if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format ||
		    c.category() == QChar::Separator_Line || c.category() == QChar::Separator_Paragraph) return false;
	return true;
}
inline std::optional<QString> linkPercentDecode(const QString &text)
{
	static const QRegularExpression invalidPercent(QStringLiteral("%(?![0-9A-Fa-f]{2})"));
	if (invalidPercent.match(text).hasMatch()) return {};
	const auto bytes = QByteArray::fromPercentEncoding(text.toUtf8());
	const auto decoded = QString::fromUtf8(bytes);
	if (decoded.toUtf8() != bytes || !linkTextSafe(decoded)) return {};
	return decoded;
}
inline std::optional<DeepLink> parseDeepLink(const QString &raw)
{
	if (raw.size() > 8192 || !linkTextSafe(raw) || raw.contains('#') || raw.contains('\\')) return {};
	QString rest;
	if (raw.startsWith(QStringLiteral("https://play.pixelview.io/"))) rest = raw.mid(26);
	else if (raw.startsWith(QStringLiteral("pixelview://play/"))) rest = raw.mid(17);
	else return {};
	const auto queryAt = rest.indexOf('?');
	const auto path = queryAt < 0 ? rest : rest.left(queryAt);
	auto session = linkPercentDecode(path);
	if (!session || session->isEmpty() || session->toUtf8().size() > 256 || session->trimmed() != *session ||
	    *session == "." || *session == ".." || session->contains('/') || session->contains('\\')) return {};
	QString token;
	QSet<QString> keys;
	if (queryAt >= 0) {
		for (const auto &item : rest.mid(queryAt + 1).split('&')) {
			const auto equal = item.indexOf('=');
			auto key = linkPercentDecode(equal < 0 ? item : item.left(equal));
			auto value = linkPercentDecode(equal < 0 ? QString() : item.mid(equal + 1));
			if (!key || !value || key->isEmpty() || keys.contains(*key)) return {};
			keys.insert(*key);
			if (*key == "token") token = *value;
		}
	}
	// Decode once; never repair form-query spaces into '+'. Require canonical pad bits.
	static const QRegularExpression base64(QStringLiteral("^[A-Za-z0-9+/_-]*={0,2}$"));
	if (!base64.match(token).hasMatch() || ((token.contains('-') || token.contains('_')) &&
	    (token.contains('+') || token.contains('/')))) return {};
	QByteArray standard = token.toLatin1().replace('-', '+').replace('_', '/');
	const auto bytes = QByteArray::fromBase64(standard, QByteArray::AbortOnBase64DecodingErrors);
	if (bytes.size() > 4096) return {};
	QByteArray canonical = bytes.toBase64();
	if (!standard.contains('=')) while (canonical.endsWith('=')) canonical.chop(1);
	if (canonical != standard) return {};
	const auto password = QString::fromUtf8(bytes);
	if (password.toUtf8() != bytes || !linkTextSafe(password)) return {};
	return DeepLink{*session, password};
}
} // namespace pixelview
