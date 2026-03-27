// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "base/object_ptr.h"
#include "base/weak_ptr.h"
#include "ui/click_handler.h"
#include "ui/effects/animations.h"
#include "ui/rect_part.h"
#include "ui/rp_widget.h"
#include "ui/style/style_core_types.h"
#include "ui/text/text.h"
#include "ui/text/text_entity.h"

#include <optional>

namespace style {
struct Toast;
} // namespace style

namespace st {
extern const style::Toast &defaultMultilineToast;
} // namespace st

namespace Ui::Toast {

struct Config;

using ClickHandlerFilter = Fn<bool(const ClickHandlerPtr&, Qt::MouseButton)>;
using ToastIconFactory = Fn<object_ptr<RpWidget>(
	not_null<RpWidget*> parent,
	const Config &config)>;

namespace internal {
class Manager;
class Widget;
[[nodiscard]] object_ptr<RpWidget> MakeIconByFactory(
	not_null<RpWidget*> parent,
	const Config &config);
} // namespace internal

inline constexpr auto kDefaultDuration = crl::time(1500);
struct Config {
	// Default way of composing the content, a FlatLabel.
	QString title;
	TextWithEntities text;
	Text::MarkedContext textContext;
	ClickHandlerFilter filter;
	int maxlines = 16;
	bool singleline = false;

	// Custom way of composing any content.
	object_ptr<RpWidget> content = { nullptr };

	// Simple icon fields.
	const style::icon *icon = nullptr;

	// Lottie fields.
	QString iconLottie;
	std::optional<style::size> iconLottieSize;
	anim::repeat iconLottieRepeat = anim::repeat::once;

	// Generic icon-content fields.
	object_ptr<RpWidget> iconContent = { nullptr };

	// Common fields.
	style::align iconAlign = style::al_left;
	std::optional<style::margins> iconPadding;

	rpl::producer<QMargins> padding;

	not_null<const style::Toast*> st = &st::defaultMultilineToast;
	RectPart attach = RectPart::None;
	rpl::producer<int> addToAttachSide;

	bool dark = false;
	bool adaptive = false;
	bool acceptinput = false;

	crl::time duration = kDefaultDuration;
	bool infinite = false; // Just ignore the duration.
};

void SetDefaultParent(not_null<QWidget*> parent);
void AddIconFactory(ToastIconFactory factory);

class Instance final : public base::has_weak_ptr {
	struct Private {
	};

public:
	Instance(
		not_null<QWidget*> widgetParent,
		Config &&config,
		const Private &);
	Instance(const Instance &other) = delete;
	Instance &operator=(const Instance &other) = delete;

	void hideAnimated();
	void hide();

	[[nodiscard]] not_null<RpWidget*> widget() const;

private:
	void shownAnimationCallback();

	const not_null<const style::Toast*> _st;
	const crl::time _hideAt = 0;

	Ui::Animations::Simple _shownAnimation;
	bool _hiding = false;
	bool _sliding = false;

	// ToastManager should reset _widget pointer if _widget is destroyed.
	friend class internal::Manager;
	friend base::weak_ptr<Instance> Show(
		not_null<QWidget*> parent,
		Config &&config);
	std::unique_ptr<internal::Widget> _widget;

};

base::weak_ptr<Instance> Show(
	not_null<QWidget*> parent,
	Config &&config);
base::weak_ptr<Instance> Show(Config &&config);
base::weak_ptr<Instance> Show(
	not_null<QWidget*> parent,
	const QString &text);
base::weak_ptr<Instance> Show(const QString &text);

} // namespace Ui::Toast
