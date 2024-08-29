// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/text/text_stack_engine.h"

#include "ui/text/text_block.h"
#include "styles/style_basic.h"

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <private/qharfbuzz_p.h>
#endif // Qt < 6.0.0

namespace Ui::Text {
namespace {

constexpr auto kMaxItemLength = 4096;

} // namespace

StackEngine::StackEngine(
	not_null<const String*> t,
	gsl::span<QScriptAnalysis> analysis,
	int from,
	int till,
	int blockIndexHint)
: StackEngine(
	t,
	from,
	((from > 0 || (till >= 0 && till < t->_text.size()))
		? QString::fromRawData(
			t->_text.constData() + from,
			((till < 0) ? int(t->_text.size()) : till) - from)
		: t->_text),
	analysis,
	blockIndexHint) {
}

StackEngine::StackEngine(
	not_null<const String*> t,
	int offset,
	const QString &text,
	gsl::span<QScriptAnalysis> analysis,
	int blockIndexHint,
	int blockIndexLimit)
: _t(t)
, _text(text)
, _analysis(analysis.data())
, _offset(offset)
, _positionEnd(_offset + _text.size())
, _font(_t->_st->font)
, _engine(_text, _font->f)
, _tBlocks(_t->_blocks)
, _bStart(begin(_tBlocks) + blockIndexHint)
, _bEnd((blockIndexLimit >= 0)
	? (begin(_tBlocks) + blockIndexLimit)
	: end(_tBlocks))
, _bCached(_bStart) {
	Expects(analysis.size() >= _text.size());

	_engine.validate();
	itemize();
}

std::vector<Block>::const_iterator StackEngine::adjustBlock(
		int offset) const {
	Expects(offset < _positionEnd);

	if (blockPosition(_bCached) > offset) {
		_bCached = begin(_tBlocks);
	}
	Assert(_bCached != end(_tBlocks));
	for (auto i = _bCached + 1; blockPosition(i) <= offset; ++i) {
		_bCached = i;
	}
	return _bCached;
}

int StackEngine::blockPosition(std::vector<Block>::const_iterator i) const {
	return (i == _bEnd) ? _positionEnd : (*i)->position();
}

int StackEngine::blockEnd(std::vector<Block>::const_iterator i) const {
	return (i == _bEnd) ? _positionEnd : blockPosition(i + 1);
}

void StackEngine::itemize() {
	const auto layoutData = _engine.layoutData;
	if (layoutData->items.size()) {
		return;
	}

	const auto length = layoutData->string.length();
	if (!length) {
		return;
	}

	_bStart = adjustBlock(_offset);
	const auto chars = _engine.layoutData->string.constData();

	{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
		QUnicodeTools::ScriptItemArray scriptItems;
		QUnicodeTools::initScripts(_engine.layoutData->string, &scriptItems);
		for (int i = 0; i < scriptItems.length(); ++i) {
			const auto &item = scriptItems.at(i);
			int end = i < scriptItems.length() - 1 ? scriptItems.at(i + 1).position : length;
			for (int j = item.position; j < end; ++j)
				_analysis[j].script = item.script;
		}
#else // Qt >= 6.0.0
		QVarLengthArray<uchar> scripts(length);
		QUnicodeTools::initScripts(reinterpret_cast<const ushort*>(chars), length, scripts.data());
		for (int i = 0; i < length; ++i)
			_analysis[i].script = scripts.at(i);
#endif // Qt < 6.0.0
	}

	// Override script and flags for emoji and custom emoji blocks.
	const auto end = _offset + length;
	for (auto block = _bStart; blockPosition(block) < end; ++block) {
		const auto type = (*block)->type();
		const auto from = std::max(_offset, int(blockPosition(block)));
		const auto till = std::min(int(end), int(blockEnd(block)));
		if (till > from) {
			if (type == TextBlockType::Emoji
				|| type == TextBlockType::CustomEmoji
				|| type == TextBlockType::Skip) {
				for (auto i = from - _offset, count = till - _offset; i != count; ++i) {
					_analysis[i].script = QChar::Script_Common;
					_analysis[i].flags = (chars[i] == QChar::Space)
						? QScriptAnalysis::None
						: QScriptAnalysis::Object;
				}
			} else {
				for (auto i = from - _offset, count = till - _offset; i != count; ++i) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
					_analysis[i].script = hbscript_to_script(script_to_hbscript(_analysis[i].script)); // retain the old behavior
#endif // Qt < 6.0.0
					if (chars[i] == QChar::LineFeed) {
						_analysis[i].flags = QScriptAnalysis::LineOrParagraphSeparator;
					} else {
						_analysis[i].flags = QScriptAnalysis::None;
					}
				}
			}
		}
	}

	{
		auto &m_string = _engine.layoutData->string;
		auto m_analysis = _analysis;
		auto &m_items = _engine.layoutData->items;

		auto start = 0;
		auto startBlock = _bStart;
		auto currentBlock = startBlock;
		auto nextBlock = currentBlock + 1;
		for (int i = 1; i != length; ++i) {
			while (blockPosition(nextBlock) <= _offset + i) {
				currentBlock = nextBlock++;
			}
			// According to the unicode spec we should be treating characters in the Common script
			// (punctuation, spaces, etc) as being the same script as the surrounding text for the
			// purpose of splitting up text. This is important because, for example, a fullstop
			// (0x2E) can be used to indicate an abbreviation and so must be treated as part of a
			// word.  Thus it must be passed along with the word in languages that have to calculate
			// word breaks. For example the thai word "[lookup-in-git]." has no word breaks
			// but the word "[lookup-too]" does.
			// Unfortuntely because we split up the strings for both wordwrapping and for setting
			// the font and because Japanese and Chinese are also aliases of the script "Common",
			// doing this would break too many things.  So instead we only pass the full stop
			// along, and nothing else.
			if (currentBlock != startBlock
				|| m_analysis[i].flags != m_analysis[start].flags) {
				// In emoji blocks we can have one item or two items.
				// First item is the emoji itself,
				// while the second item are the spaces after the emoji,
				// which fall in the same block, but have different flags.
			} else if ((*startBlock)->type() != TextBlockType::Text
				&& m_analysis[i].flags == m_analysis[start].flags) {
				// Otherwise, only text blocks may have arbitrary items.
				Assert(i - start < kMaxItemLength);
				continue;
			} else if (m_analysis[i].bidiLevel == m_analysis[start].bidiLevel
				&& m_analysis[i].flags == m_analysis[start].flags
				&& (m_analysis[i].script == m_analysis[start].script || m_string[i] == u'.')
				//&& m_analysis[i].flags < QScriptAnalysis::SpaceTabOrObject // only emojis are objects here, no tabs
				&& i - start < kMaxItemLength) {
				continue;
			}
			m_items.append(QScriptItem(start, m_analysis[start]));
			start = i;
			startBlock = currentBlock;
		}
		m_items.append(QScriptItem(start, m_analysis[start]));
	}
}

void StackEngine::updateFont(not_null<const AbstractBlock*> block) {
	const auto flags = block->flags();
	const auto newFont = WithFlags(_t->_st->font, flags);
	if (_font != newFont) {
		_font = (newFont->family() == _t->_st->font->family())
			? WithFlags(_t->_st->font, flags, newFont->flags())
			: newFont;
		_engine.fnt = _font->f;
		_engine.resetFontEngineCache();
	}
}

std::vector<Block>::const_iterator StackEngine::shapeGetBlock(int item) {
	auto &si = _engine.layoutData->items[item];
	const auto blockIt = adjustBlock(_offset + si.position);
	const auto block = blockIt->get();
	updateFont(block);
	_engine.shape(item);
	if (si.analysis.flags == QScriptAnalysis::Object) {
		si.width = block->objectWidth();
	}
	return blockIt;
}

int StackEngine::blockIndex(int position) const {
	return int(adjustBlock(_offset + position) - begin(_tBlocks));
}

} // namespace Ui::Text
