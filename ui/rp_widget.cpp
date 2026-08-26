// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/rp_widget.h"

#include "base/flat_map.h"
#include "base/invoke_queued.h"
#include "base/platform/base_platform_info.h"
#include "base/qt_signal_producer.h"
#include "ui/accessible/ui_accessible_item.h"
#include "ui/accessible/ui_accessible_widget.h"
#include "ui/gl/gl_detection.h"
#include "ui/screen_reader_mode.h"

#include <QtCore/QVariant>
#include <QtGui/QWindow>
#include <QtGui/QtEvents>
#include <QtGui/QColorSpace>
#include <QtGui/QPainter>
#include <QtWidgets/QApplication>

#include <algorithm>

namespace Ui {
namespace {

[[nodiscard]] std::vector<QPointer<QWidget>> GetChildWidgets(
		not_null<QWidget*> widget) {
	const auto &children = widget->children();
	auto result = std::vector<QPointer<QWidget>>();
	result.reserve(children.size());
	for (const auto child : children) {
		if (child && child->isWidgetType()) {
			result.push_back(static_cast<QWidget*>(child));
		}
	}
	return result;
}

} // namespace

void ToggleChildrenVisibility(not_null<QWidget*> widget, bool visible) {
	for (const auto &child : GetChildWidgets(widget)) {
		// Children that are windows themselves, like submenu windows of a
		// popup menu, manage their own visibility, QWidget::hideChildren()
		// skips them as well. Toggling them here would map and unmap their
		// surfaces in the middle of an unrelated animation.
		if (child && !child->isWindow()) {
			child->setVisible(visible);
		}
	}
}

void ResizeFitChild(
		not_null<RpWidget*> parent,
		not_null<RpWidget*> child,
		int heightMin) {
	parent->widthValue(
	) | rpl::on_next([=](int width) {
		child->resizeToWidth(width);
	}, child->lifetime());

	child->heightValue(
	) | rpl::on_next([=](int height) {
		parent->resize(parent->width(), std::max(height, heightMin));
	}, child->lifetime());
}

rpl::producer<not_null<QEvent*>> RpWidgetWrap::events() const {
	auto &stream = eventStreams().events;
	return stream.events();
}

rpl::producer<QRect> RpWidgetWrap::geometryValue() const {
	auto &stream = eventStreams().geometry;
	return stream.events_starting_with_copy(rpWidget()->geometry());
}

rpl::producer<QSize> RpWidgetWrap::sizeValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.size(); })
		| rpl::distinct_until_changed();
}

rpl::producer<int> RpWidgetWrap::heightValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.height(); })
		| rpl::distinct_until_changed();
}

rpl::producer<int> RpWidgetWrap::widthValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.width(); })
		| rpl::distinct_until_changed();
}

rpl::producer<QPoint> RpWidgetWrap::positionValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.topLeft(); })
		| rpl::distinct_until_changed();
}

rpl::producer<int> RpWidgetWrap::leftValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.left(); })
		| rpl::distinct_until_changed();
}

rpl::producer<int> RpWidgetWrap::topValue() const {
	return geometryValue()
		| rpl::map([](QRect &&value) { return value.top(); })
		| rpl::distinct_until_changed();
}

rpl::producer<int> RpWidgetWrap::desiredHeightValue() const {
	return heightValue();
}

rpl::producer<bool> RpWidgetWrap::shownValue() const {
	auto &stream = eventStreams().shown;
	return stream.events_starting_with(!rpWidget()->isHidden())
		| rpl::distinct_until_changed();
}

rpl::producer<not_null<QScreen*>> RpWidgetWrap::screenValue() const {
	auto &stream = eventStreams().screen;
	return stream.events_starting_with(rpWidget()->screen());
}

rpl::producer<bool> RpWidgetWrap::windowActiveValue() const {
	auto &stream = eventStreams().windowActive;
	return stream.events_starting_with(
		QApplication::activeWindow() == rpWidget()->window()
	) | rpl::distinct_until_changed();
}

rpl::producer<QRect> RpWidgetWrap::paintRequest() const {
	return eventStreams().paint.events();
}

void RpWidgetWrap::paintOn(Fn<void(QPainter&)> callback) {
	const auto widget = rpWidget();
	paintRequest() | rpl::on_next([=] {
		auto p = QPainter(widget);
		callback(p);
	}, lifetime());
}

rpl::producer<> RpWidgetWrap::alive() const {
	return eventStreams().alive.events();
}

rpl::producer<> RpWidgetWrap::death() const {
	return alive() | rpl::then(rpl::single(rpl::empty));
}

rpl::producer<> RpWidgetWrap::macWindowDeactivateEvents() const {
#ifdef Q_OS_MAC
	return windowActiveValue()
		| rpl::skip(1)
		| rpl::filter(!rpl::mappers::_1)
		| rpl::to_empty;
#else // Q_OS_MAC
	return rpl::never<rpl::empty_value>();
#endif // Q_OS_MAC
}

rpl::producer<WId> RpWidgetWrap::winIdValue() const {
	auto &stream = eventStreams().winId;
	return stream.events_starting_with(rpWidget()->internalWinId());
}

bool RpWidgetWrap::externalWidthWasSet() const {
	if (const auto streams = _eventStreams.get()) {
		return streams->externalWidthWasSet != 0;
	}
	return false;
}

int RpWidgetWrap::naturalWidth() const {
	if (const auto streams = _eventStreams.get()) {
		return (streams->naturalWidth != kNaturalWidthAny)
			? streams->naturalWidth
			: -1;
	}
	return -1;
}

rpl::producer<int> RpWidgetWrap::naturalWidthValue() const {
	const auto &streams = eventStreams();
	return streams.naturalWidthChanges.events_starting_with_copy(
		naturalWidth());
}

void RpWidgetWrap::setNaturalWidth(int value) {
	const auto set = uint32((value >= 0) ? value : kNaturalWidthAny);
	auto &streams = eventStreams();
	if (streams.naturalWidth != set) {
		auto weak = base::make_weak(rpWidget());
		streams.naturalWidth = set;
		streams.naturalWidthChanges.fire(naturalWidth());

		if (weak && !streams.externalWidthWasSet) {
			callResizeToNaturalWidth();
		}
	}
}

QMargins RpWidgetWrap::getMargins() const {
	return QMargins();
}

rpl::lifetime &RpWidgetWrap::lifetime() {
	return _lifetime;
}

bool RpWidgetWrap::handleEvent(QEvent *event) {
	Expects(event != nullptr);

	auto streams = _eventStreams.get();
	if (!streams) {
		return eventHook(event);
	}
	auto that = QPointer<QWidget>();
	const auto allAreObserved = streams->events.has_consumers();
	if (allAreObserved) {
		that = rpWidget();
		streams->events.fire_copy(event);
		if (!that) {
			return true;
		}
	}
	switch (event->type()) {
	case QEvent::Show:
	case QEvent::Hide:
		if (rpWidget()->isWindow() && streams->shown.has_consumers()) {
			if (!allAreObserved) {
				that = rpWidget();
			}
			streams->shown.fire_copy(!rpWidget()->isHidden());
			if (!that) {
				return true;
			}
		}
		break;

	case QEvent::WindowActivate:
	case QEvent::WindowDeactivate:
		if (streams->windowActive.has_consumers()) {
			if (!allAreObserved) {
				that = rpWidget();
			}
			streams->windowActive.fire_copy(
				QApplication::activeWindow() == rpWidget()->window());
			if (!that) {
				return true;
			}
		}
		break;

	case QEvent::Move:
	case QEvent::Resize:
		if (streams->geometry.has_consumers()) {
			if (!allAreObserved) {
				that = rpWidget();
			}
			streams->geometry.fire_copy(rpWidget()->geometry());
			if (!that) {
				return true;
			}
		}
		break;

	case QEvent::ScreenChangeInternal: {
		if (streams->screen.has_consumers()) {
			const auto screen = rpWidget()->screen();
			if (!screen) {
				// Transiently null while the last screen is removed.
				break;
			}
			if (!allAreObserved) {
				that = rpWidget();
			}
			streams->screen.fire_copy(screen);
			if (!that) {
				return true;
			}
		}
	} break;

	case QEvent::Paint:
		if (streams->paint.has_consumers()) {
			if (!allAreObserved) {
				that = rpWidget();
			}
			const auto rect = static_cast<QPaintEvent*>(event)->rect();
			streams->paint.fire_copy(rect);
			if (!that) {
				return true;
			}
		}
		break;

	case QEvent::WinIdChange:
		if (streams->winId.has_consumers()) {
			if (!allAreObserved) {
				that = rpWidget();
			}
			streams->winId.fire_copy(rpWidget()->internalWinId());
			if (!that) {
				return true;
			}
		}
	}

	return eventHook(event);
}

RpWidgetWrap::Initer::Initer(QWidget *parent, bool setZeroGeometry) {
	if (setZeroGeometry) {
		parent->setGeometry(0, 0, 0, 0);
	}
}

RpWidgetWrap::Initer::~Initer() = default;

void RpWidgetWrap::visibilityChangedHook(bool wasVisible, bool nowVisible) {
	if (nowVisible != wasVisible) {
		if (auto streams = _eventStreams.get()) {
			streams->shown.fire_copy(nowVisible);
		}
	}
}

auto RpWidgetWrap::eventStreams() const -> EventStreams& {
	if (!_eventStreams) {
		_eventStreams = std::make_unique<EventStreams>();
	}
	return *_eventStreams;
}

void AccessibilityState::writeTo(QAccessible::State &state) {
	state.checkable = checkable ? 1 : 0;
	state.checked = checked ? 1 : 0;
	state.extSelectable = extSelectable ? 1 : 0;
	state.multiSelectable = multiSelectable ? 1 : 0;
	state.pressed = pressed ? 1 : 0;
	state.readOnly = readOnly ? 1 : 0;
	state.selectable = selectable ? 1 : 0;
	state.selected = selected ? 1 : 0;
}

RpWidget::RpWidget(QWidget *parent)
: RpWidgetBase<QWidget>(parent) {
	[[maybe_unused]] static const auto Once = [] {
		auto format = QSurfaceFormat::defaultFormat();
		format.setSwapInterval(::Platform::MetalSupported() ? 1 : 0);
#ifdef DESKTOP_APP_USE_ANGLE
		format.setRedBufferSize(8);
		format.setGreenBufferSize(8);
		format.setBlueBufferSize(8);
#endif // DESKTOP_APP_USE_ANGLE
#ifdef Q_OS_MAC
		format.setColorSpace(QColorSpace::SRgb);
#endif // Q_OS_MAC
		QSurfaceFormat::setDefaultFormat(format);
		return true;
	}();
}

RpWidget::~RpWidget() {
	_initer.accessibleItems = nullptr;
}

QAccessibleInterface *RpWidget::accessibilityCreate() {
	return (accessibilityRole() != QAccessible::Role::NoRole)
		? new Accessible::Widget(this)
		: nullptr;
}

QAccessible::Role RpWidget::accessibilityRole() {
	return QAccessible::Role::NoRole;
}

Qt::FocusPolicy RpWidget::accessibilityFocusPolicy() {
	const auto role = accessibilityRole();
	const auto focusable = (role == QAccessible::Role::Button)
		|| (role == QAccessible::Role::ButtonMenu)
		|| (role == QAccessible::Role::Link)
		|| (role == QAccessible::Role::CheckBox)
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
		|| (role == QAccessible::Role::Switch)
#endif
		|| (role == QAccessible::Role::Slider);
	return focusable ? Qt::TabFocus : Qt::NoFocus;
}

QAccessible::Role RpWidget::accessibilityChildRole() const {
	return QAccessible::Role::NoRole;
}

QString RpWidget::accessibilityChildName(int index) const {
	return QString();
}

QString RpWidget::accessibilityChildDescription(int index) const {
	return QString();
}

QString RpWidget::accessibilityChildValue(int index) const {
	return QString();
}

QAccessible::State RpWidget::accessibilityChildState(int index) const {
	return QAccessible::State();
}

QRect RpWidget::accessibilityChildRect(int index) const {
	return QRect();
}

int RpWidget::accessibilityChildColumnCount(int row) const {
	return 0;
}

QAccessible::Role RpWidget::accessibilityChildSubItemRole() const {
	return QAccessible::StaticText;
}

QString RpWidget::accessibilityChildSubItemName(int row, int column) const {
	return QString();
}

QString RpWidget::accessibilityChildSubItemValue(int row, int column) const {
	return QString();
}

void RpWidget::accessibilityChildNameChanged(int index) {
	QAccessibleEvent event(this, QAccessible::NameChanged);
	event.setChild(index);
	QAccessible::updateAccessibility(&event);
}

void RpWidget::accessibilityChildDescriptionChanged(int index) {
	QAccessibleEvent event(this, QAccessible::DescriptionChanged);
	event.setChild(index);
	QAccessible::updateAccessibility(&event);
}

void RpWidget::accessibilityChildValueChanged(int index) {
	QAccessibleEvent event(this, QAccessible::ValueChanged);
	event.setChild(index);
	QAccessible::updateAccessibility(&event);
}

void RpWidget::accessibilityChildStateChanged(
		int index,
		AccessibilityState changes) {
	auto fields = QAccessible::State();
	changes.writeTo(fields);
	QAccessibleStateChangeEvent event(this, fields);
	event.setChild(index);
	QAccessible::updateAccessibility(&event);
}

void RpWidget::accessibilityChildFocused(int index) {
	QAccessibleEvent event(this, QAccessible::Focus);
	event.setChild(index);
	QAccessible::updateAccessibility(&event);
}

bool RpWidget::accessibilityChildSupportsActions(int index) const {
	return false;
}

quintptr RpWidget::accessibilityChildIdentity(int index) const {
	return 0;
}

int RpWidget::accessibilityChildIndexByIdentity(quintptr identity) const {
	return -1;
}

void RpWidget::accessibilityChildSetFocus(quintptr identity) {
}

void RpWidget::accessibilityChildActivate(quintptr identity) {
}

QString RpWidget::accessibilityName() {
	return QWidget::accessibleName();
}

void RpWidget::accessibilityNameChanged() {
	QAccessibleEvent event(this, QAccessible::NameChanged);
	QAccessible::updateAccessibility(&event);
}

QString RpWidget::accessibilityDescription() {
	return QWidget::accessibleDescription();
}

void RpWidget::accessibilityDescriptionChanged() {
	QAccessibleEvent event(this, QAccessible::DescriptionChanged);
	QAccessible::updateAccessibility(&event);
}

AccessibilityState RpWidget::accessibilityState() const {
	return {};
}

void RpWidget::accessibilityStateChanged(AccessibilityState changes) {
	auto fields = QAccessible::State();
	changes.writeTo(fields);
	QAccessibleStateChangeEvent event(this, fields);
	QAccessible::updateAccessibility(&event);
}

QString RpWidget::accessibilityValue() const {
	return QString();
}

void RpWidget::accessibilityValueChanged() {
	QAccessibleValueChangeEvent event(this, accessibilityValue());
	QAccessible::updateAccessibility(&event);
}

QStringList RpWidget::accessibilityActionNames() {
	return QStringList();
}

void RpWidget::accessibilityDoAction(const QString &name) {
}

int RpWidget::accessibilityChildCount() const {
	return -1;
}

std::vector<not_null<QWidget*>> RpWidget::accessibilityChildWidgets() const {
	return {};
}

std::optional<Qt::Orientation> RpWidget::accessibilityOrientation() const {
	return std::nullopt;
}

bool RpWidget::accessibilitySelectionList() const {
	return false;
}

RpWidget *RpWidget::accessibilityParent() const {
	return nullptr;
}

namespace {

constexpr auto kVisualTabOrderProperty = "ui_visual_tab_order";
constexpr auto kVisualTabOrderOverlayProperty = "ui_visual_tab_order_overlay";

// Whether the chain can be wired through this widget. In screen reader
// mode the accessibility layer grants the real focus policy lazily, when
// the assistive technology first queries the widget - so a freshly
// created widget (like the message list right after a chat switch) still
// reports NoFocus here. Check the declared policy instead and force the
// lazy registration right away: it applies the policy and keeps the
// widget managed across screen reader mode switches.
[[nodiscard]] bool TakesTabFocus(not_null<QWidget*> widget) {
	if (widget->focusPolicy() & Qt::TabFocus) {
		return true;
	}
	if (ScreenReaderModeActive()) {
		if (const auto rp = qobject_cast<RpWidget*>(widget.get())) {
			if (rp->accessibilityFocusPolicy() & Qt::TabFocus) {
				QAccessible::queryAccessibleInterface(rp);
				return (widget->focusPolicy() & Qt::TabFocus) != 0;
			}
		}
	}
	return false;
}

// Every Tab stop a child contributes, not just the outermost one: a widget
// can be focusable and still hold focusable widgets of its own (a bar button
// with a settings button inside it), and QWidget::setTabOrder moves a whole
// block only for real compound widgets, established through a focus proxy.
// Endpoints alone would leave the widgets between them where they were, so
// each stop is placed explicitly. Compound widgets (like InputField) keep a
// NoFocus container around a focusable inner widget without a focus proxy,
// so the chain is wired through the inner widget - setTabOrder refuses
// NoFocus arguments. Hidden descendants are collected as well: focus
// traversal skips widgets that aren't visible, so keeping them in their
// group costs nothing and means they are already in the right place when
// they are shown. Every widget on the way is queried, so the lazily granted
// screen reader focus policy gets materialized for all of them and not only
// for the first one found.
void CollectTabFocusable(
		not_null<QWidget*> widget,
		std::vector<QWidget*> &result) {
	if (TakesTabFocus(widget)) {
		result.push_back(widget);
	}
	for (const auto object : widget->children()) {
		if (object->isWidgetType()) {
			CollectTabFocusable(static_cast<QWidget*>(object), result);
		}
	}
}

// Holds the visual Tab order state of a single container. It is created only
// when a container asks for the ordering and lives as its child, found back
// through a dynamic property - so widgets that never ask store nothing.
class VisualTabOrder final : public QObject {
public:
	explicit VisualTabOrder(not_null<RpWidget*> parent);

	void schedule();
	void apply(bool force = false);

	[[nodiscard]] static VisualTabOrder *Find(not_null<QWidget*> widget);
	static void Enable(not_null<RpWidget*> widget);
	static void Disable(not_null<RpWidget*> widget);

private:
	bool eventFilter(QObject *watched, QEvent *e) override;

	const not_null<RpWidget*> _widget;

	// The children and their stops in the order they were wired last, so
	// a change that leaves the order as it is - like the list of a scroll
	// moving under its corner buttons on every scroll step - rewires
	// nothing. Weak, so a stop destroyed and another created at the same
	// address doesn't pass for the one it replaced.
	std::vector<QPointer<QWidget>> _applied;
	bool _scheduled = false;
	bool _orderDependsOnGeometry = true;
	rpl::lifetime _lifetime;

};

VisualTabOrder::VisualTabOrder(not_null<RpWidget*> parent)
: QObject(parent)
, _widget(parent) {
	parent->installEventFilter(this);

	// Focus policies that come from accessibility roles are granted only
	// once a screen reader is detected - which happens asynchronously on
	// Windows and can also be switched on while the app is running. The
	// widgets report NoFocus until then, so the order has to be redone.
	ScreenReaderModeActiveValue(
	) | rpl::on_next([=] {
		schedule();
	}, _lifetime);
}

VisualTabOrder *VisualTabOrder::Find(not_null<QWidget*> widget) {
	const auto value = widget->property(kVisualTabOrderProperty);
	return value.isValid()
		? static_cast<VisualTabOrder*>(value.value<void*>())
		: nullptr;
}

void VisualTabOrder::Enable(not_null<RpWidget*> widget) {
	if (Find(widget)) {
		return;
	}
	const auto state = new VisualTabOrder(widget);
	widget->setProperty(
		kVisualTabOrderProperty,
		QVariant::fromValue(static_cast<void*>(state)));
	state->schedule();
}

void VisualTabOrder::Disable(not_null<RpWidget*> widget) {
	if (const auto state = Find(widget)) {
		widget->setProperty(kVisualTabOrderProperty, QVariant());
		delete state;
	}
}

bool VisualTabOrder::eventFilter(QObject *watched, QEvent *e) {
	const auto type = e->type();
	if (type == QEvent::ChildAdded
		|| type == QEvent::ChildRemoved
		|| type == QEvent::LayoutRequest) {
		schedule();
	} else if (watched != _widget) {
		// This widget's own geometry and visibility can't change the order
		// of its children - the ones laid out anew get events of their own.
		if (type == QEvent::Show || type == QEvent::Hide) {
			schedule();
		} else if ((type == QEvent::Move || type == QEvent::Resize)
			&& _orderDependsOnGeometry) {
			schedule();
		}
	}
	return QObject::eventFilter(watched, e);
}

void VisualTabOrder::schedule() {
	if (_scheduled) {
		return;
	}
	_scheduled = true;
	InvokeQueued(this, [=] {
		_scheduled = false;
		apply();
	});
}

void VisualTabOrder::apply(bool force) {
	// Band vertically overlapping children together, so a row of controls
	// keeps its horizontal order even when tops differ by a few pixels.
	struct Entry {
		QWidget *widget = nullptr;
		std::vector<QWidget*> stops;
		bool overlay = false;
		int band = 0;
	};
	auto list = std::vector<Entry>();
	auto overlays = 0;
	for (const auto object : _widget->children()) {
		if (!object->isWidgetType()) {
			continue;
		}
		const auto child = static_cast<QWidget*>(object);

		// Watch the children as well: showing, hiding or moving one of them
		// doesn't produce any event on this widget, so the order would stay
		// as it was until something else happened to poke it.
		child->installEventFilter(this);

		if (child->isHidden()) {
			// Hidden widgets keep a parked / stale position, so their
			// geometry must not influence the order. They keep their
			// current chain place until they are shown - which now
			// reapplies the order through the filter above.
			continue;
		}
		if (const auto nested = Find(child)) {
			// A nested container that opted in may have changes of its own
			// waiting - let it arrange its children first, so the order
			// preserved below is its final one and not a stale one.
			nested->apply(force);
		}
		auto stops = std::vector<QWidget*>();
		CollectTabFocusable(child, stops);
		if (!stops.empty()) {
			const auto overlay = child->property(
				kVisualTabOrderOverlayProperty
			).toBool();
			overlays += overlay ? 1 : 0;
			list.push_back({ child, std::move(stops), overlay });
		}
	}

	// Geometry decides the order only between children laid out next to
	// each other, or between overlays: a single content child under a
	// single overlay keeps its order wherever it moves, so a scroll doesn't
	// have to look at its list on every scroll step.
	_orderDependsOnGeometry = (overlays > 1)
		|| (int(list.size()) - overlays > 1);
	if (list.size() < 2) {
		return;
	}
	std::stable_sort(begin(list), end(list), [](
			const Entry &a,
			const Entry &b) {
		return a.widget->y() < b.widget->y();
	});
	auto band = 0;
	auto bandBottom = 0;
	for (auto i = 0, count = int(list.size()); i != count; ++i) {
		const auto top = list[i].widget->y();
		const auto bottom = top + list[i].widget->height();
		if (!i) {
			bandBottom = bottom;
		} else if (top < bandBottom) {
			bandBottom = std::max(bandBottom, bottom);
		} else {
			++band;
			bandBottom = bottom;
		}
		list[i].band = band;
	}
	const auto rtl = style::RightToLeft();
	std::stable_sort(begin(list), end(list), [&](
			const Entry &a,
			const Entry &b) {
		if (a.overlay != b.overlay) {
			// An overlay floats over the others instead of being laid out
			// next to them, so it comes after the content it covers.
			return !a.overlay;
		} else if (a.band != b.band) {
			return a.band < b.band;
		}
		// The mirror of ordering by the left edge is ordering by the right
		// edge, not the left one backwards: children of different widths
		// starting at the same place would keep the order they were created
		// in, so a narrow overlay would come before the wide child it
		// covers instead of after it.
		const auto ax = rtl
			? a.widget->geometry().right()
			: a.widget->geometry().left();
		const auto bx = rtl
			? b.widget->geometry().right()
			: b.widget->geometry().left();
		return rtl ? (ax > bx) : (ax < bx);
	});

	// The same children with the same stops in the same order are wired
	// already. Only the chain changed behind this widget's back could say
	// otherwise, and Tab handling - the one reading the chain - redoes the
	// wiring regardless, so that stays repaired where it matters.
	auto sequence = std::vector<QWidget*>();
	for (const auto &entry : list) {
		sequence.push_back(entry.widget);
		sequence.insert(end(sequence), begin(entry.stops), end(entry.stops));
	}
	if (!force
		&& std::equal(
			begin(sequence),
			end(sequence),
			begin(_applied),
			end(_applied))) {
		return;
	}

	// Keep the stops of a single child in the order they currently sit in
	// the focus chain, so whatever arranged them - a nested container that
	// opted in, or a plain setTabOrder somewhere - is not undone here. A
	// stop that isn't in this window's chain is left out: setTabOrder only
	// works within one window anyway.
	auto positions = base::flat_map<QWidget*, int>();
	for (auto i = 0, count = int(list.size()); i != count; ++i) {
		for (const auto stop : list[i].stops) {
			positions.emplace(stop, i);
		}
	}
	auto ordered = std::vector<std::vector<QWidget*>>(list.size());
	const auto window = _widget->window();
	auto widget = window;
	do {
		const auto i = positions.find(widget);
		if (i != end(positions)) {
			ordered[i->second].push_back(widget);
		}
		widget = widget->nextInFocusChain();
	} while (widget && widget != window);

	auto flat = std::vector<QWidget*>();
	for (const auto &stops : ordered) {
		flat.insert(end(flat), begin(stops), end(stops));
	}
	for (auto i = 1, count = int(flat.size()); i != count; ++i) {
		QWidget::setTabOrder(flat[i - 1], flat[i]);
	}
	_applied.assign(begin(sequence), end(sequence));
}

} // namespace

void RpWidget::setVisualTabOrder(bool enabled) {
	if (enabled) {
		VisualTabOrder::Enable(this);
	} else {
		VisualTabOrder::Disable(this);
	}
}

void RpWidget::setVisualTabOrderOverlay(bool overlay) {
	setProperty(
		kVisualTabOrderOverlayProperty,
		overlay ? QVariant(true) : QVariant());
	RefreshVisualTabOrder(this);
}

void RpWidget::refreshVisualTabOrder() {
	if (const auto state = VisualTabOrder::Find(this)) {
		state->schedule();
	}
}

void RefreshVisualTabOrder(not_null<QWidget*> widget) {
	auto parent = widget->parentWidget();
	while (parent) {
		if (const auto state = VisualTabOrder::Find(parent)) {
			state->schedule();
		}
		parent = parent->parentWidget();
	}
}

bool RpWidget::focusNextPrevChild(bool next) {
	if (const auto state = VisualTabOrder::Find(this)) {
		state->apply(true);
	}
	return RpWidgetBase<QWidget>::focusNextPrevChild(next);
}

QAccessibleInterface *RpWidget::accessibilityChildInterface(
		int index) const {
	const auto count = accessibilityChildCount();
	if (count < 0 || index < 0 || index >= count) {
		return nullptr;
	}
	auto &items = accessibleItems();
	auto &ids = items.list;
	if (int(ids.size()) < count) {
		ids.resize(count);
	}
	// Drop a cached item whose row was reordered or replaced, so its stable
	// identity (and the data the screen reader reads) stays in sync with the
	// row currently at this index. Destroying the stale item also invalidates
	// any provider the assistive technology may still be holding for it.
	if (ids[index]) {
		const auto identity = accessibilityChildIdentity(index);
		const auto cached = dynamic_cast<Accessible::Item*>(ids[index].get());
		if (cached && cached->identity() != identity) {
			ids[index] = Accessible::UniqueId();
		}
	}
	if (!ids[index]) {
		ids[index] = Accessible::UniqueId(
			QAccessible::registerAccessibleInterface(
				new Accessible::Item(
					const_cast<RpWidget*>(this),
					index)));
	}
	return ids[index].get();
}

} // namespace Ui
