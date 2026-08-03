// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/text/text_entity.h"

#include <optional>
#include <vector>

namespace TextUtilities {

enum class HtmlTableAlignment : uchar {
	Default,
	Left,
	Center,
	Right,
};

struct HtmlTableCell {
	TextWithTags text;
	int colspan = 1;
	int rowspan = 1;
	bool header = false;
	HtmlTableAlignment alignment = HtmlTableAlignment::Default;
};

struct HtmlTableRow {
	std::vector<HtmlTableCell> cells;
};

struct HtmlTableLimits {
	int maxRows = 1024;
	int maxColumns = 64;
	int maxCells = 16384;
	int maxCellLength = 4096;
};

struct HtmlTable {
	std::vector<HtmlTableRow> rows;
	int columns = 0;
	int sourceFrom = 0;
	int sourceTill = 0;
	bool truncated = false;
};

[[nodiscard]] QString EscapeForHtml(QStringView text);
[[nodiscard]] QString TextWithTagsToHtml(const TextWithTags &text);
[[nodiscard]] QString TextForMimeDataToHtml(const TextForMimeData &text);
[[nodiscard]] std::optional<TextWithTags> TextWithTagsFromHtml(
	QStringView html);
[[nodiscard]] TextWithTags TextWithTagsFromHtmlFragment(QStringView html);
[[nodiscard]] bool HtmlContainsTable(QStringView html);
[[nodiscard]] std::optional<HtmlTable> TableFromHtml(
	QStringView html,
	const HtmlTableLimits &limits);

} // namespace TextUtilities
