// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/image/svg_safety.h"

#include "base/debug_log.h"

#include <QtCore/QHash>
#include <QtCore/QHashSeed>
#include <QtCore/QList>
#include <QtCore/QPair>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtCore/QXmlStreamReader>

namespace Images {
namespace {

constexpr auto kMaxSvgElements = 100 * 1000;
constexpr auto kMaxSvgNesting = 100;
constexpr auto kMaxRenderDepth = 256;
constexpr auto kMaxReferenceExpansion = 500 * 1000;

[[nodiscard]] bool IsName(const QString &value, const char *name) {
	return (value.compare(QLatin1String(name), Qt::CaseInsensitive) == 0);
}

[[nodiscard]] bool IsDisallowedElement(const QString &name) {
	return IsName(name, "script")
		|| IsName(name, "foreignObject")
		|| IsName(name, "iframe")
		|| IsName(name, "object")
		|| IsName(name, "embed")
		|| IsName(name, "audio")
		|| IsName(name, "video")
		|| IsName(name, "animate")
		|| IsName(name, "animateColor")
		|| IsName(name, "animateMotion")
		|| IsName(name, "animateTransform")
		|| IsName(name, "set");
}

[[nodiscard]] bool IsPatternOrMask(const QString &name) {
	return IsName(name, "pattern") || IsName(name, "mask");
}

[[nodiscard]] bool IsNonPaintSourceType(const QString &name) {
	return IsName(name, "linearGradient")
		|| IsName(name, "radialGradient")
		|| IsName(name, "stop")
		|| IsName(name, "style");
}

[[nodiscard]] QString TrimAndUnquote(QString value) {
	value = value.trimmed();
	if (value.size() < 2) {
		return value;
	}
	const auto first = value.at(0);
	const auto last = value.at(value.size() - 1);
	if (((first == QLatin1Char('"')) && (last == QLatin1Char('"')))
		|| ((first == QLatin1Char('\'')) && (last == QLatin1Char('\'')))) {
		value = value.mid(1, value.size() - 2).trimmed();
	}
	return value;
}

[[nodiscard]] bool IsAllowedReference(QString value) {
	value = TrimAndUnquote(value);
	return !value.isEmpty() && value.startsWith(QLatin1Char('#'));
}

[[nodiscard]] QString ReferencedId(QString value) {
	value = TrimAndUnquote(value);
	return (!value.isEmpty() && value.startsWith(QLatin1Char('#')))
		? value.mid(1)
		: QString();
}

// Collects targets of all url(#id) references found in text. Returns false
// if a url(...) points anywhere except a local fragment or is unterminated.
[[nodiscard]] bool CollectUrlReferences(
		const QString &text,
		QList<QString> &targets) {
	auto index = 0;
	while (true) {
		index = text.indexOf(
			QLatin1String("url("),
			index,
			Qt::CaseInsensitive);
		if (index < 0) {
			return true;
		}
		const auto start = index + 4;
		const auto end = text.indexOf(QLatin1Char(')'), start);
		if (end < 0) {
			return false;
		}
		const auto raw = text.mid(start, end - start);
		if (!IsAllowedReference(raw)) {
			return false;
		}
		if (const auto id = ReferencedId(raw); !id.isEmpty()) {
			targets.push_back(id);
		}
		index = end + 1;
	}
}

[[nodiscard]] bool HasDisallowedCss(const QString &text) {
	if (text.contains(QLatin1String("@import"), Qt::CaseInsensitive)) {
		return true;
	}
	if (text.contains(QLatin1String("@keyframes"), Qt::CaseInsensitive)
		|| text.contains(QLatin1String("animation"), Qt::CaseInsensitive)) {
		return true;
	}
	auto ignored = QList<QString>();
	return !CollectUrlReferences(text, ignored);
}

[[nodiscard]] bool IsHexDigit(QChar ch) {
	const auto u = ch.unicode();
	return (u >= '0' && u <= '9')
		|| (u >= 'a' && u <= 'f')
		|| (u >= 'A' && u <= 'F');
}

[[nodiscard]] QString HexPreprocess(
		const QString &input,
		bool *hasEscapeSequences = nullptr) {
	if (hasEscapeSequences) {
		*hasEscapeSequences = false;
	}
	const auto size = int(input.size());
	auto output = QString();
	output.reserve(size);
	auto i = 0;
	while (i < size) {
		const auto ch = input.at(i);
		if (ch != QLatin1Char('\\')) {
			output.append(ch);
			++i;
			continue;
		}
		++i;
		auto hexCount = 0;
		const auto hexStart = i;
		while ((i < size) && (hexCount < 7) && IsHexDigit(input.at(i))) {
			++hexCount;
			++i;
		}
		if (hexCount == 0) {
			if (hasEscapeSequences) {
				*hasEscapeSequences = true;
			}
			output.append(QLatin1Char('\\'));
			continue;
		}
		const auto used = (hexCount < 6) ? hexCount : 6;
		auto ok = false;
		const auto code = input.mid(hexStart, used).toUShort(&ok, 16);
		if (ok) {
			output.append(QChar(code));
			i = hexStart + used;
		} else {
			output.append(QLatin1Char('\\'));
			i = hexStart;
		}
	}
	return output;
}

enum class CssType {
	Space,
	Ident,
	Function,
	Hash,
	String,
	At,
	Other,
	Punct,
};

struct CssSymbol {
	CssType type = CssType::Other;
	int start = 0;
	int len = 0;
};

[[nodiscard]] bool CssNameChar(QChar ch) {
	const auto u = ch.unicode();
	return (u >= 'a' && u <= 'z')
		|| (u >= 'A' && u <= 'Z')
		|| (u >= '0' && u <= '9')
		|| (u == '_')
		|| (u == '-')
		|| (u >= 128);
}

[[nodiscard]] bool CssNameStart(QChar ch) {
	const auto u = ch.unicode();
	return (u >= 'a' && u <= 'z')
		|| (u >= 'A' && u <= 'Z')
		|| (u == '_')
		|| (u >= 128);
}

[[nodiscard]] QString CssLexem(const QString &text, const CssSymbol &symbol) {
	const auto end = symbol.start + symbol.len;
	auto result = QString();
	result.reserve(symbol.len);
	for (auto i = symbol.start; i < end; ++i) {
		if ((text.at(i) == QLatin1Char('\\')) && (i < end - 1)) {
			++i;
		}
		result.append(text.at(i));
	}
	return result;
}

[[nodiscard]] QList<CssSymbol> TokenizeCss(const QString &text) {
	const auto n = int(text.size());
	auto out = QList<CssSymbol>();
	const auto scanName = [&](int from) {
		auto j = from;
		while (j < n) {
			const auto ch = text.at(j);
			if (ch == QLatin1Char('\\')) {
				if (j + 1 < n) {
					const auto u = text.at(j + 1).unicode();
					if ((u != 10) && (u != 12) && (u != 13)) {
						j += 2;
						continue;
					}
				}
				break;
			} else if (CssNameChar(ch)) {
				++j;
			} else {
				break;
			}
		}
		return j;
	};
	auto i = 0;
	while (i < n) {
		const auto start = i;
		const auto ch = text.at(i);
		const auto u = ch.unicode();
		if ((u == 9) || (u == 10) || (u == 12) || (u == 13) || (u == 32)) {
			while (i < n) {
				const auto w = text.at(i).unicode();
				if ((w == 9) || (w == 10) || (w == 12) || (w == 13)
					|| (w == 32)) {
					++i;
				} else {
					break;
				}
			}
			out.push_back({ CssType::Space, start, i - start });
		} else if ((ch == QLatin1Char('/'))
			&& (i + 1 < n)
			&& (text.at(i + 1) == QLatin1Char('*'))) {
			i += 2;
			while ((i + 1 < n)
				&& !((text.at(i) == QLatin1Char('*'))
					&& (text.at(i + 1) == QLatin1Char('/')))) {
				++i;
			}
			i = (i + 1 < n) ? (i + 2) : n;
			out.push_back({ CssType::Space, start, i - start });
		} else if ((ch == QLatin1Char('"')) || (ch == QLatin1Char('\''))) {
			++i;
			while (i < n) {
				const auto d = text.at(i);
				if (d == QLatin1Char('\\')) {
					i = (i + 1 < n) ? (i + 2) : n;
					continue;
				} else if (d == ch) {
					++i;
					break;
				} else if ((d.unicode() == 10)
					|| (d.unicode() == 12)
					|| (d.unicode() == 13)) {
					break;
				}
				++i;
			}
			out.push_back({ CssType::String, start, i - start });
		} else if (ch == QLatin1Char('#')) {
			const auto e = scanName(i + 1);
			if (e > i + 1) {
				out.push_back({ CssType::Hash, start, e - start });
				i = e;
			} else {
				out.push_back({ CssType::Punct, start, 1 });
				++i;
			}
		} else if (ch == QLatin1Char('@')) {
			const auto e = scanName(i + 1);
			if (e > i + 1) {
				out.push_back({ CssType::At, start, e - start });
				i = e;
			} else {
				out.push_back({ CssType::Punct, start, 1 });
				++i;
			}
		} else if (CssNameStart(ch)
			|| (ch == QLatin1Char('\\'))
			|| ((ch == QLatin1Char('-'))
				&& (i + 1 < n)
				&& (CssNameStart(text.at(i + 1))
					|| (text.at(i + 1) == QLatin1Char('-'))
					|| (text.at(i + 1) == QLatin1Char('\\'))))) {
			auto e = scanName(i);
			if (e == i) {
				out.push_back({ CssType::Punct, start, 1 });
				++i;
			} else {
				if ((e < n) && (text.at(e) == QLatin1Char('('))) {
					++e;
					out.push_back({ CssType::Function, start, e - start });
				} else {
					out.push_back({ CssType::Ident, start, e - start });
				}
				i = e;
			}
		} else if (u >= '0' && u <= '9') {
			while (i < n) {
				const auto d = text.at(i);
				if (CssNameChar(d)
					|| (d == QLatin1Char('.'))
					|| (d == QLatin1Char('%'))) {
					++i;
				} else {
					break;
				}
			}
			out.push_back({ CssType::Other, start, i - start });
		} else {
			out.push_back({ CssType::Punct, start, 1 });
			++i;
		}
	}
	return out;
}

[[nodiscard]] QString CssReconstruct(
		const QString &text,
		const QList<CssSymbol> &tokens,
		int from,
		int to) {
	auto result = QString();
	for (auto i = from; i < to; ++i) {
		const auto &symbol = tokens.at(i);
		if (symbol.type == CssType::Space) {
			result.append(QLatin1Char(' '));
		} else {
			result.append(CssLexem(text, symbol));
		}
	}
	return result;
}

[[nodiscard]] QString GetAttributeId(QString value) {
	value = value.trimmed();
	if (value.startsWith(QLatin1String("url"))) {
		value = value.mid(3);
	}
	value = value.trimmed();
	if (!value.startsWith(QLatin1Char('('))) {
		return QString();
	}
	value = value.mid(1).trimmed();
	if (!value.startsWith(QLatin1Char('#'))) {
		return QString();
	}
	const auto close = value.indexOf(QLatin1Char(')'));
	if (close < 0) {
		return QString();
	}
	return value.left(close).trimmed().mid(1);
}

[[nodiscard]] QString ReconstructFunctionArgument(
		const QString &text,
		const QList<CssSymbol> &tokens,
		int from,
		int to,
		bool stripQuotes,
		bool &terminated,
		int &next) {
	auto k = from;
	while ((k < to) && (tokens.at(k).type == CssType::Space)) {
		++k;
	}
	auto argument = QString();
	auto depth = 1;
	for (; (k < to) && (depth > 0); ++k) {
		const auto &inner = tokens.at(k);
		if (inner.type == CssType::Function) {
			++depth;
			argument.append(CssLexem(text, inner));
		} else if (inner.type == CssType::Punct) {
			const auto c = text.at(inner.start);
			if ((c == QLatin1Char('(')) || (c == QLatin1Char('['))) {
				++depth;
				argument.append(c);
			} else if ((c == QLatin1Char(')')) || (c == QLatin1Char(']'))) {
				--depth;
				if (depth > 0) {
					argument.append(c);
				}
			} else {
				argument.append(c);
			}
		} else {
			argument.append(CssLexem(text, inner));
		}
	}
	terminated = (depth == 0);
	next = k;
	if (stripQuotes
		&& (argument.startsWith(QLatin1Char('"'))
			|| argument.startsWith(QLatin1Char('\'')))) {
		argument = (argument.size() >= 2)
			? argument.mid(1, argument.size() - 2)
			: QString();
	}
	return argument;
}

[[nodiscard]] QString ReconstructDeclarationValue(
		const QString &text,
		const QList<CssSymbol> &tokens,
		int from,
		int to,
		bool &supported) {
	supported = true;
	auto result = QString();
	auto entries = 0;
	auto pendingParen = 0;
	const auto addEntry = [&](const QString &entry) {
		if (entries++ > 0) {
			result.append(QLatin1Char(' '));
		}
		result.append(entry);
	};
	auto i = from;
	while (i < to) {
		const auto &symbol = tokens.at(i);
		if (symbol.type == CssType::Space) {
			++i;
		} else if (symbol.type == CssType::Function) {
			auto name = CssLexem(text, symbol);
			auto decoded = name;
			decoded.chop(1);
			auto terminated = false;
			auto next = i + 1;
			const auto argument = ReconstructFunctionArgument(
				text,
				tokens,
				i + 1,
				to,
				(decoded == QLatin1String("url")),
				terminated,
				next);
			addEntry(name + argument + QLatin1Char(')'));
			i = next;
		} else if (symbol.type == CssType::String) {
			auto inner = CssLexem(text, symbol);
			inner = (inner.size() >= 2)
				? inner.mid(1, inner.size() - 2)
				: QString();
			pendingParen = std::max(0, pendingParen
				+ int(inner.count(QLatin1Char('(')))
				- int(inner.count(QLatin1Char(')'))));
			addEntry(inner);
			++i;
		} else if (symbol.type == CssType::Ident) {
			if (pendingParen > 0) {
				supported = false;
				return result;
			}
			const auto lexem = CssLexem(text, symbol);
			pendingParen = std::max(0, pendingParen
				+ int(lexem.count(QLatin1Char('(')))
				- int(lexem.count(QLatin1Char(')'))));
			addEntry(lexem);
			++i;
		} else if ((symbol.type == CssType::Hash)
			|| (symbol.type == CssType::Other)) {
			if (pendingParen > 0) {
				supported = false;
				return result;
			}
			addEntry((symbol.type == CssType::Hash)
				? CssLexem(text, symbol)
				: text.mid(symbol.start, symbol.len));
			++i;
		} else if (symbol.type == CssType::Punct) {
			const auto c = text.at(symbol.start);
			if (c == QLatin1Char(',')) {
				addEntry(QString(QLatin1Char(';')));
				++i;
			} else if (c == QLatin1Char('/')) {
				addEntry(QString());
				++i;
			} else if (pendingParen > 0) {
				supported = false;
				return result;
			} else {
				break;
			}
		} else {
			break;
		}
	}
	return result;
}

// WHY: a stylesheet url(#id) resolves to the id QtSvg's value reconstruction
// + idFromUrl produce (first literal ')' wins); value forms we cannot
// reproduce exactly are declined so admission never under-measures.
[[nodiscard]] bool CollectDeclarationTarget(
		const QString &text,
		const QList<CssSymbol> &tokens,
		int from,
		int to,
		QList<QString> &targets) {
	auto i = from;
	while ((i < to) && (tokens.at(i).type == CssType::Space)) {
		++i;
	}
	if ((i >= to) || (tokens.at(i).type != CssType::Ident)) {
		return true;
	}
	++i;
	while ((i < to) && (tokens.at(i).type == CssType::Space)) {
		++i;
	}
	if ((i >= to)
		|| (tokens.at(i).type != CssType::Punct)
		|| (text.at(tokens.at(i).start) != QLatin1Char(':'))) {
		return true;
	}
	++i;
	auto supported = false;
	const auto value = ReconstructDeclarationValue(
		text,
		tokens,
		i,
		to,
		supported);
	if (!supported) {
		return false;
	}
	if (const auto id = GetAttributeId(value); !id.isEmpty()) {
		targets.push_back(id);
	}
	return true;
}

[[nodiscard]] bool CollectCssTargets(
		const QString &text,
		const QList<CssSymbol> &tokens,
		int from,
		int to,
		QList<QString> &targets) {
	auto i = from;
	while (i < to) {
		while ((i < to) && (tokens.at(i).type == CssType::Space)) {
			++i;
		}
		if (i >= to) {
			break;
		}
		const auto declStart = i;
		auto depth = 0;
		auto semi = -1;
		auto j = i;
		for (; j < to; ++j) {
			const auto &symbol = tokens.at(j);
			if (symbol.type == CssType::Function) {
				++depth;
				continue;
			} else if (symbol.type != CssType::Punct) {
				continue;
			}
			const auto c = text.at(symbol.start);
			if ((c == QLatin1Char('(')) || (c == QLatin1Char('['))) {
				++depth;
			} else if ((c == QLatin1Char(')')) || (c == QLatin1Char(']'))) {
				if (depth > 0) {
					--depth;
				}
			} else if ((depth == 0) && (c == QLatin1Char(';'))) {
				semi = j;
				break;
			}
		}
		const auto declEnd = (semi >= 0) ? semi : j;
		if (!CollectDeclarationTarget(
				text,
				tokens,
				declStart,
				declEnd,
				targets)) {
			return false;
		}
		i = (semi >= 0) ? (semi + 1) : to;
	}
	return true;
}

[[nodiscard]] bool ScanInlineStyle(
		const QString &value,
		QList<QString> &targets) {
	auto hasEscapeSequences = false;
	const auto preprocessed = HexPreprocess(value, &hasEscapeSequences);
	const auto tokens = TokenizeCss(preprocessed);
	const auto count = int(tokens.size());
	if (HasDisallowedCss(CssReconstruct(preprocessed, tokens, 0, count))) {
		return false;
	}
	const auto isSemicolon = [&](int index) {
		const auto &symbol = tokens.at(index);
		return (symbol.type == CssType::Punct)
			&& (preprocessed.at(symbol.start) == QLatin1Char(';'));
	};
	auto i = 0;
	while (i < count) {
		while ((i < count) && (tokens.at(i).type == CssType::Space)) {
			++i;
		}
		if (i >= count) {
			break;
		}
		++i;
		while ((i < count) && (tokens.at(i).type == CssType::Space)) {
			++i;
		}
		if ((i >= count)
			|| (tokens.at(i).type != CssType::Punct)
			|| (preprocessed.at(tokens.at(i).start) != QLatin1Char(':'))) {
			break;
		}
		++i;
		while ((i < count) && (tokens.at(i).type == CssType::Space)) {
			++i;
		}
		if (i >= count) {
			break;
		}
		const auto valueStart = i;
		auto valueEnd = i;
		while ((valueEnd < count) && !isSemicolon(valueEnd)) {
			++valueEnd;
		}
		auto reconstructed = QString();
		if (hasEscapeSequences) {
			for (auto k = valueStart; k < valueEnd; ++k) {
				reconstructed.append(CssLexem(preprocessed, tokens.at(k)));
			}
		} else {
			// Qt keeps the raw escaped value unless a non-hex escape sets flag.
			const auto start = tokens.at(valueStart).start;
			const auto &last = tokens.at(valueEnd - 1);
			reconstructed = preprocessed.mid(
				start,
				last.start + last.len - start);
		}
		if (const auto id = GetAttributeId(reconstructed); !id.isEmpty()) {
			targets.push_back(id);
		}
		i = (valueEnd < count) ? (valueEnd + 1) : count;
	}
	return true;
}

[[nodiscard]] bool AttributesAreSafe(
		const QXmlStreamAttributes &attributes) {
	for (const auto &attribute : attributes) {
		const auto name = attribute.name().toString();
		const auto value = attribute.value().toString();
		if (name.startsWith(QLatin1String("on"), Qt::CaseInsensitive)) {
			return false;
		}
		if (IsName(name, "href") && !IsAllowedReference(value)) {
			return false;
		}
		if (IsName(name, "style")) {
			auto ignored = QList<QString>();
			if (!ScanInlineStyle(value, ignored)) {
				return false;
			}
		}
		if (!IsName(name, "href")
			&& !IsName(name, "style")
			&& value.contains(QLatin1String("url("), Qt::CaseInsensitive)) {
			auto ignored = QList<QString>();
			if (!CollectUrlReferences(value, ignored)) {
				return false;
			}
		}
	}
	return true;
}

enum class KeyKind {
	Universal,
	Id,
	Type,
	Class,
};

[[nodiscard]] QString EncodeKey(KeyKind kind, const QString &payload = {}) {
	switch (kind) {
	case KeyKind::Id: return QLatin1Char('#') + payload;
	case KeyKind::Type: return QLatin1Char('%') + payload;
	case KeyKind::Class: return QLatin1Char('.') + payload;
	case KeyKind::Universal: break;
	}
	return QString(QLatin1Char('*'));
}

[[nodiscard]] QString CssSubjectKey(
		const QString &text,
		const QList<CssSymbol> &tokens,
		int from,
		int to,
		bool &decline) {
	decline = false;
	auto end = to;
	while ((end > from) && (tokens.at(end - 1).type == CssType::Space)) {
		--end;
	}
	auto begin = from;
	while ((begin < end) && (tokens.at(begin).type == CssType::Space)) {
		++begin;
	}
	if (begin >= end) {
		return EncodeKey(KeyKind::Universal);
	}
	auto subjectStart = begin;
	auto depth = 0;
	for (auto i = begin; i < end; ++i) {
		const auto &symbol = tokens.at(i);
		if (symbol.type == CssType::Function) {
			++depth;
		} else if (symbol.type == CssType::Space) {
			if (depth == 0) {
				subjectStart = i + 1;
			}
		} else if (symbol.type == CssType::Punct) {
			const auto c = text.at(symbol.start);
			if ((c == QLatin1Char('(')) || (c == QLatin1Char('['))) {
				++depth;
			} else if ((c == QLatin1Char(')')) || (c == QLatin1Char(']'))) {
				if (depth > 0) {
					--depth;
				}
			} else if ((depth == 0)
				&& ((c == QLatin1Char('>'))
					|| (c == QLatin1Char('+'))
					|| (c == QLatin1Char('~')))) {
				subjectStart = i + 1;
			}
		}
	}
	while ((subjectStart < end)
		&& (tokens.at(subjectStart).type == CssType::Space)) {
		++subjectStart;
	}
	auto hashId = QString();
	auto className = QString();
	auto typeName = QString();
	auto haveHash = false;
	auto haveClass = false;
	auto haveType = false;
	auto impossibleClass = false;
	depth = 0;
	for (auto i = subjectStart; i < end; ++i) {
		const auto &symbol = tokens.at(i);
		if (symbol.type == CssType::Function) {
			++depth;
		} else if (symbol.type == CssType::Hash) {
			if ((depth == 0) && !haveHash) {
				hashId = CssLexem(text, symbol).mid(1);
				haveHash = true;
			}
		} else if (symbol.type == CssType::Ident) {
			if ((depth == 0) && (i == subjectStart)) {
				typeName = CssLexem(text, symbol).toLower();
				haveType = true;
			}
		} else if (symbol.type == CssType::Punct) {
			const auto c = text.at(symbol.start);
			if (c == QLatin1Char('[')) {
				if (depth == 0) {
					auto j = i + 1;
					while ((j < end)
						&& (tokens.at(j).type == CssType::Space)) {
						++j;
					}
					if ((j < end)
						&& (tokens.at(j).type == CssType::Ident)
						&& (CssLexem(text, tokens.at(j))
							== QLatin1String("class"))) {
						++j;
						while ((j < end)
							&& (tokens.at(j).type == CssType::Space)) {
							++j;
						}
						if ((j + 1 < end)
							&& (tokens.at(j).type == CssType::Punct)
							&& (text.at(tokens.at(j).start) == QLatin1Char('~'))
							&& (tokens.at(j + 1).type == CssType::Punct)
							&& (text.at(tokens.at(j + 1).start)
								== QLatin1Char('='))) {
							j += 2;
							while ((j < end)
								&& (tokens.at(j).type == CssType::Space)) {
								++j;
							}
							if ((j < end)
								&& ((tokens.at(j).type == CssType::Ident)
									|| (tokens.at(j).type == CssType::String))) {
								auto token = CssLexem(text, tokens.at(j));
								if (tokens.at(j).type == CssType::String) {
									token = (token.size() >= 2)
										? token.mid(1, token.size() - 2)
										: QString();
								}
								if (token.isEmpty()) {
									decline = true;
								} else if (token.contains(QLatin1Char(' '))) {
									impossibleClass = true;
								} else if (!haveClass) {
									className = token;
									haveClass = true;
								}
							}
						}
					}
				}
				++depth;
			} else if (c == QLatin1Char('(')) {
				++depth;
			} else if ((c == QLatin1Char(')')) || (c == QLatin1Char(']'))) {
				if (depth > 0) {
					--depth;
				}
			} else if ((c == QLatin1Char('.')) && (depth == 0)) {
				if ((i + 1 < end)
					&& (tokens.at(i + 1).type == CssType::Ident)
					&& !haveClass) {
					className = CssLexem(text, tokens.at(i + 1));
					haveClass = true;
				}
			}
		}
	}
	if (decline || impossibleClass) {
		return QString();
	} else if (haveHash) {
		return EncodeKey(KeyKind::Id, hashId);
	} else if (haveType) {
		return EncodeKey(KeyKind::Type, typeName);
	} else if (haveClass) {
		return EncodeKey(KeyKind::Class, className);
	}
	return EncodeKey(KeyKind::Universal);
}

// QSvgRenderer traverses the document recursively: it descends the element
// tree and, for every referencing attribute (<use> href, fill/stroke/marker/
// mask/filter url(#id), gradient href), recurses into the referenced subtree.
// Newer Qt caps some of those descents, older (5.15) none, and Qt 6.11 added
// an uncapped recursion of its own in the parse-time cycle detection. So we
// model the worst case here: each id'd element records the relative depth of
// everything inside it, every reference it (transitively, through nested id'd
// children) makes, and every nested id'd element position. Then depth(id) is
// the exact upper bound of frames a render of that subtree can recurse to:
//   depth(id) = max(own subtree height,
//                   refElementDepth + 1 + depth(refTarget),
//                   childElementDepth + depth(childId)),
// and expansion(id) counts draw operations with use-multiplicity. Referenced
// ids are looked up from any entry point, so references buried in unlinked
// subtrees (which Qt still walks at parse time) are charged as well. A file
// within the limits can neither overflow a worker thread stack nor burn
// unbounded CPU, on any Qt version.
struct Reference {
	int target = 0;
	int relDepth = 0;
	qint64 count = 1;
};

struct SubtreeInfo {
	int maxRelDepth = 0;
	qint64 subtreeCount = 0;
	QList<Reference> refs;
	QList<QPair<int, int>> children;
};

struct ParseContext {
	int key = 0;
	int entryDepth = 0;
};

struct SourceAggregate {
	qint64 count = 0;
	int maxRelDepth = 0;
};

struct OpenElement {
	QList<int> keys;
	bool marked = false;
};

class GraphEvaluator {
public:
	explicit GraphEvaluator(const QHash<int, SubtreeInfo> &infos)
	: _infos(infos) {
	}

	[[nodiscard]] qint64 depthOf(int key, int level) {
		if (!_ok) {
			return 0;
		} else if (level > kMaxRenderDepth) {
			_ok = false;
			return 0;
		}
		const auto cached = _depthMemo.constFind(key);
		if (cached != _depthMemo.cend()) {
			return *cached;
		} else if (_onStack.contains(key)) {
			_ok = false;
			return 0;
		}
		_onStack.insert(key);
		auto result = qint64(1);
		const auto i = _infos.constFind(key);
		if (i != _infos.cend()) {
			result = i->maxRelDepth;
			for (const auto &ref : i->refs) {
				result = std::max(
					result,
					ref.relDepth + 1 + depthOf(ref.target, level + 1));
			}
			for (const auto &[child, relDepth] : i->children) {
				result = std::max(
					result,
					relDepth + depthOf(child, level + 1));
			}
		}
		_onStack.remove(key);
		if (_ok) {
			_depthMemo.insert(key, result);
		}
		return result;
	}

	[[nodiscard]] qint64 expansionOf(int key, int level) {
		if (!_ok) {
			return 0;
		} else if (level > kMaxRenderDepth) {
			_ok = false;
			return 0;
		}
		const auto cached = _expansionMemo.constFind(key);
		if (cached != _expansionMemo.cend()) {
			return *cached;
		} else if (_onStack.contains(key)) {
			_ok = false;
			return 0;
		}
		_onStack.insert(key);
		auto result = qint64(1);
		const auto i = _infos.constFind(key);
		if (i != _infos.cend()) {
			result = i->subtreeCount;
			for (const auto &ref : i->refs) {
				const auto sub = expansionOf(ref.target, level + 1);
				if (!_ok) {
					break;
				}
				const auto remaining = kMaxReferenceExpansion - result;
				if ((sub > 0) && (ref.count > remaining / sub)) {
					_ok = false;
					break;
				}
				result += ref.count * sub;
			}
			for (const auto &[child, relDepth] : i->children) {
				if (!_ok) {
					break;
				}
				const auto sub = expansionOf(child, level + 1) - countOf(child);
				if (!_ok) {
					break;
				}
				if (sub > kMaxReferenceExpansion - result) {
					_ok = false;
					break;
				}
				result += sub;
			}
		}
		_onStack.remove(key);
		if (_ok) {
			_expansionMemo.insert(key, result);
		}
		return result;
	}

	[[nodiscard]] qint64 countOf(int key) const {
		const auto i = _infos.constFind(key);
		return (i != _infos.cend()) ? i->subtreeCount : 1;
	}

	[[nodiscard]] bool ok() const {
		return _ok;
	}

private:
	const QHash<int, SubtreeInfo> &_infos;
	QHash<int, qint64> _depthMemo;
	QHash<int, qint64> _expansionMemo;
	QSet<int> _onStack;
	bool _ok = true;

};

struct InternKey {
	QString value;
	size_t hash = 0;
};

[[nodiscard]] bool operator==(const InternKey &a, const InternKey &b) {
	return (a.value == b.value);
}

[[nodiscard]] size_t qHash(const InternKey &key, size_t seed = 0) {
	return key.hash;
}

} // namespace

QByteArray SanitizeSvg(const QByteArray &bytes) {
	auto reader = QXmlStreamReader(bytes);
	auto hasRoot = false;
	auto inStyle = 0;
	auto elements = 0;
	auto elementIds = QStringList();
	auto contexts = QList<ParseContext>();
	auto infos = QHash<int, SubtreeInfo>();
	auto sources = QHash<int, QHash<int, SourceAggregate>>();
	auto ruleBlocks = QList<QList<int>>();
	auto keyToBlocks = QHash<int, QList<int>>();
	auto elementTypeById = QHash<int, QString>();
	auto exposedAncestorKeys = QSet<int>();
	auto openElements = QList<OpenElement>();
	auto idMap = QHash<InternKey, int>();
	const auto seed = QHashSeed::globalSeed();
	const auto intern = [&](const QString &value) {
		const auto key = InternKey{ value, qHash(value, seed) };
		const auto i = idMap.constFind(key);
		if (i != idMap.cend()) {
			return *i;
		}
		const auto handle = int(idMap.size());
		idMap.insert(key, handle);
		return handle;
	};
	const auto rootKey = intern(QString());
	const auto universalKey = intern(EncodeKey(KeyKind::Universal));
	const auto addReference = [&](const QString &target, int depth) {
		if (target.isEmpty() || contexts.isEmpty()) {
			return;
		}
		const auto &current = contexts.back();
		infos[current.key].refs.push_back(
			{ intern(target), depth - current.entryDepth + 1, qint64(1) });
	};
	while (!reader.atEnd()) {
		switch (reader.readNext()) {
		case QXmlStreamReader::StartElement: {
			const auto name = reader.name().toString();
			const auto attributes = reader.attributes();
			if (!hasRoot) {
				hasRoot = true;
				if (!IsName(name, "svg")) {
					LOG(("Svg Sanitize: Invalid root element."));
					return {};
				}
				contexts.push_back({ rootKey, 1 });
			}
			if (IsDisallowedElement(name)
				|| !AttributesAreSafe(attributes)) {
				LOG(("Svg Sanitize: Disallowed element or attribute."));
				return {};
			}
			if ((++elements > kMaxSvgElements)
				|| (elementIds.size() >= kMaxSvgNesting)) {
				LOG(("Svg Sanitize: Too many or too deeply nested elements."));
				return {};
			}
			const auto primaryId = attributes.value(QLatin1String("id"));
			const auto id = (primaryId.isEmpty()
				? attributes.value(QLatin1String("xml:id"))
				: primaryId).toString();
			const auto depth = int(elementIds.size() + 1);
			elementIds.push_back(id);
			auto idHandle = -1;
			if (!id.isEmpty()) {
				idHandle = intern(id);
				const auto parentEntry = contexts.back().entryDepth;
				infos[contexts.back().key].children.push_back(
					{ idHandle, int(depth - parentEntry + 1) });
				contexts.push_back({ idHandle, depth });
			}
			for (auto &context : contexts) {
				auto &info = infos[context.key];
				info.subtreeCount++;
				info.maxRelDepth = std::max(
					info.maxRelDepth,
					int(depth - context.entryDepth + 1));
			}
			auto keys = QList<int>();
			keys.push_back(universalKey);
			keys.push_back(intern(EncodeKey(KeyKind::Type, name.toLower())));
			if (idHandle >= 0) {
				keys.push_back(intern(EncodeKey(KeyKind::Id, id)));
			}
			const auto classValue =
				attributes.value(QLatin1String("class")).toString();
			if (!classValue.isEmpty()) {
				const auto classTokens = classValue.split(
					QLatin1Char(' '),
					Qt::SkipEmptyParts);
				for (const auto &token : classTokens) {
					keys.push_back(intern(EncodeKey(KeyKind::Class, token)));
				}
			}
			const auto ctxKey = contexts.back().key;
			const auto relDepth = int(depth - contexts.back().entryDepth + 1);
			if (!IsNonPaintSourceType(name)) {
				for (const auto key : keys) {
					auto &aggregate = sources[key][ctxKey];
					++aggregate.count;
					aggregate.maxRelDepth = std::max(
						aggregate.maxRelDepth,
						relDepth);
				}
			}
			if (idHandle >= 0) {
				auto &stored = elementTypeById[idHandle];
				if (stored.isEmpty() || IsPatternOrMask(name)) {
					stored = name;
				}
			}
			if (IsPatternOrMask(name)) {
				for (auto a = int(openElements.size()) - 1; a >= 0; --a) {
					if (openElements[a].marked) {
						break;
					}
					openElements[a].marked = true;
					for (const auto key : openElements[a].keys) {
						exposedAncestorKeys.insert(key);
					}
				}
			}
			openElements.push_back({ keys, false });
			for (const auto &attribute : attributes) {
				const auto attributeName = attribute.name().toString();
				const auto value = attribute.value().toString();
				if (IsName(attributeName, "href")) {
					addReference(ReferencedId(value), depth);
				} else if (IsName(attributeName, "style")) {
					auto targets = QList<QString>();
					if (ScanInlineStyle(value, targets)) {
						for (const auto &target : targets) {
							addReference(target, depth);
						}
					}
				} else if (value.contains(
						QLatin1String("url("),
						Qt::CaseInsensitive)) {
					auto targets = QList<QString>();
					if (CollectUrlReferences(value, targets)) {
						for (const auto &target : targets) {
							addReference(target, depth);
						}
					}
				}
			}
			if (IsName(name, "style")) {
				++inStyle;
			}
		} break;
		case QXmlStreamReader::EndElement: {
			if (!elementIds.isEmpty()) {
				if (!elementIds.back().isEmpty()) {
					contexts.pop_back();
				}
				elementIds.pop_back();
				if (!openElements.isEmpty()) {
					openElements.pop_back();
				}
			}
			if ((inStyle > 0) && IsName(reader.name().toString(), "style")) {
				--inStyle;
			}
		} break;
		case QXmlStreamReader::Characters: {
			if (inStyle > 0) {
				const auto text = reader.text().toString();
				const auto preprocessed = HexPreprocess(text);
				const auto tokens = TokenizeCss(preprocessed);
				const auto count = int(tokens.size());
				if (HasDisallowedCss(
						CssReconstruct(preprocessed, tokens, 0, count))) {
					LOG(("Svg Sanitize: Disallowed CSS."));
					return {};
				}
				auto i = 0;
				while (i < count) {
					if (tokens.at(i).type == CssType::Space) {
						++i;
						continue;
					}
					const auto preludeStart = i;
					auto depth = 0;
					auto brace = -1;
					auto semi = -1;
					auto j = i;
					for (; j < count; ++j) {
						const auto &symbol = tokens.at(j);
						if (symbol.type == CssType::Function) {
							++depth;
							continue;
						} else if (symbol.type != CssType::Punct) {
							continue;
						}
						const auto c = preprocessed.at(symbol.start);
						if ((c == QLatin1Char('('))
							|| (c == QLatin1Char('['))) {
							++depth;
						} else if ((c == QLatin1Char(')'))
							|| (c == QLatin1Char(']'))) {
							if (depth > 0) {
								--depth;
							}
						} else if (depth == 0) {
							if (c == QLatin1Char('{')) {
								brace = j;
								break;
							} else if (c == QLatin1Char('}')) {
								break;
							} else if (c == QLatin1Char(';')) {
								semi = j;
								break;
							}
						}
					}
					if (brace < 0) {
						i = (semi >= 0)
							? (semi + 1)
							: ((j < count) ? (j + 1) : count);
						continue;
					}
					auto d = 1;
					auto k = brace + 1;
					for (; (k < count) && (d > 0); ++k) {
						const auto &symbol = tokens.at(k);
						if (symbol.type != CssType::Punct) {
							continue;
						}
						const auto c = preprocessed.at(symbol.start);
						if (c == QLatin1Char('{')) {
							++d;
						} else if (c == QLatin1Char('}')) {
							--d;
						}
					}
					const auto blockFrom = brace + 1;
					const auto blockTo = (d == 0) ? (k - 1) : k;
					const auto afterBlock = (d == 0) ? k : count;
					auto firstTok = preludeStart;
					while ((firstTok < brace)
						&& (tokens.at(firstTok).type == CssType::Space)) {
						++firstTok;
					}
					const auto isAtRule = (firstTok < brace)
						&& (tokens.at(firstTok).type == CssType::At);
					if (!isAtRule) {
						auto targets = QList<QString>();
						if (!CollectCssTargets(
								preprocessed,
								tokens,
								blockFrom,
								blockTo,
								targets)) {
							LOG(("Svg Sanitize: Unsupported CSS reference."));
							return {};
						}
						if (!targets.isEmpty()) {
							const auto blockIndex = int(ruleBlocks.size());
							auto handles = QList<int>();
							handles.reserve(targets.size());
							for (const auto &target : targets) {
								handles.push_back(intern(target));
							}
							ruleBlocks.push_back(handles);
							auto selFrom = preludeStart;
							auto selDepth = 0;
							for (auto p = preludeStart; p < brace; ++p) {
								const auto &symbol = tokens.at(p);
								if (symbol.type == CssType::Function) {
									++selDepth;
									continue;
								} else if (symbol.type != CssType::Punct) {
									continue;
								}
								const auto c = preprocessed.at(symbol.start);
								if ((c == QLatin1Char('('))
									|| (c == QLatin1Char('['))) {
									++selDepth;
								} else if ((c == QLatin1Char(')'))
									|| (c == QLatin1Char(']'))) {
									if (selDepth > 0) {
										--selDepth;
									}
								} else if ((c == QLatin1Char(','))
									&& (selDepth == 0)) {
									auto decline = false;
									const auto key = CssSubjectKey(
										preprocessed,
										tokens,
										selFrom,
										p,
										decline);
									if (decline) {
										LOG(("Svg Sanitize: Unsupported CSS selector."));
										return {};
									} else if (!key.isEmpty()) {
										keyToBlocks[intern(key)].push_back(
											blockIndex);
									}
									selFrom = p + 1;
								}
							}
							auto decline = false;
							const auto key = CssSubjectKey(
								preprocessed,
								tokens,
								selFrom,
								brace,
								decline);
							if (decline) {
								LOG(("Svg Sanitize: Unsupported CSS selector."));
								return {};
							} else if (!key.isEmpty()) {
								keyToBlocks[intern(key)].push_back(blockIndex);
							}
						}
					}
					i = afterBlock;
				}
			}
		} break;
		case QXmlStreamReader::DTD: {
			// QXmlStreamReader never fetches the external subset, but it
			// expands entities declared in the internal subset inline, which
			// is an expansion-bomb vector we can't bound, so those we reject.
			if (reader.text().toString().contains(
					QLatin1String("<!ENTITY"),
					Qt::CaseInsensitive)) {
				LOG(("Svg Sanitize: Disallowed DTD entity declaration."));
				return {};
			}
		} break;
		case QXmlStreamReader::ProcessingInstruction: {
			// QSvgHandler includes a local CSS file for xml-stylesheet.
			const auto target = reader.processingInstructionTarget();
			if (target.compare(
					QLatin1String("xml-stylesheet"),
					Qt::CaseInsensitive) == 0) {
				LOG(("Svg Sanitize: Disallowed processing instruction."));
				return {};
			}
		} break;
		case QXmlStreamReader::EntityReference:
		case QXmlStreamReader::Invalid:
			LOG(("Svg Sanitize: Disallowed XML token."));
			return {};
		default: break;
		}
	}
	if (reader.hasError() || !hasRoot) {
		LOG(("Svg Sanitize: Parse error."));
		return {};
	}
	auto blockPatternOrMask = QList<bool>();
	blockPatternOrMask.reserve(ruleBlocks.size());
	for (const auto &block : ruleBlocks) {
		auto flag = false;
		for (const auto target : block) {
			const auto type = elementTypeById.constFind(target);
			if ((type != elementTypeById.cend()) && IsPatternOrMask(*type)) {
				flag = true;
				break;
			}
		}
		blockPatternOrMask.push_back(flag);
	}
	for (auto it = keyToBlocks.constBegin(); it != keyToBlocks.constEnd();
			++it) {
		if (!exposedAncestorKeys.contains(it.key())) {
			continue;
		}
		for (const auto blockIndex : it.value()) {
			if (blockPatternOrMask.at(blockIndex)) {
				// Reapplied enclosing-group styles escape the subtree bound.
				LOG(("Svg Sanitize: Reapplied stylesheet pattern reference."));
				return {};
			}
		}
	}
	auto styleLowerBound = qint64(0);
	for (auto it = keyToBlocks.constBegin(); it != keyToBlocks.constEnd();
			++it) {
		const auto source = sources.constFind(it.key());
		if (source == sources.cend()) {
			continue;
		}
		for (const auto blockIndex : it.value()) {
			const auto &targets = ruleBlocks.at(blockIndex);
			for (auto s = source->constBegin(); s != source->constEnd(); ++s) {
				const auto &aggregate = s.value();
				auto &info = infos[s.key()];
				for (const auto target : targets) {
					styleLowerBound += aggregate.count;
					if (styleLowerBound > kMaxReferenceExpansion) {
						LOG(("Svg Sanitize: Reference graph too deep or too large."));
						return {};
					}
					info.refs.push_back(
						{ target, aggregate.maxRelDepth, aggregate.count });
				}
			}
		}
	}
	auto evaluator = GraphEvaluator(infos);
	if ((evaluator.depthOf(rootKey, 1) > kMaxRenderDepth)
		|| !evaluator.ok()
		|| (evaluator.expansionOf(rootKey, 1) > kMaxReferenceExpansion)
		|| !evaluator.ok()) {
		LOG(("Svg Sanitize: Reference graph too deep or too large."));
		return {};
	}
	return bytes;
}

} // namespace Images
