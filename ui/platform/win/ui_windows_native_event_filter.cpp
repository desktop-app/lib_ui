// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "ui/platform/win/ui_windows_native_event_filter.h"

#include "base/integration.h"
#include "ui/qt_object_factory.h"
#include "ui/rp_widget.h"

#include <QtCore/QAbstractNativeEventFilter>
#include <QtCore/QCoreApplication>

namespace Ui::Platform {

class NativeEventFilter::FilterSingleton final
	: public QAbstractNativeEventFilter {
public:
	void registerFilter(HWND handle, not_null<NativeEventFilter*> filter);
	void unregisterFilter(HWND handle, not_null<NativeEventFilter*> filter);

	bool nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		native_event_filter_result *result) override;

private:
	using Change = std::pair<HWND, not_null<NativeEventFilter*>>;
	struct Filters {
		not_null<NativeEventFilter*> first;
		std::vector<not_null<NativeEventFilter*>> other;
	};

	base::flat_map<HWND, Filters> _filtersByHandle;
	base::flat_set<Change> _adding;
	base::flat_set<Change> _removing;
	bool _processing = false;

};

void NativeEventFilter::FilterSingleton::registerFilter(
		HWND handle,
		not_null<NativeEventFilter*> filter) {
	if (_processing) {
		const auto change = std::make_pair(handle, filter);
		_removing.remove(change);
		_adding.emplace(change);
		return;
	}
	const auto i = _filtersByHandle.find(handle);
	if (i == end(_filtersByHandle)) {
		_filtersByHandle.emplace(handle, Filters{ .first = filter });
	} else {
		i->second.other.push_back(filter);
	}
}

void NativeEventFilter::FilterSingleton::unregisterFilter(
		HWND handle,
		not_null<NativeEventFilter*> filter) {
	if (_processing) {
		const auto change = std::make_pair(handle, filter);
		_adding.remove(change);
		_removing.emplace(change);
		return;
	}
	const auto i = _filtersByHandle.find(handle);
	if (i != end(_filtersByHandle)) {
		if (i->second.first == filter) {
			if (i->second.other.empty()) {
				_filtersByHandle.erase(i);
			} else {
				i->second.first = i->second.other.back();
				i->second.other.pop_back();
			}
		} else {
			const auto j = ranges::find(i->second.other, filter);
			if (j != end(i->second.other)) {
				i->second.other.erase(j);
			}
		}
	}
}

bool NativeEventFilter::FilterSingleton::nativeEventFilter(
		const QByteArray &eventType,
		void *message,
		native_event_filter_result *result) {
	const auto msg = static_cast<MSG*>(message);
	const auto i = _filtersByHandle.find(msg->hwnd);
	if (i == end(_filtersByHandle)) {
		return false;
	}
	auto filtered = false;
	_processing = true;
	base::Integration::Instance().enterFromEventLoop([&] {
		filtered = i->second.first->filterNativeEvent(
			msg->message,
			msg->wParam,
			msg->lParam,
			reinterpret_cast<LRESULT*>(result));
		if (filtered) {
			return;
		}
		for (const auto filter : i->second.other) {
			if (_removing.contains(std::make_pair(msg->hwnd, filter))) {
				continue;
			}
			filtered = filter->filterNativeEvent(
				msg->message,
				msg->wParam,
				msg->lParam,
				reinterpret_cast<LRESULT*>(result));
			if (filtered) {
				break;
			}
		}
	});
	_processing = false;

	const auto destroyed = (msg->message == WM_DESTROY);
	if (destroyed) {
		_filtersByHandle.erase(i);
		filtered = false;
	}
	for (const auto &change : _adding) {
		if (!destroyed || change.first != msg->hwnd) {
			registerFilter(change.first, change.second);
		}
	}
	for (const auto &change : _removing) {
		if (!destroyed || change.first != msg->hwnd) {
			unregisterFilter(change.first, change.second);
		}
	}

	return filtered;
}

NativeEventFilter::NativeEventFilter(not_null<RpWidget*> that) {
	that->winIdValue() | rpl::start_with_next([=](WId winId) {
		if (_hwnd) {
			Singleton()->unregisterFilter(_hwnd, this);
		}
		_hwnd = reinterpret_cast<HWND>(winId);
		if (_hwnd) {
			Singleton()->registerFilter(_hwnd, this);
		}
	}, that->lifetime());
}

NativeEventFilter::~NativeEventFilter() {
	if (_hwnd) {
		Singleton()->unregisterFilter(_hwnd, this);
	}
}

auto NativeEventFilter::Singleton() -> not_null<FilterSingleton*> {
	static const auto Instance = [&] {
		const auto application = QCoreApplication::instance();
		const auto filter = Ui::CreateChild<FilterSingleton>(application);
		application->installNativeEventFilter(filter);
		return filter;
	}();
	return Instance;
}

} // namespace Ui::Platform
