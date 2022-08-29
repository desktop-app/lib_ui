// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QtGui/QColor>
#include <QtCore/QSize>
#include <QtCore/QPoint>

#include <crl/crl_time.h>

class QPainter;

namespace Ui::Text {

[[nodiscard]] int AdjustCustomEmojiSize(int emojiSize);

struct CustomEmojiPaintContext {
	QColor preview;
	QSize size; // Required only when scaled = true, for path scaling.
	crl::time now = 0;
	float64 scale = 0.;
	QPoint position;
	bool firstFrameOnly = false;
	bool paused = false;
	bool scaled = false;
};

class CustomEmoji {
public:
	virtual ~CustomEmoji() = default;

	[[nodiscard]] virtual QString entityData() = 0;

	using Context = CustomEmojiPaintContext;
	virtual void paint(QPainter &p, const Context &context) = 0;
	virtual void unload() = 0;
	[[nodiscard]] virtual bool ready() = 0;
	[[nodiscard]] virtual bool readyInDefaultState() = 0;

};

using CustomEmojiFactory = Fn<std::unique_ptr<CustomEmoji>(
	QStringView,
	Fn<void()>)>;

class ShiftedEmoji final : public CustomEmoji {
public:
	ShiftedEmoji(std::unique_ptr<CustomEmoji> wrapped, QPoint shift);

	QString entityData() override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	const std::unique_ptr<Ui::Text::CustomEmoji> _wrapped;
	const QPoint _shift;

};

class FirstFrameEmoji final : public CustomEmoji {
public:
	explicit FirstFrameEmoji(std::unique_ptr<CustomEmoji> wrapped);

	QString entityData() override;
	void paint(QPainter &p, const Context &context) override;
	void unload() override;
	bool ready() override;
	bool readyInDefaultState() override;

private:
	const std::unique_ptr<Ui::Text::CustomEmoji> _wrapped;

};

} // namespace Ui::Text
