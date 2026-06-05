// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text.h"
#include "ui/text/text_stack_engine.h"

#include <private/qtextengine_p.h>

#include <memory>
#include <vector>

namespace Ui::Text {

class DrawCache final {
public:
	struct Shape final {
		QString text;
		std::vector<QScriptAnalysis> analysis;
		std::unique_ptr<StackEngine> engine;
		int localFrom = 0;
		int lineStart = 0;
		int lineLength = 0;
		int firstItem = 0;
		int nItems = 0;
		std::vector<int> visualOrder;
		std::vector<int> blockIndices;
	};

	struct Line final {
		int lineStart = 0;
		int lineEnd = 0;
		int lineStartBlock = 0;
		int blocksEnd = 0;
		int y = 0;
		int height = 0;
		int ascent = 0;
		int startLineWidth = 0;
		QFixed lineWidth = 0;
		QFixed widthLeft = 0;
		bool elided = false;
		bool metadataReady = false;
		std::unique_ptr<Shape> shape;
	};

	void clear();
	[[nodiscard]] bool empty() const;
	Line &line(int index);
	[[nodiscard]] const Line *line(int index) const;

private:
	std::vector<Line> _lines;

};

} // namespace Ui::Text
