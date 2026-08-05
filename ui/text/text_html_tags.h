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
	TextWithTags caption;
	int columns = 0;
	int sourceFrom = 0;
	int sourceTill = 0;
	bool truncated = false;
};

enum class HtmlBlockKind : uchar {
	Paragraph,
	Heading,
	Divider,
	Quote,
	Pullquote,
	Code,
	Footer,
	List,
	Details,
	Table,
	Photo,
	Video,
	Audio,
	Collage,
	Slideshow,
	Map,
	Math,
};

struct HtmlMedia {
	QString source;
	QString identity;
	int width = 0;
	int height = 0;
	bool spoiler = false;
	bool autoplay = false;
	bool loop = false;
};

struct HtmlMapPoint {
	QString latitude;
	QString longitude;
	int zoom = 0;
};

enum class HtmlListKind : uchar {
	Bullet,
	Ordered,
};

enum class HtmlTaskState : uchar {
	None,
	Unchecked,
	Checked,
};

struct HtmlBlock;

struct HtmlListItem {
	TextWithTags text;
	std::vector<HtmlBlock> blocks;
	QString anchorId;
	HtmlTaskState taskState = HtmlTaskState::None;
	std::optional<int> value;
};

struct HtmlBlock {
	HtmlBlockKind kind = HtmlBlockKind::Paragraph;
	TextWithTags text;
	TextWithTags caption;
	QString language;
	QString anchorId;
	int headingLevel = 0;
	HtmlListKind listKind = HtmlListKind::Bullet;
	bool listReversed = false;
	bool detailsOpen = false;
	std::optional<int> listStart;
	QString listType;
	std::optional<HtmlTable> table;
	std::vector<HtmlListItem> items;
	std::vector<HtmlBlock> children;
	HtmlMedia media;
	HtmlMapPoint mapPoint;
	QString formula;
};

struct HtmlBlocksLimits {
	int maxBlocks = 1024;
	int maxDepth = 16;
	int maxBlockLength = 16384;
	int maxTotalLength = 65536;
	HtmlTableLimits table;
};

struct HtmlBlocks {
	std::vector<HtmlBlock> blocks;
	bool truncated = false;
};

[[nodiscard]] QString EscapeForHtml(QStringView text);
[[nodiscard]] QString TextWithTagsToHtml(const TextWithTags &text);
[[nodiscard]] QString TextForMimeDataToHtml(const TextForMimeData &text);
[[nodiscard]] std::optional<TextWithTags> TextWithTagsFromHtml(
	QStringView html,
	bool richFormatting = false);
[[nodiscard]] TextWithTags TextWithTagsFromHtmlFragment(QStringView html);
[[nodiscard]] bool HtmlContainsTable(QStringView html);
[[nodiscard]] std::optional<HtmlTable> TableFromHtml(
	QStringView html,
	const HtmlTableLimits &limits);

[[nodiscard]] std::optional<HtmlBlocks> BlocksFromHtml(
	QStringView html,
	const HtmlBlocksLimits &limits);

} // namespace TextUtilities
