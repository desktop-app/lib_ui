// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text.h"

#include <private/qunicodetables_p.h>
#include <private/qtextengine_p.h>

#define BIDI_DEBUG if (1) ; else qDebug

namespace Ui::Text {

enum { BidiDebugEnabled = false };

class BidiAlgorithm {
public:
	template<typename T> using Vector = QVarLengthArray<T, 64>;

	BidiAlgorithm(const QChar *text, QScriptAnalysis *analysis, int length, bool baseDirectionIsRtl,
		Blocks::const_iterator startInBlocks,
		Blocks::const_iterator endInBlocks,
		int offsetInBlocks)
		: text(text),
		  analysis(analysis),
		  length(length),
		  baseLevel(baseDirectionIsRtl ? 1 : 0),
		_startInBlocks(startInBlocks),
		_endInBlocks(endInBlocks),
		_currentBlock(_startInBlocks),
		_offsetInBlocks(offsetInBlocks)
	{

	}

	struct IsolatePair {
		int start;
		int end;
	};

	void initScriptAnalysisAndIsolatePairs(Vector<IsolatePair> &isolatePairs)
	{
		int isolateStack[128];
		int isolateLevel = 0;
		// load directions of string, and determine isolate pairs
		for (int i = 0; i < length; ++i) {
			int pos = i;
			const auto info = infoAt(i);
			if (info.surrogate) {
				++i;
				analysis[i].bidiDirection = QChar::DirNSM;
			}
			const auto p = info.properties;
			analysis[pos].bidiDirection = QChar::Direction(p->direction);
			switch (QChar::Direction(p->direction)) {
			case QChar::DirON:
				// all mirrored chars are DirON
				if (p->mirrorDiff)
					analysis[pos].bidiFlags = QScriptAnalysis::BidiMirrored;
				break;
			case QChar::DirLRE:
			case QChar::DirRLE:
			case QChar::DirLRO:
			case QChar::DirRLO:
			case QChar::DirPDF:
			case QChar::DirBN:
				analysis[pos].bidiFlags = QScriptAnalysis::BidiMaybeResetToParagraphLevel|QScriptAnalysis::BidiBN;
				break;
			case QChar::DirLRI:
			case QChar::DirRLI:
			case QChar::DirFSI:
				if (isolateLevel < 128) {
					isolateStack[isolateLevel] = isolatePairs.size();
					isolatePairs.append({ pos, length });
				}
				++isolateLevel;
				analysis[pos].bidiFlags = QScriptAnalysis::BidiMaybeResetToParagraphLevel;
				break;
			case QChar::DirPDI:
				if (isolateLevel > 0) {
					--isolateLevel;
					if (isolateLevel < 128)
						isolatePairs[isolateStack[isolateLevel]].end = pos;
				}
				Q_FALLTHROUGH();
			case QChar::DirWS:
				analysis[pos].bidiFlags = QScriptAnalysis::BidiMaybeResetToParagraphLevel;
				break;
			case QChar::DirS:
			case QChar::DirB:
				analysis[pos].bidiFlags = QScriptAnalysis::BidiResetToParagraphLevel;
				if (text[pos] == QChar::ParagraphSeparator) {
					// close all open isolates as we start a new paragraph
					while (isolateLevel > 0) {
						--isolateLevel;
						if (isolateLevel < 128)
							isolatePairs[isolateStack[isolateLevel]].end = pos;
					}
				}
				break;
			default:
				break;
			}
		}
	}

	struct DirectionalRun {
		int start;
		int end;
		int continuation;
		ushort level;
		bool isContinuation;
		bool hasContent;
	};

	void generateDirectionalRuns(const Vector<IsolatePair> &isolatePairs, Vector<DirectionalRun> &runs)
	{
		struct DirectionalStack {
			enum { MaxDepth = 125 };
			struct Item {
				ushort level;
				bool isOverride;
				bool isIsolate;
				int runBeforeIsolate;
			};
			Item items[128];
			int counter = 0;

			void push(Item i) {
				items[counter] = i;
				++counter;
			}
			void pop() {
				--counter;
			}
			int depth() const {
				return counter;
			}
			const Item &top() const {
				return items[counter - 1];
			}
		} stack;
		int overflowIsolateCount = 0;
		int overflowEmbeddingCount = 0;
		int validIsolateCount = 0;

		ushort level = baseLevel;
		bool override = false;
		stack.push({ level, false, false, -1 });

		BIDI_DEBUG() << "resolving explicit levels";
		int runStart = 0;
		int continuationFrom = -1;
		int lastRunWithContent = -1;
		bool runHasContent = false;

		auto appendRun = [&](int runEnd) {
			if (runEnd < runStart)
				return;
			bool isContinuation = false;
			if (continuationFrom != -1) {
				runs[continuationFrom].continuation = runs.size();
				isContinuation = true;
			} else if (lastRunWithContent != -1 && level == runs.at(lastRunWithContent).level) {
				runs[lastRunWithContent].continuation = runs.size();
				isContinuation = true;
			}
			if (runHasContent)
				lastRunWithContent = runs.size();
			BIDI_DEBUG() << "   appending run start/end" << runStart << runEnd << "level" << level;
			runs.append({ runStart, runEnd, -1, level, isContinuation, runHasContent });
			runHasContent = false;
			runStart = runEnd + 1;
			continuationFrom = -1;
		};

		int isolatePairPosition = 0;

		for (int i = 0; i < length; ++i) {
			QChar::Direction dir = analysis[i].bidiDirection;


			auto doEmbed = [&](bool isRtl, bool isOverride, bool isIsolate) {
				if (isIsolate) {
					if (override)
						analysis[i].bidiDirection = (level & 1) ? QChar::DirR : QChar::DirL;
					runHasContent = true;
					lastRunWithContent = -1;
					++isolatePairPosition;
				}
				int runBeforeIsolate = runs.size();
				ushort newLevel = isRtl ? ((stack.top().level + 1) | 1) : ((stack.top().level + 2) & ~1);
				if (newLevel <= DirectionalStack::MaxDepth && !overflowEmbeddingCount && !overflowIsolateCount) {
					if (isIsolate)
						++validIsolateCount;
					else
						runBeforeIsolate = -1;
					appendRun(isIsolate ? i : i - 1);
					BIDI_DEBUG() << "pushing new item on stack: level" << (int)newLevel << "isOverride" << isOverride << "isIsolate" << isIsolate << runBeforeIsolate;
					stack.push({ newLevel, isOverride, isIsolate, runBeforeIsolate });
					override = isOverride;
					level = newLevel;
				} else {
					if (isIsolate)
						++overflowIsolateCount;
					else if (!overflowIsolateCount)
						++overflowEmbeddingCount;
				}
				if (!isIsolate) {
					if (override)
						analysis[i].bidiDirection = (level & 1) ? QChar::DirR : QChar::DirL;
					else
						analysis[i].bidiDirection = QChar::DirBN;
				}
			};

			switch (dir) {
			case QChar::DirLRE:
				doEmbed(false, false, false);
				break;
			case QChar::DirRLE:
				doEmbed(true, false, false);
				break;
			case QChar::DirLRO:
				doEmbed(false, true, false);
				break;
			case QChar::DirRLO:
				doEmbed(true, true, false);
				break;
			case QChar::DirLRI:
				doEmbed(false, false, true);
				break;
			case QChar::DirRLI:
				doEmbed(true, false, true);
				break;
			case QChar::DirFSI: {
				bool isRtl = false;
				if (isolatePairPosition < isolatePairs.size()) {
					const auto &pair = isolatePairs.at(isolatePairPosition);
					Q_ASSERT(pair.start == i);
					isRtl = QStringView(text + pair.start + 1, pair.end - pair.start - 1).isRightToLeft();
				}
				doEmbed(isRtl, false, true);
				break;
			}

			case QChar::DirPDF:
				if (override)
					analysis[i].bidiDirection = (level & 1) ? QChar::DirR : QChar::DirL;
				else
					analysis[i].bidiDirection = QChar::DirBN;
				if (overflowIsolateCount) {
					; // do nothing
				} else if (overflowEmbeddingCount) {
					--overflowEmbeddingCount;
				} else if (!stack.top().isIsolate && stack.depth() >= 2) {
					appendRun(i);
					stack.pop();
					override = stack.top().isOverride;
					level = stack.top().level;
					BIDI_DEBUG() << "popped PDF from stack, level now" << (int)stack.top().level;
				}
				break;
			case QChar::DirPDI:
				runHasContent = true;
				if (overflowIsolateCount) {
					--overflowIsolateCount;
				} else if (validIsolateCount == 0) {
					; // do nothing
				} else {
					appendRun(i - 1);
					overflowEmbeddingCount = 0;
					while (!stack.top().isIsolate)
						stack.pop();
					continuationFrom = stack.top().runBeforeIsolate;
					BIDI_DEBUG() << "popped PDI from stack, level now" << (int)stack.top().level << "continuation from" << continuationFrom;
					stack.pop();
					override = stack.top().isOverride;
					level = stack.top().level;
					lastRunWithContent = -1;
					--validIsolateCount;
				}
				if (override)
					analysis[i].bidiDirection = (level & 1) ? QChar::DirR : QChar::DirL;
				break;
			case QChar::DirB:
				// paragraph separator, go down to base direction, reset all state
				if (text[i].unicode() == QChar::ParagraphSeparator) {
					appendRun(i - 1);
					while (stack.counter > 1) {
						// there might be remaining isolates on the stack that are missing a PDI. Those need to get
						// a continuation indicating to take the eos from the end of the string (ie. the paragraph level)
						const auto &t = stack.top();
						if (t.isIsolate) {
							runs[t.runBeforeIsolate].continuation = -2;
						}
						--stack.counter;
					}
					continuationFrom = -1;
					lastRunWithContent = -1;
					validIsolateCount = 0;
					overflowIsolateCount = 0;
					overflowEmbeddingCount = 0;
					level = baseLevel;
				}
				break;
			default:
				runHasContent = true;
				Q_FALLTHROUGH();
			case QChar::DirBN:
				if (override)
					analysis[i].bidiDirection = (level & 1) ? QChar::DirR : QChar::DirL;
				break;
			}
		}
		appendRun(length - 1);
		while (stack.counter > 1) {
			// there might be remaining isolates on the stack that are missing a PDI. Those need to get
			// a continuation indicating to take the eos from the end of the string (ie. the paragraph level)
			const auto &t = stack.top();
			if (t.isIsolate) {
				runs[t.runBeforeIsolate].continuation = -2;
			}
			--stack.counter;
		}
	}

	void resolveExplicitLevels(Vector<DirectionalRun> &runs)
	{
		Vector<IsolatePair> isolatePairs;

		initScriptAnalysisAndIsolatePairs(isolatePairs);
		generateDirectionalRuns(isolatePairs, runs);
	}

	struct IsolatedRunSequenceIterator {
		struct Position {
			int current = -1;
			int pos = -1;

			Position() = default;
			Position(int current, int pos) : current(current), pos(pos) {}

			bool isValid() const { return pos != -1; }
			void clear() { pos = -1; }
		};
		IsolatedRunSequenceIterator(const Vector<DirectionalRun> &runs, int i)
			: runs(runs),
			  current(i)
		{
			pos = runs.at(current).start;
		}
		int operator *() const { return pos; }
		bool atEnd() const { return pos < 0; }
		void operator++() {
			++pos;
			if (pos > runs.at(current).end) {
				current = runs.at(current).continuation;
				if (current > -1)
					pos = runs.at(current).start;
				else
					pos = -1;
			}
		}
		void setPosition(Position p) {
			current = p.current;
			pos = p.pos;
		}
		Position position() const {
			return Position(current, pos);
		}
		bool operator !=(int position) const {
			return pos != position;
		}

		const Vector<DirectionalRun> &runs;
		int current;
		int pos;
	};


	void resolveW1W2W3(const Vector<DirectionalRun> &runs, int i, QChar::Direction sos)
	{
		QChar::Direction last = sos;
		QChar::Direction lastStrong = sos;
		IsolatedRunSequenceIterator it(runs, i);
		while (!it.atEnd()) {
			int pos = *it;

			// Rule W1: Resolve NSM
			QChar::Direction current = analysis[pos].bidiDirection;
			if (current == QChar::DirNSM) {
				current = last;
				analysis[pos].bidiDirection = current;
			} else if (current >= QChar::DirLRI) {
				last = QChar::DirON;
			} else if (current == QChar::DirBN) {
				current = last;
			} else {
				// there shouldn't be any explicit embedding marks here
				Q_ASSERT(current != QChar::DirLRE);
				Q_ASSERT(current != QChar::DirRLE);
				Q_ASSERT(current != QChar::DirLRO);
				Q_ASSERT(current != QChar::DirRLO);
				Q_ASSERT(current != QChar::DirPDF);

				last = current;
			}

			// Rule W2
			if (current == QChar::DirEN && lastStrong == QChar::DirAL) {
				current = QChar::DirAN;
				analysis[pos].bidiDirection = current;
			}

			// remember last strong char for rule W2
			if (current == QChar::DirL || current == QChar::DirR) {
				lastStrong = current;
			} else if (current == QChar::DirAL) {
				// Rule W3
				lastStrong = current;
				analysis[pos].bidiDirection = QChar::DirR;
			}
			last = current;
			++it;
		}
	}


	void resolveW4(const Vector<DirectionalRun> &runs, int i, QChar::Direction sos)
	{
		// Rule W4
		QChar::Direction secondLast = sos;

		IsolatedRunSequenceIterator it(runs, i);
		int lastPos = *it;
		QChar::Direction last = analysis[lastPos].bidiDirection;

//            BIDI_DEBUG() << "Applying rule W4/W5";
		++it;
		while (!it.atEnd()) {
			int pos = *it;
			QChar::Direction current = analysis[pos].bidiDirection;
			if (current == QChar::DirBN) {
				++it;
				continue;
			}
//                BIDI_DEBUG() << pos << secondLast << last << current;
			if (last == QChar::DirES && current == QChar::DirEN && secondLast == QChar::DirEN) {
				last = QChar::DirEN;
				analysis[lastPos].bidiDirection = last;
			} else if (last == QChar::DirCS) {
				if (current == QChar::DirEN && secondLast == QChar::DirEN) {
					last = QChar::DirEN;
					analysis[lastPos].bidiDirection = last;
				} else if (current == QChar::DirAN && secondLast == QChar::DirAN) {
					last = QChar::DirAN;
					analysis[lastPos].bidiDirection = last;
				}
			}
			secondLast = last;
			last = current;
			lastPos = pos;
			++it;
		}
	}

	void resolveW5(const Vector<DirectionalRun> &runs, int i)
	{
		// Rule W5
		IsolatedRunSequenceIterator::Position lastETPosition;

		IsolatedRunSequenceIterator it(runs, i);
		int lastPos = *it;
		QChar::Direction last = analysis[lastPos].bidiDirection;
		if (last == QChar::DirET || last == QChar::DirBN)
			lastETPosition = it.position();

		++it;
		while (!it.atEnd()) {
			int pos = *it;
			QChar::Direction current = analysis[pos].bidiDirection;
			if (current == QChar::DirBN) {
				++it;
				continue;
			}
			if (current == QChar::DirET) {
				if (last == QChar::DirEN) {
					current = QChar::DirEN;
					analysis[pos].bidiDirection = current;
				} else if (!lastETPosition.isValid()) {
					lastETPosition = it.position();
				}
			} else if (lastETPosition.isValid()) {
				if (current == QChar::DirEN) {
					it.setPosition(lastETPosition);
					while (it != pos) {
						int pos = *it;
						analysis[pos].bidiDirection = QChar::DirEN;
						++it;
					}
				}
				lastETPosition.clear();
			}
			last = current;
			lastPos = pos;
			++it;
		}
	}

	void resolveW6W7(const Vector<DirectionalRun> &runs, int i, QChar::Direction sos)
	{
		QChar::Direction lastStrong = sos;
		IsolatedRunSequenceIterator it(runs, i);
		while (!it.atEnd()) {
			int pos = *it;

			// Rule W6
			QChar::Direction current = analysis[pos].bidiDirection;
			if (current == QChar::DirBN) {
				++it;
				continue;
			}
			if (current == QChar::DirET || current == QChar::DirES || current == QChar::DirCS) {
				analysis[pos].bidiDirection = QChar::DirON;
			}

			// Rule W7
			else if (current == QChar::DirL || current == QChar::DirR) {
				lastStrong = current;
			} else if (current == QChar::DirEN && lastStrong == QChar::DirL) {
				analysis[pos].bidiDirection = lastStrong;
			}
			++it;
		}
	}

	struct BracketPair {
		int first;
		int second;

		bool isValid() const { return second > 0; }

		QChar::Direction containedDirection(const QScriptAnalysis *analysis, QChar::Direction embeddingDir) const {
			int isolateCounter = 0;
			QChar::Direction containedDir = QChar::DirON;
			for (int i = first + 1; i < second; ++i) {
				QChar::Direction dir = analysis[i].bidiDirection;
				if (isolateCounter) {
					if (dir == QChar::DirPDI)
						--isolateCounter;
					continue;
				}
				if (dir == QChar::DirL) {
					containedDir = dir;
					if (embeddingDir == dir)
						break;
				} else if (dir == QChar::DirR || dir == QChar::DirAN || dir == QChar::DirEN) {
					containedDir = QChar::DirR;
					if (embeddingDir == QChar::DirR)
						break;
				} else if (dir == QChar::DirLRI || dir == QChar::DirRLI || dir == QChar::DirFSI)
					++isolateCounter;
			}
			BIDI_DEBUG() << "    contained dir for backet pair" << first << "/" << second << "is" << containedDir;
			return containedDir;
		}
	};


	struct BracketStack {
		struct Item {
			Item() = default;
			Item(uint pairedBracked, int position) : pairedBracked(pairedBracked), position(position) {}
			uint pairedBracked = 0;
			int position = 0;
		};

		void push(uint closingUnicode, int pos) {
			if (position < MaxDepth)
				stack[position] = Item(closingUnicode, pos);
			++position;
		}
		int match(uint unicode) {
			Q_ASSERT(!overflowed());
			int p = position;
			while (--p >= 0) {
				if (stack[p].pairedBracked == unicode ||
					// U+3009 and U+2329 are canonical equivalents of each other. Fortunately it's the only pair in Unicode 10
					(stack[p].pairedBracked == 0x3009 && unicode == 0x232a) ||
					(stack[p].pairedBracked == 0x232a && unicode == 0x3009)) {
					position = p;
					return stack[p].position;
				}

			}
			return -1;
		}

		enum { MaxDepth = 63 };
		Item stack[MaxDepth];
		int position = 0;

		bool overflowed() const { return position > MaxDepth; }
	};

	void resolveN0(const Vector<DirectionalRun> &runs, int i, QChar::Direction sos)
	{
		ushort level = runs.at(i).level;

		Vector<BracketPair> bracketPairs;
		{
			BracketStack bracketStack;
			IsolatedRunSequenceIterator it(runs, i);
			while (!it.atEnd()) {
				int pos = *it;
				QChar::Direction dir = analysis[pos].bidiDirection;
				if (dir == QChar::DirON) {
					const QUnicodeTables::Properties *p = infoAt(pos).properties;
					if (p->mirrorDiff) {
						// either opening or closing bracket
						if (p->category == QChar::Punctuation_Open) {
							// opening bracked
							uint closingBracked = text[pos].unicode() + p->mirrorDiff;
							bracketStack.push(closingBracked, bracketPairs.size());
							if (bracketStack.overflowed()) {
								bracketPairs.clear();
								break;
							}
							bracketPairs.append({ pos, -1 });
						} else if (p->category == QChar::Punctuation_Close) {
							int pairPos = bracketStack.match(text[pos].unicode());
							if (pairPos != -1)
								bracketPairs[pairPos].second = pos;
						}
					}
				}
				++it;
			}
		}

		if (BidiDebugEnabled && bracketPairs.size()) {
			BIDI_DEBUG() << "matched bracket pairs:";
			for (int i = 0; i < bracketPairs.size(); ++i)
				BIDI_DEBUG() << "   " << bracketPairs.at(i).first << bracketPairs.at(i).second;
		}

		QChar::Direction lastStrong = sos;
		IsolatedRunSequenceIterator it(runs, i);
		QChar::Direction embeddingDir = (level & 1) ? QChar::DirR : QChar::DirL;
		for (int i = 0; i < bracketPairs.size(); ++i) {
			const auto &pair = bracketPairs.at(i);
			if (!pair.isValid())
				continue;
			QChar::Direction containedDir = pair.containedDirection(analysis, embeddingDir);
			if (containedDir == QChar::DirON) {
				BIDI_DEBUG() << "    3: resolve bracket pair" << i << "to DirON";
				continue;
			} else if (containedDir == embeddingDir) {
				analysis[pair.first].bidiDirection = embeddingDir;
				analysis[pair.second].bidiDirection = embeddingDir;
				BIDI_DEBUG() << "    1: resolve bracket pair" << i << "to" << embeddingDir;
			} else {
				// case c.
				while (it.pos < pair.first) {
					int pos = *it;
					switch (analysis[pos].bidiDirection) {
					case QChar::DirR:
					case QChar::DirEN:
					case QChar::DirAN:
						lastStrong = QChar::DirR;
						break;
					case QChar::DirL:
						lastStrong = QChar::DirL;
						break;
					default:
						break;
					}
					++it;
				}
				analysis[pair.first].bidiDirection = lastStrong;
				analysis[pair.second].bidiDirection = lastStrong;
				BIDI_DEBUG() << "    2: resolve bracket pair" << i << "to" << lastStrong;
			}
			for (int i = pair.second + 1; i < length; ++i) {
				if (infoAt(i).properties->direction == QChar::DirNSM)
					analysis[i].bidiDirection = analysis[pair.second].bidiDirection;
				else
					break;
			}
		}
	}

	void resolveN1N2(const Vector<DirectionalRun> &runs, int i, QChar::Direction sos, QChar::Direction eos)
	{
		// Rule N1 & N2
		QChar::Direction lastStrong = sos;
		IsolatedRunSequenceIterator::Position niPos;
		IsolatedRunSequenceIterator it(runs, i);
//            QChar::Direction last = QChar::DirON;
		while (1) {
			int pos = *it;

			QChar::Direction current = pos >= 0 ? analysis[pos].bidiDirection : eos;
			QChar::Direction currentStrong = current;
			switch (current) {
			case QChar::DirEN:
			case QChar::DirAN:
				currentStrong = QChar::DirR;
				Q_FALLTHROUGH();
			case QChar::DirL:
			case QChar::DirR:
				if (niPos.isValid()) {
					QChar::Direction dir = currentStrong;
					if (lastStrong != currentStrong)
						dir = (runs.at(i).level) & 1 ? QChar::DirR : QChar::DirL;
					it.setPosition(niPos);
					while (*it != pos) {
						if (analysis[*it].bidiDirection != QChar::DirBN)
							analysis[*it].bidiDirection = dir;
						++it;
					}
					niPos.clear();
				}
				lastStrong = currentStrong;
				break;

			case QChar::DirBN:
			case QChar::DirS:
			case QChar::DirWS:
			case QChar::DirON:
			case QChar::DirFSI:
			case QChar::DirLRI:
			case QChar::DirRLI:
			case QChar::DirPDI:
			case QChar::DirB:
				if (!niPos.isValid())
					niPos = it.position();
				break;

			default:
				Q_UNREACHABLE();
			}
			if (it.atEnd())
				break;
//                last = current;
			++it;
		}
	}

	void resolveImplicitLevelsForIsolatedRun(const Vector<DirectionalRun> &runs, int i)
	{
		// Rule X10
		int level = runs.at(i).level;
		int before = i - 1;
		while (before >= 0 && !runs.at(before).hasContent)
			--before;
		int level_before = (before >= 0) ? runs.at(before).level : baseLevel;
		int after = i;
		while (runs.at(after).continuation >= 0)
			after = runs.at(after).continuation;
		if (runs.at(after).continuation == -2) {
			after = runs.size();
		} else {
			++after;
			while (after < runs.size() && !runs.at(after).hasContent)
				++after;
		}
		int level_after = (after == runs.size()) ? baseLevel : runs.at(after).level;
		QChar::Direction sos = (qMax(level_before, level) & 1) ? QChar::DirR : QChar::DirL;
		QChar::Direction eos = (qMax(level_after, level) & 1) ? QChar::DirR : QChar::DirL;

		if (BidiDebugEnabled) {
			BIDI_DEBUG() << "Isolated run starting at" << i << "sos/eos" << sos << eos;
			BIDI_DEBUG() << "before implicit level processing:";
			IsolatedRunSequenceIterator it(runs, i);
			while (!it.atEnd()) {
				BIDI_DEBUG() << "    " << *it << Qt::hex << text[*it].unicode() << analysis[*it].bidiDirection;
				++it;
			}
		}

		resolveW1W2W3(runs, i, sos);
		resolveW4(runs, i, sos);
		resolveW5(runs, i);

		if (BidiDebugEnabled) {
			BIDI_DEBUG() << "after W4/W5";
			IsolatedRunSequenceIterator it(runs, i);
			while (!it.atEnd()) {
				BIDI_DEBUG() << "    " << *it << Qt::hex << text[*it].unicode() << analysis[*it].bidiDirection;
				++it;
			}
		}

		resolveW6W7(runs, i, sos);

		// Resolve neutral types

		// Rule N0
		resolveN0(runs, i, sos);
		resolveN1N2(runs, i, sos, eos);

		BIDI_DEBUG() << "setting levels (run at" << level << ")";
		// Rules I1 & I2: set correct levels
		{
			ushort level = runs.at(i).level;
			IsolatedRunSequenceIterator it(runs, i);
			while (!it.atEnd()) {
				int pos = *it;

				QChar::Direction current = analysis[pos].bidiDirection;
				switch (current) {
				case QChar::DirBN:
					break;
				case QChar::DirL:
					analysis[pos].bidiLevel = (level + 1) & ~1;
					break;
				case QChar::DirR:
					analysis[pos].bidiLevel = level | 1;
					break;
				case QChar::DirAN:
				case QChar::DirEN:
					analysis[pos].bidiLevel = (level + 2) & ~1;
					break;
				default:
					Q_UNREACHABLE();
				}
				BIDI_DEBUG() << "    " << pos << current << analysis[pos].bidiLevel;
				++it;
			}
		}
	}

	void resolveImplicitLevels(const Vector<DirectionalRun> &runs)
	{
		for (int i = 0; i < runs.size(); ++i) {
			if (runs.at(i).isContinuation)
				continue;

			resolveImplicitLevelsForIsolatedRun(runs, i);
		}
	}

	bool checkForBidi() const
	{
		if (baseLevel != 0)
			return true;
		for (int i = 0; i < length; ++i) {
			if (text[i].unicode() >= 0x590) {
				switch (infoAt(i).properties->direction) {
				case QChar::DirR: case QChar::DirAN:
				case QChar::DirLRE: case QChar::DirLRO: case QChar::DirAL:
				case QChar::DirRLE: case QChar::DirRLO: case QChar::DirPDF:
				case QChar::DirLRI: case QChar::DirRLI: case QChar::DirFSI: case QChar::DirPDI:
					return true;
				default:
					break;
				}
			}
		}
		return false;
	}

	bool process()
	{
		memset(analysis, 0, length * sizeof(QScriptAnalysis));

		bool hasBidi = checkForBidi();

		if (!hasBidi)
			return false;

		if (BidiDebugEnabled) {
			BIDI_DEBUG() << ">>>> start bidi, text length" << length;
			for (int i = 0; i < length; ++i)
				BIDI_DEBUG() << Qt::hex << "    (" << i << ")" << text[i].unicode() << text[i].direction();
		}

		{
			Vector<DirectionalRun> runs;
			resolveExplicitLevels(runs);

			if (BidiDebugEnabled) {
				BIDI_DEBUG() << "resolved explicit levels, nruns" << runs.size();
				for (int i = 0; i < runs.size(); ++i)
					BIDI_DEBUG() << "    " << i << "start/end" << runs.at(i).start << runs.at(i).end << "level" << (int)runs.at(i).level << "continuation" << runs.at(i).continuation;
			}

			// now we have a list of isolated run sequences inside the vector of runs, that can be fed
			// through the implicit level resolving

			resolveImplicitLevels(runs);
		}

		BIDI_DEBUG() << "Rule L1:";
		// Rule L1:
		bool resetLevel = true;
		for (int i = length - 1; i >= 0; --i) {
			if (analysis[i].bidiFlags & QScriptAnalysis::BidiResetToParagraphLevel) {
				BIDI_DEBUG() << "resetting pos" << i << "to baselevel";
				analysis[i].bidiLevel = baseLevel;
				resetLevel = true;
			} else if (resetLevel && analysis[i].bidiFlags & QScriptAnalysis::BidiMaybeResetToParagraphLevel) {
				BIDI_DEBUG() << "resetting pos" << i << "to baselevel (maybereset flag)";
				analysis[i].bidiLevel = baseLevel;
			} else {
				resetLevel = false;
			}
		}

		// set directions for BN to the minimum of adjacent chars
		// This makes is possible to be conformant with the Bidi algorithm even though we don't
		// remove BN and explicit embedding chars from the stream of characters to reorder
		int lastLevel = baseLevel;
		int lastBNPos = -1;
		for (int i = 0; i < length; ++i) {
			if (analysis[i].bidiFlags & QScriptAnalysis::BidiBN) {
				if (lastBNPos < 0)
					lastBNPos = i;
				analysis[i].bidiLevel = lastLevel;
			} else {
				int l = analysis[i].bidiLevel;
				if (lastBNPos >= 0) {
					if (l < lastLevel) {
						while (lastBNPos < i) {
							analysis[lastBNPos].bidiLevel = l;
							++lastBNPos;
						}
					}
					lastBNPos = -1;
				}
				lastLevel = l;
			}
		}
		if (lastBNPos >= 0 && baseLevel < lastLevel) {
			while (lastBNPos < length) {
				analysis[lastBNPos].bidiLevel = baseLevel;
				++lastBNPos;
			}
		}

		if (BidiDebugEnabled) {
			BIDI_DEBUG() << "final resolved levels:";
			for (int i = 0; i < length; ++i)
				BIDI_DEBUG() << "    " << i << Qt::hex << text[i].unicode() << Qt::dec << (int)analysis[i].bidiLevel;
		}

		return true;
	}


	const QChar *text;
	QScriptAnalysis *analysis;
	int length;
	char baseLevel;

	Blocks::const_iterator _startInBlocks;
	Blocks::const_iterator _endInBlocks;
	mutable Blocks::const_iterator _currentBlock;
	int _offsetInBlocks;

	struct Info {
		const QUnicodeTables::Properties *properties = nullptr;
		bool surrogate = false;
	};
	[[nodiscard]] Info infoAt(int i) const {
		if (_currentBlock != _startInBlocks
			&& (*_currentBlock)->position() > _offsetInBlocks + i) {
			_currentBlock = _startInBlocks;
		}
		auto next = _currentBlock + 1;
		while (next != _endInBlocks
			&& (*next)->position() <= _offsetInBlocks + i) {
			_currentBlock = next;
			++next;
		}
		const auto type = (*_currentBlock)->type();
		const auto object = (type == TextBlockType::Emoji)
			|| (type == TextBlockType::CustomEmoji)
			|| (type == TextBlockType::Skip);

		constexpr auto kQt5 = (QT_VERSION < QT_VERSION_CHECK(6, 0, 0));
		using wide = std::conditional_t<kQt5, uint, char32_t>;
		using narrow = std::conditional_t<kQt5, ushort, char16_t>;

		auto uc = wide(text[i].unicode());
		if (QChar::isHighSurrogate(uc) && i < length - 1 && text[i + 1].isLowSurrogate()) {
			uc = QChar::surrogateToUcs4(ushort(uc), text[i + 1].unicode());

			return {
				.properties = QUnicodeTables::properties(object
					? wide(QChar::ObjectReplacementCharacter)
					: uc),
				.surrogate = true,
			};
		}
		return {
			.properties = QUnicodeTables::properties(object
				? narrow(QChar::ObjectReplacementCharacter)
				: narrow(uc)),
			.surrogate = false,
		};
	}
};

} // namespace Ui::Text

#undef BIDI_DEBUG
