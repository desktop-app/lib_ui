// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <QtCore/QEvent>

class QAccessibleInterface;

namespace Ui::Accessibility {

class Event final : public QEvent {
public:
	using QEvent::QEvent;

	static auto Type() {
		static const auto Result = QEvent::Type(QEvent::registerEventType());
		return Result;
	}

	void set(QAccessibleInterface *interface) {
		_interface = interface;
	}

	[[nodiscard]] QAccessibleInterface *interface() const {
		return _interface;
	}

private:
	QAccessibleInterface *_interface = nullptr;

};

void Init();

} // namespace Ui::Accessibility
