// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/image/svg_safety.h"

#include "base/debug_log.h"

#include <QtCore/QHash>
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

[[nodiscard]] bool HasDisallowedCss(
		const QString &text,
		QList<QString> *targets = nullptr) {
	if (text.contains(QLatin1String("@import"), Qt::CaseInsensitive)) {
		return true;
	}
	if (text.contains(QLatin1String("@keyframes"), Qt::CaseInsensitive)
		|| text.contains(QLatin1String("animation"), Qt::CaseInsensitive)) {
		return true;
	}
	auto ignored = QList<QString>();
	return !CollectUrlReferences(text, targets ? *targets : ignored);
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
		if (IsName(name, "style") && HasDisallowedCss(value)) {
			return false;
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
struct SubtreeInfo {
	int maxRelDepth = 0;
	qint64 subtreeCount = 0;
	QList<QPair<QString, int>> refs;
	QList<QPair<QString, int>> children;
};

struct ParseContext {
	QString key;
	int entryDepth = 0;
};

class GraphEvaluator {
public:
	explicit GraphEvaluator(const QHash<QString, SubtreeInfo> &infos)
	: _infos(infos) {
	}

	[[nodiscard]] qint64 depthOf(const QString &key, int level) {
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
			for (const auto &[target, relDepth] : i->refs) {
				result = std::max(
					result,
					relDepth + 1 + depthOf(target, level + 1));
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

	[[nodiscard]] qint64 expansionOf(const QString &key, int level) {
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
			for (const auto &[target, relDepth] : i->refs) {
				result += expansionOf(target, level + 1);
			}
			for (const auto &[child, relDepth] : i->children) {
				result += expansionOf(child, level + 1) - countOf(child);
			}
			if (result > kMaxReferenceExpansion) {
				_ok = false;
			}
		}
		_onStack.remove(key);
		if (_ok) {
			_expansionMemo.insert(key, result);
		}
		return result;
	}

	[[nodiscard]] qint64 countOf(const QString &key) const {
		const auto i = _infos.constFind(key);
		return (i != _infos.cend()) ? i->subtreeCount : 1;
	}

	[[nodiscard]] bool ok() const {
		return _ok;
	}

private:
	const QHash<QString, SubtreeInfo> &_infos;
	QHash<QString, qint64> _depthMemo;
	QHash<QString, qint64> _expansionMemo;
	QSet<QString> _onStack;
	bool _ok = true;

};

} // namespace

QByteArray SanitizeSvg(const QByteArray &bytes) {
	auto reader = QXmlStreamReader(bytes);
	auto hasRoot = false;
	auto inStyle = 0;
	auto elements = 0;
	auto elementIds = QStringList();
	auto contexts = QList<ParseContext>();
	auto infos = QHash<QString, SubtreeInfo>();
	const auto addReference = [&](const QString &target, int depth) {
		if (target.isEmpty() || contexts.isEmpty()) {
			return;
		}
		const auto &current = contexts.back();
		infos[current.key].refs.push_back(
			{ target, depth - current.entryDepth + 1 });
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
				contexts.push_back({ QString(), 1 });
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
			const auto id = attributes.value(QLatin1String("id")).toString();
			const auto depth = int(elementIds.size() + 1);
			elementIds.push_back(id);
			if (!id.isEmpty()) {
				auto &current = infos[contexts.back().key];
				const auto &parent = contexts.back();
				current.children.push_back(
					{ id, int(depth - parent.entryDepth + 1) });
				contexts.push_back({ id, int(depth) });
			}
			for (auto &context : contexts) {
				auto &info = infos[context.key];
				info.subtreeCount++;
				info.maxRelDepth = std::max(
					info.maxRelDepth,
					int(depth - context.entryDepth + 1));
			}
			for (const auto &attribute : attributes) {
				const auto attributeName = attribute.name().toString();
				const auto value = attribute.value().toString();
				if (IsName(attributeName, "href")) {
					addReference(ReferencedId(value), depth);
				} else {
					auto targets = QList<QString>();
					if ((IsName(attributeName, "style")
							|| value.contains(
								QLatin1String("url("),
								Qt::CaseInsensitive))
						&& CollectUrlReferences(value, targets)) {
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
			}
			if ((inStyle > 0) && IsName(reader.name().toString(), "style")) {
				--inStyle;
			}
		} break;
		case QXmlStreamReader::Characters: {
			if (inStyle > 0) {
				const auto text = reader.text().toString();
				auto targets = QList<QString>();
				if (HasDisallowedCss(text, &targets)) {
					LOG(("Svg Sanitize: Disallowed CSS."));
					return {};
				}
				for (const auto &target : targets) {
					infos[QString()].refs.push_back(
						{ target, kMaxSvgNesting });
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
	auto evaluator = GraphEvaluator(infos);
	if ((evaluator.depthOf(QString(), 1) > kMaxRenderDepth)
		|| !evaluator.ok()
		|| (evaluator.expansionOf(QString(), 1) > kMaxReferenceExpansion)
		|| !evaluator.ok()) {
		LOG(("Svg Sanitize: Reference graph too deep or too large."));
		return {};
	}
	return bytes;
}

} // namespace Images
