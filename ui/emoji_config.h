// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/basic_types.h"
#include "base/binary_guard.h"
#include "base/qt/qt_string_view.h"
#include "emoji.h"

#include <QtGui/QPainter>
#include <QtGui/QPixmap>

#include <rpl/producer.h>

namespace Ui {
namespace Emoji {
namespace internal {

[[nodiscard]] QString CacheFileFolder();
[[nodiscard]] QString SetDataPath(int id);

} // namespace internal

void Init();
void Clear();

void ClearIrrelevantCache();

// Thread safe, callback is called on main thread.
void SwitchToSet(int id, Fn<void(bool)> callback);

[[nodiscard]] int CurrentSetId();
[[nodiscard]] int NeedToSwitchBackToId();
void ClearNeedSwitchToId();
[[nodiscard]] bool SetIsReady(int id);
[[nodiscard]] rpl::producer<> Updated();

[[nodiscard]] int GetSizeNormal();
[[nodiscard]] int GetSizeLarge();
#ifdef Q_OS_MAC
[[nodiscard]] int GetSizeTouchbar();
#endif

class One {
	struct CreationTag {
	};

public:
	One(One &&other) = default;
	One(
		const QString &id,
		EmojiPtr original,
		uint32 index,
		bool hasPostfix,
		bool colorizable,
		const CreationTag &);

	[[nodiscard]] QString id() const {
		return _id;
	}
	[[nodiscard]] QString text() const {
		return hasPostfix() ? (_id + QChar(kPostfix)) : _id;
	}

	[[nodiscard]] bool colored() const {
		return (_original != nullptr);
	}
	[[nodiscard]] EmojiPtr original() const {
		return _original ? _original : this;
	}
	[[nodiscard]] QString nonColoredId() const {
		return original()->id();
	}

	[[nodiscard]] bool hasPostfix() const {
		return _hasPostfix;
	}

	[[nodiscard]] bool hasVariants() const {
		return _colorizable || colored();
	}
	[[nodiscard]] int variantsCount() const;
	[[nodiscard]] int variantIndex(EmojiPtr variant) const;
	[[nodiscard]] EmojiPtr variant(int index) const;

	[[nodiscard]] int index() const {
		return _index;
	}
	[[nodiscard]] int sprite() const {
		return int(_index >> 9);
	}
	[[nodiscard]] int row() const {
		return int((_index >> 5) & 0x0FU);
	}
	[[nodiscard]] int column() const {
		return int(_index & 0x1FU);
	}

	[[nodiscard]] QString toUrl() const {
		return "emoji://e." + QString::number(index());
	}

	[[nodiscard]] uint8 surrogatePairs() const {
		return _surrogatePairs;
	}

private:
	const QString _id;
	const EmojiPtr _original = nullptr;
	const uint32 _index = 0;
	const bool _hasPostfix = false;
	const bool _colorizable = false;
	const uint8 _surrogatePairs;

	friend void internal::Init();

};

[[nodiscard]] inline EmojiPtr FromUrl(const QString &url) {
	auto start = qstr("emoji://e.");
	if (url.startsWith(start)) {
		return internal::ByIndex(base::StringViewMid(url, start.size()).toInt()); // skip emoji://e.
	}
	return nullptr;
}

[[nodiscard]] inline EmojiPtr Find(const QChar *start, const QChar *end, int *outLength = nullptr) {
	return internal::Find(start, end, outLength);
}

[[nodiscard]] inline EmojiPtr Find(const QString &text, int *outLength = nullptr) {
	return Find(text.constBegin(), text.constEnd(), outLength);
}

[[nodiscard]] QString IdFromOldKey(uint64 oldKey);

[[nodiscard]] inline EmojiPtr FromOldKey(uint64 oldKey) {
	return Find(IdFromOldKey(oldKey));
}

[[nodiscard]] inline int ColorIndexFromCode(uint32 code) {
	switch (code) {
	case 0xD83CDFFBU: return 1;
	case 0xD83CDFFCU: return 2;
	case 0xD83CDFFDU: return 3;
	case 0xD83CDFFEU: return 4;
	case 0xD83CDFFFU: return 5;
	}
	return 0;
}

[[nodiscard]] inline int ColorIndexFromOldKey(uint64 oldKey) {
	return ColorIndexFromCode(uint32(oldKey & 0xFFFFFFFFLLU));
}

QVector<EmojiPtr> GetDefaultRecent();

const QPixmap &SinglePixmap(EmojiPtr emoji, int fontHeight);
void Draw(QPainter &p, EmojiPtr emoji, int size, int x, int y);

class UniversalImages {
public:
	explicit UniversalImages(int id);

	int id() const;
	bool ensureLoaded();
	void clear();

	void draw(QPainter &p, EmojiPtr emoji, int size, int x, int y) const;

	// This method must be thread safe and so it is called after
	// the _id value is fixed and all _sprites are loaded.
	QImage generate(int size, int index) const;

private:
	const int _id = 0;
	std::vector<QImage> _sprites;

};

[[nodiscard]] const std::shared_ptr<UniversalImages> &SourceImages();
void ClearSourceImages(const std::shared_ptr<UniversalImages> &images);

} // namespace Emoji
} // namespace Ui
